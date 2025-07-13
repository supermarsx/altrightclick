#include <windows.h>
#include <iostream>

HHOOK hMouseHook;

// Low-level mouse hook procedure
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        PMSLLHOOKSTRUCT pMouse = (PMSLLHOOKSTRUCT)lParam;

        // Check for left button down
        if (wParam == WM_LBUTTONDOWN) {
            // Is Alt pressed?
            if (GetAsyncKeyState(VK_MENU) & 0x8000) {
                // Suppress the original left click by returning 1
                // Simulate a right click
                INPUT input[2] = {};

                input[0].type = INPUT_MOUSE;
                input[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;

                input[1].type = INPUT_MOUSE;
                input[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;

                SendInput(2, input, sizeof(INPUT));
                return 1; // Block the left click event
            }
        }
    }
    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}

int main() {
    // Set the low-level mouse hook
    hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
    if (!hMouseHook) {
        std::cerr << "Failed to install mouse hook!" << std::endl;
        return 1;
    }

    std::cout << "Alt + Left Click will now perform a Right Click. Press ESC to quit." << std::endl;

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        // Allow exit by pressing ESC
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hMouseHook);
    return 0;
}
