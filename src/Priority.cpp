#include "Priority.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace {

constexpr DWORD kBoostedPriority = ABOVE_NORMAL_PRIORITY_CLASS;

bool IsShellProcess(std::wstring_view name)
{
    constexpr std::array names{
        L"explorer.exe", L"dwm.exe", L"ShellExperienceHost.exe",
        L"StartMenuExperienceHost.exe", L"SearchHost.exe", L"SearchApp.exe",
        L"LockApp.exe", L"TextInputHost.exe", L"LogonUI.exe", L"sihost.exe",
        L"ApplicationFrameHost.exe", L"RuntimeBroker.exe", L"conhost.exe"
    };
    return std::ranges::any_of(names, [name](std::wstring_view systemName) {
        return CompareStringOrdinal(name.data(), static_cast<int>(name.size()),
                   systemName.data(), static_cast<int>(systemName.size()), TRUE) == CSTR_EQUAL;
    });
}

} // namespace

DWORD WindowProcess(HWND window)
{
    DWORD pid = 0;
    if (window) {
        GetWindowThreadProcessId(window, &pid);
    }
    return pid;
}

ULONGLONG ProcessCreationTime(HANDLE process)
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        return 0;
    }
    return (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

PriorityManager::~PriorityManager()
{
    Restore();
}

BoostResult PriorityManager::Boost(const AppTarget& target)
{
    const DWORD pid = target.pid;
    if (!pid || pid == GetCurrentProcessId() || WindowProcess(target.window) != pid ||
        !IsWindowVisible(target.window) || target.window == GetShellWindow() ||
        target.window == GetDesktopWindow()) {
        return BoostResult::unchanged;
    }
    Refresh();
    if (pid_ != pid && !Restore()) {
        return BoostResult::restorePending;
    }

    constexpr DWORD access = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION |
        SYNCHRONIZE;
    UniqueWinHandle process(OpenProcess(access, FALSE, pid));
    if (!process || !target.created || ProcessCreationTime(process.get()) != target.created) {
        return BoostResult::denied;
    }
    if (process_) {
        if (WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT &&
            GetPriorityClass(process_.get()) == kBoostedPriority) {
            return BoostResult::alreadyBoosted;
        }
        if (!Restore()) {
            return BoostResult::restorePending;
        }
    }
    BOOL critical = FALSE;
    if (!IsProcessCritical(process.get(), &critical)) {
        return BoostResult::denied;
    }
    if (critical) {
        return BoostResult::unchanged;
    }

    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length)) {
        return BoostResult::denied;
    }
    path.resize(length);
    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    if (IsShellProcess(name)) {
        return BoostResult::unchanged;
    }

    const DWORD priority = GetPriorityClass(process.get());
    if (!priority) {
        return BoostResult::denied;
    }
    if (priority != NORMAL_PRIORITY_CLASS || WindowProcess(target.window) != pid ||
        !IsWindowVisible(target.window) ||
        WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT) {
        return BoostResult::unchanged;
    }
    if (!SetPriorityClass(process.get(), kBoostedPriority)) {
        return BoostResult::denied;
    }

    const DWORD applied = GetPriorityClass(process.get());
    if (applied && applied != kBoostedPriority) {
        return BoostResult::denied;
    }
    process_ = std::move(process);
    pid_ = pid;
    originalPriority_ = priority;
    name_ = std::move(name);
    if (applied == kBoostedPriority) {
        return BoostResult::boosted;
    }
    // Retain ownership until the result can be verified or rolled back.
    return Restore() ? BoostResult::denied : BoostResult::restorePending;
}

bool PriorityManager::Restore()
{
    if (!process_) {
        return true;
    }
    // The retained handle refers to the original process even if its PID is reused.
    const HANDLE handle = process_.get();
    if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) {
        Forget();
        return true;
    }

    DWORD priority = GetPriorityClass(handle);
    if (priority == kBoostedPriority && SetPriorityClass(handle, originalPriority_)) {
        priority = GetPriorityClass(handle);
    }
    // Preserve a newer priority set outside WhipCPU.
    if ((priority && priority != kBoostedPriority) ||
        WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) {
        Forget();
        return true;
    }
    return false;
}

void PriorityManager::Refresh()
{
    if (process_) {
        const DWORD priority = GetPriorityClass(process_.get());
        if ((priority && priority != kBoostedPriority) ||
            WaitForSingleObject(process_.get(), 0) == WAIT_OBJECT_0) {
            Forget();
        }
    }
}

void PriorityManager::Forget()
{
    process_.reset();
    pid_ = 0;
    originalPriority_ = 0;
    name_.clear();
}
