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
