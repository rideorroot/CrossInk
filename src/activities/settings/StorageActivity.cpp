#include "StorageActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Upper bound on entries visited, so a pathological card cannot keep the
// screen busy indefinitely. The result is then marked as approximate.
constexpr uint32_t MAX_SCAN_ENTRIES = 60000;
constexpr unsigned long PROGRESS_RENDER_MS = 700;
constexpr int BAR_HEIGHT = 34;
constexpr int BAR_BORDER = 2;
constexpr int BAR_RADIUS = 6;
constexpr int SWATCH_SIZE = 18;
constexpr int MIN_SEGMENT_WIDTH = 3;

bool isBookFile(const std::string_view name) {
  return FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) || FsHelpers::hasTxtExtension(name) ||
         FsHelpers::hasMarkdownExtension(name);
}

// Hidden folders hold CrossInk's own data (.crosspoint), fonts, dictionaries
// and OS metadata; they all count as System.
bool skipDirectory(const char* name) { return name[0] == '.' || std::strcmp(name, "System Volume Information") == 0; }

// Decimal units, like the iPhone and macOS storage views.
void formatBytes(const uint64_t bytes, char* out, const size_t len) {
  const char* sep = tr(STR_STORAGE_DECIMAL_SEP);
  constexpr uint64_t KB = 1000ULL;
  constexpr uint64_t MB = KB * 1000ULL;
  constexpr uint64_t GB = MB * 1000ULL;
  if (bytes >= GB) {
    const uint64_t tenths = (bytes + GB / 20) / (GB / 10);
    snprintf(out, len, "%llu%s%llu GB", static_cast<unsigned long long>(tenths / 10), sep,
             static_cast<unsigned long long>(tenths % 10));
  } else if (bytes >= MB) {
    snprintf(out, len, "%llu MB", static_cast<unsigned long long>((bytes + MB / 2) / MB));
  } else {
    snprintf(out, len, "%llu KB", static_cast<unsigned long long>((bytes + KB / 2) / KB));
  }
}

enum class Fill : uint8_t { Solid, Shaded, Empty };

void fillSegment(const GfxRenderer& renderer, const int x, const int y, const int w, const int h, const Fill fill) {
  if (w <= 0 || h <= 0) return;
  switch (fill) {
    case Fill::Solid:
      renderer.fillRect(x, y, w, h, true);
      break;
    case Fill::Shaded:
      renderer.fillRectDither(x, y, w, h, DarkGray);
      break;
    case Fill::Empty:
      break;
  }
}
}  // namespace

void StorageActivity::onEnter() {
  Activity::onEnter();
  totalBytes = Storage.totalBytes();
  usedBytes = std::min(Storage.usedBytes(), totalBytes);
  bookBytes = 0;
  bookCount = 0;
  visitedEntries = 0;
  scanTruncated = false;
  pendingDirs.clear();
  if (totalBytes == 0) {
    LOG_ERR("STORAGE", "SD card size unavailable");
    state = State::Unavailable;
  } else {
    pendingDirs.reserve(16);
    pendingDirs.emplace_back("/");
    state = State::Scanning;
  }
  lastProgressRender = millis();
  requestUpdate();
}

void StorageActivity::onExit() {
  Activity::onExit();
  pendingDirs.clear();
  pendingDirs.shrink_to_fit();
}

void StorageActivity::scanNextDirectory() {
  std::string path = std::move(pendingDirs.back());
  pendingDirs.pop_back();

  auto dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    LOG_ERR("STORAGE", "Cannot open %s", path.c_str());
    return;
  }

  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (++visitedEntries > MAX_SCAN_ENTRIES) {
      entry.close();
      scanTruncated = true;
      pendingDirs.clear();
      break;
    }
    entry.getName(name, sizeof(name));
    const bool isDir = entry.isDirectory();
    const uint64_t size = isDir ? 0 : entry.fileSize();
    entry.close();
    if (isDir) {
      if (skipDirectory(name)) continue;
      std::string child = path;
      if (child.back() != '/') child += '/';
      child += name;
      pendingDirs.push_back(std::move(child));
    } else if (name[0] != '.' && isBookFile(name)) {
      bookBytes += size;
      ++bookCount;
    }
  }
  dir.close();
}

void StorageActivity::finishScan() {
  // File sizes ignore cluster slack, so clamp to what the card reports as used.
  bookBytes = std::min(bookBytes, usedBytes);
  LOG_INF("STORAGE", "total=%llu used=%llu books=%llu (%u files, %u entries%s)",
          static_cast<unsigned long long>(totalBytes), static_cast<unsigned long long>(usedBytes),
          static_cast<unsigned long long>(bookBytes), static_cast<unsigned>(bookCount),
          static_cast<unsigned>(visitedEntries), scanTruncated ? ", truncated" : "");
  state = State::Done;
  requestUpdate();
}

void StorageActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (state != State::Scanning) return;
  if (pendingDirs.empty()) {
    finishScan();
    return;
  }
  scanNextDirectory();
  if (millis() - lastProgressRender >= PROGRESS_RENDER_MS) {
    lastProgressRender = millis();
    requestUpdate();
  }
}

void StorageActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  renderer.clearScreen();

  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, header, tr(STR_STORAGE), false);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_STORAGE));
  }

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (state == State::Unavailable) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_STORAGE_UNAVAILABLE));
    renderer.displayBuffer();
    return;
  }

  const int left = metrics.contentSidePadding;
  const int width = std::max(1, pageWidth - metrics.contentSidePadding * 2);
  int y = header.y + header.height + metrics.verticalSpacing * 2;

  char totalText[24];
  char usedText[24];
  formatBytes(totalBytes, totalText, sizeof(totalText));
  formatBytes(usedBytes, usedText, sizeof(usedText));
  char headline[96];
  snprintf(headline, sizeof(headline), tr(STR_STORAGE_USED_OF), usedText, totalText);
  renderer.drawText(UI_12_FONT_ID, left, y, headline, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

  const bool scanning = state == State::Scanning;
  const uint64_t freeBytes = totalBytes - usedBytes;
  // While the walk runs only used/free is known; books appear when it finishes.
  const uint64_t books = scanning ? 0 : bookBytes;
  const uint64_t system = usedBytes - books;

  // Segment widths: proportional, but any non-empty share stays visible.
  const int innerX = left + BAR_BORDER;
  const int innerY = y + BAR_BORDER;
  const int innerW = width - BAR_BORDER * 2;
  const int innerH = BAR_HEIGHT - BAR_BORDER * 2;
  const auto share = [&](const uint64_t part) {
    if (part == 0) return 0;
    const int w = static_cast<int>(static_cast<double>(part) * innerW / static_cast<double>(totalBytes));
    return std::max(w, MIN_SEGMENT_WIDTH);
  };
  const int systemW = std::min(share(system), innerW);
  const int booksW = std::min(share(books), innerW - systemW);
  renderer.drawRoundedRect(left, y, width, BAR_HEIGHT, BAR_BORDER, BAR_RADIUS, true);
  fillSegment(renderer, innerX, innerY, systemW, innerH, Fill::Solid);
  fillSegment(renderer, innerX + systemW, innerY, booksW, innerH, Fill::Shaded);
  if (booksW > 0) renderer.drawLine(innerX + systemW, innerY, innerX + systemW, innerY + innerH - 1, true);
  y += BAR_HEIGHT + metrics.verticalSpacing * 2;

  // Legend: swatch, label, size (right-aligned), optional detail line.
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const auto legendRow = [&](const Fill fill, const char* label, const uint64_t bytes, const char* detail,
                             const bool known) {
    const int swatchY = y + (lineHeight - SWATCH_SIZE) / 2;
    renderer.drawRect(left, swatchY, SWATCH_SIZE, SWATCH_SIZE, true);
    fillSegment(renderer, left + 1, swatchY + 1, SWATCH_SIZE - 2, SWATCH_SIZE - 2, fill);
    renderer.drawText(UI_10_FONT_ID, left + SWATCH_SIZE + 12, y, label);
    char sizeText[24];
    if (known) {
      formatBytes(bytes, sizeText, sizeof(sizeText));
    } else {
      snprintf(sizeText, sizeof(sizeText), "%s", tr(STR_STORAGE_SCANNING));
    }
    const int sizeW = renderer.getTextWidth(UI_10_FONT_ID, sizeText);
    renderer.drawText(UI_10_FONT_ID, left + width - sizeW, y, sizeText);
    y += lineHeight;
    if (detail && detail[0] != '\0') {
      renderer.drawText(SMALL_FONT_ID, left + SWATCH_SIZE + 12, y, detail);
      y += smallLineHeight;
    }
    y += metrics.verticalSpacing;
  };

  char bookDetail[64] = "";
  if (!scanning) {
    snprintf(bookDetail, sizeof(bookDetail), tr(STR_STORAGE_BOOK_COUNT), static_cast<unsigned>(bookCount));
    if (scanTruncated) std::strncat(bookDetail, " (~)", sizeof(bookDetail) - std::strlen(bookDetail) - 1);
  }
  legendRow(Fill::Solid, tr(STR_STORAGE_SYSTEM), system, scanning ? "" : tr(STR_STORAGE_SYSTEM_HINT), !scanning);
  legendRow(Fill::Shaded, tr(STR_STORAGE_BOOKS), books, bookDetail, !scanning);
  legendRow(Fill::Empty, tr(STR_STORAGE_FREE), freeBytes, "", true);

  if (scanning) {
    char progress[64];
    snprintf(progress, sizeof(progress), tr(STR_STORAGE_SCAN_PROGRESS), static_cast<unsigned>(bookCount));
    renderer.drawText(SMALL_FONT_ID, left, y, progress);
  }

  renderer.displayBuffer();
}
