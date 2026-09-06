#pragma once

namespace HotkeyHandler {
    // Register a keyboard input listener with SKSE Menu Framework.
    // The listener watches for the configured hide-toggle key and flips
    // Visibility's manual-hide state.
    void Register();
}
