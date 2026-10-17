#pragma once
#include <stdint.h>

// Polls the Pi's player state in a background task.
void showStateBegin();
// True while a song is loaded (playing or paused). Unknown or unreachable counts as idle.
bool showPlaying(uint32_t nowMs);
// Treat the show as playing for a few seconds after a start is sent, so a quick
// follow-up gesture is not blocked while the next poll catches up.
void showAssumePlaying(uint32_t nowMs);
// When the last poll of the Pi succeeded; false if none has yet.
bool showLastPollOk(uint32_t& atMs);
// Same after a stop is sent, so a stale poll does not bring the play loop back.
void showAssumeStopped(uint32_t nowMs);
