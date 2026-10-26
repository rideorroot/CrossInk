#include "AuthorSort.h"

#include <NaturalSort.h>

#include <algorithm>
#include <cstring>
#include <numeric>

namespace AuthorSort {
namespace {

std::string trim(const std::string& s) {
  size_t begin = 0;
  size_t end = s.size();
  while (begin < end && (s[begin] == ' ' || s[begin] == '\t')) ++begin;
  while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t')) --end;
  return s.substr(begin, end - begin);
}

std::string stripExtension(const std::string& name) {
  const size_t dot = name.rfind('.');
  return (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
}

// Cuts a multi-author string down to its first author.
std::string firstAuthor(const std::string& author) {
  size_t cut = author.size();
  for (const char* sep : {" & ", ", ", " und ", " and ", "; ", " / "}) {
    const size_t pos = author.find(sep);
    if (pos != std::string::npos && pos > 0) cut = std::min(cut, pos);
  }
  return trim(author.substr(0, cut));
}

std::string lastWord(const std::string& name) {
  const size_t space = name.rfind(' ');
  return space == std::string::npos ? name : name.substr(space + 1);
}

struct Entry {
  size_t index;
  bool isDir;
  ParsedName parsed;
};

}  // namespace

ParsedName parse(const std::string& fileName) {
  ParsedName out;
  const std::string stem = stripExtension(fileName);
  const size_t sep = stem.rfind(" - ");
  if (sep == std::string::npos) return out;
  out.title = trim(stem.substr(0, sep));
  out.author = trim(stem.substr(sep + 3));
  if (out.title.empty() || out.author.empty()) return ParsedName{};
  out.surname = lastWord(firstAuthor(out.author));
  return out;
}

void sortFileList(std::vector<std::string>& entries) {
  std::vector<Entry> keyed;
  keyed.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    const std::string& name = entries[i];
    const bool isDir = !name.empty() && name.back() == '/';
    keyed.push_back({i, isDir, isDir ? ParsedName{} : parse(name)});
  }

  std::stable_sort(keyed.begin(), keyed.end(), [&](const Entry& a, const Entry& b) {
    if (a.isDir != b.isDir) return a.isDir;
    if (!a.isDir) {
      const bool aHas = !a.parsed.surname.empty();
      const bool bHas = !b.parsed.surname.empty();
      if (aHas != bHas) return aHas;
      if (aHas) {
        int cmp = FsHelpers::naturalCompare(a.parsed.surname.c_str(), b.parsed.surname.c_str());
        if (cmp != 0) return cmp < 0;
        cmp = FsHelpers::naturalCompare(a.parsed.author.c_str(), b.parsed.author.c_str());
        if (cmp != 0) return cmp < 0;
        cmp = FsHelpers::naturalCompare(a.parsed.title.c_str(), b.parsed.title.c_str());
        if (cmp != 0) return cmp < 0;
      }
    }
    return FsHelpers::naturalCompare(entries[a.index].c_str(), entries[b.index].c_str()) < 0;
  });

  std::vector<std::string> sorted;
  sorted.reserve(entries.size());
  for (const auto& e : keyed) sorted.push_back(std::move(entries[e.index]));
  entries.swap(sorted);
}

}  // namespace AuthorSort
