#pragma once
#include <stdint.h>
#include "show_events.h"

// The WebSocket to /api/buttons/ws (show-events contract): show events and this pillar's
// program commands. Runs in its own task; loop() takes what arrived.
void showLinkBegin();
bool showLinkConnected();
// Latest show event since the last call, if any.
bool showLinkTakeEvent(ShowEvent& out);
// Next program command (also fed by the state-reply backup), if any.
bool showLinkTakeCommand(PillarCommand& out);
void showLinkOfferCommand(const PillarCommand& command);
