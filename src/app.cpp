#include "arc/app.h"

#include <windows.h>
#include "arc/config.h"

namespace arc {

int run_message_loop(unsigned int exit_vk) {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (exit_vk && (GetAsyncKeyState(static_cast<int>(exit_vk)) & 0x8000)) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

}  // namespace arc
