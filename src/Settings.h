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
#include <mutex>

namespace Settings {

    struct Config {
        bool  enabled      = true;
        float x            = 40.0f;    // top-left of the bar, screen px
        float y            = 980.0f;
        float width        = 320.0f;   // bar size, screen px
        float height       = 24.0f;
        float alpha        = 1.0f;     // whole-widget opacity
        bool  showText     = true;     // "37 / 100" centred on the bar
        bool  showLevel    = true;     // "Lv 3" + XP progress strip under the bar
        float textSizePx   = 18.0f;
        float xpBarHeight  = 5.0f;
        bool  useTextures  = true;     // use Interface/HUDWidgets/lilith/*.dds when present
        // 0 = auto (energy0..8.dds stage frames if present, else bar_fill crop, else flat),
        // 1 = force crop (bar_bg/bar_fill/bar_frame), 2 = force stage frames.
        int   fillMode     = 0;
        // With textures: bar height follows the image's aspect ratio at the chosen width
        // (an ornate 1024x250 frame is not squashed into a 320x24 strip). Off = width/height as set.
        bool  keepAspect   = true;
        // Multiply-tint stage frames by drain state (lilac idle / pink draining / red drain to
        // death). On by default (users want the colour change); off keeps a frame's own colours.
        bool  tintStages   = true;
        // How often the Papyrus values are re-read (ms). Reads are cheap and run
        // on the render thread, but energy only changes a few times a minute.
        int   readCadenceMs = 500;
        // DirectInput scan code for the manual hide-toggle key. 14 = Backspace.
        int   hideHotkeyDX = 14;
        // Mirror the vanilla compass _alpha (iHUD, Sandbox When Idle, ...).
        bool  followCompassHide = true;
    };

    std::unique_lock<std::mutex> Lock();
    Config& Get();

    void Load();
    void Save();

    void MarkDirty();
    bool TakeDirty();
}
