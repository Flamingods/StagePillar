#pragma once
#include <stdint.h>
#include "pillar_player.h"
#include "pole_render.h"
#include "show_events.h"

// Top of the layer stack (pillar LED contract §9): preview > show event (state not idle) >
// everything PillarPlayer draws (gesture one-shot > program > slot base). Without a
// WebSocket connection the show counts as idle.
class PillarMixer {
public:
    explicit PillarMixer(PillarPlayer& player) : player_(player), palette_(defaultPalette()) {}

    void setShowEvent(const ShowEvent& event, uint32_t nowMs) {
        event_ = event;
        eventAtMs_ = nowMs;
    }
    void setShowConnected(bool connected) { connected_ = connected; }
    void setPalette(const Palette& palette) { palette_ = palette; }
    uint32_t paletteVersion() const { return palette_.version; }

    bool showOwnsLeds() const { return connected_ && event_.state != ShowState::Idle; }

    void render(uint32_t nowMs, Rgb* frame, uint16_t count) {
        if (!player_.previewActive() && showOwnsLeds()) {
            renderPole(event_.pole, nowMs - eventAtMs_, nowMs, palette_, frame, count);
            return;
        }
        player_.render(nowMs, frame, count);
    }

private:
    PillarPlayer& player_;
    Palette palette_;
    ShowEvent event_;
    uint32_t eventAtMs_ = 0;
    bool connected_ = false;
};
