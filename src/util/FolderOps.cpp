#include "FolderOps.h"

#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>

#include <cstring>
#include <vector>

#include "CrossPointState.h"
#include "activities/boot_sleep/ImageFolderIndex.h"
#include "util/BookMoveUtils.h"

namespace FolderOps {
namespace {

constexpr int MAX_DEPTH = 12;

std::string joinPath(const std::string& dir, const std::string& name) {
  return (dir.empty() || dir.back() == '/') ? dir + name : dir + "/" + name;
}

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::string::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
}

bool isInside(const std::string& path, const std::string& dir) {
  return path == dir || (path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 && path[dir.size()] == '/');
}

std::string rebase(const std::string& path, const std::string& oldDir, const std::string& newDir) {
  return newDir + path.substr(oldDir.size());
}

bool movedOk(const BookMoveUtils::RenameMigrationResult r) {
  return r == BookMoveUtils::RenameMigrationResult::Success || r == BookMoveUtils::RenameMigrationResult::KeepRenamed;
}

void relocateInto(const std::string& src, const std::string& dst, const int depth, size_t& moved, size_t& failed) {
  if (depth > MAX_DEPTH) {
    LOG_ERR("FOLDEROPS", "Folder too deep, leaving in place: %s", src.c_str());
    ++failed;
    return;
  }
  if (!Storage.exists(dst.c_str()) && !Storage.mkdir(dst.c_str())) {
    LOG_ERR("FOLDEROPS", "Cannot create %s", dst.c_str());
    ++failed;
    return;
  }

  // Collect names first: renaming while iterating a FAT directory can skip
  // or repeat entries.
  std::vector<std::string> files;
  std::vector<std::string> dirs;
  {
    auto dir = Storage.open(src.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      LOG_ERR("FOLDEROPS", "Cannot open %s", src.c_str());
      ++failed;
      return;
    }
    char name[256];
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));
      (entry.isDirectory() ? dirs : files).emplace_back(name);
      entry.close();
    }
    dir.close();
  }

  for (const auto& name : files) {
    const std::string from = joinPath(src, name);
    const std::string to = joinPath(dst, name);
    if (Storage.exists(to.c_str())) {
      LOG_ERR("FOLDEROPS", "Target exists, leaving in place: %s", to.c_str());
      ++failed;
      continue;
    }
    if (movedOk(BookMoveUtils::renameFilePreservingBookState(from, to))) {
      ++moved;
    } else {
      LOG_ERR("FOLDEROPS", "Could not move %s", from.c_str());
      ++failed;
    }
  }
  for (const auto& name : dirs) {
    relocateInto(joinPath(src, name), joinPath(dst, name), depth + 1, moved, failed);
  }

  // rmdir only succeeds on an empty folder, so anything left behind stays.
  if (!Storage.rmdir(src.c_str())) {
    LOG_INF("FOLDEROPS", "Source folder kept (not empty): %s", src.c_str());
  }
}

}  // namespace

std::string sanitizeUserName(const std::string& input) {
  std::string text = input;
  // " : " / ":" -> " – " (en dash); the plain hyphen stays the author separator.
  std::string out;
  out.reserve(text.size() + 8);
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == ':') {
      while (!out.empty() && out.back() == ' ') out.pop_back();
      out += " \xE2\x80\x93 ";
      while (i + 1 < text.size() && text[i + 1] == ' ') ++i;
    } else if (c == '?' || c == '*' || c == '<' || c == '>' || c == '|') {
      continue;
    } else if (c == '"') {
      out += '\'';
    } else if (c == '/' || c == '\\') {
      out += '-';
    } else if (static_cast<unsigned char>(c) < 0x20) {
      continue;
    } else {
      out += c;
    }
  }
  replaceAll(out, "  ", " ");
  size_t begin = 0;
  size_t end = out.size();
  while (begin < end && (out[begin] == ' ' || out[begin] == '.')) ++begin;
  while (end > begin && (out[end - 1] == ' ' || out[end - 1] == '.')) --end;
  return out.substr(begin, end - begin);
}

Result moveFile(const std::string& srcPath, const std::string& destDir, std::string& newPath) {
  const size_t slash = srcPath.rfind('/');
  const std::string name = slash == std::string::npos ? srcPath : srcPath.substr(slash + 1);
  newPath = joinPath(destDir, name);
  if (newPath == srcPath) return Result::InvalidTarget;
  if (Storage.exists(newPath.c_str())) return Result::Exists;

  const auto r = BookMoveUtils::renameFilePreservingBookState(srcPath, newPath);
  if (r == BookMoveUtils::RenameMigrationResult::DestinationStateExists) return Result::Exists;
  if (!movedOk(r)) return Result::Failed;
  library::invalidateLibraryIndex();
  ImageFolderIndex::invalidateForPath(srcPath.c_str());
  ImageFolderIndex::invalidateForPath(newPath.c_str());
  return Result::Ok;
}

Result relocateFolder(const std::string& srcPath, const std::string& dstPath, size_t& moved, size_t& failed) {
  moved = 0;
  failed = 0;
  if (srcPath.empty() || srcPath == "/" || dstPath.empty() || isInside(dstPath, srcPath)) {
    return Result::InvalidTarget;
  }
  if (Storage.exists(dstPath.c_str())) return Result::Exists;

  library::invalidateLibraryIndex();
  relocateInto(srcPath, dstPath, 0, moved, failed);
  library::invalidateLibraryIndex();
  ImageFolderIndex::invalidateForPath(srcPath.c_str());
  ImageFolderIndex::invalidateForPath(dstPath.c_str());

  // Folder-level references that are not tied to a single book.
  bool stateChanged = false;
  if (!APP_STATE.preferredSleepFolderPath.empty() && isInside(APP_STATE.preferredSleepFolderPath, srcPath)) {
    APP_STATE.preferredSleepFolderPath = rebase(APP_STATE.preferredSleepFolderPath, srcPath, dstPath);
    stateChanged = true;
  }
  if (stateChanged && !APP_STATE.saveToFile()) LOG_ERR("FOLDEROPS", "Could not save moved sleep folder path");

  LOG_INF("FOLDEROPS", "Relocated %s -> %s (%u moved, %u left)", srcPath.c_str(), dstPath.c_str(),
          static_cast<unsigned>(moved), static_cast<unsigned>(failed));
  return failed == 0 ? Result::Ok : (moved > 0 ? Result::Partial : Result::Failed);
}

Result createFolder(const std::string& parentDir, const std::string& name, std::string& newPath) {
  if (name.empty()) return Result::InvalidTarget;
  newPath = joinPath(parentDir, name);
  if (Storage.exists(newPath.c_str())) return Result::Exists;
  if (!Storage.mkdir(newPath.c_str())) {
    LOG_ERR("FOLDEROPS", "Cannot create %s", newPath.c_str());
    return Result::Failed;
  }
  library::invalidateLibraryIndex();
  return Result::Ok;
}

}  // namespace FolderOps
