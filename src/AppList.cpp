#include "AppList.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <utility>

namespace {

BOOL CALLBACK CollectWindow(HWND window, LPARAM context)
{
    const DWORD pid = WindowProcess(window);
    if (!pid || pid == GetCurrentProcessId() || !IsWindowVisible(window) ||
        window == GetShellWindow() || window == GetDesktopWindow()) {
        return TRUE;
    }
    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & WS_EX_TOOLWINDOW) || (GetWindow(window, GW_OWNER) && !(style & WS_EX_APPWINDOW))) {
        return TRUE;
    }
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
        return TRUE;
    }

    wchar_t title[256]{};
    if (!GetWindowTextW(window, title, ARRAYSIZE(title))) {
        return TRUE;
    }
    auto& apps = *reinterpret_cast<std::vector<OpenApp>*>(context);
    if (std::ranges::any_of(apps, [pid](const OpenApp& app) { return app.target.pid == pid; })) {
        return TRUE;
    }

    const UniqueWinHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    const auto created = process ? ProcessCreationTime(process.get()) : 0;
    apps.push_back({{window, pid, created}, title, nullptr});
    return TRUE;
}

std::unique_ptr<Gdiplus::Bitmap> ReadIcon(const AppTarget& target)
{
    DWORD_PTR result = 0;
    constexpr UINT flags = SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT;
    if (SendMessageTimeoutW(target.window, WM_GETICON, ICON_BIG, 0, flags, 25, &result) && !result) {
        SendMessageTimeoutW(target.window, WM_GETICON, ICON_SMALL2, 0, flags, 25, &result);
    }
    HICON icon = reinterpret_cast<HICON>(result);
    if (!icon) {
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(target.window, GCLP_HICON));
    }
    if (!icon) {
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(target.window, GCLP_HICONSM));
    }

    HICON extracted = nullptr;
    if (!icon) {
        const UniqueWinHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid));
        std::wstring path(32768, L'\0');
        DWORD length = static_cast<DWORD>(path.size());
        if (process && QueryFullProcessImageNameW(process.get(), 0, path.data(), &length)) {
            ExtractIconExW(path.c_str(), 0, &extracted, nullptr, 1);
            icon = extracted;
        }
    }
    if (!icon) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    std::unique_ptr<Gdiplus::Bitmap> image(icon ? Gdiplus::Bitmap::FromHICON(icon) : nullptr);
    if (extracted) {
        DestroyIcon(extracted);
    }
    if (image && image->GetLastStatus() != Gdiplus::Ok) {
        image.reset();
    }
    return image;
}

} // namespace

void RefreshOpenApps(std::vector<OpenApp>& apps)
{
    std::vector<OpenApp> found;
    EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&found));

    std::vector<OpenApp> next;
    next.reserve(found.size());
    // Preserve the order and icons of existing rows as other apps gain focus.
    for (auto& current : apps) {
        const auto match = std::ranges::find_if(found, [&current](const OpenApp& app) {
            return app.target.pid == current.target.pid &&
                app.target.created == current.target.created;
        });
        if (match != found.end()) {
            match->icon = std::move(current.icon);
            next.push_back(std::move(*match));
            found.erase(match);
        }
    }
    std::ranges::sort(found, [](const OpenApp& left, const OpenApp& right) {
        return CompareStringOrdinal(left.title.c_str(), -1, right.title.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    for (auto& app : found) {
        next.push_back(std::move(app));
    }
    apps = std::move(next);
}

void LoadAppIcon(OpenApp& app)
{
    if (!app.icon) {
        app.icon = ReadIcon(app.target);
    }
}
