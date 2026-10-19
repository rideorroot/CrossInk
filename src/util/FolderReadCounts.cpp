#include "FolderReadCounts.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>

#include <cctype>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>

#include "CrossPointSettings.h"
#include "FinishedBooksStore.h"

namespace FolderReadCounts {
namespace {

// 64 slots * 24 bytes ~= 1.5 KB of .bss. Enough for a categorised library;
// a miss only costs a re-walk of that folder.
constexpr size_t CACHE_SLOTS = 64;
// Guards against pathological trees (and the loop watchdog) on the C3.
constexpr uint32_t MAX_ENTRIES_PER_WALK = 4000;
constexpr int MAX_DEPTH = 12;
constexpr unsigned long SLOW_WALK_MS = 300;
// One shared name buffer per walk instead of one per recursion level keeps the
// loop-task stack flat regardless of folder depth.
constexpr size_t NAME_BUFFER_SIZE = 256;

struct Slot {
  uint64_t key = 0;
  uint32_t lastUse = 0;
  Counts counts;
  bool used = false;
};

struct CacheState {
  Slot slots[CACHE_SLOTS];
  uint32_t finishedGeneration = 0;
  uint32_t treeGeneration = 0;
  uint32_t useClock = 0;
  uint8_t showHidden = 0;
  bool valid = false;
};

CacheState& cache() {
  static CacheState state;
  return state;
}

std::mutex& cacheMutex() {
  static std::mutex mutex;
  return mutex;
}

uint64_t pathKey(const std::string& path) {
  size_t len = path.size();
  while (len > 1 && path[len - 1] == '/') --len;
  uint64_t hash = 1469598103934665603ULL;
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint8_t>(path[i]);
    hash *= 1099511628211ULL;
  }
  return hash;
}

void validateLocked() {
  auto& state = cache();
  const uint32_t finishedGeneration = FINISHED_BOOKS.generation();
  const uint32_t treeGeneration = library::fileTreeGeneration();
  const uint8_t showHidden = SETTINGS.showHiddenFiles ? 1 : 0;
  if (state.valid && state.finishedGeneration == finishedGeneration && state.treeGeneration == treeGeneration &&
      state.showHidden == showHidden) {
    return;
  }
  for (auto& slot : state.slots) slot.used = false;
  state.finishedGeneration = finishedGeneration;
  state.treeGeneration = treeGeneration;
  state.showHidden = showHidden;
  state.valid = true;
}

bool lookupLocked(const uint64_t key, Counts& out) {
  auto& state = cache();
  for (auto& slot : state.slots) {
    if (slot.used && slot.key == key) {
      slot.lastUse = ++state.useClock;
      out = slot.counts;
      return true;
    }
  }
  return false;
}

void storeLocked(const uint64_t key, const Counts& counts) {
  auto& state = cache();
  Slot* existing = nullptr;
  Slot* empty = nullptr;
  Slot* oldest = nullptr;
  for (auto& slot : state.slots) {
    if (slot.used && slot.key == key) {
      existing = &slot;
      break;
    }
    if (!slot.used) {
      if (!empty) empty = &slot;
    } else if (!oldest || slot.lastUse < oldest->lastUse) {
      oldest = &slot;
    }
  }
  Slot* target = existing ? existing : (empty ? empty : oldest);
  if (!target) return;
  target->key = key;
  target->counts = counts;
  target->used = true;
  target->lastUse = ++state.useClock;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

// Mirrors the file browser's visibility rules so the numbers match what the
// user can actually browse to.
bool skipEntry(const char* name) {
  const std::string_view view{name};
  if (view.empty() || view.rfind("._", 0) == 0) return true;
  if (name[0] == '.') {
    // App cache/metadata is never a book folder, even with hidden files shown.
    if (view == ".crosspoint" || view == ".DS_Store" || view == ".Spotlight-V100" || view == ".Trashes" ||
        view == ".fseventsd") {
      return true;
    }
    return !SETTINGS.showHiddenFiles;
  }
  return equalsIgnoreCase(view, "System Volume Information") || equalsIgnoreCase(view, "$RECYCLE.BIN");
}

uint16_t saturatingAdd(const uint16_t a, const uint32_t b) {
  const uint32_t sum = static_cast<uint32_t>(a) + b;
  return sum > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(sum);
}

struct WalkContext {
  std::unique_ptr<char[]> name;
  uint32_t visited = 0;
  unsigned long startMs = 0;
  SlowWalkCallback onSlowWalk = nullptr;
  void* user = nullptr;
  bool slowReported = false;
};

bool walkLocked(WalkContext& ctx, std::string& path, int depth, Counts& counts);

bool countsForLocked(WalkContext& ctx, std::string& path, const int depth, Counts& out) {
  const uint64_t key = pathKey(path);
  if (lookupLocked(key, out)) return true;
  Counts counts;
  if (!walkLocked(ctx, path, depth, counts)) return false;
  storeLocked(key, counts);
  out = counts;
  return true;
}

bool walkLocked(WalkContext& ctx, std::string& path, const int depth, Counts& counts) {
  if (depth > MAX_DEPTH) {
    LOG_DBG("FOLDERCNT", "Too deep, skipping counts: %s", path.c_str());
    return false;
  }
  auto dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  const size_t baseLen = path.size();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (++ctx.visited > MAX_ENTRIES_PER_WALK) {
      LOG_INF("FOLDERCNT", "Entry budget exhausted under %s", path.c_str());
      entry.close();
      dir.close();
      return false;
    }
    if (!ctx.slowReported && ctx.onSlowWalk && (ctx.visited & 0x0F) == 0 && millis() - ctx.startMs > SLOW_WALK_MS) {
      ctx.slowReported = true;
      ctx.onSlowWalk(ctx.user);
    }

    entry.getName(ctx.name.get(), NAME_BUFFER_SIZE);
    const bool isDir = entry.isDirectory();
    entry.close();
    if (skipEntry(ctx.name.get())) continue;

    if (path.back() != '/') path += '/';
    path += ctx.name.get();
    bool ok = true;
    if (isDir) {
      Counts sub;
      ok = countsForLocked(ctx, path, depth + 1, sub);
      if (ok) {
        counts.finished = saturatingAdd(counts.finished, sub.finished);
        counts.total = saturatingAdd(counts.total, sub.total);
      }
    } else if (FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path)) {
      counts.total = saturatingAdd(counts.total, 1);
      if (FINISHED_BOOKS.isFinishedBook(path)) counts.finished = saturatingAdd(counts.finished, 1);
    }
    path.resize(baseLen);
    if (!ok) {
      dir.close();
      return false;
    }
  }

  const bool failed = FsHelpers::directoryIterationFailed(dir);
  dir.close();
  if (failed) LOG_ERR("FOLDERCNT", "Directory listing failed: %s", path.c_str());
  return !failed;
}

}  // namespace

bool lookupCached(const std::string& dirPath, Counts& out) {
  std::lock_guard<std::mutex> lock(cacheMutex());
  validateLocked();
  return lookupLocked(pathKey(dirPath), out);
}

bool compute(const std::string& dirPath, Counts& out, const SlowWalkCallback onSlowWalk, void* user) {
  if (dirPath.empty()) return false;
  // Load the finished index before taking our lock so a first-time rebuild
  // happens outside the walk and does not count against its time budget.
  FINISHED_BOOKS.warmUp();

  std::lock_guard<std::mutex> lock(cacheMutex());
  validateLocked();
  if (lookupLocked(pathKey(dirPath), out)) return true;

  WalkContext ctx;
  ctx.name = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!ctx.name) {
    LOG_ERR("FOLDERCNT", "Name buffer alloc failed");
    return false;
  }
  ctx.startMs = millis();
  ctx.onSlowWalk = onSlowWalk;
  ctx.user = user;

  std::string path = dirPath;
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  path.reserve(path.size() + 128);
  const bool ok = countsForLocked(ctx, path, 0, out);
  LOG_DBG("FOLDERCNT", "%s: %u/%u (%u entries, %lu ms)", dirPath.c_str(), out.finished, out.total,
          static_cast<unsigned>(ctx.visited), millis() - ctx.startMs);
  return ok;
}

}  // namespace FolderReadCounts
