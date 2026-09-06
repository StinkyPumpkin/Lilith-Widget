#pragma once

namespace Visibility {
    // Should the HUD widgets paint this frame?
    // Hides during: blocking menus, paused state, auto-vanity camera, manual
    // hide toggle (hotkey).
    bool ShouldRender();

    // Toggle the manual-hide state (called by the hotkey handler).
    void ToggleManualHide();
    // Read current manual-hide state.
    bool IsManuallyHidden();
}
