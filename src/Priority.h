#pragma once

#include <windows.h>

#include <memory>
#include <string>

enum class BoostResult { boosted, alreadyBoosted, unchanged, denied, restorePending, disabled };

DWORD WindowProcess(HWND window);

struct AppTarget {
    HWND window{};
    DWORD pid{};
    ULONGLONG created{};
};

ULONGLONG ProcessCreationTime(HANDLE process);

struct CloseWinHandle {
    void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};

using UniqueWinHandle = std::unique_ptr<void, CloseWinHandle>;

class PriorityManager {
public:
    ~PriorityManager();

    BoostResult Boost(const AppTarget& target);
    bool Restore();
    void Refresh();

    DWORD Pid() const { return pid_; }
    const std::wstring& Name() const { return name_; }

private:
    void Forget();

    UniqueWinHandle process_;
    DWORD pid_{};
    DWORD originalPriority_{};
    std::wstring name_;
};
