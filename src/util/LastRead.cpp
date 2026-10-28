#include "LastRead.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "CrossPointState.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "fontIds.h"

namespace LastRead {

uint32_t pack(const uint16_t year, const uint8_t month, const uint8_t day, const uint8_t hour, const uint8_t minute) {
  // year-2000 : 8 bits | month : 4 | day : 5 | hour : 5 | minute : 6, plus a
  // valid bit so a stored midnight on 1.1.2000 is still non-zero.
  const uint32_t y = year >= 2000 ? static_cast<uint32_t>(year - 2000) & 0xFF : 0;
  return (1u << 31) | (y << 20) | ((month & 0x0Fu) << 16) | ((day & 0x1Fu) << 11) | ((hour & 0x1Fu) << 6) |
         (minute & 0x3Fu);
}

void markNow() {
  ReadingStatsDateTime now;
  if (!getCurrentLocalReadingStatsDateTime(now)) return;
  APP_STATE.lastReadStamp = pack(now.date.year, now.date.month, now.date.day, now.hour, now.minute);
}

bool format(char* buf, const size_t len) {
  const uint32_t stamp = APP_STATE.lastReadStamp;
  if (len == 0 || (stamp & (1u << 31)) == 0) return false;
  const unsigned month = (stamp >> 16) & 0x0F;
  const unsigned day = (stamp >> 11) & 0x1F;
  const unsigned hour = (stamp >> 6) & 0x1F;
  const unsigned minute = stamp & 0x3F;
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59) return false;
  snprintf(buf, len, tr(STR_LAST_READ_FORMAT), day, month, hour, minute);
  return true;
}

int drawFootnote(const GfxRenderer& renderer, const bool black) {
  constexpr int BOTTOM_MARGIN = 6;
  const int screenH = renderer.getScreenHeight();
  char text[48];
  if (!format(text, sizeof(text))) return screenH;
  const int y = screenH - renderer.getLineHeight(SMALL_FONT_ID) - BOTTOM_MARGIN;
  const int w = renderer.getTextWidth(SMALL_FONT_ID, text);
  renderer.drawText(SMALL_FONT_ID, (renderer.getScreenWidth() - w) / 2, y, text, black);
  return y;
}

}  // namespace LastRead
