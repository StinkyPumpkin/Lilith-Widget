// Lilith Widget - SKSE energy bar for Children of Lilith
// Copyright (C) 2026 StinkyPumpkin
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program. If
// not, see <https://www.gnu.org/licenses/>.

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
