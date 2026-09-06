#include "SettingsUI.h"
#include "Settings.h"

#include "SKSEMenuFramework.h"

namespace {
    void __stdcall RenderLayout() {
        Settings::Config cfg;
        { auto lk = Settings::Lock(); cfg = Settings::Get(); }
        bool dirty = false;
        if (ImGuiMCP::Checkbox("Enabled", &cfg.enabled)) dirty = true;
        if (ImGuiMCP::SliderFloat("X", &cfg.x, 0.0f, 3840.0f, "%.0f")) dirty = true;
        if (ImGuiMCP::SliderFloat("Y", &cfg.y, 0.0f, 2160.0f, "%.0f")) dirty = true;
        if (ImGuiMCP::SliderFloat("Bar width (px)", &cfg.width, 60.0f, 1200.0f, "%.0f")) dirty = true;
        if (ImGuiMCP::SliderFloat("Bar height (px)", &cfg.height, 6.0f, 200.0f, "%.0f")) dirty = true;
        if (ImGuiMCP::SliderFloat("Opacity", &cfg.alpha, 0.05f, 1.0f, "%.2f")) dirty = true;
        if (ImGuiMCP::Checkbox("Show energy text", &cfg.showText)) dirty = true;
        if (ImGuiMCP::Checkbox("Show level + XP strip", &cfg.showLevel)) dirty = true;
        if (ImGuiMCP::SliderFloat("Text size (px)", &cfg.textSizePx, 8.0f, 96.0f, "%.0f")) dirty = true;
        if (ImGuiMCP::SliderFloat("XP strip height (px)", &cfg.xpBarHeight, 1.0f, 40.0f, "%.0f")) dirty = true;
        if (ImGuiMCP::Checkbox("Use DDS textures when present", &cfg.useTextures)) dirty = true;
        ImGuiMCP::Text("Textures: Data/Interface/HUDWidgets/lilith/bar_bg.dds, bar_fill.dds, bar_frame.dds, icon.dds");
        if (dirty) Settings::MarkDirty();
        { auto lk = Settings::Lock(); Settings::Get() = cfg; }
    }

    void __stdcall RenderVisibility() {
        Settings::Config cfg;
        { auto lk = Settings::Lock(); cfg = Settings::Get(); }
        bool dirty = false;
        ImGuiMCP::Text("Hide-toggle hotkey (DirectInput scan code)");
        ImGuiMCP::Text("Common: 14=Backspace, 15=Tab, 28=Enter, 57=Space, 0=unbound");
        if (ImGuiMCP::SliderInt("Scan code##hide", &cfg.hideHotkeyDX, 0, 220)) dirty = true;
        ImGuiMCP::Text("");
        if (ImGuiMCP::Checkbox("Follow compass hide (iHUD / Sandbox When Idle / etc.)", &cfg.followCompassHide)) dirty = true;
        if (ImGuiMCP::SliderInt("Read cadence (ms)", &cfg.readCadenceMs, 100, 5000)) dirty = true;
        if (dirty) Settings::MarkDirty();
        { auto lk = Settings::Lock(); Settings::Get() = cfg; }
    }
}

namespace SettingsUI {
    void Register() {
        const bool installed = SKSEMenuFramework::IsInstalled();
        const HMODULE handle = ::GetModuleHandleW(L"SKSEMenuFramework");
        SKSE::log::info("SettingsUI::Register - IsInstalled={} GetModuleHandle={}", installed, static_cast<void*>(handle));
        if (!installed || !handle) {
            SKSE::log::error("SKSE Menu Framework not loadable - settings menu unavailable");
            return;
        }
        SKSEMenuFramework::SetSection("Lilith Widget");
        SKSEMenuFramework::AddSectionItem("Layout",     RenderLayout);
        SKSEMenuFramework::AddSectionItem("Visibility", RenderVisibility);
        SKSE::log::info("SettingsUI: registered 'Lilith Widget' section in MCP");
    }
}
