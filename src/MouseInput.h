#pragma once

#include <windows.h>

#include <atomic>
#include <future>
#include <thread>

inline constexpr UINT kMouseMoved = WM_APP + 1;
inline constexpr UINT kMouseClicked = WM_APP + 2;

class MouseInput {
public:
    ~MouseInput();

    bool Start(HWND receiver);
    void Stop();
    void Configure(bool enabled, bool trackMotion);
    void AcknowledgeMove() { moveQueued_ = false; }

private:
    static LRESULT CALLBACK Hook(int code, WPARAM message, LPARAM data);
    void Run(std::promise<bool> ready);

    static thread_local MouseInput* current_;
    HWND receiver_{};
    DWORD threadId_{};
    std::thread thread_;
    std::atomic<bool> enabled_{true};
    std::atomic<bool> trackMotion_{true};
    std::atomic<bool> moveQueued_{false};
};
