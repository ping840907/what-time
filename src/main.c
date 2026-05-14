#include <pebble.h>

// ── State ─────────────────────────────────────────────────────────────────────

typedef enum { STANDBY, SLIDING, ATTENTION, TIMED } State;

static State      s_state       = STANDBY;
static Window    *s_win;

// Standby layers: "What" and "Time?" as separate layers so they sit close.
static TextLayer *s_what;
static TextLayer *s_time_q;

// Attention block: "Time\nDoesn't\nMatter." — animated as one unit.
static TextLayer *s_matter;

// Delayed reveal: "Fine, it's xx:xx"
static TextLayer *s_fine;

// Timers & animation
static AppTimer         *s_reveal_timer;
static AppTimer         *s_sleep_timer;
static PropertyAnimation *s_prop_anim;

// Pre-computed frames for the slide animation (set in window_load).
static GRect s_matter_offscreen;   // "Time" at bottom, rest below screen
static GRect s_matter_final;       // vertically centred final position

static char s_tbuf[40];

// ── Helpers ───────────────────────────────────────────────────────────────────

// Reluctant phrases — %s is replaced with the formatted time.
static const char * const PHRASES[] = {
  "Fine, it's %s.",
  "Ugh... %s.",
  "It's %s. Happy now?",
  "If you must know, %s.",
  "%s. There, I said it.",
  "Oh, for— %s.",
  "Do I have to? %s.",
  "Alright, alright. %s.",
};

// Single point for 24/12-hour system preference; picks a random phrase.
static void apply_time(struct tm *lt) {
  char ts[8];
  strftime(ts, sizeof(ts), clock_is_24h_style() ? "%H:%M" : "%I:%M", lt);
  snprintf(s_tbuf, sizeof(s_tbuf),
           PHRASES[rand() % (int)ARRAY_LENGTH(PHRASES)], ts);
  text_layer_set_text(s_fine, s_tbuf);
}

static void refresh_fine_text(void) {
  time_t now = time(NULL);
  apply_time(localtime(&now));
}

// ── Timer callbacks ───────────────────────────────────────────────────────────

static void go_standby(void *unused) {
  s_sleep_timer = NULL;
  s_state       = STANDBY;

  // Reset matter layer to final frame so next slide starts fresh.
  layer_set_frame(text_layer_get_layer(s_matter), s_matter_final);

  layer_set_hidden(text_layer_get_layer(s_what),   false);
  layer_set_hidden(text_layer_get_layer(s_time_q), false);
  layer_set_hidden(text_layer_get_layer(s_matter), true);
  layer_set_hidden(text_layer_get_layer(s_fine),   true);
}

static void show_fine(void *unused) {
  s_reveal_timer = NULL;
  s_state        = TIMED;
  // Capture time at the exact moment of display, not at the earlier trigger.
  refresh_fine_text();
  layer_set_hidden(text_layer_get_layer(s_fine), false);
  // Return to standby after another 8.5 s (total ~10 s since attention).
  s_sleep_timer = app_timer_register(8500, go_standby, NULL);
}

// ── Animation ─────────────────────────────────────────────────────────────────

static void anim_stopped(Animation *anim, bool finished, void *ctx) {
  PropertyAnimation *pa = (PropertyAnimation *)ctx;
  property_animation_destroy(pa);

  // If this is still the current animation, advance state.
  if (pa == s_prop_anim) {
    s_prop_anim = NULL;
    if (finished) {
      s_state        = ATTENTION;
      s_reveal_timer = app_timer_register(1500, show_fine, NULL);
    }
  }
}

static void cancel_everything(void) {
  if (s_reveal_timer) { app_timer_cancel(s_reveal_timer); s_reveal_timer = NULL; }
  if (s_sleep_timer)  { app_timer_cancel(s_sleep_timer);  s_sleep_timer  = NULL; }

  if (s_prop_anim) {
    // Nullify first so the stopped handler knows this is a cancelled slide.
    PropertyAnimation *old = s_prop_anim;
    s_prop_anim = NULL;
    animation_unschedule(property_animation_get_animation(old));
    // stopped handler fires (sync or next frame) and destroys `old`.
  }
}

// ── Attention entry ───────────────────────────────────────────────────────────

static void enter_attention(void) {
  cancel_everything();

  // Hide standby; hide fine label until the timer fires.
  layer_set_hidden(text_layer_get_layer(s_what),   true);
  layer_set_hidden(text_layer_get_layer(s_time_q), true);
  layer_set_hidden(text_layer_get_layer(s_fine),   true);

  // Place attention block with "Time" visible at the screen bottom.
  layer_set_frame(text_layer_get_layer(s_matter), s_matter_offscreen);
  layer_set_hidden(text_layer_get_layer(s_matter), false);

  // Slide up to the centred final position.
  s_prop_anim = property_animation_create_layer_frame(
      text_layer_get_layer(s_matter),
      &s_matter_offscreen,
      &s_matter_final);

  Animation *anim = property_animation_get_animation(s_prop_anim);
  animation_set_duration(anim, 480);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_handlers(anim,
      (AnimationHandlers){ .stopped = anim_stopped },
      s_prop_anim);   // pass pointer as context for destruction

  s_state = SLIDING;
  animation_schedule(anim);
}

// ── Input + system event handlers ────────────────────────────────────────────

static void btn_down(ClickRecognizerRef r, void *ctx) {
  enter_attention();
}

// raw_click fires on button-down; also receives touch taps on emery / gabbro.
static void click_cfg(void *ctx) {
  window_raw_click_subscribe(BUTTON_ID_UP,     btn_down, NULL, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN,   btn_down, NULL, NULL);
  window_raw_click_subscribe(BUTTON_ID_SELECT, btn_down, NULL, NULL);
  window_raw_click_subscribe(BUTTON_ID_BACK,   btn_down, NULL, NULL);
}

// Flick / wrist-raise via accelerometer.
static void accel_tap(AccelAxisType axis, int32_t dir) {
  enter_attention();
}

// Watchface regains focus after a system overlay (notification, quick launch,
// timeline peek…) is dismissed — system has already done its thing.
static void focus_handler(bool in_focus) {
  if (in_focus) enter_attention();
}

// Bluetooth connect / disconnect — system shows a status banner and vibrates.
static void connection_handler(bool connected) {
  enter_attention();
}

// ── Text layer factory ────────────────────────────────────────────────────────

static TextLayer *make_layer(Layer *root, GRect frame, GFont font,
                             GTextAlignment align, const char *text) {
  TextLayer *tl = text_layer_create(frame);
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_text_color(tl, GColorWhite);
  text_layer_set_font(tl, font);
  text_layer_set_text_alignment(tl, align);
  text_layer_set_overflow_mode(tl, GTextOverflowModeWordWrap);
  if (text) text_layer_set_text(tl, text);
  layer_add_child(root, text_layer_get_layer(tl));
  return tl;
}

// ── Window lifecycle ──────────────────────────────────────────────────────────

static void window_load(Window *win) {
  Layer *root  = window_get_root_layer(win);
  GRect  b     = layer_get_bounds(root);
  window_set_background_color(win, GColorBlack);

  GFont bold  = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_18);

  // ── Standby layout ─────────────────────────────────────────────────────────
  // Two separate layers placed close together around vertical centre.
  // line_h = 48 gives each BITHAM_42_BOLD line a snug, no-gap fit.
  const int line_h = 48;
  const int cy     = b.size.h / 2;

  s_what   = make_layer(root, GRect(0, cy - line_h, b.size.w, line_h),
                        bold, GTextAlignmentCenter, "What");
  s_time_q = make_layer(root, GRect(0, cy,          b.size.w, line_h),
                        bold, GTextAlignmentCenter, "Time?");

  // ── Attention layout ────────────────────────────────────────────────────────
  // Three lines: ~3 × 48 px = 144 px total, with a small buffer.
  const int attn_h = line_h * 3 + 12;
  s_matter_final = GRect(0, (b.size.h - attn_h) / 2, b.size.w, attn_h);

  // Starting position: first line ("Time") sits at the very bottom of screen;
  // the remaining two lines extend below (clipped by the display bounds).
  s_matter_offscreen = GRect(0, b.size.h - line_h, b.size.w, attn_h);

  s_matter = make_layer(root, s_matter_final, bold,
                        GTextAlignmentCenter, "Time\nDoesn't\nMatter.");
  layer_set_hidden(text_layer_get_layer(s_matter), true);

  // ── Fine label ──────────────────────────────────────────────────────────────
  // FONT_KEY_GOTHIC_18, right-aligned, pinned to the bottom-right corner.
  // On round displays (chalk / gabbro) we shift inward a little.
#if defined(PBL_ROUND)
  const int fine_margin = b.size.w / 8;
#else
  const int fine_margin = 6;
#endif
  const int fine_h = 22;
  s_fine = make_layer(root,
                      GRect(fine_margin,
                            b.size.h - fine_h - fine_margin,
                            b.size.w - fine_margin * 2,
                            fine_h),
                      small, GTextAlignmentRight, NULL);
  layer_set_hidden(text_layer_get_layer(s_fine), true);

  // ── Services ────────────────────────────────────────────────────────────────
  window_set_click_config_provider(win, click_cfg);
  accel_tap_service_subscribe(accel_tap);
  app_focus_service_subscribe_handlers((AppFocusHandlers){
    .will_focus = focus_handler,
  });
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = connection_handler,
  });

  enter_attention();
}

static void window_unload(Window *win) {
  cancel_everything();
  accel_tap_service_unsubscribe();
  app_focus_service_unsubscribe();
  connection_service_unsubscribe();

  text_layer_destroy(s_what);
  text_layer_destroy(s_time_q);
  text_layer_destroy(s_matter);
  text_layer_destroy(s_fine);
}

// ── Entry point ───────────────────────────────────────────────────────────────

static void init(void) {
  srand((unsigned)time(NULL));
  s_win = window_create();
  window_set_window_handlers(s_win, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_win, true);
}

static void deinit(void) {
  window_destroy(s_win);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
