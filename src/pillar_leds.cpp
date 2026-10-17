#include "pillar_leds.h"
#include <FastLED.h>
#include "config.h"
#include "pillar_mixer.h"
#include "pillar_player.h"
#include "plan_store.h"
#include "program_control.h"
#include "show_link.h"
#include "show_state.h"

static const uint16_t kLeds = 100;
static const uint32_t kFrameMs = 20;

static CRGB leds[kLeds];
static Rgb frame[kLeds];  // what the plans drew; frame steps snapshot this
static Rgb out[kLeds];    // frame plus the fail overlay
static uint32_t lastFrameMs = 0;

// Touched only from loop().
static PillarPlayer player(planStoreLookup);
static PillarMixer mixer(player);
static ProgramControl programControl;
static uint16_t programShown = 0;  // library id handed to the player, 0 = none
static bool failActive = false;
static uint32_t failStartMs = 0;

// Set from the sender task, consumed by loop().
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static bool failPending = false;

void pillarLedsBegin() {
    FastLED.addLeds<WS2812B, PILLAR_LED_PIN, GRB>(leds, kLeds);
    FastLED.setBrightness(PILLAR_BRIGHTNESS);
    FastLED.clear(true);
    player.reload(millis());  // plans were loaded from flash after the player was built
}

void pillarLedsPlay(Gesture gesture, uint32_t nowMs) {
    player.trigger(slotFor(gesture), nowMs);
}

void pillarLedsFail() {
    portENTER_CRITICAL(&mux);
    failPending = true;
    portEXIT_CRITICAL(&mux);
}

void pillarLedsUpdate(uint32_t nowMs, bool otaActive, bool showRunning) {
    if (nowMs - lastFrameMs < kFrameMs) return;
    lastFrameMs = nowMs;

    if (otaActive) {
        FastLED.clear(true);
        return;
    }

    portENTER_CRITICAL(&mux);
    bool fail = failPending;
    failPending = false;
    portEXIT_CRITICAL(&mux);
    if (fail) {
        failActive = true;
        failStartMs = nowMs;
    }

    if (planStoreApplyPending()) player.reload(nowMs);
    Plan preview;
    if (planStoreTakePreview(preview)) player.playPreview(preview, nowMs);
    if (planStoreTakePreviewEnd()) player.endPreview(nowMs);

    // Live show from the WebSocket.
    ShowEvent event;
    if (showLinkTakeEvent(event)) mixer.setShowEvent(event, nowMs);
    mixer.setShowConnected(showLinkConnected());
    Palette palette;
    if (planStoreTakePalette(palette)) mixer.setPalette(palette);

    // Commanded program: the latest command, resolved against the stored library.
    bool libraryChanged = planStoreApplyPrograms();
    PillarCommand command;
    while (showLinkTakeCommand(command)) programControl.offer(command);
    uint32_t pollAtMs;
    if (showLastPollOk(pollAtMs)) programControl.pollOk(pollAtMs);
    uint16_t wanted = programControl.wanted(nowMs);
    if (wanted != programShown || (libraryChanged && wanted != 0)) {
        Plan program;
        if (wanted != 0 && planStoreProgram(wanted, program)) {
            player.setProgram(program, nowMs);
            programShown = wanted;
        } else {
            player.clearProgram(nowMs);  // released, paused, or not in the library yet
            programShown = 0;
        }
    }

    player.setShowRunning(showRunning, nowMs);
    mixer.render(nowMs, frame, kLeds);
    if (player.takeProgramFinished()) {
        programControl.finished();
        programShown = 0;
    }
    for (uint16_t i = 0; i < kLeds; i++) out[i] = frame[i];
    if (failActive && !overlayFail(nowMs - failStartMs, out, kLeds)) failActive = false;

    for (uint16_t i = 0; i < kLeds; i++) leds[i] = CRGB(out[i].r, out[i].g, out[i].b);
    FastLED.show();
}
