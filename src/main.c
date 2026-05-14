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

static char s_tbuf[32];

// ── Helpers ───────────────────────────────────────────────────────────────────

// Single point for 24/12-hour system preference.
static void apply_time(struct tm *lt) {
  strftime(s_tbuf, sizeof(s_tbuf),
           clock_is_24h_style() ? "Fine, it's %H:%M" : "Fine, it's %I:%M",
           lt);
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
  refresh_fine_text();

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

// ── Input handlers ────────────────────────────────────────────────────────────

static void btn_handler(ClickRecognizerRef r, void *ctx) {
  enter_attention();
}

// On emery / gabbro the click config provider also receives touch taps
// delivered through the same ClickRecognizer infrastructure.
static void click_cfg(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP,     btn_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   btn_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, btn_handler);
  // BUTTON_ID_BACK exits to system on watchfaces; omit to preserve that.
}

// Flick / wrist-raise via accelerometer.
static void accel_tap(AccelAxisType axis, int32_t dir) {
  enter_attention();
}

// Keep time label live while it is displayed.
static void tick_cb(struct tm *tm, TimeUnits u) {
  if (s_state != TIMED) return;
  apply_time(tm);
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
  tick_timer_service_subscribe(MINUTE_UNIT, tick_cb);
}

static void window_unload(Window *win) {
  cancel_everything();
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();

  text_layer_destroy(s_what);
  text_layer_destroy(s_time_q);
  text_layer_destroy(s_matter);
  text_layer_destroy(s_fine);
}

// ── Entry point ───────────────────────────────────────────────────────────────

static void init(void) {
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
