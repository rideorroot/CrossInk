#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Compact SD-backed index of which books are marked as finished.
//
// The source of truth stays the per-book reading stats file
// (`<cache>/stats_v5.bin`, field `isCompleted`). Reading that file for every
// row of the file browser (and for every book under a folder when counting
// "read/total") would cost one SD open per book, so this store keeps a sorted
// set of 64-bit keys in RAM, one per finished book, persisted to
// /.crosspoint/finished_books.bin.
//
// The key is a hash of the book's cache directory name (e.g. "epub_123..."),
// which is itself derived from the book path. That lets BookReadingStats::save()
// keep the index in sync for every code path that changes completion (reader,
// file browser, library, stats editor) without knowing the book path.
//
// If the index file is missing or unreadable it is rebuilt once by scanning
// the cache directories under /.crosspoint.
class FinishedBooksStore {
 public:
  static FinishedBooksStore& getInstance();

  // True when the book at `bookPath` (EPUB/XTC) is marked as finished.
  // Loads (or rebuilds) the index on first use.
  bool isFinishedBook(const std::string& bookPath);

  // Loads the index ahead of time so later lookups stay in RAM.
  void warmUp();

  // Notifications from the stats layer. `cachePath` is the book cache directory.
  void onStatsSaved(const std::string& cachePath, bool completed);
  void onStatsRemoved(const std::string& cachePath);
  void onCacheDirRenamed(const std::string& oldCachePath, const std::string& newCachePath);

  // Bumped every time the finished set changes; lets folder counters drop
  // stale cached numbers.
  uint32_t generation() const { return generation_.load(std::memory_order_relaxed); }

  // Cache key for a book path, or false when the file type has no completion state.
  static bool keyForBookPath(const std::string& bookPath, uint64_t& key);
  static uint64_t keyForCachePath(const std::string& cachePath);

 private:
  FinishedBooksStore() = default;

  bool ensureLoadedLocked();
  bool loadFromFileLocked();
  void rebuildFromCachesLocked();
  bool saveLocked() const;
  bool containsLocked(uint64_t key) const;
  // Returns true when membership changed.
  bool setLocked(uint64_t key, bool finished);

  std::mutex mutex_;
  std::vector<uint64_t> keys_;  // sorted, unique
  bool loaded_ = false;
  std::atomic<uint32_t> generation_{0};
};

#define FINISHED_BOOKS FinishedBooksStore::getInstance()
