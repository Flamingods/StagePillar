# StagePillar Button

ESP32 firmware for the stage button. Press gestures send actions to StageController
(`POST http://flamingods.local:8000/api/buttons/press/<action>`).

| Gesture             | Action    |
|---------------------|-----------|
| 1 press             | `start`   |
| 2 presses           | `claps`   |
| 3 presses           | `special` |
| 4 presses           | `skip`    |
| 5+ presses          | `special` |
| Long press (1.5 s)  | `stop`    |

A gesture fires 0.4 s after the last release; a long press fires while held.

While no song is loaded (stopped, or the Pi unreachable; paused counts as running) only `start` (single press) and `stop`
(long press) do anything; other gestures are ignored, with no request and no LED effect. The button polls
`GET /api/player/state` every second.

## Wiring

Button on **GPIO 12** (internal pull-up), reads HIGH while pressed. Status on the onboard LED (GPIO 2):
one blink = sent, three fast blinks = failed, slow blink = no WiFi.

## Pillar LEDs

100 × WS2812B on **GPIO 4** (GRB), brightness capped at 80/255. Built-in effects live in
`lib/pillar/led_catalogue.cpp`; default sequences per slot in `lib/pillar/pillar_player.cpp`.

| Slot      | Default                                                              |
|-----------|----------------------------------------------------------------------|
| idle      | Slow rainbow flowing up (loops while no show is running)             |
| start     | Bouncing comet, magenta → cyan → gold, one colour per pass (loops for the whole song) |
| `claps`   | White sparkles                                                       |
| `special` | Pulses blue → purple → magenta                                       |
| `skip`    | Cyan band runs up                                                    |
| `stop`    | Fades red to a dim glow                                              |

These are the code defaults. StageController can upload a plan per slot (effects and per-pixel
frames, see `StageController/docs/pillar-led-contract.md`); the pillar stores them on LittleFS,
fetches new ones when the server's plans version changes, and can play an unsaved draft
("Play on pillar") without storing it.

A gesture plays its slot once, then returns to the play loop while the show runs, or to idle.
When the show ends (stop pressed or the song ends) the stop fade plays, then idle. Two quick red
flashes mean the press did not reach the server. The strip is dark during OTA.

## Show (StageController)

The pillars are the two sides of the stage, **L** and **R** (seen from the entrance). Every
gesture is sent to `POST /api/buttons/press` with its side, real tap count and
`first_press_ago_ms`; StageController's show director decides what it does (games, launches,
claps, ...). The pillar keeps a WebSocket to `/api/buttons/ws` and draws the show's pole for
its side (fill, glow, pulse, blink, drain, drifting palette colours, like Tom's emulator). The
Pi can also command a program from its library per side. What is drawn, top wins:

1. "Play on pillar" preview
2. the show's pole, during a game (show state not `idle`)
3. a gesture's one-shot slot
4. the commanded program
5. the slot base: idle rainbow, or the play comet while a song is loaded

Without the Pi the pillar keeps working on its own slots. Contracts:
`StageController/docs/show-events-contract.md` and `StageController/docs/pillar-led-contract.md`.

## Setup

Each build is for one side: the environment sets the side and the matching OTA hostname
(`stage-pillar-l.local` / `stage-pillar-r.local`).

```bash
cp include/secrets.example.h include/secrets.h        # then edit
pio test -e native                                     # unit tests on the host
pio run -e pillarL -t upload                           # first flash of a board over USB
ESP_OTA_PASSWORD=... pio run -e pillarR-ota -t upload  # OTA to stage-pillar-r.local
ESP_OTA_PASSWORD=... pio run -e pillarL-ota -t upload  # OTA to stage-pillar-l.local
```
