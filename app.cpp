// FoxrCI — Win32 GDI 宿主程序
//
// 设计约束：只用 main()，不写 wWinMain / WinMain。
// 原理：MinGW 的 PE 头 subsystem = WINDOWS 时，链接器默认入口是
// WinMainCRTStartup；显式把入口改成 mainCRTStartup，就会去调 main()，
// 而且不会分配控制台窗口。CMakeLists 里已加 -Wl,-e,mainCRTStartup。
//
// 拿不到 WinMain 的三个参数？都有替代：
//   hInstance -> GetModuleHandleW(nullptr)
//   nCmdShow  -> GetStartupInfoW().wShowWindow
//   lpCmdLine -> GetCommandLineW() / CommandLineToArgvW()
//
// 编译：cmake -S . -B build -G Ninja && cmake --build build

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

constexpr wchar_t kClassName[] = L"FoxrCIMainWindow";
constexpr wchar_t kTitle[]     = L"FoxrCI";

HFONT g_font = nullptr;

// ---- 绘制 --------------------------------------------------------------
// GDI 全在这一处画。要做自己的界面，从这里长出去。
void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    // 双缓冲：画在内存位图上再整体贴过去，避免 WM_ERASEBKGND 的闪烁
    HDC     mem     = CreateCompatibleDC(dc);
    HBITMAP bmp     = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);
    HGDIOBJ old_fnt = SelectObject(mem, g_font);

    FillRect(mem, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(0x1a, 0x1a, 0x1a));

    RECT text = rc;
    text.left += 28;
    text.top  += 28;
    DrawTextW(mem,
              L"FoxrCI\n\n"
              L"GDI 已就绪。\n"
              L"这个窗口由 main() 启动，没有 wWinMain。",
              -1, &text, DT_LEFT | DT_TOP | DT_NOCLIP);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old_fnt);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;  // 背景已在 Paint 里整块覆盖，别让系统再擦一遍

    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// 用系统界面字体，别用 CreateFontW 硬编码字号（DWM 缩放下会糊）
HFONT MakeUiFont() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

int RunApp(HINSTANCE inst, int show_cmd) {
    g_font = MakeUiFont();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.hCursor       = static_cast<HCURSOR>(LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW));
    wc.hbrBackground = nullptr;  // 自己画，不要系统背景刷
    wc.lpszClassName = kClassName;

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, kClassName, kTitle,
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, show_cmd);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_font) {
        DeleteObject(g_font);
        g_font = nullptr;
    }
    return static_cast<int>(msg.wParam);
}

}  // namespace

int main() {
    SetProcessDPIAware();  // 高 DPI 下不糊；Win10 1703+ 可用 SetProcessDpiAwarenessContext

    // main() 没有 nCmdShow，用 STARTUPINFO 里的值（资源管理器双击时会带上）
    int show_cmd = SW_SHOWDEFAULT;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    GetStartupInfoW(&si);
    if (si.dwFlags & STARTF_USESHOWWINDOW) {
        show_cmd = si.wShowWindow;
    }

    return RunApp(GetModuleHandleW(nullptr), show_cmd);
}
