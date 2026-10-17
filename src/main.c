#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* Infrastructure diagnostic only. No original game logic or graphics. */
static uint32_t pixels[144][160];
static void draw_diagnostic(void) {
    const uint32_t shades[] = {0xe0f8d0, 0x88c070, 0x346856, 0x081820};
    for (int y = 0; y < 144; ++y)
        for (int x = 0; x < 160; ++x)
            pixels[y][x] = shades[(x / 40 + y / 36) % 4];
}
static LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM a, LPARAM b) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (m == WM_KEYDOWN && a == VK_ESCAPE) { DestroyWindow(w); return 0; }
    if (m == WM_PAINT) {
        PAINTSTRUCT paint; HDC dc = BeginPaint(w, &paint);
        RECT rect; GetClientRect(w, &rect);
        int width = rect.right, height = rect.bottom;
        int scale = width / 160 < height / 144 ? width / 160 : height / 144;
        FillRect(dc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (scale > 0) {
            BITMAPINFO info = {0};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = 160; info.bmiHeader.biHeight = -144;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc, (width-160*scale)/2, (height-144*scale)/2,
                160*scale, 144*scale, 0, 0, 160, 144, pixels, &info,
                DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(w, &paint); return 0;
    }
    return DefWindowProcW(w, m, a, b);
}
int main(int argc, char **argv) {
    draw_diagnostic();
    if (argc == 2 && strcmp(argv[1], "--smoke-test") == 0) {
        if (pixels[0][0] != 0xe0f8d0 || pixels[0][40] != 0x88c070 ||
            pixels[143][159] != 0x346856) return 1;
        puts("PASS: native executable and diagnostic framebuffer (not gameplay)");
        return 0;
    }
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW cls = {0}; cls.lpfnWndProc = window_proc;
    cls.style = CS_HREDRAW | CS_VREDRAW;
    cls.hInstance = instance; cls.lpszClassName = L"AlleywayResearch";
    cls.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassW(&cls)) return 2;
    RECT r = {0, 0, 640, 576}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowW(cls.lpszClassName,
        L"Alleyway PC research | diagnostic only | Esc to close",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right-r.left, r.bottom-r.top, NULL, NULL, instance, NULL);
    if (!window) return 3;
    MSG msg; int result;
    while ((result = GetMessageW(&msg, NULL, 0, 0)) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return result < 0 ? 4 : 0;
}
