#include "HudUI.h"

#include "CoLReader.h"
#include "Settings.h"
#include "Visibility.h"

#include "SKSEMenuFramework.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <unordered_map>

#include <d3d11.h>
#include <string>

// Energy bar for Children of Lilith, drawn with the ImGui draw list so it needs no
// art to work. If the user drops DDS files in Data/Interface/HUDWidgets/lilith/ they
// replace the flat shapes:
//   bar_bg.dds     full bar background (drawn at bar size)
//   bar_fill.dds   fill, cropped left->right by energy percent + tinted by drain state
//   bar_frame.dds  drawn over the fill at bar size
//   icon.dds       square icon left of the bar
// Stage mode (like the arousal widget's aroused0..8 set): energy0.dds .. energy8.dds are
// complete bar images, one per level; the level comes from energy percent and the frame
// is drawn at bar size, tinted by drain state. Chosen automatically when all 9 exist
// (settings fillMode 0), or forced with fillMode 2.
namespace {
    namespace DL = ImGuiMCP::ImDrawListManager;
    using ImGuiMCP::ImVec2;
    using ImGuiMCP::ImU32;

    ImGuiMCP::ImTextureID g_texBg = nullptr, g_texFill = nullptr, g_texFrame = nullptr, g_texIcon = nullptr;
    constexpr int kStages = 9;
    std::array<ImGuiMCP::ImTextureID, kStages> g_stage{};
    bool g_stagesComplete = false;   // all 9 stage frames loaded
    bool g_registered = false;

    // Native pixel size of each loaded texture (ImTextureID is an ID3D11ShaderResourceView*
    // under SKSE Menu Framework's DX11 backend). Used to keep the bar at the art's aspect.
    std::unordered_map<void*, ImVec2> g_texSize;

    ImVec2 QueryTexSize(ImGuiMCP::ImTextureID id) {
        ImVec2 out{ 0, 0 };
        auto* srv = static_cast<ID3D11ShaderResourceView*>(id);
        if (!srv) return out;
        ID3D11Resource* res = nullptr;
        srv->GetResource(&res);
        if (!res) return out;
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) && tex) {
            D3D11_TEXTURE2D_DESC d{};
            tex->GetDesc(&d);
            out = { static_cast<float>(d.Width), static_cast<float>(d.Height) };
            tex->Release();
        }
        res->Release();
        return out;
    }

    // Aspect (h/w) of a texture, or 0 if unknown.
    float TexAspect(ImGuiMCP::ImTextureID id) {
        auto it = g_texSize.find(id);
        if (it == g_texSize.end() || it->second.x <= 0.0f) return 0.0f;
        return it->second.y / it->second.x;
    }

    // Same mapping as the arousal widget: 0..99 -> 0..8 in 12-point steps, 100 -> 8.
    int StageFromPercent(float pct) {
        const int v = static_cast<int>(pct * 100.0f + 0.5f);
        if (v <= 0) return 0;
        if (v >= 100) return kStages - 1;
        return std::min(v / 12, kStages - 1);
    }

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
        const bool  tex = cfg.useTextures;
        const float gap = 4.0f;

        // Which art draws the bar this frame: stage frame (one image per level) or crop set.
        bool stageMode = false;
        if (tex) {
            if (cfg.fillMode == 2)      stageMode = true;
            else if (cfg.fillMode == 0) stageMode = g_stagesComplete;
        }
        ImGuiMCP::ImTextureID stageTex = nullptr;
        if (stageMode) {
            stageTex = g_stage[StageFromPercent(pct)];
            if (!stageTex) stageMode = false;   // forced but frame missing -> fall back
        }

        // Bar height: from the art's aspect ratio at the chosen width (keepAspect), else as set.
        float h = std::max(cfg.height, 2.0f);
        if (tex && cfg.keepAspect) {
            const float asp = stageMode ? TexAspect(stageTex)
                            : (g_texBg ? TexAspect(g_texBg) : (g_texFill ? TexAspect(g_texFill) : 0.0f));
            if (asp > 0.0f) h = std::max(w * asp, 2.0f);
        }

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

        if (stageMode) {
            // Stage frames are finished paintings: tint only if asked (breathe still fades the alpha).
            const Rgb stageCol = cfg.tintStages ? pal.light : Rgb{ 255, 255, 255 };
            DL::AddImage(dl, stageTex, p0, p1, { 0, 0 }, { 1, 1 }, Col(stageCol, fillA));
        } else {
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
        }   // !stageMode

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

    // When the framework (DirectXTK's DDS loader) rejects a file that exists, say why in
    // terms a painter can act on. The common one: block-compressed (BC1/BC3/BC7...) textures
    // must have width AND height that are multiples of 4, or D3D11 refuses to create them.
    std::string ExplainDDS(const char* path) {
        std::error_code ec;
        const auto full = std::filesystem::current_path() / path;
        if (!std::filesystem::exists(full, ec)) return "file not found";
        std::ifstream f(full, std::ios::binary);
        char h[148] = {};
        f.read(h, sizeof(h));
        if (f.gcount() < 128 || std::memcmp(h, "DDS ", 4) != 0) return "exists but is not a DDS file";
        std::uint32_t height, width;
        std::memcpy(&height, h + 12, 4);
        std::memcpy(&width, h + 16, 4);
        char fourcc[5] = { h[84], h[85], h[86], h[87], 0 };
        std::uint32_t dxgi = 0;
        const bool dx10 = std::memcmp(fourcc, "DX10", 4) == 0;
        if (dx10 && f.gcount() >= 132) std::memcpy(&dxgi, h + 128, 4);
        const bool blockCompressed = dx10 ? (dxgi >= 70 && dxgi <= 99) /* BC1..BC7 */ : (fourcc[0] == 'D' && fourcc[1] == 'X' && fourcc[2] == 'T');
        std::string s = std::format("exists: {}x{}, {}{}", width, height,
                                    dx10 ? "DX10 header, DXGI format " : "FourCC ",
                                    dx10 ? std::to_string(dxgi) : std::string(fourcc[0] ? fourcc : "none (uncompressed)"));
        if (blockCompressed && ((width % 4) || (height % 4)))
            s += " - REJECTED: block-compressed textures need width and height that are multiples of 4 (resize, or export uncompressed B8G8R8A8)";
        else
            s += " - loader rejected it (try uncompressed B8G8R8A8 with mipmaps)";
        return s;
    }

    ImGuiMCP::ImTextureID TryLoad(const char* name) {
        char p[128];
        std::snprintf(p, sizeof(p), "Data/Interface/HUDWidgets/lilith/%s.dds", name);
        auto t = SKSEMenuFramework::LoadTexture(p);
        if (t) {
            const ImVec2 sz = QueryTexSize(t);
            g_texSize[t] = sz;
            SKSE::log::info("HudUI - texture {}: loaded ({}x{})", p, static_cast<int>(sz.x), static_cast<int>(sz.y));
        } else {
            SKSE::log::info("HudUI - texture {}: not loaded ({}); flat shapes used for this piece", p, ExplainDDS(p));
        }
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

        int stagesLoaded = 0;
        for (int i = 0; i < kStages; ++i) {
            char n[16];
            std::snprintf(n, sizeof(n), "energy%d", i);
            g_stage[i] = TryLoad(n);
            if (g_stage[i]) ++stagesLoaded;
        }
        g_stagesComplete = (stagesLoaded == kStages);
        SKSE::log::info("HudUI::Register - stage frames {}/{} -> stage mode {}", stagesLoaded, kStages,
                        g_stagesComplete ? "available" : "unavailable (auto uses crop/flat)");

        SKSEMenuFramework::AddHudElement(Render);
        g_registered = true;
        SKSE::log::info("HudUI::Register - HUD element registered");
    }
}
