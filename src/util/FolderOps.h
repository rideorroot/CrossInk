#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// File-browser folder and move operations that keep per-book reader state
// (progress, bookmarks, clippings, stats, finished mark, recents) attached to
// the books. All paths are absolute SD paths without a trailing slash.
namespace FolderOps {

// Makes a user-typed name storable on FAT/exFAT with readable substitutes:
// ":" -> " – ", "?*<>|" dropped, '"' -> "'", "/" and "\" -> "-".
// Leading/trailing spaces and dots are trimmed. May return an empty string.
std::string sanitizeUserName(const std::string& name);

enum class Result : uint8_t { Ok, Exists, InvalidTarget, Partial, Failed };

// Moves one file into `destDir`, keeping its name. `newPath` receives the
// destination path on success.
Result moveFile(const std::string& srcPath, const std::string& destDir, std::string& newPath);

// Moves or renames a folder (with everything inside) to `dstPath`, which must
// not exist yet and must not lie inside `srcPath`. Files move one at a time so
// each book's state migrates with it. `failed` counts entries that stayed
// behind (the result is then Partial and the source folder is kept).
Result relocateFolder(const std::string& srcPath, const std::string& dstPath, size_t& moved, size_t& failed);

// Creates `parentDir`/`name` (name already sanitized).
Result createFolder(const std::string& parentDir, const std::string& name, std::string& newPath);

}  // namespace FolderOps
