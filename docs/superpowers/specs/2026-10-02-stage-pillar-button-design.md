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

## Idle gate (added 2026-10-02; REMOVED by the show alignment below)

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

## Two pillars (added 2026-10-17; pillar_id REPLACED by side L/R, see show alignment)

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

## Programs commanded by the Pi (added 2026-10-17; UDP REPLACED by WebSocket, see show alignment)

A library of pre-built LED programs lives on StageController; the Pi tells each pillar
which program to play. The gesture slots stay as the fallback behaviour.

**Library**
- Programs are PLP1 files (same format as slots: effects, frames, transitions, loop flag),
  identified by a number 1..255. Both pillars download the whole library ahead of time.
- The state reply carries `pillar_programs_version`. When it changes, the ESP fetches
  `GET /api/pillar/programs/index` (`{"version":N,"ids":[1,4,7]}`) and then each
  `GET /api/pillar/programs/<id>.bin` into staging, and `loop()` swaps the round in, as
  for slots (a version mismatch mid-round retries; an unplayable program is skipped).
- Limits, enforced by the server: at most 24 programs and 1 MB total.

**Commands (per pillar)**
- A command is `{seq, program}`; `program` 0 means release. `seq` is a server counter
  that never decreases (persisted); the ESP applies a command only if `seq` is greater
  than the last applied one.
- Fast path: UDP to the pillar's port 4210, sent to the address of its last poll. Packet,
  12 bytes, little-endian: `"PLC1"` | `seq` u32 | `program` u16 | reserved u16.
- Backup: the state reply for `pillar_id=N` carries that pillar's current command as
  `pillar_command: {"seq":S,"program":P}`, so a lost UDP packet is corrected within ~1 s.
  After an ESP reboot (last seq 0) this restores the active program.

**Playback**
- An active program replaces the base layer (idle, or the play loop while the show runs).
- Gestures still play their one-shot slot, then return to the program.
- A non-looping program that finishes, or a release, ends the program and the pillar
  goes back to normal behaviour. Losing the Pi (no successful poll for 10 s)
  only pauses the program: the pillar shows normal behaviour, keeps the command, and
  resumes the program when polls succeed again (unless a newer command released it).
- The ESP always keeps the latest command (seq, program), even when it does not have
  that program yet (a program saved and commanded at once can arrive before the library
  sync). An unknown id means "normal behaviour for now": after every library sync the
  ESP looks the command up again and starts the program if it now exists. If the active
  program's file changed in a sync, it restarts from the new file.
- A pillar the server has not heard from yet gets no UDP packet; it picks the command up
  from its first state reply.
- Preview ("Play on pillar") is unchanged and still interrupts everything.

## Show alignment (added 2026-10-17)

Aligns the pillars with Tom's show design (`StageController/docs/show-events-contract.md`,
fcf52b3) and the pillar LED contract. Where this section conflicts with the sections above,
this section wins.

**Identity: sides, not ids**
- `PILLAR_SIDE` is `L` or `R` (left/right seen from the entrance), set per PlatformIO env:
  `pillarL` / `pillarL-ota` → `stage-pillar-l.local`, `pillarR` / `pillarR-ota` →
  `stage-pillar-r.local`. The current board (192.168.0.101, flashed as `stage-pillar-1`) is
  **R**; the second board is **L**. `PILLAR_ID` and the pillar1/2 envs are removed.
- Poll: `GET /api/player/state?client=pillar&side=<L|R>&running_version=<n>`.

**Presses**
- `POST /api/buttons/press` with JSON `{"action", "side", "taps", "first_press_ago_ms"}`.
  `taps` is the real tap count (the detector's cap rises from 5 to 20; a long press sends
  `stop` with taps 1). `first_press_ago_ms` = time from the gesture's debounced first-press
  edge to the moment the sender task sends, so it includes the gap wait and queue time.
- The ESP idle gate is removed: every gesture is sent; the show director decides. The
  start/stop assumption window (`ShowLatch`) stays for the LEDs' play loop.

**Live show events (WebSocket)**
- The ESP keeps a WebSocket open to `ws://<pi>:8000/api/buttons/ws` (reconnect with backoff)
  and ignores every message except `action: "show"` and `action: "pillar_command"`.
- From a show event it takes `state` and its own side's pole: `pct`, `color`, `level`,
  `mode`, `ms`. It ignores `buttons` (no separate ring: the pillar is the button), `perimeter`,
  `song`, `thunder` and unknown fields.
- Drawing a pole on the 100 LEDs (index 0 = bottom, toward the button):
  - `off`: dark. `solid`: LEDs below `pct`% lit.
  - `glow`: all LEDs lit at `level`% brightness (default 35), regardless of `pct`.
  - `pulse`: the `solid` fill with a brightness triangle 0→100→0 over `ms` (default 1000).
  - `blink`: the `solid` fill on/off, 500 ms period, 50 % duty.
  - `drain`: the fill falls linearly from `pct` to 0 over `ms`, from when the event arrived.
  - Colour per LED i at time t: palette `ramp` = [dim, base, bright]; for every name except
    `white`, drift: k = 0.5 + 0.5·sin(0.33·i − 2π·t/period), colour = lerp(dim, base, 2k) if
    k < 0.5 else lerp(base, bright, 2k − 1); `white` uses `base`. Then × `gain`, capped at 255.
    `period` is the colour's `drift_ms`. This matches Tom's emulator (`stageLook.ts drift()`).
- Palette: `GET /api/show/palette` → `{"version", "colors": {name: {"ramp", "gain",
  "drift_ms"}}}`, fetched at boot and whenever `show_palette_version` in the state reply
  changes. Kept in RAM (small); a built-in copy of today's spec.json values is the fallback
  until the first fetch succeeds.
- The latest show event is drawn until the next one arrives; the ESP never times intro steps.

**Programs (replaces the UDP part above)**
- Commands arrive on the same WebSocket as `{"action":"pillar_command","side","seq","program"}`;
  the ESP takes only its own side. The state reply's `pillar_command` stays as the backup.
  The library, seq rule, keep-unknown-command rule and 10 s pause rule are unchanged.

**Layer order (top wins)**
1. Preview ("Play on pillar").
2. Show event, when `state` is not `idle`.
3. Gesture one-shot slot (only drawn while the show state is `idle`).
4. Commanded program.
5. Slot base: idle rainbow, or the play loop while a song is loaded.
With no WebSocket connection the show state counts as `idle`, so the pillar keeps working on
its own slots and programs.

**Testing (native)**
- Press body builder (side, taps, first_press_ago_ms) and the detector's real tap count and
  first-press time.
- Show-event parser (state, own side's pole, ignoring other fields) and pillar_command parser.
- Pole renderer: each mode, pct fill, glow level, pulse/blink/drain timing, drift formula
  against values computed from the formula, white without drift, gain cap.
- Layer compositor: the order above, including show idle falling through to lower layers.

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
