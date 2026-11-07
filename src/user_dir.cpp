// The user folder (saves, settings, caches, DLC): Documents\fm3, in the Documents folder Windows
// reports (a moved Documents folder isn't %USERPROFILE%\Documents). FM3_USER_DIR=<dir> keeps
// everything in a folder of the caller's choosing instead (test runs, a copy that travels).
// Adapted from the FM4 ReGPU port (MIT; see NOTICE.md).
#include "user_dir.h"

#include <windows.h>

#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

const std::filesystem::path& DocumentsUserDir()
{
    static const std::filesystem::path dir = [] {
        std::error_code ec;
        if (wchar_t custom[MAX_PATH]; GetEnvironmentVariableW(L"FM3_USER_DIR", custom, MAX_PATH) - 1u < MAX_PATH - 1u)
            return std::filesystem::absolute(std::filesystem::path(custom), ec);
        std::filesystem::path documents;
        PWSTR known = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &known)) && known)
            documents = known;
        CoTaskMemFree(known);
        return documents.empty() ? std::filesystem::path("user") : documents / "fm3";
    }();
    return dir;
}
