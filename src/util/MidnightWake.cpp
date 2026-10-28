#include "MidnightWake.h"

#include <HalClock.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "activities/reader/ReadingStatsUtils.h"

#ifndef SIMULATOR
#include <esp_attr.h>
#include <esp_sleep.h>
#endif

namespace MidnightWake {
namespace {

constexpr uint32_t MAGIC = 0x4D4E5754;  // "MNWT"
// Land safely after midnight even with a few minutes of clock skew.
constexpr uint32_t AFTER_MIDNIGHT_SECONDS = 120;
// Early wakes re-arm; stop after a few so a broken clock cannot loop.
constexpr uint8_t MAX_REARMS = 6;

#ifndef SIMULATOR
RTC_DATA_ATTR uint32_t rtcMagic = 0;
RTC_DATA_ATTR uint32_t rtcArmedDay = 0;
RTC_DATA_ATTR uint8_t rtcRearms = 0;
RTC_DATA_ATTR uint8_t rtcFromTimeout = 0;
#else
uint32_t rtcMagic = 0;
uint32_t rtcArmedDay = 0;
uint8_t rtcRearms = 0;
uint8_t rtcFromTimeout = 0;
#endif

bool streakCheckDrawn = false;
uint32_t pendingSeconds = 0;

bool secondsUntilMidnight(uint32_t& seconds, uint32_t& today) {
  ReadingStatsDateTime now;
  if (!getCurrentLocalReadingStatsDateTime(now)) return false;
  today = readingStatsDayIndex(now.date);
  const uint32_t elapsed = static_cast<uint32_t>(now.hour) * 3600 + static_cast<uint32_t>(now.minute) * 60;
  seconds = 24 * 3600 - elapsed + AFTER_MIDNIGHT_SECONDS;
  return true;
}

void enableTimer(const uint32_t seconds) {
#ifndef SIMULATOR
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
#endif
  LOG_INF("MIDNIGHT", "Sleep screen redraw armed in %u s", static_cast<unsigned>(seconds));
}

}  // namespace

void beginSleepScreen() { streakCheckDrawn = false; }

void noteStreakCheckDrawn() { streakCheckDrawn = true; }

bool armAfterSleepScreen(const bool fromTimeout) {
  rtcMagic = 0;
  if (!streakCheckDrawn || SETTINGS.sleepStreakMidnightRefresh == 0 || !halClock.isAvailable()) return false;
  uint32_t seconds = 0;
  uint32_t today = 0;
  if (!secondsUntilMidnight(seconds, today)) return false;
  rtcMagic = MAGIC;
  rtcArmedDay = today;
  rtcRearms = 0;
  rtcFromTimeout = fromTimeout ? 1 : 0;
  enableTimer(seconds);
  return true;
}

BootAction onBoot() {
#ifdef SIMULATOR
  rtcMagic = 0;
  return BootAction::None;
#else
  const bool timerWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  const bool armed = rtcMagic == MAGIC;
  if (!timerWake || !armed) {
    rtcMagic = 0;
    return BootAction::None;
  }
  uint32_t seconds = 0;
  uint32_t today = 0;
  if (secondsUntilMidnight(seconds, today) && today == rtcArmedDay && rtcRearms < MAX_REARMS) {
    // Woke before midnight: the slow clock ran fast.
    ++rtcRearms;
    pendingSeconds = seconds;
    LOG_INF("MIDNIGHT", "Woke early, sleeping again (%u)", static_cast<unsigned>(rtcRearms));
    return BootAction::SleepAgain;
  }
  rtcMagic = 0;
  LOG_INF("MIDNIGHT", "Redrawing sleep screen after midnight");
  return BootAction::Redraw;
#endif
}

bool redrawFromTimeout() { return rtcFromTimeout != 0; }

void armRemaining() {
  if (pendingSeconds > 0) enableTimer(pendingSeconds);
}

}  // namespace MidnightWake
