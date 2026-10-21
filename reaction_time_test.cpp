// MODEZERO- click toggles black/white, W = white, B = black 
// esc to quit

//#define USETHREADS    // render on a second thread; the main thread blocks on input
#define MODEZERO        // comment out for the reaction-time benchmark; leave in for black/white toggle
//#define FPSLIMIT 4000 // uncomment to uncap
#define GPUSYNC
//#define PINCORE 4     // pin the process to the Nth available logical core (1-based)
// viewport size
#define HEIGHT GetSystemMetrics(SM_CYSCREEN)
#define WIDTH  GetSystemMetrics(SM_CXSCREEN)
//<----------- no need to change below this point ----------->

#include <d3d9.h>
#include <atomic>
#include <chrono>
#ifndef MODEZERO
#include <climits>
#include <cstdio>
#include <random>
#endif
#ifdef USETHREADS
#include <thread>
#endif

using Clock = std::chrono::steady_clock;
static inline long long nowMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

constexpr D3DCOLOR BLACK   = 0x00000000;
constexpr D3DCOLOR WHITE   = 0x00FFFFFF;
constexpr D3DCOLOR RED     = 0x00FF0000;
constexpr D3DCOLOR GREEN_C = 0x0000FF00;

static std::atomic<bool> stop{ false };

#ifdef MODEZERO
static std::atomic<D3DCOLOR> currentColor{ WHITE };
#else
enum State : int { STATS = 0, WAITING = 1, GREEN = 2 };
static std::atomic<int> state{ STATS };
static std::atomic<long long> redStartUs{ 0 };
static std::atomic<long long> greenDueUs{ 0 };
static std::atomic<long long> greenShownUs{ 0 };

// Input-thread only.
static int earlyClicks = 0;
static long long lastClick = 0, minClick = LLONG_MAX, maxClick = 0, clickSum = 0, clickCount = 0;
static std::mt19937 rng = [] {
    std::seed_seq seq{ std::random_device{}(), static_cast<unsigned>(nowMicros()) };
    return std::mt19937(seq);
}();
static std::uniform_int_distribution<int> delayMs(3000, 3999);  // red -> green delay
#endif

#ifdef FPSLIMIT
constexpr long long FRAME_US = FPSLIMIT > 0 ? 1000000LL / FPSLIMIT : 0;
#endif
#ifdef GPUSYNC
static IDirect3DQuery9* gpuQuery = nullptr;
#endif

static void present(IDirect3DDevice9* dev, D3DCOLOR color) {
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, color, 0.0f, 0);
    dev->Present(nullptr, nullptr, nullptr, nullptr);
#ifdef GPUSYNC
    if (gpuQuery) {
        gpuQuery->Issue(D3DISSUE_END);
        while (gpuQuery->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {}
    }
#endif
}

#ifndef MODEZERO
static void clearConsole() {  // to replace system("CLS")
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &csbi)) return;
    DWORD cells = static_cast<DWORD>(csbi.dwSize.X) * static_cast<DWORD>(csbi.dwSize.Y), written;
    COORD home = { 0, 0 };
    FillConsoleOutputCharacterW(h, L' ', cells, home, &written);
    FillConsoleOutputAttribute(h, csbi.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(h, home);
}

static void printStats() {
    std::printf("\n last click: %.1f ms\n min click: %.1f ms\n max click: %.1f ms\n"
                " average click: %.1f ms\n successful clicks: %lld\n early clicks: %d\n",
                lastClick / 1000.0, minClick / 1000.0, maxClick / 1000.0,
                (double)clickSum / clickCount / 1000.0, clickCount, earlyClicks);
    std::fflush(stdout);
}

static void onClick(long long t) {
    int s = state.load(std::memory_order_acquire);
    if (s == WAITING) {
        if (t - redStartUs.load(std::memory_order_relaxed) < 500000) return;  // grace period for double clicks
        int expected = WAITING;
        if (state.compare_exchange_strong(expected, STATS)) { earlyClicks++; return; }
        s = expected;  // the screen turned green while checking
    }
    if (s == GREEN) {
        long long shown = greenShownUs.load(std::memory_order_acquire);
        state.store(STATS, std::memory_order_release);
        if (shown == 0 || t < shown) { earlyClicks++; return; }  // clicked before green was on screen
        lastClick = t - shown;
        if (lastClick < minClick) minClick = lastClick;
        if (lastClick > maxClick) maxClick = lastClick;
        clickSum += lastClick;
        clickCount++;
        printStats();
    } else if (s == STATS) {
        clearConsole();
        greenShownUs.store(0, std::memory_order_relaxed);
        redStartUs.store(t, std::memory_order_relaxed);
        greenDueUs.store(t + 1000LL * delayMs(rng), std::memory_order_relaxed);
        state.store(WAITING, std::memory_order_release);  // publish the timestamps above
    }
}
#endif

static void onKey(USHORT vkey) {
    if (vkey == VK_ESCAPE) { stop.store(true); return; }
#ifdef MODEZERO
    if (vkey == 'W') currentColor.store(WHITE, std::memory_order_relaxed);
    else if (vkey == 'B') currentColor.store(BLACK, std::memory_order_relaxed);
#endif
}

static void onRawInput(long long t, LPARAM lParam) {
    RAWINPUT ri;
    UINT size = sizeof(ri);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;
    if (ri.header.dwType == RIM_TYPEMOUSE) {
        if (ri.data.mouse.usButtonFlags & (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN)) {
#ifdef MODEZERO
            currentColor.store(currentColor.load(std::memory_order_relaxed) == WHITE ? BLACK : WHITE,
                               std::memory_order_relaxed);
#else
            onClick(t);
#endif
        }
    } else if (ri.header.dwType == RIM_TYPEKEYBOARD) {  // only read keyboard fields from keyboard packets
        UINT m = ri.data.keyboard.Message;
        if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) onKey(ri.data.keyboard.VKey);
    }
    (void)t;
}

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INPUT:
        onRawInput(nowMicros(), lParam);
        if (GET_RAWINPUT_CODE_WPARAM(wParam) == RIM_INPUT)
            DefWindowProcW(hWnd, msg, wParam, lParam);
        return 0;
    case WM_CLOSE:
        stop.store(true);
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        stop.store(true);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

static void renderFrame(IDirect3DDevice9* dev) {
#ifndef MODEZERO
    if (state.load(std::memory_order_acquire) == WAITING &&
        nowMicros() >= greenDueUs.load(std::memory_order_relaxed)) {
        int expected = WAITING;
        if (state.compare_exchange_strong(expected, GREEN)) {
            present(dev, GREEN_C);
            greenShownUs.store(nowMicros(), std::memory_order_release);
            return;
        }
    }
#endif
#ifdef FPSLIMIT
    static long long lastFrameUs = 0;
    long long t = nowMicros();
    if (t - lastFrameUs < FRAME_US) return;
    lastFrameUs = t;
#endif
#ifdef MODEZERO
    present(dev, currentColor.load(std::memory_order_relaxed));
#else
    int s = state.load(std::memory_order_relaxed);
    present(dev, s == WAITING ? RED : s == GREEN ? GREEN_C : BLACK);
#endif
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.lpszClassName = L"ReactionTimeTest";
    if (!RegisterClassExW(&wc)) return -1;

    // WS_EX_TOPMOST is an extended style, so it belongs in the first argument.
    HWND hWnd = CreateWindowExW(WS_EX_TOPMOST, L"ReactionTimeTest", L"Reaction Time Test", WS_POPUP,
                                0, 0, screenW, screenH, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return -1;
    ShowWindow(hWnd, nCmdShow);

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return -1;

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = FALSE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hWnd;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = static_cast<UINT>(screenW);
    pp.BackBufferHeight = static_cast<UINT>(screenH);
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;  // vsync off

    IDirect3DDevice9* dev = nullptr;
    if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                 D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev)) || !dev) {
        d3d->Release();
        return -1;
    }
    D3DVIEWPORT9 viewport = { 0, 0, static_cast<DWORD>(WIDTH), static_cast<DWORD>(HEIGHT), 0.0f, 1.0f };
    if (FAILED(dev->SetViewport(&viewport))) { dev->Release(); d3d->Release(); return -1; }
#ifdef GPUSYNC
    if (FAILED(dev->CreateQuery(D3DQUERYTYPE_EVENT, &gpuQuery))) gpuQuery = nullptr;  // fall back to no sync
#endif

    RAWINPUTDEVICE devices[2] = {
        { 0x01, 0x06, RIDEV_NOLEGACY, hWnd },  // keyboard
        { 0x01, 0x02, RIDEV_NOLEGACY, hWnd },  // mouse
    };
    if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) return -1;

    ShowCursor(FALSE);
    SetCursor(nullptr);
    dev->ShowCursor(FALSE);

    HANDLE process = GetCurrentProcess();
#ifdef PINCORE
    DWORD_PTR procMask, sysMask;
    if (GetProcessAffinityMask(process, &procMask, &sysMask)) {
        int n = 0;
        for (int bit = 0; bit < static_cast<int>(sizeof(DWORD_PTR) * 8); ++bit) {
            DWORD_PTR b = static_cast<DWORD_PTR>(1) << bit;
            if ((procMask & b) && ++n == PINCORE) { SetProcessAffinityMask(process, b); break; }
        }
    }
#endif
    SetPriorityClass(process, REALTIME_PRIORITY_CLASS);  // widnows grants high instead unless run as admin

    MSG msg;
#ifdef USETHREADS
    std::thread renderThread([dev] {
        while (!stop.load(std::memory_order_relaxed)) renderFrame(dev);
    });
    while (!stop.load()) {
        if (GetMessageW(&msg, nullptr, 0, 0) <= 0) { stop.store(true); break; }  // WM_QUIT or error
        DispatchMessageW(&msg);
    }
    renderThread.join();
#else
    while (!stop.load(std::memory_order_relaxed)) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { stop.store(true); break; }
            DispatchMessageW(&msg);
        }
        renderFrame(dev);
    }
#endif

#ifdef GPUSYNC
    if (gpuQuery) gpuQuery->Release();
#endif
    dev->Release();
    d3d->Release();
    if (IsWindow(hWnd)) DestroyWindow(hWnd);
    ShowCursor(TRUE);
#ifndef MODEZERO
    std::printf("\nPress Enter to Continue\n");
    std::getchar();
#endif
    return 0;
}

int main() {  // console entry point so the stats have somewhere to print
    return WinMain(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOWNORMAL);
}

// all done