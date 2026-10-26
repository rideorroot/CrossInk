#pragma once

#include <GfxRenderer.h>

#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"

// Small check mark drawn after the reading-streak label once today's reading
// has extended the streak. Drawn with lines rather than a pre-rotated bitmap,
// so it follows every screen orientation.
namespace StreakCheck {

constexpr int SIZE = 16;
constexpr int GAP = 8;
constexpr int STROKE = 3;

// True when the RTC knows today's date and today is already recorded in the
// reading history (the streak's newest day is today).
inline bool extendedToday(const GlobalReadingStats& stats) {
  ReadingStatsDateTime today;
  return getCurrentLocalReadingStatsDateTime(today) && stats.readOnDay(today.date);
}

// Width the mark adds to a row, including the gap before it.
inline int trailingWidth() { return GAP + SIZE; }

// Draws the mark with its left edge at x, vertically centred on centerY.
inline void draw(const GfxRenderer& renderer, const int x, const int centerY, const bool black) {
  const int top = centerY - SIZE / 2;
  // Short stroke down-right, long stroke up-right.
  const int midX = x + SIZE * 3 / 8;
  const int bottomY = top + SIZE - 2;
  renderer.drawLine(x + 1, top + SIZE / 2, midX, bottomY, STROKE, black);
  renderer.drawLine(midX, bottomY, x + SIZE - 1, top + 2, STROKE, black);
}

}  // namespace StreakCheck
