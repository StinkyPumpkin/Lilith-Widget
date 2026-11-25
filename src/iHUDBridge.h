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

#pragma once

// Bridge for receiving messages from iHUDClaude.dll over SKSE messaging.
// iHUDClaude's "Universal Hide" feature dispatches to this listener so we
// hide our own widgets in coordination with the rest of the user's HUD.
namespace iHUDBridge {

    // Message types, must stay in sync with iHUDClaude's sender-side enum.
    enum Message : uint32_t {
        kHideAll                 = 1,  // hide all our widgets until RestoreAll
        kRestoreAll              = 2,  // restore visibility
        kRespectArousalThreshold = 3,  // payload: float; only hide if current arousal < threshold
    };

    // Register the SKSE messaging listener for sender "iHUDClaude".
    // Call from kPostLoad.
    void Register();

    // True iff iHUDClaude has sent kHideAll and not yet sent kRestoreAll
    // (modulated by kRespectArousalThreshold if applicable).
    bool IsHiddenByExternal();
}
