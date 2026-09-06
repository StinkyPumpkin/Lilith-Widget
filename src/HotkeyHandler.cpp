#include "HotkeyHandler.h"

#include "Settings.h"
#include "Visibility.h"

#include "SKSEMenuFramework.h"

#include <RE/B/ButtonEvent.h>
#include <RE/I/InputEvent.h>

namespace {
    bool __stdcall OnInput(RE::InputEvent* e) {
        if (!e) return false;

        // Step the linked list from this head and process button-down events.
        // We never consume input — game still receives the keypress so it
        // doesn't break anything else listening for the same key.
        for (auto* cur = e; cur; cur = cur->next) {
            auto* btn = cur->AsButtonEvent();
            if (!btn) continue;
            if (!btn->IsDown()) continue;
            if (btn->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;

            int hotkey = 0;
            { auto lk = Settings::Lock(); hotkey = Settings::Get().hideHotkeyDX; }
            if (hotkey <= 0) continue; // unbound

            if (static_cast<int>(btn->GetIDCode()) == hotkey) {
                Visibility::ToggleManualHide();
                SKSE::log::info("HotkeyHandler: hide toggled -> {}",
                                Visibility::IsManuallyHidden());
            }
        }
        return false;
    }
}

namespace HotkeyHandler {

    void Register() {
        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::error("Cannot register hide hotkey - framework not loadable");
            return;
        }
        SKSEMenuFramework::AddInputEvent(OnInput);
        SKSE::log::info("HotkeyHandler: input listener registered");
    }
}
