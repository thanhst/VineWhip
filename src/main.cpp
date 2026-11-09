#include "Panel.h"
#include "resource.h"

#include <mmsystem.h>
#include <shellapi.h>
#include <strsafe.h>
#include <windowsx.h>

#include <chrono>
#include <optional>

namespace {

using Clock = std::chrono::steady_clock;

constexpr UINT kTrayEvent = WM_APP + 5;
constexpr UINT kTrayIcon = 1;
constexpr int kBoostHotkey = 1;
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT_PTR kMaintenanceTimer = 2;
constexpr UINT_PTR kTrayTimer = 3;
constexpr DWORD kPanelStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL;
constexpr DWORD kPanelExStyle = WS_EX_APPWINDOW;

enum MenuItem : UINT {
    menuWhip = 100, menuAnimation, menuSound, menuReducedMotion,
    menuLow, menuNormal, menuCrazy, menuStatus, menuRestore, menuExit, menuBoost, menuOpen
};

struct Strike {
    AppTarget target;
    Clock::time_point start;
    bool allowBoost;
    std::optional<BoostResult> result;
};

float Elapsed(const Strike& strike)
{
    return std::chrono::duration<float, std::milli>(Clock::now() - strike.start).count();
}

RECT PanelBounds(UINT dpi)
{
    const int scale = static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI);
    RECT bounds{0, 0, MulDiv(Panel::width, scale, 96), MulDiv(Panel::height, scale, 96)};
    AdjustWindowRectExForDpi(&bounds, kPanelStyle, FALSE, kPanelExStyle, static_cast<UINT>(scale));
    bounds.right += GetSystemMetricsForDpi(SM_CXVSCROLL, static_cast<UINT>(scale));
    return bounds;
}

class App {
public:
    explicit App(HINSTANCE instance) : instance_(instance) {}

    ~App()
    {
        if (sound_) {
            PlaySoundW(nullptr, nullptr, 0);
        }
        priorities_.Restore();
        if (window_) {
            if (boostHotkeyRegistered_) {
                UnregisterHotKey(window_, kBoostHotkey);
            }
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
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = L"WhipCPU.Controller";
        if (!icon_ || !RegisterClassW(&windowClass)) {
            return false;
        }

        const RECT initial = PanelBounds(GetDpiForSystem());
        window_ = CreateWindowExW(kPanelExStyle, windowClass.lpszClassName,
            L"VineWhip", kPanelStyle, CW_USEDEFAULT, CW_USEDEFAULT, initial.right - initial.left,
            initial.bottom - initial.top, nullptr, nullptr, instance_, this);
        if (!window_) {
            return false;
        }
        const RECT sized = PanelBounds(GetDpiForWindow(window_));
        SetWindowPos(window_, nullptr, 0, 0, sized.right - sized.left, sized.bottom - sized.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        boostHotkeyRegistered_ = RegisterHotKey(window_, kBoostHotkey,
            MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B') != FALSE;
        if (!AddTrayIcon()) {
            SetTimer(window_, kTrayTimer, 1000, nullptr);
        }
        panel_.Refresh(window_);
        ShowWindow(window_, SW_SHOWNORMAL);
        SyncTimers();
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
            app = static_cast<App*>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (app && app->window_) {
            return app->HandleMessage(message, wParam, lParam);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (taskbarCreated_ && message == taskbarCreated_) {
            if (!AddTrayIcon()) {
                SetTimer(window_, kTrayTimer, 1000, nullptr);
            }
            return 0;
        }
        switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(window_, &paint);
            const std::optional<PanelStrike> effect = strike_
                ? std::optional(PanelStrike{strike_->target, strike_->result, Elapsed(*strike_)})
                : std::nullopt;
            panel_.Paint(window_, dc, {whip_, boostEnabled_, animation_, reducedMotion_,
                restorePending_, boostHotkeyRegistered_, priorities_.Pid(), intensity_},
                effect ? &*effect : nullptr);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE:
            panel_.PointAt({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window_, 0};
                trackingMouse_ = TrackMouseEvent(&tracking) != FALSE;
            }
            if (!frameInterval_) {
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            panel_.Leave();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (panel_.HitsBoostSwitch(window_, point)) {
                ToggleBoost();
            } else {
                HitRow(panel_.HitTest(window_, point));
            }
            return 0;
        }
        case WM_VSCROLL:
            panel_.Scroll(window_, LOWORD(wParam));
            return 0;
        case WM_MOUSEWHEEL:
            panel_.Wheel(window_, GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_KEYDOWN:
            if (PanelKey(wParam, lParam)) {
                return 0;
            }
            break;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) {
                panel_.Leave();
            } else if (PanelVisible()) {
                panel_.Refresh(window_);
            }
            SyncTimers();
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(window_, nullptr, FALSE);
            break;
        case WM_DPICHANGED: {
            const auto& suggested = *reinterpret_cast<const RECT*>(lParam);
            const RECT bounds = PanelBounds(HIWORD(wParam));
            SetWindowPos(window_, nullptr, suggested.left, suggested.top,
                bounds.right - bounds.left, bounds.bottom - bounds.top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_HOTKEY:
            if (wParam == static_cast<WPARAM>(kBoostHotkey) && boostHotkeyRegistered_) {
                ToggleBoost();
            }
            return 0;
        case WM_CONTEXTMENU:
            ShowMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case kTrayEvent:
            if (LOWORD(lParam) == WM_CONTEXTMENU) {
                ShowMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
            } else if (LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT) {
                OpenPanel();
            }
            return 0;
        case WM_TIMER:
            if (wParam == kFrameTimer) {
                Frame();
            } else if (wParam == kMaintenanceTimer) {
                Maintain();
            } else if (wParam == kTrayTimer && AddTrayIcon()) {
                KillTimer(window_, kTrayTimer);
            }
            return 0;
        case WM_CLOSE:
            closing_ = true;
            strike_.reset();
            if (RestoreAll()) {
                PostQuitMessage(0);
            } else {
                NotifyRestoreFailure();
                SyncTimers();
            }
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wParam) {
                closing_ = true;
                strike_.reset();
                priorities_.Restore();
                PostQuitMessage(0);
            }
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    bool PanelKey(WPARAM key, LPARAM flags)
    {
        int index = panel_.Selection();
        switch (key) {
        case VK_UP: --index; break;
        case VK_DOWN: ++index; break;
        case VK_PRIOR: index -= Panel::visibleRows; break;
        case VK_NEXT: index += Panel::visibleRows; break;
        case VK_HOME: index = 0; break;
        case VK_END: index = panel_.Count() - 1; break;
        case VK_RETURN:
        case VK_SPACE:
            if (!(flags & (1L << 30))) {
                HitRow(index);
            }
            return true;
        default:
            return false;
        }
        panel_.Select(window_, index);
        return true;
    }

    void HitRow(int index)
    {
        const auto* app = panel_.AppAt(index);
        if (!app) {
            return;
        }
        const AppTarget target = app->target;
        panel_.Select(window_, index);
        if (!whip_ || closing_) {
            return;
        }
        strike_ = Strike{target, Clock::now(), boostEnabled_, std::nullopt};
        if (!animation_ || reducedMotion_) {
            ApplyStrike();
        }
        if (!animation_) {
            strike_.reset();
        }
        InvalidateRect(window_, nullptr, FALSE);
        SyncTimers();
    }

    void ApplyStrike()
    {
        auto result = restorePending_ ? BoostResult::restorePending : BoostResult::disabled;
        if (strike_->allowBoost && boostEnabled_) {
            result = restorePending_ && !RestoreAll() ?
                BoostResult::restorePending : priorities_.Boost(strike_->target);
            restorePending_ = result == BoostResult::restorePending;
        }
        strike_->result = result;
        if (sound_) {
            PlaySoundW(MAKEINTRESOURCEW(IDR_WHIP_CRACK), instance_,
                SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
        }
        UpdateTooltip();
    }

    void Frame()
    {
        if (strike_) {
            const float elapsed = Elapsed(*strike_);
            if (!strike_->result && elapsed >= 220.0f) {
                ApplyStrike();
            }
            if (elapsed >= 750.0f) {
                strike_.reset();
            }
        }
        if (PanelVisible()) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        SyncTimers();
    }

    bool PanelVisible() const
    {
        return IsWindowVisible(window_) && !IsIconic(window_);
    }

    void SyncTimers()
    {
        const bool visible = PanelVisible();
        const bool workers = visible && !closing_ && whip_ && animation_ && !reducedMotion_ &&
            panel_.Count() > 0;
        const UINT interval = strike_ ? (workers ? 15 : 33) : (workers ? 33 : 0);
        if (interval != frameInterval_) {
            KillTimer(window_, kFrameTimer);
            frameInterval_ = interval && SetTimer(window_, kFrameTimer, interval, nullptr) ? interval : 0;
        }
        const bool maintenance = visible || priorities_.Pid() != 0;
        if (maintenance && !maintenanceRunning_) {
            maintenanceRunning_ = SetTimer(window_, kMaintenanceTimer, 1000, nullptr) != 0;
        } else if (!maintenance && maintenanceRunning_) {
            KillTimer(window_, kMaintenanceTimer);
            maintenanceRunning_ = false;
        }
    }

    void Maintain()
    {
        if (restorePending_) {
            if (RestoreAll() && closing_) {
                PostQuitMessage(0);
                return;
            }
        } else {
            const DWORD previous = priorities_.Pid();
            priorities_.Refresh();
            if (priorities_.Pid() != previous) {
                UpdateTooltip();
            }
        }
        if (PanelVisible()) {
            panel_.Refresh(window_);
        }
        SyncTimers();
    }

    bool RestoreAll()
    {
        restorePending_ = !priorities_.Restore();
        UpdateTooltip();
        return !restorePending_;
    }

    void ToggleBoost()
    {
        boostEnabled_ = !boostEnabled_;
        if (!boostEnabled_) {
            const bool restored = RestoreAll();
            if (strike_) {
                strike_->allowBoost = false;
                if (strike_->result) {
                    strike_->result = restored ? BoostResult::disabled : BoostResult::restorePending;
                }
            }
            if (restored) {
                Notify(L"CPU boost disabled.", NIIF_INFO);
            } else {
                NotifyRestoreFailure();
            }
        } else {
            UpdateTooltip();
            Notify(L"CPU boost enabled.", NIIF_INFO);
        }
        InvalidateRect(window_, nullptr, FALSE);
        SyncTimers();
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
            return boostEnabled_ ? L"Boosted: none" : L"CPU Boost off";
        }
        return (restorePending_ ? L"Restore pending: " : L"Boosted: ") + priorities_.Name() +
            L" (PID " + std::to_wstring(priorities_.Pid()) + L")";
    }

    void SetTooltip(NOTIFYICONDATAW& data) const
    {
        const std::wstring tip = (whip_ ? L"VineWhip | " : L"VineWhip (off) | ") + BoostStatus();
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

    void Notify(const wchar_t* message, DWORD flags) const
    {
        auto data = TrayData();
        data.uFlags = NIF_INFO;
        data.dwInfoFlags = flags;
        StringCchCopyW(data.szInfoTitle, ARRAYSIZE(data.szInfoTitle), L"VineWhip");
        StringCchCopyW(data.szInfo, ARRAYSIZE(data.szInfo), message);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void NotifyRestoreFailure() const
    {
        Notify(L"Windows refused to restore the original priority. WhipCPU will retry.", NIIF_WARNING);
    }

    void OpenPanel()
    {
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
        panel_.Refresh(window_);
        SyncTimers();
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
        AppendMenuW(menu, MF_STRING, menuOpen, L"Open VineWhip");
        AppendMenuW(menu, check(whip_), menuWhip, L"Whip Mode");
        AppendMenuW(menu, check(boostEnabled_), menuBoost, boostHotkeyRegistered_ ?
            L"CPU Boost\tCtrl+Alt+B" : L"CPU Boost (shortcut unavailable)");
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
            GetCursorPos(&anchor);
        }
        menuOpen_ = true;
        panel_.Leave();
        SetForegroundWindow(window_);
        const UINT command = static_cast<UINT>(TrackPopupMenu(menu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, anchor.x, anchor.y, 0, window_, nullptr));
        PostMessageW(window_, WM_NULL, 0, 0);
        DestroyMenu(menu);
        menuOpen_ = false;
        ApplyMenu(command);
    }

    void ApplyMenu(UINT command)
    {
        switch (command) {
        case menuOpen: OpenPanel(); break;
        case menuWhip:
            whip_ = !whip_;
            if (!whip_) {
                strike_.reset();
                if (!RestoreAll()) {
                    NotifyRestoreFailure();
                }
            }
            break;
        case menuBoost: ToggleBoost(); break;
        case menuAnimation:
            animation_ = !animation_;
            if (!animation_ && strike_) {
                if (!strike_->result) {
                    ApplyStrike();
                }
                strike_.reset();
            }
            break;
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
            strike_.reset();
            if (!RestoreAll()) {
                NotifyRestoreFailure();
            }
            break;
        case menuExit: SendMessageW(window_, WM_CLOSE, 0, 0); break;
        }
        InvalidateRect(window_, nullptr, FALSE);
        UpdateTooltip();
        SyncTimers();
    }

    HINSTANCE instance_{};
    HWND window_{};
    HICON icon_{};
    UINT taskbarCreated_{};
    Panel panel_;
    PriorityManager priorities_;
    std::optional<Strike> strike_;
    Intensity intensity_{Intensity::normal};
    UINT frameInterval_{};
    bool whip_{true};
    bool boostEnabled_{true};
    bool boostHotkeyRegistered_{};
    bool animation_{true};
    bool sound_{};
    bool reducedMotion_{};
    bool restorePending_{};
    bool maintenanceRunning_{};
    bool trackingMouse_{};
    bool menuOpen_{};
    bool closing_{};
};

int RunApp(HINSTANCE instance)
{
    App app(instance);
    if (!app.Initialize()) {
        MessageBoxW(nullptr, L"Could not open the VineWhip window.",
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
        MessageBoxW(nullptr, L"VineWhip is already running.",
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
