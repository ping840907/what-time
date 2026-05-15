# What Time?

A Pebble watchface for people who believe that time, frankly, shouldn't matter — but are occasionally willing to admit what it is.

---

## The Concept

Most watchfaces greet you with the time front and centre, eager to be useful.

**What Time?** does not want to be useful. It wants to be left alone.

In standby, it presents you with a philosophical position:

```
    What
   Time?
```

Should you insist — by pressing a button, flicking your wrist, tapping the screen, or simply receiving a notification — the watch will sigh, animate dramatically, and reveal:

```
    Time
   Doesn't
   Matter.
```

After a brief pause to let that sink in, it reluctantly concedes and types out something like:

> *Fine, it's 14:23.*

Or perhaps:

> *Ugh... 14:23.*

Or, if it's having a particularly bad day:

> *Do I have to? 14:23.*

Ten seconds later, it goes back to ignoring you.

---

## Features

- **Reluctant time display** — twenty random phrases, randomly selected each reveal
- **Smooth animations** — "?" slides off, "Time" glides to centre, then up into the attention block; the whole sequence reverses when returning to standby
- **Typewriter effect** — the phrase appears word by word, with natural pauses after punctuation
- **Fully configurable via Clay** — toggle time display, phrase, typewriter effect; set reveal delay; pick colours
- **Custom colours** — background and text colour pickers on colour platforms; invert toggle on B&W
- **Responsive layout** — `BITHAM_42_BOLD` on Emery's 200 px display; `BITHAM_30_BLACK` everywhere else
- **System-aware** — triggers on wrist flick, button press, screen tap (Emery/Gabbro), app focus return, and Bluetooth events; backlight-on transitions are observed via `backlight_service` so touch-wake on Emery/Gabbro is captured without using the watchapp-only `touch_service` API; note that button and touch triggers are silenced by the OS when the Ambient Light Sensor suppresses the backlight in bright conditions — wrist flick remains effective regardless
- **24 / 12-hour** — respects the system clock format

---

## Supported Platforms

| Platform | Hardware | Notes |
|----------|----------|-------|
| Aplite | Pebble OG, Pebble Steel | B&W, smaller font |
| Basalt | Pebble Time | Colour rect, smaller font |
| Chalk | Pebble Time Round | Colour round, smaller font |
| Diorite | Pebble 2 SE | B&W, smaller font |
| Emery | Pebble Time 2 | Colour rect 200 px, full-size font |
| Flint | Pebble 2 HR | B&W, smaller font |
| Gabbro | Pebble Time Round 2 | Colour round, smaller font |

---

## Building

This project is compiled on [Core Devices' CloudPebble](https://cloudpebble.repebble.com) — the rebuilt browser-based IDE maintained by Core Devices as part of the [Repebble](https://repebble.com) initiative. Core Devices picked up where the Rebble community left off: rather than simply keeping the old services alive, they open-sourced PebbleOS, built new Pebble-compatible hardware, and reconstructed CloudPebble from the ground up for modern use. The SDK in use is Core Devices' [PebbleOS SDK](https://developer.repebble.com/sdk/), which builds on Rebble's maintained fork of the original Pebble SDK and adds support for new Core Devices hardware and APIs (RGB backlight, touch input, speaker output).

If you prefer a local build, you'll need the Core Devices PebbleOS SDK toolchain and the Clay dependency:

```bash
# Install Clay (one-time)
npm install

# Build
pebble build

# Install to a connected watch / emulator
pebble install --emulator basalt
```

The Clay configuration UI is available through the Repebble companion app's watchface settings page once the watchface is installed.

---

## Configuration

| Setting | Default | Platforms | Description |
|---------|---------|-----------|-------------|
| Still Show the Time | On | All | Whether to reveal the time at all, or just let "Time Doesn't Matter." speak for itself |
| Reveal Delay | 2 s | All | How long to display the attention block before caving (1–10 s) |
| Show Reluctant Phrase | On | All | Random complaint alongside the time, or just the bare digits |
| Typewriter Effect | On | All | Reveal the phrase word by word, or all at once |
| Background Colour | Black | Colour only | Window background colour |
| Text Colour | White | Colour only | Text colour |
| Invert Colours | Off | B&W only | Swap to white background, black text |

The Reveal Delay, phrase, and typewriter settings are hidden in the config UI when "Still Show the Time" is off (since they become irrelevant).

> **Timing note:** Reveal Delay is the time spent on the attention block *before* the time appears — not the total display duration. The time is then shown for 10 seconds regardless of the delay setting. At maximum delay (10 s), the total cycle is 10 + 10 = 20 seconds.

Settings are persisted across restarts.

---

## Animation Sequence

For the curious:

```
[Standby]  "What / Time?"
     │  trigger (button / flick / tap / event)
     ▼
[Phase 1, 200 ms]  "?" slides off right · "Time" glides to centre
     │
     ▼
[Phase 2, 400 ms]  "Time" slides up to top of attention block
     │  seamless swap
     ▼
[Attention]  "Time / Doesn't / Matter."  (reveal_delay seconds)
     │
     ├─ show_time OFF ──► go_standby immediately (skip to Return Phase A)
     │
     ▼  show_time ON
[Timed]  Phrase types itself out, word by word
     │  8.5 seconds
     ▼
[Return Phase A, 400 ms]  "Time" descends from the attention block
     │
     ▼
[Return Phase B, 200 ms]  "Time" drifts left · "?" slides back in
     │
     ▼
[Standby]  "What / Time?"  — and so it goes
```

---

## Credits

**Concept & direction:** the owner of this repository, who apparently wanted their watch to have a bad attitude.

**Code:** Almost entirely written by [Claude](https://claude.ai) (Anthropic's AI assistant), across an extended pair-programming session in which Claude was asked to implement increasingly elaborate animation choreography and somehow never complained about it — unlike the watchface itself.

The irony of using an AI that genuinely enjoys being helpful to build a watchface that refuses to be helpful was not lost on anyone.

---

*Pebble lives on.*
