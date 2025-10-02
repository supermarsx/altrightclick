#pragma once

namespace arc {

// Runs the Windows message loop until the given virtual-key is pressed (e.g., VK_ESCAPE).
int run_message_loop(unsigned int exit_vk = 0x1B);

}  // namespace arc
