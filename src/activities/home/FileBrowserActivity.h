#pragma once

#include <FileIndex.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <I18n.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 public:
  // Picker modes return their selected path via ActivityResult.
  enum class Mode { Books, PickFirmware, PickDirectory };

 private:
  // FreeInkApp hosts the file list (themed rows, icons, touch routing); the
  // header stays on GUI.drawHeader for the battery indicator.
  using UiApp = freeink::ui::FreeInkApp<20, 4>;

  // Deletion
  void promptDeleteFile(const std::string& fullPath, const std::string& entry);
  void promptDeleteDirectory(const std::string& fullPath, const std::string& entry,
                             bool ignoreInitialConfirmRelease = false);
  void showDirectoryActionMenu(const std::string& entry, bool ignoreInitialConfirmRelease = false);
  void pinSleepFavorite(const std::string& fullPath);
  void unpinSleepFavorite();
  bool isPinnedSleepFavorite(const std::string& fullPath) const;
  void setPreferredSleepFolder(const std::string& fullPath);
  void clearPreferredSleepFolder();
  bool isPreferredSleepFolder(const std::string& fullPath) const;
  void pinBootFavorite(const std::string& fullPath);
  void unpinBootFavorite();
  bool isPinnedBootFavorite(const std::string& fullPath) const;
  void showFileActionMenu(const std::string& entry, bool ignoreInitialConfirmRelease = false);
  void startRenameFile(const std::string& fullPath, const std::string& entry);
  void startMoveFile(const std::string& fullPath);
  void startRenameFolder(const std::string& fullPath, const std::string& entry);
  void startMoveFolder(const std::string& fullPath);
  void promptNewFolder();
  void pickDestinationFolder(const std::string& startPath,
                             void (*onPicked)(FileBrowserActivity*, const std::string&, const std::string&),
                             const std::string& sourcePath);
  static void onFilePicked(FileBrowserActivity* self, const std::string& sourcePath, const std::string& destDir);
  static void onFolderPicked(FileBrowserActivity* self, const std::string& sourcePath, const std::string& destDir);
  // Reloads the list and puts the selection on `entryName` (if present).
  void reloadAndSelect(const std::string& entryName);
  void showToast(const char* message);
  void renameFile(const std::string& oldPath, const std::string& oldEntry, const std::string& newStem,
                  const std::string& extension);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;
  bool showFileSelection = true;

  bool lockLongPressBack = false;
  // Set once a slow folder-count walk has shown the loading popup for this load.
  bool folderCountPopupShown = false;
  bool longPressBackHandled = false;
  bool longPressConfirmHandled = false;
  bool pendingCompletedFeedback = false;
  bool completedFeedbackIsFinished = false;
  unsigned long completedFeedbackShowTime = 0UL;
  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;

  Mode mode = Mode::Books;

  // Files state
  static constexpr size_t INDEX_ROW_CACHE_SIZE = 32;
  std::string basepath = "/";
  std::vector<std::string> files;
  // Per-row "finished << 16 | total" for directory rows in `files`; UNKNOWN when
  // not counted. Kept per activity so a long folder walk cannot evict rows that
  // are on screen from the shared counts cache.
  static constexpr uint32_t FOLDER_COUNT_UNKNOWN = 0xFFFFFFFFu;
  std::vector<uint32_t> folderCounts;
  std::unique_ptr<char[]> fileNameBuffer;
  std::unique_ptr<FileIndex> fileIndex;
  std::unique_ptr<FileIndex::Entry> indexEntry;
  std::array<std::string, INDEX_ROW_CACHE_SIZE> indexCachedNames;
  std::array<size_t, INDEX_ROW_CACHE_SIZE> indexCachedRows{};
  bool usingIndex = false;
  bool fileListMemoryLimited = false;
  bool fileListReadFailed = false;

  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;
  // render() rebuilds the app's interaction table; loop() only routes touch
  // snapshots against it while this is true (the two run on different tasks).
  std::atomic<bool> uiReady{false};
  int visibleRows = 1;  // rows per page at the current scale; set by the screen builder
  int topIndex = 0;     // viewport scroll position, decoupled from the selection
  // The current FreeInkUI action payload is int16_t. For larger folders the
  // renderer supplies a local row number and this records its absolute base.
  size_t actionWindowFirst = 0;
  freeink::ui::ListNav listNav;

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onSettingsEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
  void activateSelected();
  void navigateBack();
  void openSettings();

  // Data loading
  void clearIndexNameCache();
  void loadFiles();
  // Caller holds RenderLock while replacing the list backing storage together
  // with its associated navigation state.
  void loadFilesLocked();
  bool loadFilesIntoVector(size_t cap, bool& overflow);
  size_t entryCount() const;
  // List rows: the entries plus a trailing "+ New Folder" row where creating
  // folders makes sense.
  bool showsNewFolderRow() const;
  size_t rowCount() const { return entryCount() + (showsNewFolderRow() ? 1 : 0); }
  bool isNewFolderRow(size_t row) const { return showsNewFolderRow() && row == entryCount(); }
  const char* entryNameAt(size_t row);
  void toggleHiddenFiles();
  // Fills the folder read-count cache for directory rows (Books mode only).
  void precomputeFolderReadCounts();
  void sortLoadedFiles();
  static void onSlowFolderCountWalk(void* user);
  size_t findEntry(const std::string& name);

  StrId pickTitleId = StrId::STR_SELECT_RECEIVE_FOLDER;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books);
  // Header text in PickDirectory mode.
  void setPickTitle(const StrId titleId) { pickTitleId = titleId; }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
