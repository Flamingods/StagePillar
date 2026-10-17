# StagePillar Button — Design

Date: 2026-10-02

## Goal

ESP32 firmware for a single physical stage button. The performer uses press
gestures to trigger show actions on the StageController server (FastAPI on the
RPi, `flamingods.local`). Firmware is updatable over the air.

This is a new PlatformIO project. `~/gits/Flamingods/esps/button` was read for
context only (OTA setup, PlatformIO layout); no code is reused.

## Gesture → action mapping

| Gesture             | Action    | Server effect (existing)                    |
|---------------------|-----------|---------------------------------------------|
| 1 press             | `start`   | Run "main" sequence + start/resume playlist |
| 2 presses           | `claps`   | Run "claps" sequence + claps overlay sound  |
| 3 presses           | `special` | Run "special" sequence                      |
| 4 presses           | `skip`    | Skip to next song                           |
| Long press (≥1.5 s) | `stop`    | Stop music, turn all devices off            |
| 5+ presses          | `special` | Same as 3 presses                           |

Request: `POST http://<pi>:8000/api/buttons/press/<action>`, empty body.
The server already implements this endpoint; no server changes are needed.

## Idle gate (added 2026-10-02)

The button polls `GET /api/player/state` every 1 s in a background task. While
no song is loaded (`current_song` is null, unknown, or Pi unreachable; paused counts as running), only `start`
(single press) and `stop` (long press) are acted on; every other gesture is ignored: no POST and no LED
effect. After a `start` the show is treated as playing for 3 s so a quick
follow-up gesture is not blocked while the next poll catches up.

## LED plans (added 2026-10-02)

The format, slots, playback rules, defaults and HTTP endpoints are defined in the
shared contract `StageController/docs/pillar-led-contract.md`; this section covers
the ESP side only.

- `lib/pillar/led_catalogue.*`: the effect maths (contract §8).
- `lib/pillar/plp.*`: PLP1 reader with CRC check, plus an encoder for effect-only plans.
  Code defaults are encoded as PLP1 at startup, so defaults, uploads and previews share
  one reader. The golden file from StageController's compiler is a native test fixture.
- `lib/pillar/sequence.*`: plays one plan. Only the current step is read from its
  source (at most one 700-byte frame). Frame pixels fade from a snapshot of the strip
  taken when the step starts.
- `lib/pillar/pillar_player.*`: playback rules (contract §2) plus preview handling.
- `src/plan_store.*`: LittleFS. The poll only records the server's plans version and
  preview id; a separate download task fetches a full round of six slots into
  `/plans/new` when the version changes. A slot that is missing (404) or not playable
  uses its default; only a version change mid-round fails the round, retried with
  backoff (5 s doubling to 5 min). The task also builds each plan's index, so `loop()`
  only renames files and copies indexes (no flash reads, no gesture stalls), then the
  player restarts its base loop (a one-shot in progress ends). Previews go to
  `/preview.new` and are swapped to `/preview.bin` the same way; a preview also ends by
  itself after 60 s.
- After a local start or stop the button trusts its own action over the poll for 3 s
  (`lib/pillar/show_latch.h`), so a stale poll cannot replay the stop fade.
- The poll reports `?client=pillar&running_version=<n>` so the server can show pillar status.

## Two pillars (added 2026-10-17)

Two pillars with identical hardware and behaviour: same LED plans, same gestures,
same show-state rules, same preview. The ID only tells them apart for status, logs
and OTA. A later "dual games mode" will need a separate input mode; it is out of scope here.

- `PILLAR_ID` (1 or 2) is a build flag set per PlatformIO environment. Each environment
  also sets the matching OTA target, so a build for one pillar cannot be flashed to the
  other by accident:
  - `pillar1` / `pillar1-ota` → `stage-pillar-1.local`
  - `pillar2` / `pillar2-ota` → `stage-pillar-2.local`
  The old `esp32dev` / `esp32dev-ota` environments are removed. A build without
  `PILLAR_ID` fails with a clear error.
- The hostname (WiFi, mDNS, OTA) is `stage-pillar-<id>`. The existing board
  (192.168.0.101) becomes pillar 1; its first OTA upload goes to the old `stage-pillar`
  name.
- Every request carries the ID as a query parameter:
  - poll: `GET /api/player/state?client=pillar&pillar_id=<id>&running_version=<n>`
  - press: `POST /api/buttons/press/<action>?pillar_id=<id>`
  The current server ignores unknown query parameters, so the firmware can ship
  before the server change. StageController tracks status per pillar (contract change,
  agreed with that side).
- Plans and previews stay shared: one set of slot files and one preview for both pillars.
- Tests: native tests for the URL builders (poll and press carry `pillar_id`); on
  hardware, pillar 1 answers as `stage-pillar-1.local` and the server lists it as pillar 1.

## Hardware

- Button on **GPIO 12**, `INPUT_PULLUP`. Pressed = HIGH (matches the existing board wiring).
- Status feedback on the onboard LED, **GPIO 2**.

## Gesture timing

| Parameter  | Value   | Meaning                                                   |
|------------|---------|-----------------------------------------------------------|
| Debounce   | 30 ms   | A level change must be stable this long to count          |
| Gap        | 400 ms  | Quiet time after a release that ends a multi-press gesture|
| Long press | 1500 ms | Hold duration that emits `Long` immediately, while held   |

Rules:

- A press is counted on its debounced release, unless it became a long press.
- When a hold reaches 1500 ms, `Long` is emitted at once. Any taps earlier in
  the same gesture are discarded. The release that follows emits nothing.
- After the last release, if 400 ms pass with no new press, emit the count:
  1→`Single`, 2→`Double`, 3→`Triple`, 4→`Quad`; ≥5→`Many` (sends `special`).
- Known cost: a single press fires about 400 ms after release.

## Architecture

```
loop() ──► GestureDetector.update(level, now) ──► gesture? ──► FreeRTOS queue (len 4)
                                                                     │
                                         sender task ◄───────────────┘
                                              │ actionFor(gesture)
                                              ▼
                                   HTTP POST to Pi (2 s timeout)
                                              │
                                              ▼
                                         LED feedback
```

| File                   | Responsibility                                                             |
|------------------------|----------------------------------------------------------------------------|
| `src/gesture.{h,cpp}`  | Pure C++ state machine: `(bool pressed, uint32_t nowMs) → Gesture`. No Arduino deps. |
| `src/actions.h`        | `const char* actionFor(Gesture)`. Pure, no Arduino deps.                   |
| `src/net.{h,cpp}`      | WiFi connect + auto-reconnect; resolve `flamingods.local` via mDNS, cache the IP, fall back to `SERVER_FALLBACK_IP`. |
| `src/sender.{h,cpp}`   | FreeRTOS task: dequeue gesture → POST → LED feedback.                      |
| `src/status_led.{h,cpp}` | Non-blocking LED patterns.                                               |
| `src/ota.{h,cpp}`      | ArduinoOTA: hostname `stage-pillar`, password from secrets, `otaActive()` flag. |
| `src/main.cpp`         | Wiring only: setup, then call each module from `loop()`.                   |
| `include/secrets.h`    | Gitignored: `WIFI_SSID`, `WIFI_PASSWORD`, `OTA_PASSWORD`, `SERVER_HOST`, `SERVER_PORT`, `SERVER_FALLBACK_IP`. |
| `include/secrets.example.h` | Committed template with placeholder values.                         |

## Error handling

- **POST timeout** is 2 s. **No retries**: a duplicate `start` or `claps` on
  stage is worse than a miss; the performer can press again.
- **Connection or lookup failure**: re-resolve the server address once (the Pi's
  IP may have changed). The failed gesture is not re-sent.
- **WiFi down**: gestures are dropped and logged, not queued, so stale presses
  never fire later. WiFi reconnects in the background.
- **Queue full** (4): the new gesture is dropped and logged.
- **Stale gesture**: a gesture that waited more than 1 s in the queue (behind a
  slow POST) is dropped, so a backlog never fires late.
- **Reply timeout**: the server runs the whole device sequence before replying,
  so a request that was sent but got no reply within 2 s counts as sent (single
  blink, address kept). Only failures to connect or send, and non-2xx replies, blink as failed.
- **During OTA**: gestures are ignored.

## Status LED (GPIO 2)

| Pattern           | Meaning                      |
|-------------------|------------------------------|
| One short blink   | Server returned 2xx          |
| Three fast blinks | POST failed (error/non-2xx)  |
| Slow blink        | WiFi disconnected            |
| Off               | Idle and connected           |

## Logging

Serial at 115200 baud, one line per gesture and per POST result (action, HTTP
code, latency).

## Build environments (`platformio.ini`)

- `esp32dev`: USB upload.
- `esp32dev-ota`: `upload_protocol = espota`, `upload_port = stage-pillar.local`,
  auth from env `ESP_OTA_PASSWORD`.
- `native`: host unit tests (Unity) for `gesture` and `actions`.

## Testing

Native unit tests for `GestureDetector`, driven by synthetic `(level, time)` sequences:

- 1, 2, 3, 4 taps → correct gesture.
- 5 and 8 taps → `Many`.
- Bounce shorter than 30 ms → not counted.
- Long press → `Long` at 1500 ms while held; the release emits nothing.
- Taps followed by a long press → only `Long`.
- Gap at 399 ms continues the gesture; at 400 ms it ends it.
- Hold just under 1500 ms → counted as a tap.

Native test for `actionFor` covering every gesture.

On hardware: perform each gesture, then
`curl http://flamingods.local:8000/api/buttons/recent` to confirm the action arrived.

## Out of scope

- An HTTP server on the ESP32 (status endpoints, etc.).
- LED strips or other outputs beyond the onboard LED.
- Changes to the StageController server.
- Configuring the mapping at runtime; it is a compile-time table.
