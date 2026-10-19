#pragma once

#include <cstdint>
#include <string>

// "Finished / total" book counts per folder for the file browser.
//
// Counts are recursive (a category folder includes its sub-folders) and only
// include EPUB/XTC files, the formats that can be marked as finished. Results
// are cached in a small fixed-size RAM table and dropped whenever the finished
// set or the file tree changes, so walking the SD card happens once per
// folder visit after a change, not on every redraw.
namespace FolderReadCounts {

struct Counts {
  uint16_t finished = 0;
  uint16_t total = 0;
};

// Called once if a walk takes long enough that the user should see feedback.
using SlowWalkCallback = void (*)(void* user);

// Returns cached counts without touching the SD card.
bool lookupCached(const std::string& dirPath, Counts& out);

// Returns counts for `dirPath`, walking the tree when they are not cached.
// Returns false when the folder could not be read completely (too deep, too
// many entries, or an SD error); nothing is shown for it then.
bool compute(const std::string& dirPath, Counts& out, SlowWalkCallback onSlowWalk = nullptr, void* user = nullptr);

}  // namespace FolderReadCounts
