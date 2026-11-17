#include "NativeFileDialog.hpp"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>

#include <filesystem>

namespace optinspect::gui {

namespace {

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &w[0], n);
    return w;
}

std::string toAnsi(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

template <typename T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

struct ComScope {
    HRESULT hr;
    ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
    bool usable() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

std::optional<std::string> runDialog(void* owner, const std::string& title, const std::vector<FileFilter>& filters,
                                     const std::string& initial_directory, bool folder) {
    ComScope com;
    if (!com.usable()) return std::nullopt;

    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (folder) options |= FOS_PICKFOLDERS;
    else options |= FOS_FILEMUSTEXIST;
    dialog->SetOptions(options);
    dialog->SetTitle(toWide(title).c_str());

    std::vector<std::wstring> names;
    std::vector<std::wstring> specs;
    if (!folder && !filters.empty()) {
        for (const FileFilter& f : filters) {
            names.push_back(toWide(f.name));
            specs.push_back(toWide(f.spec));
        }
        std::vector<COMDLG_FILTERSPEC> raw;
        for (std::size_t i = 0; i < names.size(); ++i) raw.push_back({names[i].c_str(), specs[i].c_str()});
        dialog->SetFileTypes(static_cast<UINT>(raw.size()), raw.data());
    }

    if (!initial_directory.empty()) {
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::absolute(initial_directory, ec);
        ComPtr<IShellItem> folder_item;
        if (!ec && SUCCEEDED(SHCreateItemFromParsingName(absolute.wstring().c_str(), nullptr, IID_PPV_ARGS(&folder_item)))) {
            dialog->SetFolder(folder_item.p);
        }
    }

    if (FAILED(dialog->Show(static_cast<HWND>(owner)))) return std::nullopt;  // includes cancel

    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return std::nullopt;
    const std::string result = toAnsi(path);
    CoTaskMemFree(path);
    if (result.empty()) return std::nullopt;
    return result;
}

}  // namespace

std::optional<std::string> openFileDialog(void* owner_window, const std::string& title,
                                          const std::vector<FileFilter>& filters,
                                          const std::string& initial_directory) {
    return runDialog(owner_window, title, filters, initial_directory, false);
}

std::optional<std::string> pickFolderDialog(void* owner_window, const std::string& title,
                                            const std::string& initial_directory) {
    return runDialog(owner_window, title, {}, initial_directory, true);
}

}  // namespace optinspect::gui

#else

namespace optinspect::gui {

std::optional<std::string> openFileDialog(void*, const std::string&, const std::vector<FileFilter>&,
                                          const std::string&) {
    return std::nullopt;
}
std::optional<std::string> pickFolderDialog(void*, const std::string&, const std::string&) { return std::nullopt; }

}  // namespace optinspect::gui

#endif
