#include "show_state.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include "config.h"
#include "net.h"
#include "plan_store.h"
#include "show_gate.h"
#include "show_latch.h"
#include "show_link.h"
#include "urls.h"

static ShowLatch latch;
static volatile bool lastReported = false;
static volatile uint32_t lastPollOkMs = 0;
static volatile bool everPolled = false;

// Polls the player state, reporting which plans version the pillar runs, and tells the
// plan store what the server offers. Returns whether the show is running.
static bool pollState() {
    if (!netConnected()) return false;
    IPAddress server = netServerIp();
    char path[96];
    if (!pollPath(path, sizeof(path), PILLAR_SIDE, planStoreVersion())) return false;
    String url = "http://" + server.toString() + ":" + String(SERVER_PORT) + path;
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(url)) return false;
    int code = http.GET();
    String body = code == 200 ? http.getString() : String();
    http.end();
    if (code != 200) return false;

    lastPollOkMs = millis();
    everPolled = true;
    uint32_t version = 0, previewId = 0, programsVersion = 0, paletteVersion = 0;
    if (parseUintField(body.c_str(), "pillar_plans_version", version)) {
        parseUintField(body.c_str(), "pillar_preview_id", previewId);
        parseUintField(body.c_str(), "pillar_programs_version", programsVersion);
        parseUintField(body.c_str(), "show_palette_version", paletteVersion);
        planStoreSetTarget(version, previewId, programsVersion, paletteVersion);
    }
    // Backup copy of this side's program command, in case a socket message was missed.
    PillarCommand command;
    if (parseStateCommand(body.c_str(), command) && command.seq > 0) showLinkOfferCommand(command);
    return parseShowRunning(body.c_str());
}

static void pollTask(void*) {
    for (;;) {
        bool now = pollState();
        if (now != lastReported) Serial.printf("Show: %s\n", now ? "running" : "idle");
        lastReported = now;
        latch.report(now);
        vTaskDelay(pdMS_TO_TICKS(SHOW_POLL_MS));
    }
}

void showStateBegin() {
    xTaskCreate(pollTask, "show", 8192, nullptr, 1, nullptr);
}

bool showPlaying(uint32_t nowMs) {
    return latch.running(nowMs);
}

bool showLastPollOk(uint32_t& atMs) {
    atMs = lastPollOkMs;
    return everPolled;
}

void showAssumePlaying(uint32_t nowMs) {
    latch.assumeStarted(nowMs);
}

void showAssumeStopped(uint32_t nowMs) {
    latch.assumeStopped(nowMs);
}
