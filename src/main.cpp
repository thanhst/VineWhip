#include "MouseInput.h"
#include "Overlay.h"
#include "Priority.h"
#include "resource.h"

#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <strsafe.h>
#include <windowsx.h>

#include <chrono>
#include <optional>
#include <system_error>

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr UINT kForegroundChanged = WM_APP + 4;
constexpr UINT kTrayEvent = WM_APP + 5;
constexpr UINT kTrayIcon = 1;
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT_PTR kRestoreTimer = 2;
constexpr UINT_PTR kTrayTimer = 3;

enum MenuItem : UINT {
    menuWhip = 100, menuAnimation, menuSound, menuReducedMotion,
    menuLow, menuNormal, menuCrazy, menuStatus, menuRestore, menuExit
};

struct PendingClick {
    POINT point;
    HWND target;
    DWORD pid;
    Clock::time_point deadline;
};

struct Strike {
    POINT point;
    DWORD pid;
    BoostResult result;
    Clock::time_point start;
    bool soundDue;
};

class App {
public:
    explicit App(HINSTANCE instance) : instance_(instance) {}

    ~App()
    {
        input_.Stop();
        if (foregroundHook_) {
            UnhookWinEvent(foregroundHook_);
            eventWindow_ = nullptr;
        }
        if (sound_) {
            PlaySoundW(nullptr, nullptr, 0);
        }
        priorities_.Restore();
        if (window_) {
            auto data = TrayData();
            Shell_NotifyIconW(NIM_DELETE, &data);
            DestroyWindow(window_);
        }
    }

    bool Initialize()
    {
        BOOL systemAnimations = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &systemAnimations, 0);
        reducedMotion_ = !systemAnimations;
        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        icon_ = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_WHIPCPU));

        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hIcon = icon_;
        windowClass.lpszClassName = L"WhipCPU.Controller";
        if (!icon_ || !RegisterClassW(&windowClass)) {
            return false;
        }
        // A hidden top-level window receives the Explorer restart broadcast.
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass.lpszClassName,
            L"WhipCPU", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance_, this);
        if (!window_ || !overlay_.Create(instance_, window_)) {
            return false;
        }
        eventWindow_ = window_;
        foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            nullptr, ForegroundEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
        if (!foregroundHook_ || !AddTrayIcon()) {
            return false;
        }
        input_.Configure(whip_, ShowIdleWhip());
        try {
            if (!input_.Start(window_)) {
                return false;
            }
        } catch (const std::system_error&) {
            return false;
        }
        RefreshOverlay();
        return true;
    }

    int Run()
    {
        MSG message{};
        BOOL status;
        while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return status < 0 ? 1 : static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            return TRUE;
        }
        if (app && app->window_) {
            return app->HandleMessage(message, wParam, lParam);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    static void CALLBACK ForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
    {
        if (eventWindow_) {
            PostMessageW(eventWindow_, kForegroundChanged, 0, 0);
        }
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == taskbarCreated_) {
            if (!AddTrayIcon()) {
                SetTimer(window_, kTrayTimer, 1000, nullptr);
            }
            return 0;
        }
        switch (message) {
        case kMouseMoved:
            if (ShowIdleWhip()) {
                if (!strike_) {
                    RefreshOverlay();
                }
            } else {
                input_.AcknowledgeMove();
            }
            return 0;
        case kMouseClicked:
            BeginClick({static_cast<LONG>(wParam), static_cast<LONG>(lParam)});
            return 0;
        case kForegroundChanged:
            CheckForeground();
            return 0;
        case kRefreshOverlay:
        case WM_DISPLAYCHANGE:
            RefreshOverlay();
            return 0;
        case kTrayEvent:
            if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == NIN_SELECT ||
                LOWORD(lParam) == NIN_KEYSELECT) {
                ShowMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
            }
            return 0;
        case WM_TIMER:
            if (wParam == kFrameTimer) {
                Frame();
            } else if (wParam == kRestoreTimer) {
                RestoreAll();
            } else if (wParam == kTrayTimer && AddTrayIcon()) {
                KillTimer(window_, kTrayTimer);
            }
            return 0;
        case WM_CLOSE:
            if (RestoreAll()) {
                PostQuitMessage(0);
            } else {
                NotifyRestoreFailure();
            }
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wParam) {
                priorities_.Restore();
                PostQuitMessage(0);
            }
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    void BeginClick(POINT point)
    {
        if (!whip_ || menuOpen_) {
            return;
        }
        pendingClick_.reset();
        const HWND target = GetAncestor(overlay_.WindowAt(point), GA_ROOT);
        const DWORD pid = WindowProcess(target);
        if (pid && pid != GetCurrentProcessId() && target != GetDesktopWindow() &&
            target != GetShellWindow()) {
            // The hook sees input before the clicked window has had time to activate.
            pendingClick_ = PendingClick{point, target, pid, Clock::now() + 200ms};
            StartFrameTimer();
        }
    }

    void ResolveClick(Clock::time_point now)
    {
        if (!pendingClick_) {
            return;
        }
        const auto click = *pendingClick_;
        const HWND foreground = GetForegroundWindow();
        if (now > click.deadline || WindowProcess(click.target) != click.pid) {
            pendingClick_.reset();
            return;
        }
        if (WindowProcess(foreground) != click.pid) {
            return;
        }

        pendingClick_.reset();
        const auto result = restorePending_ && !RestoreAll() ?
            BoostResult::restorePending : priorities_.Boost(foreground);
        SetRestorePending(result == BoostResult::restorePending);
        UpdateTooltip();

        if (animation_) {
            strike_ = Strike{click.point, click.pid, result, now, sound_};
            overlayDirty_ = true;
        } else if (sound_) {
            Crack();
        }
    }

    void Frame()
    {
        // Keep move notifications coalesced until the next frame.
        input_.AcknowledgeMove();
        const auto now = Clock::now();
        ResolveClick(now);
        if (strike_) {
            const float ms = std::chrono::duration<float, std::milli>(now - strike_->start).count();
            if (strike_->soundDue && ms >= (reducedMotion_ ? 0.0f : 205.0f)) {
                strike_->soundDue = false;
                if (sound_) {
                    Crack();
                }
            }
            if (ms >= (reducedMotion_ ? 650.0f : 500.0f)) {
                strike_.reset();
                overlayDirty_ = true;
            } else if (!reducedMotion_ || overlayDirty_) {
                overlay_.DrawStrike(strike_->point, ms, strike_->result, intensity_, reducedMotion_);
                overlayDirty_ = false;
            }
        }
        if (!strike_ && overlayDirty_) {
            POINT cursor{};
            if (ShowIdleWhip() && GetCursorPos(&cursor)) {
                overlay_.DrawIdle(cursor);
            } else {
                overlay_.Hide();
            }
            overlayDirty_ = false;
        }
        if (!strike_ && !pendingClick_) {
            KillTimer(window_, kFrameTimer);
            frameTimerRunning_ = false;
        }
    }

    void StartFrameTimer()
    {
        if (!frameTimerRunning_) {
            frameTimerRunning_ = SetTimer(window_, kFrameTimer, 15, nullptr) != 0;
        }
    }

    void RefreshOverlay()
    {
        overlayDirty_ = true;
        if (ShowIdleWhip() || strike_) {
            StartFrameTimer();
        } else {
            overlay_.Hide();
        }
    }

    bool ShowIdleWhip() const { return whip_ && animation_ && !reducedMotion_ && !menuOpen_; }

    void Crack() const
    {
        PlaySoundW(MAKEINTRESOURCEW(IDR_WHIP_CRACK), instance_,
            SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
    }

    void CheckForeground()
    {
        const DWORD pid = WindowProcess(GetForegroundWindow());
        if (priorities_.Pid()) {
            const bool restored = restorePending_ ?
                priorities_.Restore() : priorities_.RestoreIfDifferent(pid);
            SetRestorePending(!restored);
            UpdateTooltip();
        }
        if (strike_ && strike_->pid != pid) {
            strike_.reset();
            RefreshOverlay();
        }
    }

    bool RestoreAll()
    {
        const bool restored = priorities_.Restore();
        SetRestorePending(!restored);
        UpdateTooltip();
        return restored;
    }

    void SetRestorePending(bool pending)
    {
        if (pending != restorePending_) {
            restorePending_ = pending;
            if (pending) {
                SetTimer(window_, kRestoreTimer, 1000, nullptr);
            } else {
                KillTimer(window_, kRestoreTimer);
            }
        }
    }

    NOTIFYICONDATAW TrayData() const
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kTrayIcon;
        return data;
    }

    std::wstring BoostStatus() const
    {
        if (!priorities_.Pid()) {
            return L"Boosted: none";
        }
        return (restorePending_ ? L"Restore pending: " : L"Boosted: ") + priorities_.Name() +
            L" (PID " + std::to_wstring(priorities_.Pid()) + L")";
    }

    void SetTooltip(NOTIFYICONDATAW& data) const
    {
        const std::wstring tip = (whip_ ? L"WhipCPU | " : L"WhipCPU (off) | ") + BoostStatus();
        StringCchCopyW(data.szTip, ARRAYSIZE(data.szTip), tip.c_str());
        data.uFlags |= NIF_TIP | NIF_SHOWTIP;
    }

    bool AddTrayIcon() const
    {
        auto data = TrayData();
        data.uFlags = NIF_ICON | NIF_MESSAGE;
        data.hIcon = icon_;
        data.uCallbackMessage = kTrayEvent;
        SetTooltip(data);
        if (!Shell_NotifyIconW(NIM_ADD, &data)) {
            return false;
        }
        data.uVersion = NOTIFYICON_VERSION_4;
        if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
            Shell_NotifyIconW(NIM_DELETE, &data);
            return false;
        }
        return true;
    }

    void UpdateTooltip() const
    {
        auto data = TrayData();
        SetTooltip(data);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void NotifyRestoreFailure() const
    {
        auto data = TrayData();
        data.uFlags = NIF_INFO;
        data.dwInfoFlags = NIIF_WARNING;
        StringCchCopyW(data.szInfoTitle, ARRAYSIZE(data.szInfoTitle), L"WhipCPU");
        StringCchCopyW(data.szInfo, ARRAYSIZE(data.szInfo),
            L"Windows refused to restore the original priority. WhipCPU will retry.");
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void ShowMenu(POINT anchor)
    {
        if (menuOpen_) {
            return;
        }
        const HMENU menu = CreatePopupMenu();
        const HMENU levels = CreatePopupMenu();
        if (!menu || !levels) {
            if (menu) DestroyMenu(menu);
            if (levels) DestroyMenu(levels);
            return;
        }
        const auto check = [](bool checked) -> UINT { return checked ? MF_CHECKED : MF_UNCHECKED; };
        AppendMenuW(menu, check(whip_), menuWhip, L"Whip Mode");
        AppendMenuW(menu, check(animation_), menuAnimation, L"Animation");
        AppendMenuW(menu, check(sound_), menuSound, L"Sound");
        AppendMenuW(menu, check(reducedMotion_), menuReducedMotion, L"Reduced Motion");
        AppendMenuW(levels, MF_STRING, menuLow, L"Low");
        AppendMenuW(levels, MF_STRING, menuNormal, L"Normal");
        AppendMenuW(levels, MF_STRING, menuCrazy, L"Crazy");
        CheckMenuRadioItem(levels, menuLow, menuCrazy,
            menuLow + static_cast<UINT>(intensity_), MF_BYCOMMAND);
        if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(levels), L"Animation Intensity")) {
            DestroyMenu(levels);
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_GRAYED, menuStatus, BoostStatus().c_str());
        AppendMenuW(menu, MF_STRING, menuRestore, L"Restore All Priorities");
        AppendMenuW(menu, MF_STRING, menuExit, L"Exit");

        if (anchor.x == -1 && anchor.y == -1) {
            NOTIFYICONIDENTIFIER identifier{};
            identifier.cbSize = sizeof(identifier);
            identifier.hWnd = window_;
            identifier.uID = kTrayIcon;
            RECT iconBounds{};
            if (SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &iconBounds))) {
                anchor = {iconBounds.left, iconBounds.top};
            } else {
                GetCursorPos(&anchor);
            }
        }
        menuOpen_ = true;
        pendingClick_.reset();
        strike_.reset();
        overlay_.Hide();
        input_.Configure(false, false);
        SetForegroundWindow(window_);
        const UINT command = static_cast<UINT>(TrackPopupMenu(menu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, anchor.x, anchor.y, 0, window_, nullptr));
        PostMessageW(window_, WM_NULL, 0, 0);
        DestroyMenu(menu);
        auto data = TrayData();
        Shell_NotifyIconW(NIM_SETFOCUS, &data);
        menuOpen_ = false;
        ApplyMenu(command);
    }

    void ApplyMenu(UINT command)
    {
        switch (command) {
        case menuWhip:
            whip_ = !whip_;
            if (!whip_ && !RestoreAll()) {
                NotifyRestoreFailure();
            }
            break;
        case menuAnimation: animation_ = !animation_; break;
        case menuSound:
            sound_ = !sound_;
            if (!sound_) {
                PlaySoundW(nullptr, nullptr, 0);
            }
            break;
        case menuReducedMotion: reducedMotion_ = !reducedMotion_; break;
        case menuLow:
        case menuNormal:
        case menuCrazy: intensity_ = static_cast<Intensity>(command - menuLow); break;
        case menuRestore:
            if (!RestoreAll()) {
                NotifyRestoreFailure();
            }
            break;
        case menuExit: SendMessageW(window_, WM_CLOSE, 0, 0); break;
        }
        input_.Configure(whip_, ShowIdleWhip());
        RefreshOverlay();
        UpdateTooltip();
    }

    inline static HWND eventWindow_{};
    HINSTANCE instance_{};
    HWND window_{};
    HICON icon_{};
    HWINEVENTHOOK foregroundHook_{};
    UINT taskbarCreated_{};
    MouseInput input_;
    Overlay overlay_;
    PriorityManager priorities_;
    std::optional<PendingClick> pendingClick_;
    std::optional<Strike> strike_;
    Intensity intensity_{Intensity::normal};
    bool whip_{true};
    bool animation_{true};
    bool sound_{};
    bool reducedMotion_{};
    bool restorePending_{};
    bool frameTimerRunning_{};
    bool overlayDirty_{};
    bool menuOpen_{};
};

int RunApp(HINSTANCE instance)
{
    App app(instance);
    if (!app.Initialize()) {
        MessageBoxW(nullptr, L"Could not start the input, graphics, or tray components.",
            L"WhipCPU", MB_OK | MB_ICONERROR);
        return 1;
    }
    return app.Run();
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    const UniqueWinHandle singleton(CreateMutexW(nullptr, FALSE, L"Local\\WhipCPU.Desktop"));
    if (!singleton) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"WhipCPU is already running in the notification area.",
            L"WhipCPU", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token{};
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"Could not start GDI+.", L"WhipCPU", MB_OK | MB_ICONERROR);
        return 1;
    }
    const int result = RunApp(instance);
    Gdiplus::GdiplusShutdown(token);
    return result;
}
