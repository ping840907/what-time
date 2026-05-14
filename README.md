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

Eight seconds later, it goes back to ignoring you.

---

## Features

- **Reluctant time display** — eight random phrases, randomly selected each reveal
- **Smooth animations** — "?" slides off, "Time" glides to centre, then up into the attention block; the whole sequence reverses when returning to standby
- **Typewriter effect** — the phrase appears word by word, with natural pauses after punctuation
- **Fully configurable via Clay** — toggle the phrase, toggle the typewriter effect, set the reveal delay (1–10 s)
- **Responsive layout** — `BITHAM_42_BOLD` on Emery's 200 px display; `BITHAM_30_BLACK` everywhere else
- **System-aware** — triggers on button press, wrist flick, screen tap (Emery/Gabbro), app focus return, and Bluetooth events
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

This project uses the Pebble SDK with Waf and the Rebble-maintained Clay framework.

```bash
# Install Clay (one-time)
npm install

# Build
pebble build

# Install to a connected watch / emulator
pebble install --emulator basalt
```

The Clay configuration UI is available through the Pebble app's watchface settings page once the watchface is installed.

---

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| Show Reluctant Phrase | On | Whether to show a random complaint or just the bare time |
| Typewriter Effect | On | Reveal the phrase word by word, or all at once |
| Reveal Delay | 2 s | How long to display the attention block before caving |

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
     ▼
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

*Built with the [Rebble](https://rebble.io) community SDK. Pebble lives on.*
