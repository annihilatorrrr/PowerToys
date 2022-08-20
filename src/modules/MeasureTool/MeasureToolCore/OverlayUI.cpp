#include "pch.h"

#include "BoundsToolOverlayUI.h"
#include "MeasureToolOverlayUI.h"
#include "OverlayUI.h"

#include <common/Display/dpi_aware.h>
#include <common/Display/monitors.h>
#include <common/logger/logger.h>
#include <common/Themes/windows_colors.h>
#include <common/utils/window.h>

namespace NonLocalizable
{
    const wchar_t MeasureToolOverlayWindowName[] = L"PowerToys.MeasureToolOverlayWindow";
    const wchar_t BoundsToolOverlayWindowName[] = L"PowerToys.BoundsToolOverlayWindow";
}

//#define DEBUG_OVERLAY

void SetClipBoardToText(const std::wstring_view text)
{
    if (!OpenClipboard(nullptr))
    {
        return;
    }

    wil::unique_hglobal handle{ GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>((text.length() + 1) * sizeof(wchar_t))) };
    if (!handle)
    {
        CloseClipboard();
        return;
    }

    if (auto* bufPtr = static_cast<wchar_t*>(GlobalLock(handle.get())); bufPtr != nullptr)
    {
        text.copy(bufPtr, text.length());
        GlobalUnlock(handle.get());
    }

    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, handle.get());
    CloseClipboard();
}

LRESULT CALLBACK measureToolWndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    switch (message)
    {
    case WM_CREATE:
    {
        auto commonState = GetWindowCreateParam<const CommonState*>(lparam);
        StoreWindowParam(window, commonState);

#if !defined(DEBUG_OVERLAY)
        for (; ShowCursor(false) > 0;)
            ;
#endif
        break;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        break;
    case WM_KEYUP:
        if (wparam == VK_ESCAPE)
        {
            PostMessageW(window, WM_CLOSE, {}, {});
        }
        break;
    case WM_RBUTTONUP:
        PostMessageW(window, WM_CLOSE, {}, {});
        break;
    case WM_LBUTTONUP:
        if (auto commonState = GetWindowParam<const CommonState*>(window))
        {
            commonState->overlayBoxText.Read([](const OverlayBoxText& text) {
                SetClipBoardToText(text.buffer);
            });
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK boundsToolWndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    switch (message)
    {
    case WM_CLOSE:
        DestroyWindow(window);
        break;
    case WM_CREATE:
    {
        auto toolState = GetWindowCreateParam<BoundsToolState*>(lparam);
        StoreWindowParam(window, toolState);
        break;
    }
    case WM_KEYUP:
        if (wparam == VK_ESCAPE)
        {
            PostMessageW(window, WM_CLOSE, {}, {});
        }
        break;
    case WM_LBUTTONDOWN:
    {
        auto toolState = GetWindowParam<BoundsToolState*>(window);
        POINT cursorPos = toolState->commonState->cursorPos;
        ScreenToClient(window, &cursorPos);

        D2D_POINT_2F newRegionStart = { .x = static_cast<float>(cursorPos.x), .y = static_cast<float>(cursorPos.y) };
        toolState->currentRegionStart = newRegionStart;
        break;
    }
    // We signal this when active monitor has changed -> must reset the state
    case WM_USER:
    {
        auto toolState = GetWindowParam<BoundsToolState*>(window);
        toolState->currentRegionStart = std::nullopt;
        break;
    }
    case WM_LBUTTONUP:
        if (auto toolState = GetWindowParam<BoundsToolState*>(window); toolState->currentRegionStart.has_value())
        {
            const auto cursorPos = toolState->commonState->cursorPos;
            D2D_POINT_2F newRegionEnd = { .x = static_cast<float>(cursorPos.x), .y = static_cast<float>(cursorPos.y) };
            toolState->currentRegionStart = std::nullopt;

            toolState->commonState->overlayBoxText.Read([](const OverlayBoxText& text) {
                SetClipBoardToText(text.buffer);
            });
        }
        break;
    case WM_RBUTTONUP:
        PostMessageW(window, WM_CLOSE, {}, {});
        break;
    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

void CreateOverlayWindowClasses()
{
    WNDCLASSEXW wcex{ .cbSize = sizeof(WNDCLASSEX), .hInstance = GetModuleHandleW(nullptr) };
    wcex.lpfnWndProc = measureToolWndProc;
    wcex.lpszClassName = NonLocalizable::MeasureToolOverlayWindowName;
    RegisterClassExW(&wcex);

    wcex.lpfnWndProc = boundsToolWndProc;
    wcex.lpszClassName = NonLocalizable::BoundsToolOverlayWindowName;
    wcex.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    RegisterClassExW(&wcex);
}

HWND CreateOverlayUIWindow(const CommonState& commonState,
                           const MonitorInfo& monitor,
                           const wchar_t* windowClass,
                           void* extraParam)
{
    static std::once_flag windowClassesCreatedFlag;
    std::call_once(windowClassesCreatedFlag, CreateOverlayWindowClasses);

    const auto screenArea = monitor.GetScreenSize(false);
    HWND window{ CreateWindowExW(WS_EX_TOOLWINDOW,
                                 windowClass,
                                 L"PowerToys.MeasureToolOverlay",
                                 WS_POPUP,
                                 screenArea.left(),
                                 screenArea.top(),
                                 screenArea.width(),
                                 screenArea.height(),
                                 nullptr,
                                 nullptr,
                                 GetModuleHandleW(nullptr),
                                 extraParam) };
    winrt::check_bool(window);
    ShowWindow(window, SW_SHOWNORMAL);
#if !defined(DEBUG_OVERLAY)
    SetWindowPos(window, HWND_TOPMOST, {}, {}, {}, {}, SWP_NOMOVE | SWP_NOSIZE);
#else
    (void)window;
#endif

    const int pos = -GetSystemMetrics(SM_CXVIRTUALSCREEN) - 8;
    if (wil::unique_hrgn hrgn{ CreateRectRgn(pos, 0, (pos + 1), 1) })
    {
        DWM_BLURBEHIND bh = { DWM_BB_ENABLE | DWM_BB_BLURREGION, TRUE, hrgn.get(), FALSE };
        DwmEnableBlurBehindWindow(window, &bh);
    }

    RECT windowRect = {};
    // Exclude toolbar from the window's region to be able to use toolbar during tool usage.
    if (monitor.IsPrimary() && GetWindowRect(window, &windowRect))
    {
        // will be freed during SetWindowRgn call
        const HRGN windowRegion{ CreateRectRgn(windowRect.left, windowRect.top, windowRect.right, windowRect.bottom) };
        wil::unique_hrgn toolbarRegion{ CreateRectRgn(commonState.toolbarBoundingBox.left(),
                                                      commonState.toolbarBoundingBox.top(),
                                                      commonState.toolbarBoundingBox.right(),
                                                      commonState.toolbarBoundingBox.bottom()) };
        const auto res = CombineRgn(windowRegion, windowRegion, toolbarRegion.get(), RGN_DIFF);
        if (res != ERROR)
            SetWindowRgn(window, windowRegion, true);
    }

    return window;
}

std::vector<D2D1::ColorF> AppendCommonOverlayUIColors(const D2D1::ColorF lineColor)
{
    D2D1::ColorF foreground = D2D1::ColorF::Black;
    D2D1::ColorF background = D2D1::ColorF(0.96f, 0.96f, 0.96f, 1.0f);
    D2D1::ColorF border = D2D1::ColorF(0.44f, 0.44f, 0.44f, 0.4f);

    if (WindowsColors::is_dark_mode())
    {
        foreground = D2D1::ColorF::White;
        background = D2D1::ColorF(0.17f, 0.17f, 0.17f, 1.0f);
        border = D2D1::ColorF(0.44f, 0.44f, 0.44f, 0.4f);
    }

    return { lineColor, foreground, background, border };
}

void OverlayUIState::RunUILoop()
{
    while (IsWindow(_window))
    {
        _d2dState.rt->BeginDraw();
        _d2dState.rt->Clear(D2D1::ColorF(1.f, 1.f, 1.f, 0.f));
        const auto cursor = _commonState.cursorPos;
        const bool cursorOverToolbar = _commonState.toolbarBoundingBox.inside(cursor);
        const bool cursorOnScreen = _monitorArea.inside(cursor);
        const bool draw = !cursorOverToolbar && cursorOnScreen;
        if (draw)
        {
            _tickFunc();
        }

        _d2dState.rt->EndDraw();
        if (draw)
        {
            InvalidateRect(_window, nullptr, true);
        }

        if (cursorOnScreen != _cursorOnScreen)
        {
            _cursorOnScreen = cursorOnScreen;
            PostMessageW(_window, WM_USER, {}, {});
            ShowWindow(_window, cursorOnScreen ? SW_SHOW : SW_HIDE);
        }

        run_message_loop(true, 1);
    }
}

template<typename StateT, typename TickFuncT>
OverlayUIState::OverlayUIState(StateT& toolState,
                               TickFuncT tickFunc,
                               const CommonState& commonState,
                               HWND window) :
    _window{ window },
    _commonState{ commonState },
    _d2dState{ window, AppendCommonOverlayUIColors(commonState.lineColor) },
    _tickFunc{ [&] {
        tickFunc(_commonState, toolState, _window, _d2dState);
    } }
{
}

OverlayUIState::~OverlayUIState() noexcept
{
    PostMessageW(_window, WM_CLOSE, {}, {});
    // Extra cautious to not trigger termination due to noexcept
    try
    {
        _uiThread.join();
    }
    catch (...)
    {
    }
}

// Returning unique_ptr, since we need to pin ui state in memory
template<typename ToolT, typename TickFuncT>
inline std::unique_ptr<OverlayUIState> OverlayUIState::CreateInternal(ToolT& toolState,
                                                                      TickFuncT tickFunc,
                                                                      const CommonState& commonState,
                                                                      const wchar_t* toolWindowClassName,
                                                                      void* windowParam,
                                                                      const MonitorInfo& monitor)
{
    wil::shared_event uiCreatedEvent(wil::EventOptions::ManualReset);
    std::unique_ptr<OverlayUIState> uiState;
    auto threadHandle = SpawnLoggedThread(L"OverlayUI thread", [&] {
        const HWND window = CreateOverlayUIWindow(commonState, monitor, toolWindowClassName, windowParam);
        uiState = std::unique_ptr<OverlayUIState>{ new OverlayUIState{ toolState, tickFunc, commonState, window } };
        uiState->_monitorArea = monitor.GetScreenSize(true);
        uiCreatedEvent.SetEvent();

        uiState->RunUILoop();
        commonState.sessionCompletedCallback();
    });

    uiState->_uiThread = std::move(threadHandle);
    uiCreatedEvent.wait();
    return uiState;
}

std::unique_ptr<OverlayUIState> OverlayUIState::Create(Serialized<MeasureToolState>& toolState,
                                                       const CommonState& commonState,
                                                       const MonitorInfo& monitor)
{
    return OverlayUIState::CreateInternal(toolState,
                                          DrawMeasureToolTick,
                                          commonState,
                                          NonLocalizable::MeasureToolOverlayWindowName,
                                          // ok to cast away const, since member access is serialized
                                          (void*)(&commonState),
                                          monitor);
}

std::unique_ptr<OverlayUIState> OverlayUIState::Create(BoundsToolState& toolState,
                                                       const CommonState& commonState,
                                                       const MonitorInfo& monitor)
{
    return OverlayUIState::CreateInternal(toolState,
                                          DrawBoundsToolTick,
                                          commonState,
                                          NonLocalizable::BoundsToolOverlayWindowName,
                                          &toolState,
                                          monitor);
}