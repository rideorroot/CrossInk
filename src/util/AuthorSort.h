#pragma once

#include <string>
#include <vector>

// File browser ordering by author, for books named "<Title> - <Author>".
//
// The author is the text after the LAST " - " in the file name (without
// extension), so "Series 1 - Volume - Author" works too. Books are ordered by
// the first author's last name, then the full author, then the title.
// Folders stay first; files without " - " follow the books, by name.
namespace AuthorSort {

struct ParsedName {
  std::string title;    // empty when the name has no " - "
  std::string author;   // full author text
  std::string surname;  // last word of the first author
};

ParsedName parse(const std::string& fileName);

// Sorts entries as returned by the file browser (directories end in '/').
void sortFileList(std::vector<std::string>& entries);

}  // namespace AuthorSort
