// external read-only CS2 ESP: RPM + click-through layered GDI overlay
// F1 esp on/off, F2 team check, END quit
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include "offsets.hpp"

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

static HANDLE    g_proc;
static uintptr_t g_client;
static int       g_w, g_h;
static bool      g_esp = true, g_teamCheck = true;

template <class T> T rpm(uintptr_t a) {
    T v{};
    ReadProcessMemory(g_proc, reinterpret_cast<LPCVOID>(a), &v, sizeof(T), nullptr);
    return v;
}

static DWORD findPid(const wchar_t* name) {
    DWORD pid = 0;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e{ sizeof(e) };
    for (BOOL ok = Process32FirstW(s, &e); ok; ok = Process32NextW(s, &e))
        if (!_wcsicmp(e.szExeFile, name)) { pid = e.th32ProcessID; break; }
    CloseHandle(s);
    return pid;
}

static uintptr_t findModule(DWORD pid, const wchar_t* name) {
    uintptr_t base = 0;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32W e{ sizeof(e) };
    for (BOOL ok = Module32FirstW(s, &e); ok; ok = Module32NextW(s, &e))
        if (!_wcsicmp(e.szModule, name)) { base = reinterpret_cast<uintptr_t>(e.modBaseAddr); break; }
    CloseHandle(s);
    return base;
}

// entity list: chunk = list[(idx >> 9)], entry = chunk[idx & 0x1FF]
static uintptr_t entityByIndex(uintptr_t list, uint32_t idx) {
    uintptr_t chunk = rpm<uintptr_t>(list + off::entityChunkBase + 8 * (idx >> 9));
    if (!chunk) return 0;
    return rpm<uintptr_t>(chunk + off::entityStride * (idx & 0x1FF));
}

// view matrix is row-major 4x4, clip = M * [x y z 1]; screen Y flipped (NDC +y is up)
static bool w2s(const float m[16], const Vec3& p, Vec2& out) {
    float w = m[12] * p.x + m[13] * p.y + m[14] * p.z + m[15];
    if (w < 0.01f) return false;
    float x = m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3];
    float y = m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7];
    out.x = (g_w * 0.5f) * (1.f + x / w);
    out.y = (g_h * 0.5f) * (1.f - y / w);
    return true;
}

static void drawFrame(HDC dc) {
    RECT rc{ 0, 0, g_w, g_h };
    FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH)); // black = colorkey, transparent
    if (!g_esp) return;

    uintptr_t list  = rpm<uintptr_t>(g_client + off::dwEntityList);
    uintptr_t local = rpm<uintptr_t>(g_client + off::dwLocalPlayerPawn);
    if (!list || !local) return;

    float vm[16];
    ReadProcessMemory(g_proc, (LPCVOID)(g_client + off::dwViewMatrix), vm, sizeof(vm), nullptr);
    uint8_t localTeam = rpm<uint8_t>(local + off::m_iTeamNum);
    Vec3 localPos = rpm<Vec3>(local + off::m_vOldOrigin);

    static HPEN enemyPen = CreatePen(PS_SOLID, 1, RGB(255, 60, 60));
    static HPEN teamPen  = CreatePen(PS_SOLID, 1, RGB(60, 140, 255));
    static HPEN outPen   = CreatePen(PS_SOLID, 1, RGB(1, 1, 1));
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetBkMode(dc, TRANSPARENT);

    for (uint32_t i = 1; i <= 64; ++i) {
        uintptr_t ctrl = entityByIndex(list, i);
        if (!ctrl) continue;
        uint32_t hPawn = rpm<uint32_t>(ctrl + off::m_hPlayerPawn);
        if (hPawn == 0xFFFFFFFF) continue;
        uintptr_t pawn = entityByIndex(list, hPawn & 0x7FFF);
        if (!pawn || pawn == local) continue;

        int hp = rpm<int>(pawn + off::m_iHealth);
        if (hp <= 0 || hp > 100 || rpm<uint8_t>(pawn + off::m_lifeState) != 0) continue;
        uint8_t team = rpm<uint8_t>(pawn + off::m_iTeamNum);
        bool enemy = team != localTeam;
        if (g_teamCheck && !enemy) continue;

        Vec3 feet = rpm<Vec3>(pawn + off::m_vOldOrigin);
        Vec3 head{ feet.x, feet.y, feet.z + 72.f };
        Vec2 sf, sh;
        if (!w2s(vm, feet, sf) || !w2s(vm, head, sh)) continue;

        float h = sf.y - sh.y, w = h * 0.5f;
        int l = int(sh.x - w * 0.5f), t = int(sh.y), r = int(sh.x + w * 0.5f), b = int(sf.y);

        SelectObject(dc, outPen);
        Rectangle(dc, l - 1, t - 1, r + 2, b + 2);
        SelectObject(dc, enemy ? enemyPen : teamPen);
        Rectangle(dc, l, t, r + 1, b + 1);

        // health bar left of box
        int barTop = b - int(h * hp / 100.f);
        RECT bg{ l - 6, t - 1, l - 2, b + 1 }, fg{ l - 5, barTop, l - 3, b };
        FillRect(dc, &bg, (HBRUSH)GetStockObject(DKGRAY_BRUSH));
        HBRUSH hb = CreateSolidBrush(RGB(255 - hp * 255 / 100, hp * 255 / 100, 0));
        FillRect(dc, &fg, hb);
        DeleteObject(hb);

        char name[128]{};
        ReadProcessMemory(g_proc, (LPCVOID)(ctrl + off::m_iszPlayerName), name, sizeof(name) - 1, nullptr);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT nr{ l - 100, t - 16, r + 100, t };
        DrawTextA(dc, name, -1, &nr, DT_CENTER | DT_SINGLELINE | DT_NOCLIP);

        float dx = feet.x - localPos.x, dy = feet.y - localPos.y, dz = feet.z - localPos.z;
        char dist[32];
        snprintf(dist, sizeof(dist), "%dm", int(std::sqrt(dx * dx + dy * dy + dz * dz) * 0.0254f));
        RECT dr{ l - 100, b + 2, r + 100, b + 18 };
        DrawTextA(dc, dist, -1, &dr, DT_CENTER | DT_SINGLELINE | DT_NOCLIP);
    }
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int main() {
    DWORD pid = findPid(L"cs2.exe");
    if (!pid) { puts("cs2.exe not running"); return 1; }
    g_proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    g_client = findModule(pid, L"client.dll");
    if (!g_proc || !g_client) { puts("open/module failed (run as admin?)"); return 1; }
    printf("pid %lu client.dll %p\n", pid, (void*)g_client);

    g_w = GetSystemMetrics(SM_CXSCREEN);
    g_h = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ovl";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
                                wc.lpszClassName, L"", WS_POPUP, 0, 0, g_w, g_h,
                                nullptr, nullptr, wc.hInstance, nullptr);
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(hwnd, SW_SHOW);

    HDC wdc = GetDC(hwnd);
    HDC mdc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, g_w, g_h);
    SelectObject(mdc, bmp);

    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetAsyncKeyState(VK_END) & 1) break;
        if (GetAsyncKeyState(VK_F1) & 1) g_esp = !g_esp;
        if (GetAsyncKeyState(VK_F2) & 1) g_teamCheck = !g_teamCheck;

        DWORD code = 0;
        if (!GetExitCodeProcess(g_proc, &code) || code != STILL_ACTIVE) break;

        drawFrame(mdc);
        BitBlt(wdc, 0, 0, g_w, g_h, mdc, 0, 0, SRCCOPY);
        Sleep(5);
    }
done:
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(hwnd, wdc);
    DestroyWindow(hwnd);
    CloseHandle(g_proc);
    return 0;
}
