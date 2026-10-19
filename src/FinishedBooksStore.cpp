#include "FinishedBooksStore.h"

#include <Arduino.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>

#include "activities/reader/BookReadingStats.h"

namespace {
constexpr char INDEX_PATH[] = "/.crosspoint/finished_books.bin";
constexpr char INDEX_TMP_PATH[] = "/.crosspoint/finished_books.bin.tmp";
constexpr char CACHE_ROOT[] = "/.crosspoint";
constexpr uint8_t INDEX_MAGIC[4] = {'F', 'B', 'I', '1'};
// A finished-book index this large would mean ~400 KB on the C3; refuse it
// as corrupt rather than trying to allocate.
constexpr uint32_t MAX_INDEX_ENTRIES = 20000;

uint64_t fnv1a64(const char* data, const size_t len) {
  uint64_t hash = 1469598103934665603ULL;
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint8_t>(data[i]);
    hash *= 1099511628211ULL;
  }
  return hash;
}

void writeLe32(uint8_t* out, const uint32_t value) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

void writeLe64(uint8_t* out, const uint64_t value) {
  for (int i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t readLe32(const uint8_t* in) {
  uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(in[i]) << (8 * i);
  return value;
}

uint64_t readLe64(const uint8_t* in) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(in[i]) << (8 * i);
  return value;
}

bool isBookCacheDirName(const char* name) {
  return std::strncmp(name, "epub_", 5) == 0 || std::strncmp(name, "xtc_", 4) == 0;
}
}  // namespace

FinishedBooksStore& FinishedBooksStore::getInstance() {
  static FinishedBooksStore instance;
  return instance;
}

uint64_t FinishedBooksStore::keyForCachePath(const std::string& cachePath) {
  // Hash only the directory name so "/.crosspoint/epub_1" and a trailing
  // slash variant map to the same key.
  size_t end = cachePath.size();
  while (end > 0 && cachePath[end - 1] == '/') --end;
  if (end == 0) return fnv1a64("", 0);
  const size_t slash = cachePath.rfind('/', end - 1);
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  return fnv1a64(cachePath.data() + start, end - start);
}

bool FinishedBooksStore::keyForBookPath(const std::string& bookPath, uint64_t& key) {
  if (FsHelpers::hasEpubExtension(bookPath)) {
    // Pure path hash: no SD access (unlike constructing an Epub, which may
    // probe for a legacy cache directory).
    key = keyForCachePath(Epub::cachePathForFilePath(bookPath, CACHE_ROOT));
    return true;
  }
  if (FsHelpers::hasXtcExtension(bookPath)) {
    key = keyForCachePath(Xtc(bookPath, CACHE_ROOT).getCachePath());
    return true;
  }
  return false;
}

bool FinishedBooksStore::isFinishedBook(const std::string& bookPath) {
  uint64_t key = 0;
  if (!keyForBookPath(bookPath, key)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ensureLoadedLocked()) return false;
  return containsLocked(key);
}

void FinishedBooksStore::warmUp() {
  std::lock_guard<std::mutex> lock(mutex_);
  ensureLoadedLocked();
}

void FinishedBooksStore::onStatsSaved(const std::string& cachePath, const bool completed) {
  if (cachePath.empty()) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ensureLoadedLocked()) return;
  // Also corrects an index that drifted (e.g. stats edited on another device).
  if (setLocked(keyForCachePath(cachePath), completed)) saveLocked();
}

void FinishedBooksStore::onStatsRemoved(const std::string& cachePath) { onStatsSaved(cachePath, false); }

void FinishedBooksStore::onCacheDirRenamed(const std::string& oldCachePath, const std::string& newCachePath) {
  if (oldCachePath.empty() || newCachePath.empty()) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ensureLoadedLocked()) return;
  const uint64_t oldKey = keyForCachePath(oldCachePath);
  const uint64_t newKey = keyForCachePath(newCachePath);
  if (oldKey == newKey) return;
  const bool wasFinished = containsLocked(oldKey);
  bool changed = setLocked(oldKey, false);
  changed = setLocked(newKey, wasFinished) || changed;
  if (changed) saveLocked();
}

bool FinishedBooksStore::containsLocked(const uint64_t key) const {
  return std::binary_search(keys_.begin(), keys_.end(), key);
}

bool FinishedBooksStore::setLocked(const uint64_t key, const bool finished) {
  const auto it = std::lower_bound(keys_.begin(), keys_.end(), key);
  const bool present = it != keys_.end() && *it == key;
  if (present == finished) return false;
  if (finished) {
    keys_.insert(it, key);
  } else {
    keys_.erase(it);
  }
  generation_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool FinishedBooksStore::ensureLoadedLocked() {
  if (loaded_) return true;
  if (!loadFromFileLocked()) {
    rebuildFromCachesLocked();
    saveLocked();
  }
  loaded_ = true;
  generation_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool FinishedBooksStore::loadFromFileLocked() {
  keys_.clear();
  if (!Storage.exists(INDEX_PATH)) return false;

  FsFile file;
  if (!Storage.openFileForRead("FINISHED", INDEX_PATH, file)) {
    LOG_ERR("FINISHED", "Could not open %s", INDEX_PATH);
    return false;
  }

  uint8_t header[8];
  if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) ||
      std::memcmp(header, INDEX_MAGIC, sizeof(INDEX_MAGIC)) != 0) {
    LOG_ERR("FINISHED", "Invalid finished-books index header, rebuilding");
    file.close();
    return false;
  }
  const uint32_t count = readLe32(header + 4);
  if (count > MAX_INDEX_ENTRIES) {
    LOG_ERR("FINISHED", "Finished-books index too large (%u), rebuilding", static_cast<unsigned>(count));
    file.close();
    return false;
  }

  keys_.reserve(count);
  uint8_t entry[8];
  for (uint32_t i = 0; i < count; ++i) {
    if (file.read(entry, sizeof(entry)) != static_cast<int>(sizeof(entry))) {
      LOG_ERR("FINISHED", "Truncated finished-books index, rebuilding");
      file.close();
      keys_.clear();
      return false;
    }
    keys_.push_back(readLe64(entry));
  }
  file.close();

  std::sort(keys_.begin(), keys_.end());
  keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());
  LOG_INF("FINISHED", "Loaded %u finished books", static_cast<unsigned>(keys_.size()));
  return true;
}

void FinishedBooksStore::rebuildFromCachesLocked() {
  keys_.clear();
  auto root = Storage.open(CACHE_ROOT);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    LOG_INF("FINISHED", "No cache directory yet; starting with an empty index");
    return;
  }

  // One-time migration cost: one stats read per book that has ever been opened.
  const unsigned long start = millis();
  char name[64];
  size_t scanned = 0;
  for (auto entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    const bool isDir = entry.isDirectory();
    entry.getName(name, sizeof(name));
    entry.close();
    if (!isDir || !isBookCacheDirName(name)) continue;
    ++scanned;
    const std::string cachePath = std::string(CACHE_ROOT) + "/" + name;
    if (BookReadingStats::load(cachePath).isCompleted) {
      keys_.push_back(keyForCachePath(cachePath));
    }
  }
  root.close();

  std::sort(keys_.begin(), keys_.end());
  keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());
  LOG_INF("FINISHED", "Rebuilt finished-books index: %u of %u cached books finished (%lu ms)",
          static_cast<unsigned>(keys_.size()), static_cast<unsigned>(scanned), millis() - start);
}

bool FinishedBooksStore::saveLocked() const {
  Storage.ensureDirectoryExists(CACHE_ROOT);
  if (Storage.exists(INDEX_TMP_PATH)) Storage.remove(INDEX_TMP_PATH);

  FsFile file;
  if (!Storage.openFileForWrite("FINISHED", INDEX_TMP_PATH, file)) {
    LOG_ERR("FINISHED", "Could not write %s", INDEX_TMP_PATH);
    return false;
  }

  uint8_t header[8];
  std::memcpy(header, INDEX_MAGIC, sizeof(INDEX_MAGIC));
  writeLe32(header + 4, static_cast<uint32_t>(keys_.size()));
  bool ok = file.write(header, sizeof(header)) == sizeof(header);

  uint8_t entry[8];
  for (size_t i = 0; ok && i < keys_.size(); ++i) {
    writeLe64(entry, keys_[i]);
    ok = file.write(entry, sizeof(entry)) == sizeof(entry);
  }
  ok = ok && file.sync();
  ok = file.close() && ok;
  if (!ok) {
    LOG_ERR("FINISHED", "Short write for %s", INDEX_TMP_PATH);
    Storage.remove(INDEX_TMP_PATH);
    return false;
  }

  // If power fails between these two steps the index is simply missing and is
  // rebuilt from the per-book stats on next use.
  if (Storage.exists(INDEX_PATH) && !Storage.remove(INDEX_PATH)) {
    LOG_ERR("FINISHED", "Could not replace %s", INDEX_PATH);
    Storage.remove(INDEX_TMP_PATH);
    return false;
  }
  if (!Storage.rename(INDEX_TMP_PATH, INDEX_PATH)) {
    LOG_ERR("FINISHED", "Could not publish %s", INDEX_PATH);
    return false;
  }
  return true;
}
