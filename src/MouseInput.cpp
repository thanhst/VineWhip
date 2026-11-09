#include "MouseInput.h"

#include <utility>

thread_local MouseInput* MouseInput::current_ = nullptr;

MouseInput::~MouseInput()
{
    Stop();
}

bool MouseInput::Start(HWND receiver)
{
    receiver_ = receiver;
    std::promise<bool> ready;
    auto started = ready.get_future();
    thread_ = std::thread(&MouseInput::Run, this, std::move(ready));
    if (!started.get()) {
        thread_.join();
        return false;
    }
    return true;
}

void MouseInput::Stop()
{
    if (thread_.joinable()) {
        PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
        thread_.join();
    }
}

void MouseInput::Configure(bool enabled, bool trackMotion)
{
    enabled_ = enabled;
    trackMotion_ = trackMotion;
}

void MouseInput::Run(std::promise<bool> ready)
{
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    threadId_ = GetCurrentThreadId();
    current_ = this;
    const HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, Hook, GetModuleHandleW(nullptr), 0);
    ready.set_value(hook != nullptr);
    if (hook) {
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            DispatchMessageW(&message);
        }
        UnhookWindowsHookEx(hook);
    }
    current_ = nullptr;
}

LRESULT CALLBACK MouseInput::Hook(int code, WPARAM message, LPARAM data)
{
    if (code == HC_ACTION && current_->enabled_) {
        const auto& event = *reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        if (!(event.flags & LLMHF_INJECTED)) {
            if (message == WM_LBUTTONDOWN) {
                PostMessageW(current_->receiver_, kMouseClicked,
                    static_cast<WPARAM>(event.pt.x), static_cast<LPARAM>(event.pt.y));
            } else if (message == WM_MOUSEMOVE && current_->trackMotion_ &&
                !current_->moveQueued_.exchange(true)) {
                if (!PostMessageW(current_->receiver_, kMouseMoved, 0, 0)) {
                    current_->AcknowledgeMove();
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}
