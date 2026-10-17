#pragma once
#include <stddef.h>
#include <stdint.h>

// Messages on the WebSocket /api/buttons/ws (StageController show-events contract and
// pillar LED contract). The pillar only uses show events and its own program commands.

enum class ShowState : uint8_t { Idle, Launching, Intro, Playing, Failing, Other };
enum class PoleMode : uint8_t { Off, Glow, Solid, Pulse, Blink, Drain };

constexpr uint8_t kDefaultGlowLevel = 35;  // ambientGlowPct

struct PoleState {
    PoleMode mode = PoleMode::Off;
    uint8_t pct = 0;                 // 0..100, bottom-up fill
    uint8_t level = kDefaultGlowLevel;  // glow brightness 0..100
    bool hasMs = false;
    uint32_t ms = 0;                 // pulse period or drain duration
    char color[8] = "";              // palette name, "" when off
};

struct ShowEvent {
    ShowState state = ShowState::Idle;
    PoleState pole;  // this pillar's side
};

struct PillarCommand {
    uint32_t seq = 0;
    uint16_t program = 0;  // 0 = release
};

enum class WsMessage : uint8_t { Ignore, Show, Command };

// Show: fills `show` with the state and this side's pole (a missing pole is off).
// Command: a pillar_command for this side. Anything else, or bad JSON: Ignore.
WsMessage parseWsMessage(const char* json, size_t len, char side, ShowEvent& show, PillarCommand& cmd);

// The backup copy of this pillar's command in the state reply ("pillar_command").
bool parseStateCommand(const char* body, PillarCommand& out);
