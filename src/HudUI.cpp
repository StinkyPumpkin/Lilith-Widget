#include "HudUI.h"

#include "CoLReader.h"
#include "Settings.h"
#include "Visibility.h"

#include "SKSEMenuFramework.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

// Energy bar for Children of Lilith, drawn with the ImGui draw list so it needs no
// art to work. If the user drops DDS files in Data/Interface/HUDWidgets/lilith/ they
// replace the flat shapes:
//   bar_bg.dds     full bar background (drawn at bar size)
//   bar_fill.dds   fill, cropped left->right by energy percent + tinted by drain state
//   bar_frame.dds  drawn over the fill at bar size
//   icon.dds       square icon left of the bar
namespace {
    namespace DL = ImGuiMCP::ImDrawListManager;
    using ImGuiMCP::ImVec2;
    using ImGuiMCP::ImU32;

    ImGuiMCP::ImTextureID g_texBg = nullptr, g_texFill = nullptr, g_texFrame = nullptr, g_texIcon = nullptr;
    bool g_registered = false;

    constexpr int OverlayFlags =
        ImGuiMCP::ImGuiWindowFlags_NoTitleBar
        | ImGuiMCP::ImGuiWindowFlags_NoResize
        | ImGuiMCP::ImGuiWindowFlags_NoMove
        | ImGuiMCP::ImGuiWindowFlags_NoScrollbar
        | ImGuiMCP::ImGuiWindowFlags_NoCollapse
        | ImGuiMCP::ImGuiWindowFlags_NoBackground
        | ImGuiMCP::ImGuiWindowFlags_NoSavedSettings
        | ImGuiMCP::ImGuiWindowFlags_NoInputs
        | ImGuiMCP::ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiMCP::ImGuiWindowFlags_NoNav
        | ImGuiMCP::ImGuiWindowFlags_NoDocking
        | ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize;

    struct Rgb { int r, g, b; };
    struct Palette { Rgb light, dark; };

    // Drain-state palettes. 1 and 2 are the exact gradients CoL's own meter used;
    // 0 (not draining) is a succubus lilac instead of CoL's plain white.
    Palette PaletteFor(int drainCode) {
        switch (drainCode) {
        case 1:  return { {255, 207, 242}, {255, 157, 227} };   // draining: pink
        case 2:  return { {255, 102, 102}, {255,  51,  51} };   // drain to death: red
        default: return { {215, 170, 255}, {160,  80, 230} };   // idle: lilac
        }
    }

    ImU32 Col(Rgb c, float a) {
        const int ai = static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
        return IM_COL32(c.r, c.g, c.b, ai);
    }

    // Slow pulse (0..1) used to breathe the fill when energy is low.
    float Pulse(float periodSec) {
        using clock = std::chrono::steady_clock;
        static const clock::time_point s0 = clock::now();
        const float t = std::chrono::duration<float>(clock::now() - s0).count();
        return 0.5f * (1.0f - std::cos(6.2831853f * t / periodSec));
    }

    ImVec2 TextSize(ImGuiMCP::ImFont* font, float px, const char* s) {
        return ImGuiMCP::ImFontManger::CalcTextSizeA(font, px, 3.402823466e+38F, 0.0f, s, nullptr, nullptr);
    }

    void __stdcall Render() {
        if (!Visibility::ShouldRender()) return;
        Settings::Config cfg;
        { auto lk = Settings::Lock(); cfg = Settings::Get(); }
        if (!cfg.enabled) return;

        CoLReader::Refresh(cfg.readCadenceMs);
        const auto st = CoLReader::Get();
        if (!st.available || !st.succubus || !st.energyOk) return;

        const float pct = st.energyMax > 0.0f ? std::clamp(st.energy / st.energyMax, 0.0f, 1.0f) : 0.0f;
        const float A   = std::clamp(cfg.alpha, 0.0f, 1.0f);
        const float w   = std::max(cfg.width, 8.0f);
        const float h   = std::max(cfg.height, 2.0f);
        const bool  tex = cfg.useTextures;
        const float gap = 4.0f;

        auto* font = ImGuiMCP::GetFont();

        // Layout: [icon] [bar / xp strip] [Lv N]
        const bool  hasIcon = tex && g_texIcon;
        const float iconSz  = hasIcon ? h * 1.5f : 0.0f;
        const float xpH     = cfg.showLevel ? std::max(cfg.xpBarHeight, 1.0f) : 0.0f;

        char lvl[32] = {};
        ImVec2 lvlSz{0, 0};
        if (cfg.showLevel) {
            std::snprintf(lvl, sizeof(lvl), "Lv %d", st.level);
            lvlSz = TextSize(font, cfg.textSizePx, lvl);
        }

        const float totalW = iconSz + (hasIcon ? gap : 0.0f) + w + (cfg.showLevel ? gap + lvlSz.x : 0.0f);
        const float barsH  = h + (cfg.showLevel ? gap + xpH : 0.0f);
        const float totalH = std::max(iconSz, barsH);

        ImGuiMCP::SetNextWindowPos({ cfg.x, cfg.y }, ImGuiMCP::ImGuiCond_Always, { 0, 0 });
        bool open = true;
        if (!ImGuiMCP::Begin("##lilithwidget", &open, OverlayFlags)) { ImGuiMCP::End(); return; }

        const ImVec2 origin = ImGuiMCP::GetCursorScreenPos();
        ImGuiMCP::Dummy({ totalW, totalH });
        auto* dl = ImGuiMCP::GetWindowDrawList();
        if (!dl) { ImGuiMCP::End(); return; }

        float cx = origin.x;
        const float barTop = origin.y + (totalH - barsH) * 0.5f;

        // Icon
        if (hasIcon) {
            const float iy = origin.y + (totalH - iconSz) * 0.5f;
            DL::AddImage(dl, g_texIcon, { cx, iy }, { cx + iconSz, iy + iconSz }, { 0, 0 }, { 1, 1 }, Col({255, 255, 255}, A));
            cx += iconSz + gap;
        }

        // Energy bar
        const ImVec2 p0{ cx, barTop };
        const ImVec2 p1{ cx + w, barTop + h };
        const Palette pal = PaletteFor(st.drainCode);
        float fillA = A;
        if (pct < 0.2f && st.drainCode != 2) fillA *= 0.55f + 0.45f * Pulse(1.6f);   // low-energy breathe
        const float rounding = std::min(h * 0.35f, 8.0f);

        if (tex && g_texBg) {
            DL::AddImage(dl, g_texBg, p0, p1, { 0, 0 }, { 1, 1 }, Col({255, 255, 255}, A));
        } else {
            DL::AddRectFilled(dl, p0, p1, Col({0, 0, 0}, 0.60f * A), rounding, ImGuiMCP::ImDrawFlags_RoundCornersAll);
        }
        if (pct > 0.0f) {
            const ImVec2 f1{ p0.x + w * pct, p1.y };
            if (tex && g_texFill) {
                DL::AddImage(dl, g_texFill, p0, f1, { 0, 0 }, { pct, 1 }, Col(pal.light, fillA));
            } else {
                // Vertical gradient light -> dark, inset 1px so the frame reads.
                const ImVec2 i0{ p0.x + 1.0f, p0.y + 1.0f };
                const ImVec2 i1{ std::max(f1.x - 1.0f, i0.x + 1.0f), f1.y - 1.0f };
                DL::AddRectFilledMultiColor(dl, i0, i1,
                    Col(pal.light, fillA), Col(pal.light, fillA), Col(pal.dark, fillA), Col(pal.dark, fillA));
            }
        }
        if (tex && g_texFrame) {
            DL::AddImage(dl, g_texFrame, p0, p1, { 0, 0 }, { 1, 1 }, Col({255, 255, 255}, A));
        } else {
            DL::AddRect(dl, p0, p1, Col({255, 255, 255}, 0.35f * A), rounding, ImGuiMCP::ImDrawFlags_RoundCornersAll, 1.0f);
        }

        // Energy text, centred on the bar with a soft shadow.
        if (cfg.showText) {
            char txt[48];
            std::snprintf(txt, sizeof(txt), "%d / %d",
                          static_cast<int>(st.energy + 0.5f), static_cast<int>(st.energyMax + 0.5f));
            const ImVec2 ts = TextSize(font, cfg.textSizePx, txt);
            const ImVec2 tp{ p0.x + (w - ts.x) * 0.5f, p0.y + (h - ts.y) * 0.5f };
            DL::AddText(dl, font, cfg.textSizePx, { tp.x + 1.0f, tp.y + 1.0f }, Col({0, 0, 0}, 0.8f * A), txt);
            DL::AddText(dl, font, cfg.textSizePx, tp, Col({255, 255, 255}, A), txt);
        }

        // Level: XP strip under the bar + "Lv N" to the right.
        if (cfg.showLevel) {
            const ImVec2 x0{ p0.x, p1.y + gap };
            const ImVec2 x1{ p1.x, x0.y + xpH };
            const float xr = std::min(xpH * 0.5f, 3.0f);
            DL::AddRectFilled(dl, x0, x1, Col({0, 0, 0}, 0.60f * A), xr, ImGuiMCP::ImDrawFlags_RoundCornersAll);
            if (st.levelRatio > 0.0f) {
                DL::AddRectFilled(dl, x0, { x0.x + (x1.x - x0.x) * st.levelRatio, x1.y },
                                  Col({255, 205, 90}, A), xr, ImGuiMCP::ImDrawFlags_RoundCornersAll);
            }
            const ImVec2 lp{ p1.x + gap, p0.y + (h - lvlSz.y) * 0.5f };
            DL::AddText(dl, font, cfg.textSizePx, { lp.x + 1.0f, lp.y + 1.0f }, Col({0, 0, 0}, 0.8f * A), lvl);
            DL::AddText(dl, font, cfg.textSizePx, lp, Col({255, 225, 160}, A), lvl);
        }

        ImGuiMCP::End();
    }

    ImGuiMCP::ImTextureID TryLoad(const char* name) {
        char p[128];
        std::snprintf(p, sizeof(p), "Data/Interface/HUDWidgets/lilith/%s.dds", name);
        auto t = SKSEMenuFramework::LoadTexture(p);
        SKSE::log::info("HudUI - texture {}: {}", p, t ? "loaded" : "not found (flat shapes used)");
        return t;
    }
}

namespace HudUI {

    void Register() {
        if (g_registered) return;
        const bool installed = SKSEMenuFramework::IsInstalled();
        const HMODULE handle = ::GetModuleHandleW(L"SKSEMenuFramework");
        SKSE::log::info("HudUI::Register - IsInstalled={} GetModuleHandle={}", installed, static_cast<void*>(handle));
        if (!installed || !handle) {
            SKSE::log::error("SKSE Menu Framework not loadable - HUD bar won't render");
            return;
        }
        g_texBg    = TryLoad("bar_bg");
        g_texFill  = TryLoad("bar_fill");
        g_texFrame = TryLoad("bar_frame");
        g_texIcon  = TryLoad("icon");

        SKSEMenuFramework::AddHudElement(Render);
        g_registered = true;
        SKSE::log::info("HudUI::Register - HUD element registered");
    }
}
