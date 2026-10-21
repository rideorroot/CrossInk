#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

// Settings > System > Storage: SD card usage split into System, Books and Free,
// drawn as one segmented bar with a legend.
//
// Card totals come from the filesystem (fast). The Books share needs a walk of
// the visible folders; it runs one directory per loop() pass so the screen
// stays responsive and shows progress on large libraries.
class StorageActivity final : public Activity {
 public:
  explicit StorageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Storage", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
#ifdef SIMULATOR
  bool simulatorScanDone() const { return state == State::Done; }
  uint32_t simulatorBookCount() const { return bookCount; }
  uint64_t simulatorBookBytes() const { return bookBytes; }
#endif

 private:
  enum class State : uint8_t { Scanning, Done, Unavailable };

  void scanNextDirectory();
  void finishScan();

  State state = State::Scanning;
  uint64_t totalBytes = 0;
  uint64_t usedBytes = 0;
  uint64_t bookBytes = 0;
  uint32_t bookCount = 0;
  uint32_t visitedEntries = 0;
  bool scanTruncated = false;
  unsigned long lastProgressRender = 0;
  std::vector<std::string> pendingDirs;
};
