#pragma once

#include <cstdint>

// CrossInk: the sleep screen is drawn once and then held by the e-ink panel.
// When it shows today's streak check mark, arm a timer that wakes the device
// shortly after local midnight so the sleep screen can be redrawn without the
// mark. The RC sleep clock drifts, so an early wake re-arms and sleeps again
// without touching the display.
namespace MidnightWake {

enum class BootAction : uint8_t { None, SleepAgain, Redraw };

// Call before the sleep screen is drawn.
void beginSleepScreen();
// Call from the streak check renderer.
void noteStreakCheckDrawn();
// Call after the sleep screen is drawn; arms the timer when needed.
bool armAfterSleepScreen(bool fromTimeout);

// Call once at boot, after the clock and settings are available.
BootAction onBoot();
// fromTimeout of the sleep that armed the redraw.
bool redrawFromTimeout();
// Re-arm after an early wake (call right before deep sleep).
void armRemaining();

}  // namespace MidnightWake
