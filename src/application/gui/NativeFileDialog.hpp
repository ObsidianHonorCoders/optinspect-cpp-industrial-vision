#pragma once

#include <optional>
#include <string>
#include <vector>

namespace optinspect::gui {

struct FileFilter {
    std::string name;  ///< e.g. "Images"
    std::string spec;  ///< e.g. "*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff"
};

/// Native Windows file/folder dialogs built on the COM IFileOpenDialog API (no extra
/// dependency). All functions return nullopt when the user cancels or the dialog
/// cannot be created, and never throw. `owner_window` is the HWND (may be null).
/// The returned path uses the system ANSI code page, like the rest of the file
/// handling in this code base. On non-Windows builds they always return nullopt.
std::optional<std::string> openFileDialog(void* owner_window, const std::string& title,
                                          const std::vector<FileFilter>& filters,
                                          const std::string& initial_directory = {});
std::optional<std::string> pickFolderDialog(void* owner_window, const std::string& title,
                                            const std::string& initial_directory = {});

}  // namespace optinspect::gui
