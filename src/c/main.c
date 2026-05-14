#include <pebble.h>

// ── Settings keys (match package.json messageKeys) ────────────────────────────

#define KEY_SHOW_PHRASE    0
#define KEY_REVEAL_DELAY   1
#define KEY_USE_TYPEWRITER 2
#define KEY_SHOW_TIME      3
#define KEY_BG_COLOR       4
#define KEY_TEXT_COLOR     5
#define KEY_INVERT         6

// ── Animation timing ──────────────────────────────────────────────────────────

#define PHASE1_MS 200   // "?" exits / "Time" centres (and reverse)
#define PHASE2_MS 400   // "Time" slides up / down (and reverse)

// ── State ─────────────────────────────────────────────────────────────────────

typedef enum { STANDBY, CENTERING, SLIDING, ATTENTION, TIMED, RETURNING } State;

static State  s_state = STANDBY;
static Window *s_win;

// Standby: "What" + "Time" + "?" as three separate layers.
static TextLayer *s_what;
static TextLayer *s_time;   // just "Time", left-aligned, manually positioned
static TextLayer *s_quest;  // just "?",    left-aligned, manually positioned

// Attention block revealed after the slide.
static TextLayer *s_matter;

// Delayed reveal.
static TextLayer *s_fine;

// Timers.
static AppTimer *s_reveal_timer;
static AppTimer *s_sleep_timer;

// One PropertyAnimation per moving layer (at most two run concurrently).
static PropertyAnimation *s_pa_time;
static PropertyAnimation *s_pa_quest;

// Pre-computed frames (set in window_load).
static GRect s_time_standby_frame;   // "Time" within "Time?" (standby Y)
static GRect s_time_center_frame;    // "Time" centred alone  (standby Y)
static GRect s_time_top_frame;       // "Time" at top of attention block
static GRect s_quest_standby_frame;  // "?" next to "Time"
static GRect s_quest_offscreen;      // "?" off the right edge

// Typewriter buffers.
static char     s_tbuf[64];
static char     s_dbuf[64];
static int      s_type_pos;
static AppTimer *s_type_timer;

// ── Settings ──────────────────────────────────────────────────────────────────

static bool     s_show_phrase    = true;
static bool     s_use_typewriter = true;
static int      s_reveal_delay   = 2;
static bool     s_show_time      = true;
static uint32_t s_bg_color       = 0x000000;  // black
static uint32_t s_text_color     = 0xFFFFFF;  // white
static bool     s_invert         = false;

static void load_settings(void) {
  if (persist_exists(KEY_SHOW_PHRASE))
    s_show_phrase = persist_read_bool(KEY_SHOW_PHRASE);
  if (persist_exists(KEY_USE_TYPEWRITER))
    s_use_typewriter = persist_read_bool(KEY_USE_TYPEWRITER);
  if (persist_exists(KEY_REVEAL_DELAY))
    s_reveal_delay = persist_read_int(KEY_REVEAL_DELAY);
  if (persist_exists(KEY_SHOW_TIME))
    s_show_time = persist_read_bool(KEY_SHOW_TIME);
  if (persist_exists(KEY_BG_COLOR))
    s_bg_color = (uint32_t)persist_read_int(KEY_BG_COLOR);
  if (persist_exists(KEY_TEXT_COLOR))
    s_text_color = (uint32_t)persist_read_int(KEY_TEXT_COLOR);
  if (persist_exists(KEY_INVERT))
    s_invert = persist_read_bool(KEY_INVERT);
}

static void apply_colors(void) {
  if (!s_what) return;
#if defined(PBL_COLOR)
  GColor bg   = GColorFromHEX(s_bg_color);
  GColor text = GColorFromHEX(s_text_color);
#else
  GColor bg   = s_invert ? GColorWhite : GColorBlack;
  GColor text = s_invert ? GColorBlack : GColorWhite;
#endif
  window_set_background_color(s_win, bg);
  text_layer_set_text_color(s_what,   text);
  text_layer_set_text_color(s_time,   text);
  text_layer_set_text_color(s_quest,  text);
  text_layer_set_text_color(s_matter, text);
  text_layer_set_text_color(s_fine,   text);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static const char * const PHRASES[] = {
  "Fine, it's %s.",
  "Ugh... %s.",
  "It's %s. Happy now?",
  "If you must know, %s.",
  "%s. There, I said it.",
  "Oh, for— %s.",
  "Do I have to? %s.",
  "Alright, alright. %s.",
  "Okay, you win. It's %s.",
  "Since you insist... %s.",
  "It's %s. Now shoo.",
  "I checked. It's %s.",
  "Not that it matters. %s.",
  "Sigh... %s.",
  "If I must: %s.",
  "Regrettably, it's %s.",
  "I suppose it's %s.",
  "return \"%s\"; // meh",
  "Just this once: %s.",
  "Oh, apparently it's %s.",
};

static int s_phrase_bag[ARRAY_LENGTH(PHRASES)];
static int s_phrase_bag_pos = 0;

static void shuffle_bag(void) {
  int n = (int)ARRAY_LENGTH(PHRASES);
  for (int i = 0; i < n; i++) s_phrase_bag[i] = i;
  for (int i = n - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int tmp = s_phrase_bag[i];
    s_phrase_bag[i] = s_phrase_bag[j];
    s_phrase_bag[j] = tmp;
  }
  s_phrase_bag_pos = 0;
}

static void apply_time(struct tm *lt) {
  char ts[8];
  strftime(ts, sizeof(ts), clock_is_24h_style() ? "%H:%M" : "%I:%M", lt);
  if (s_show_phrase) {
    if (s_phrase_bag_pos >= (int)ARRAY_LENGTH(PHRASES)) shuffle_bag();
    snprintf(s_tbuf, sizeof(s_tbuf),
             PHRASES[s_phrase_bag[s_phrase_bag_pos++]], ts);
  } else {
    snprintf(s_tbuf, sizeof(s_tbuf), "%s", ts);
  }
  text_layer_set_text(s_fine, s_tbuf);
}

static void refresh_fine_text(void) {
  time_t now = time(NULL);
  apply_time(localtime(&now));
}

// ── Typewriter ────────────────────────────────────────────────────────────────

#define TYPE_WORD_MS  150
#define TYPE_PUNCT_MS 150

static void type_next(void *unused) {
  s_type_timer = NULL;
  while (s_tbuf[s_type_pos] == ' ') s_type_pos++;
  if (s_tbuf[s_type_pos] == '\0') return;
  while (s_tbuf[s_type_pos] != '\0' && s_tbuf[s_type_pos] != ' ') s_type_pos++;
  memcpy(s_dbuf, s_tbuf, s_type_pos);
  s_dbuf[s_type_pos] = '\0';
  text_layer_set_text(s_fine, s_dbuf);
  if (s_tbuf[s_type_pos] == '\0') return;
  char last = s_tbuf[s_type_pos - 1];
  bool after_punct = (last == '.' || last == ',' || last == '!' ||
                      last == '?' || last == ';');
  uint32_t delay = TYPE_WORD_MS + (after_punct ? TYPE_PUNCT_MS : 0U);
  s_type_timer = app_timer_register(delay, type_next, NULL);
}

static void start_typewriter(void) {
  if (s_type_timer) { app_timer_cancel(s_type_timer); s_type_timer = NULL; }
  s_dbuf[0]  = '\0';
  s_type_pos = 0;
  text_layer_set_text(s_fine, s_dbuf);
  layer_set_hidden(text_layer_get_layer(s_fine), false);
  s_type_timer = app_timer_register(TYPE_WORD_MS, type_next, NULL);
}

// ── Forward declarations ──────────────────────────────────────────────────────

static void go_standby(void *unused);
static void start_phase2(void);
static void start_return_phaseB(void);

// ── Timer callbacks ───────────────────────────────────────────────────────────

static void show_fine(void *unused) {
  s_reveal_timer = NULL;
  s_state        = TIMED;
  refresh_fine_text();
  if (s_use_typewriter) {
    start_typewriter();
  } else {
    layer_set_hidden(text_layer_get_layer(s_fine), false);
  }
  s_sleep_timer = app_timer_register(10000, go_standby, NULL);
}

// ── Animation callbacks ───────────────────────────────────────────────────────
//
// Pattern: each stopped handler receives ctx = the PropertyAnimation* that was
// created for that slot.  We check `is_current` (global still points to ctx)
// to distinguish a normal finish from a cancel-and-replace.  Only the current
// owner clears the global; the old one just destroys itself.

// ---- Phase 2 (forward): "Time" slides up → seamless swap to s_matter ---------

static void phase2_stopped(Animation *anim, bool finished, void *ctx) {
  PropertyAnimation *pa = (PropertyAnimation *)ctx;
  bool is_current = (s_pa_time == pa);
  if (is_current) s_pa_time = NULL;
  property_animation_destroy(pa);
  if (finished && is_current) {
    layer_set_hidden(text_layer_get_layer(s_time),   true);
    layer_set_frame(text_layer_get_layer(s_time), s_time_center_frame);
    layer_set_hidden(text_layer_get_layer(s_quest),  true);
    layer_set_hidden(text_layer_get_layer(s_matter), false);
    s_state = ATTENTION;
    uint32_t delay_ms = (uint32_t)s_reveal_delay * 1000;
    if (delay_ms == 0) delay_ms = 1;
    if (s_show_time) {
      s_reveal_timer = app_timer_register(delay_ms, show_fine, NULL);
    } else {
      // Skip time display; use sleep_timer slot to return to standby.
      s_sleep_timer = app_timer_register(delay_ms, go_standby, NULL);
    }
  }
}

static void start_phase2(void) {
  GRect from = layer_get_frame(text_layer_get_layer(s_time));
  s_pa_time = property_animation_create_layer_frame(
      text_layer_get_layer(s_time), &from, &s_time_top_frame);
  Animation *a = property_animation_get_animation(s_pa_time);
  animation_set_duration(a, PHASE2_MS);
  animation_set_curve(a, AnimationCurveEaseOut);
  animation_set_handlers(a,
      (AnimationHandlers){ .stopped = phase2_stopped }, s_pa_time);
  s_state = SLIDING;
  animation_schedule(a);
}

// ---- Phase 1 (forward): "?" exits right, "Time" slides to centre -------------

static void phase1_quest_stopped(Animation *anim, bool finished, void *ctx) {
  PropertyAnimation *pa = (PropertyAnimation *)ctx;
  bool is_current = (s_pa_quest == pa);
  if (is_current) s_pa_quest = NULL;
  property_animation_destroy(pa);
  (void)finished;
}

static void phase1_time_stopped(Animation *anim, bool finished, void *ctx) {
  PropertyAnimation *pa = (PropertyAnimation *)ctx;
  bool is_current = (s_pa_time == pa);
  if (is_current) s_pa_time = NULL;
  property_animation_destroy(pa);
  if (finished && is_current) start_phase2();
}

// ---- Phase B (reverse): "Time" returns left, "?" slides back in --------------

static void return_phaseB_quest_stopped(Animation *anim, bool finished, void *ctx) {
  PropertyAnimation *pa = (PropertyAnimation *)ctx;
  bool is_current = (s_pa_quest == pa);
  if (is_current) s_pa_quest = NULL;
  property_animation_destroy(pa);
  (void)finished;
}

static void return_phaseB_time_stopped(Animation *anim, bool finished, void *ctx) {
  PropertyAnimation *pa = (PropertyAnimation *)ctx;
  bool is_current = (s_pa_time == pa);
  if (is_current) s_pa_time = NULL;
  property_animation_destroy(pa);
  if (finished && is_current) {
    layer_set_hidden(text_layer_get_layer(s_what), false);
    s_state = STANDBY;
  }
}

static void start_return_phaseB(void) {
  layer_set_hidden(text_layer_get_layer(s_quest), false);

  GRect from_t = layer_get_frame(text_layer_get_layer(s_time));
  s_pa_time = property_animation_create_layer_frame(
      text_layer_get_layer(s_time), &from_t, &s_time_standby_frame);
  Animation *at = property_animation_get_animation(s_pa_time);
  animation_set_duration(at, PHASE1_MS);
  animation_set_curve(at, AnimationCurveEaseIn);
  animation_set_handlers(at,
      (AnimationHandlers){ .stopped = return_phaseB_time_stopped }, s_pa_time);

  GRect from_q = layer_get_frame(text_layer_get_layer(s_quest));
  s_pa_quest = property_animation_create_layer_frame(
      text_layer_get_layer(s_quest), &from_q, &s_quest_standby_frame);
  Animation *aq = property_animation_get_animation(s_pa_quest);
  animation_set_duration(aq, PHASE1_MS);
  animation_set_curve(aq, AnimationCurveEaseOut);
  animation_set_handlers(aq,
      (AnimationHandlers){ .stopped = return_phaseB_quest_stopped }, s_pa_quest);

  animation_schedule(at);
  animation_schedule(aq);
}

// ---- Phase A (reverse): "Time" descends from top of attention block ----------

static void return_phaseA_stopped(Animation *anim, bool finished, void *ctx) {
  PropertyAnimation *pa = (PropertyAnimation *)ctx;
  bool is_current = (s_pa_time == pa);
  if (is_current) s_pa_time = NULL;
  property_animation_destroy(pa);
  if (finished && is_current) start_return_phaseB();
}

static void go_standby(void *unused) {
  s_sleep_timer = NULL;
  if (s_type_timer) { app_timer_cancel(s_type_timer); s_type_timer = NULL; }

  layer_set_hidden(text_layer_get_layer(s_fine),   true);
  layer_set_hidden(text_layer_get_layer(s_matter), true);

  // Re-appear "Time" at the top of the attention block and slide it down.
  layer_set_frame(text_layer_get_layer(s_time), s_time_top_frame);
  layer_set_hidden(text_layer_get_layer(s_time), false);
  layer_set_frame(text_layer_get_layer(s_quest), s_quest_offscreen);

  s_pa_time = property_animation_create_layer_frame(
      text_layer_get_layer(s_time), &s_time_top_frame, &s_time_center_frame);
  Animation *a = property_animation_get_animation(s_pa_time);
  animation_set_duration(a, PHASE2_MS);
  animation_set_curve(a, AnimationCurveEaseIn);
  animation_set_handlers(a,
      (AnimationHandlers){ .stopped = return_phaseA_stopped }, s_pa_time);
  s_state = RETURNING;
  animation_schedule(a);
}

// ── Cancel ────────────────────────────────────────────────────────────────────

static void cancel_everything(void) {
  if (s_reveal_timer) { app_timer_cancel(s_reveal_timer); s_reveal_timer = NULL; }
  if (s_sleep_timer)  { app_timer_cancel(s_sleep_timer);  s_sleep_timer  = NULL; }
  if (s_type_timer)   { app_timer_cancel(s_type_timer);   s_type_timer   = NULL; }

  if (s_pa_time) {
    PropertyAnimation *old = s_pa_time;
    s_pa_time = NULL;
    animation_unschedule(property_animation_get_animation(old));
  }
  if (s_pa_quest) {
    PropertyAnimation *old = s_pa_quest;
    s_pa_quest = NULL;
    animation_unschedule(property_animation_get_animation(old));
  }
}

// ── Attention entry ───────────────────────────────────────────────────────────

static void enter_attention(void) {
  cancel_everything();

  layer_set_hidden(text_layer_get_layer(s_what),   true);
  layer_set_hidden(text_layer_get_layer(s_matter), true);
  layer_set_hidden(text_layer_get_layer(s_fine),   true);
  layer_set_hidden(text_layer_get_layer(s_time),   false);
  layer_set_hidden(text_layer_get_layer(s_quest),  false);

  // Start from wherever each layer is now (handles re-triggers mid-animation).
  GRect from_t = layer_get_frame(text_layer_get_layer(s_time));
  GRect from_q = layer_get_frame(text_layer_get_layer(s_quest));

  s_pa_time = property_animation_create_layer_frame(
      text_layer_get_layer(s_time), &from_t, &s_time_center_frame);
  Animation *at = property_animation_get_animation(s_pa_time);
  animation_set_duration(at, PHASE1_MS);
  animation_set_curve(at, AnimationCurveEaseOut);
  animation_set_handlers(at,
      (AnimationHandlers){ .stopped = phase1_time_stopped }, s_pa_time);

  s_pa_quest = property_animation_create_layer_frame(
      text_layer_get_layer(s_quest), &from_q, &s_quest_offscreen);
  Animation *aq = property_animation_get_animation(s_pa_quest);
  animation_set_duration(aq, PHASE1_MS);
  animation_set_curve(aq, AnimationCurveEaseIn);
  animation_set_handlers(aq,
      (AnimationHandlers){ .stopped = phase1_quest_stopped }, s_pa_quest);

  s_state = CENTERING;
  animation_schedule(at);
  animation_schedule(aq);
}

// ── Input + system event handlers ────────────────────────────────────────────

static void btn_down(ClickRecognizerRef r, void *ctx) { enter_attention(); }

static void click_cfg(void *ctx) {
  window_raw_click_subscribe(BUTTON_ID_UP,     btn_down, NULL, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN,   btn_down, NULL, NULL);
  window_raw_click_subscribe(BUTTON_ID_SELECT, btn_down, NULL, NULL);
  window_raw_click_subscribe(BUTTON_ID_BACK,   btn_down, NULL, NULL);
}

static void accel_tap(AccelAxisType axis, int32_t dir) {
  if (s_state == STANDBY) enter_attention();
}

static void focus_handler(bool in_focus) {
  if (in_focus && s_state == STANDBY) enter_attention();
}

static void connection_handler(bool connected) {
  if (s_state == STANDBY) enter_attention();
}

// ── AppMessage (Clay settings) ────────────────────────────────────────────────

static void inbox_handler(DictionaryIterator *iter, void *ctx) {
  Tuple *t;
  bool colors_changed = false;

  t = dict_find(iter, KEY_SHOW_PHRASE);
  if (t) {
    s_show_phrase = (bool)t->value->int32;
    persist_write_bool(KEY_SHOW_PHRASE, s_show_phrase);
  }

  t = dict_find(iter, KEY_USE_TYPEWRITER);
  if (t) {
    s_use_typewriter = (bool)t->value->int32;
    persist_write_bool(KEY_USE_TYPEWRITER, s_use_typewriter);
  }

  t = dict_find(iter, KEY_REVEAL_DELAY);
  if (t) {
    s_reveal_delay = (int)t->value->int32;
    if (s_reveal_delay < 0)  s_reveal_delay = 0;
    if (s_reveal_delay > 10) s_reveal_delay = 10;
    persist_write_int(KEY_REVEAL_DELAY, s_reveal_delay);
  }

  t = dict_find(iter, KEY_SHOW_TIME);
  if (t) {
    s_show_time = (bool)t->value->int32;
    persist_write_bool(KEY_SHOW_TIME, s_show_time);
  }

  t = dict_find(iter, KEY_BG_COLOR);
  if (t) {
    s_bg_color = (uint32_t)t->value->int32;
    persist_write_int(KEY_BG_COLOR, (int32_t)s_bg_color);
    colors_changed = true;
  }

  t = dict_find(iter, KEY_TEXT_COLOR);
  if (t) {
    s_text_color = (uint32_t)t->value->int32;
    persist_write_int(KEY_TEXT_COLOR, (int32_t)s_text_color);
    colors_changed = true;
  }

  t = dict_find(iter, KEY_INVERT);
  if (t) {
    s_invert = (bool)t->value->int32;
    persist_write_bool(KEY_INVERT, s_invert);
    colors_changed = true;
  }

  if (colors_changed) apply_colors();
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
  Layer *root = window_get_root_layer(win);
  GRect  b    = layer_get_bounds(root);
  bool small_screen = b.size.w < 200;
  GFont bold  = fonts_get_system_font(small_screen ? FONT_KEY_BITHAM_30_BLACK
                                                   : FONT_KEY_BITHAM_42_BOLD);
  GFont small = fonts_get_system_font(small_screen ? FONT_KEY_GOTHIC_14
                                                   : FONT_KEY_GOTHIC_18);

  const int line_h = small_screen ? 36 : 48;
  const int cy     = b.size.h / 2;

  // ── Measure "Time" and "?" to compute exact standby positions ──────────────
  GRect measure = GRect(0, 0, b.size.w, line_h * 2);
  GSize time_sz  = graphics_text_layout_get_content_size(
      "Time", bold, measure, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize quest_sz = graphics_text_layout_get_content_size(
      "?", bold, measure, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

  int total_w  = time_sz.w + quest_sz.w;
  int start_x  = (b.size.w - total_w)  / 2;   // left edge of "Time" in "Time?"
  int center_x = (b.size.w - time_sz.w) / 2;  // left edge of "Time" centred alone

  s_time_standby_frame  = GRect(start_x,             cy, time_sz.w,       line_h);
  s_time_center_frame   = GRect(center_x,            cy, time_sz.w,       line_h);
  s_quest_standby_frame = GRect(start_x + time_sz.w, cy, quest_sz.w + 4,  line_h);
  s_quest_offscreen     = GRect(b.size.w + 4,        cy, quest_sz.w + 4,  line_h);

  // ── Attention layout ────────────────────────────────────────────────────────
  const int attn_h = line_h * 3 + 12;
  const int attn_y = (b.size.h - attn_h) / 2;
  s_time_top_frame = GRect(center_x, attn_y, time_sz.w, line_h);

  // ── Standby layers ──────────────────────────────────────────────────────────
  s_what  = make_layer(root, GRect(0, cy - line_h, b.size.w, line_h),
                       bold, GTextAlignmentCenter, "What");
  s_time  = make_layer(root, s_time_standby_frame,  bold, GTextAlignmentLeft, "Time");
  s_quest = make_layer(root, s_quest_standby_frame, bold, GTextAlignmentLeft, "?");

  // ── Attention block (hidden until animation completes) ──────────────────────
  s_matter = make_layer(root, GRect(0, attn_y, b.size.w, attn_h),
                        bold, GTextAlignmentCenter, "Time\nDoesn't\nMatter.");
  layer_set_hidden(text_layer_get_layer(s_matter), true);

  // ── Fine label ──────────────────────────────────────────────────────────────
#if defined(PBL_ROUND)
  const int fine_margin      = b.size.w / 8;
  const int fine_right_nudge = 7;
#else
  const int fine_margin      = 6;
  const int fine_right_nudge = 0;
#endif
  const int fine_h = small_screen ? 18 : 22;
  s_fine = make_layer(root,
                      GRect(fine_margin,
                            b.size.h - fine_h - fine_margin,
                            b.size.w - fine_margin * 2 - fine_right_nudge,
                            fine_h),
                      small, GTextAlignmentRight, NULL);
  layer_set_hidden(text_layer_get_layer(s_fine), true);

  // ── Services ────────────────────────────────────────────────────────────────
  apply_colors();

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

  text_layer_destroy(s_what);   s_what   = NULL;
  text_layer_destroy(s_time);   s_time   = NULL;
  text_layer_destroy(s_quest);  s_quest  = NULL;
  text_layer_destroy(s_matter); s_matter = NULL;
  text_layer_destroy(s_fine);   s_fine   = NULL;
}

// ── Entry point ───────────────────────────────────────────────────────────────

static void init(void) {
  srand((unsigned)time(NULL));
  shuffle_bag();
  load_settings();
  app_message_register_inbox_received(inbox_handler);
  app_message_open(128, 8);
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
