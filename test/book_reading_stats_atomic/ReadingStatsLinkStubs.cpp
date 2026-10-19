#include <ReadingStatsUtils.h>

bool ReadingStatsDate::isValid() const { return year >= 2000 && month >= 1 && month <= 12 && day >= 1 && day <= 31; }

void ReadingStatsDate::clear() {
  year = 0;
  month = 0;
  day = 0;
}

void recordReadingSpanIntoBuckets(std::array<uint32_t, READING_TIME_BUCKET_COUNT>&,
                                  std::array<uint32_t, READING_DAY_OF_WEEK_COUNT>&, const ReadingStatsDateTime&,
                                  uint32_t) {}

// BookReadingStats keeps the finished-books index in sync; these tests cover
// the stats file transaction only.
#include "FinishedBooksStore.h"

FinishedBooksStore& FinishedBooksStore::getInstance() {
  static FinishedBooksStore instance;
  return instance;
}
void FinishedBooksStore::onStatsSaved(const std::string&, bool) {}
void FinishedBooksStore::onStatsRemoved(const std::string&) {}
