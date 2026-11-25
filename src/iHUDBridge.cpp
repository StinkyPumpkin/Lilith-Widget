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

#include "iHUDBridge.h"

#include "CoLReader.h"

#include <SKSE/SKSE.h>
#include <atomic>

namespace {
    // True when iHUDClaude has requested universal hide.
    std::atomic<bool>  g_pendingHide{false};
    // If > 0, only honor the hide if current arousal is BELOW this value
    // (i.e., the user wants the widget to remain visible at high arousal).
    // 0 disables threshold gating (always honor hide).
    std::atomic<float> g_arousalThreshold{0.0f};

    void __cdecl Callback(SKSE::MessagingInterface::Message* msg) {
        if (!msg) return;
        switch (msg->type) {
        case iHUDBridge::kHideAll:
            g_pendingHide.store(true, std::memory_order_relaxed);
            SKSE::log::info("iHUDBridge: HideAll from iHUDClaude");
            break;

        case iHUDBridge::kRestoreAll:
            g_pendingHide.store(false, std::memory_order_relaxed);
            g_arousalThreshold.store(0.0f, std::memory_order_relaxed);
            SKSE::log::info("iHUDBridge: RestoreAll from iHUDClaude");
            break;

        case iHUDBridge::kRespectArousalThreshold: {
            float threshold = 0.0f;
            if (msg->data && msg->dataLen >= sizeof(float)) {
                threshold = *static_cast<float*>(msg->data);
            }
            g_arousalThreshold.store(threshold, std::memory_order_relaxed);
            SKSE::log::info("iHUDBridge: RespectArousalThreshold {} from iHUDClaude", threshold);
            break;
        }

        default:
            SKSE::log::warn("iHUDBridge: unknown message type {}", msg->type);
            break;
        }
    }
}

namespace iHUDBridge {

    void Register() {
        auto* mi = SKSE::GetMessagingInterface();
        if (!mi) {
            SKSE::log::error("iHUDBridge: no SKSE messaging interface");
            return;
        }
        if (!mi->RegisterListener("iHUDClaude", Callback)) {
            SKSE::log::error("iHUDBridge: RegisterListener('iHUDClaude') failed");
            return;
        }
        // Also listen for TFCam (free camera HUD hide)
        mi->RegisterListener("TFCam", Callback);
        SKSE::log::info("iHUDBridge: listener registered for senders 'iHUDClaude' + 'TFCam'");
    }

    bool IsHiddenByExternal() {
        if (!g_pendingHide.load(std::memory_order_relaxed)) return false;

        // Threshold gating: if a threshold was set, only hide when current
        // arousal is below it. (User wants widget to stay visible at high
        // arousal even when iHUDClaude says "hide all".)
        const float threshold = g_arousalThreshold.load(std::memory_order_relaxed);
        if (threshold > 0.0f) {
            // For this widget the threshold is an ENERGY percent: stay visible
            // while energy is at/above it (mirrors the arousal semantics).
            const auto st = CoLReader::Get();
            if (st.available && st.energyMax > 0.0f &&
                (st.energy / st.energyMax) * 100.0f >= threshold) {
                return false;
            }
        }
        return true;
    }
}
