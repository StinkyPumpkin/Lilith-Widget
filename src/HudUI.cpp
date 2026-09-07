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
#include <vector>
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

    // Art is indexed by drain state: 0 idle, 1 draining, 2 drain to death. State 0 is the base
    // set (energyN.dds / bar_*.dds); states 1 and 2 are optional "_drain" / "_death" variants.
    // A state with no art of its own falls back to the base art with the drain tint.
    constexpr int kStates = 3;
    constexpr const char* kStateSuffix[kStates] = { "", "_drain", "_death" };
    ImGuiMCP::ImTextureID g_texIcon = nullptr;
    std::array<ImGuiMCP::ImTextureID, kStates> g_texBg{}, g_texFill{}, g_texFrame{};
    constexpr int kStages = 9;
    std::array<std::array<ImGuiMCP::ImTextureID, kStages>, kStates> g_stage{};
    bool g_stagesComplete = false;   // all 9 base stage frames loaded
    bool g_registered = false;

    // Native pixel size of each loaded texture (ImTextureID is an ID3D11ShaderResourceView*
    // under SKSE Menu Framework's DX11 backend). Used to keep the bar at the art's aspect.
    std::unordered_map<void*, ImVec2> g_texSize;
    // uv of the art's bottom-right corner; (1,1) unless the texture was padded (see PadBlockCompressed).
    std::unordered_map<void*, ImVec2> g_texUv;

    ImVec2 UvMax(ImGuiMCP::ImTextureID id) {
        auto it = g_texUv.find(id);
        return it == g_texUv.end() ? ImVec2{ 1, 1 } : it->second;
    }

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
        // Per-state art: use the drain-state set when the painter supplied it (exact colours,
        // no tint); otherwise the base set, tinted.
        const int state = std::clamp(st.drainCode, 0, kStates - 1);
        auto pick = [&](const std::array<ImGuiMCP::ImTextureID, kStates>& set, bool& exact) {
            if (state > 0 && set[state]) { exact = true; return set[state]; }
            exact = false; return set[0];
        };
        ImGuiMCP::ImTextureID stageTex = nullptr;
        bool stageExact = false;
        if (stageMode) {
            const int idx = StageFromPercent(pct);
            if (state > 0 && g_stage[state][idx]) { stageTex = g_stage[state][idx]; stageExact = true; }
            else stageTex = g_stage[0][idx];
            if (!stageTex) stageMode = false;   // forced but frame missing -> fall back
        }
        bool bgExact = false, fillExact = false, frameExact = false;
        const auto texBg    = pick(g_texBg, bgExact);
        const auto texFill  = pick(g_texFill, fillExact);
        const auto texFrame = pick(g_texFrame, frameExact);

        // Bar height: from the art's aspect ratio at the chosen width (keepAspect), else as set.
        float h = std::max(cfg.height, 2.0f);
        if (tex && cfg.keepAspect) {
            const float asp = stageMode ? TexAspect(stageTex)
                            : (texBg ? TexAspect(texBg) : (texFill ? TexAspect(texFill) : 0.0f));
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
            DL::AddImage(dl, g_texIcon, { cx, iy }, { cx + iconSz, iy + iconSz }, { 0, 0 }, UvMax(g_texIcon), Col({255, 255, 255}, A));
            cx += iconSz + gap;
        }

        // Energy bar
        const ImVec2 p0{ cx, barTop };
        const ImVec2 p1{ cx + w, barTop + h };
        const Palette pal = PaletteFor(st.drainCode);
        float fillA = A;
        if (pct < 0.2f && st.drainCode != 2) fillA *= 0.55f + 0.45f * Pulse(1.6f);   // low-energy breathe
        const float rounding = std::min(h * 0.35f, 8.0f);

        const Rgb white{ 255, 255, 255 };
        if (stageMode) {
            // Exact per-state painting -> no tint. Base painting -> drain tint unless turned off.
            const Rgb stageCol = (stageExact || !cfg.tintStages) ? white : pal.light;
            DL::AddImage(dl, stageTex, p0, p1, { 0, 0 }, UvMax(stageTex), Col(stageCol, fillA));
        } else {
        if (tex && texBg) {
            DL::AddImage(dl, texBg, p0, p1, { 0, 0 }, UvMax(texBg), Col(white, A));
        } else {
            DL::AddRectFilled(dl, p0, p1, Col({0, 0, 0}, 0.60f * A), rounding, ImGuiMCP::ImDrawFlags_RoundCornersAll);
        }
        if (pct > 0.0f) {
            const ImVec2 f1{ p0.x + w * pct, p1.y };
            if (tex && texFill) {
                DL::AddImage(dl, texFill, p0, f1, { 0, 0 }, { pct * UvMax(texFill).x, UvMax(texFill).y }, Col(fillExact ? white : pal.light, fillA));
            } else {
                // Vertical gradient light -> dark, inset 1px so the frame reads.
                const ImVec2 i0{ p0.x + 1.0f, p0.y + 1.0f };
                const ImVec2 i1{ std::max(f1.x - 1.0f, i0.x + 1.0f), f1.y - 1.0f };
                DL::AddRectFilledMultiColor(dl, i0, i1,
                    Col(pal.light, fillA), Col(pal.light, fillA), Col(pal.dark, fillA), Col(pal.dark, fillA));
            }
        }
        if (tex && texFrame) {
            DL::AddImage(dl, texFrame, p0, p1, { 0, 0 }, UvMax(texFrame), Col(white, A));
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

    // D3D11 refuses a block-compressed (BC1..BC7) texture whose top mip is not a multiple of 4
    // wide and high, but the DDS file already stores whole 4x4 blocks (ceil(w/4) x ceil(h/4)).
    // So a 1024x250 BC7 export is byte-for-byte a valid 1024x252 texture: rewrite the header's
    // width/height to the padded size into a cache file and load that. The extra rows are the
    // encoder's edge padding; the draw crops them off with uvMax (see g_texUv).
    // Returns the cache path, or empty if the file needs no help.
    std::string PadBlockCompressed(const std::filesystem::path& full, const char* name, ImVec2& srcSize) {
        std::ifstream f(full, std::ios::binary);
        std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (buf.size() < 148 || std::memcmp(buf.data(), "DDS ", 4) != 0) return {};
        std::uint32_t height, width;
        std::memcpy(&height, buf.data() + 12, 4);
        std::memcpy(&width, buf.data() + 16, 4);
        const bool dx10 = std::memcmp(buf.data() + 84, "DX10", 4) == 0;
        std::uint32_t dxgi = 0;
        if (dx10) std::memcpy(&dxgi, buf.data() + 128, 4);
        const bool bc = dx10 ? (dxgi >= 70 && dxgi <= 99) : (buf[84] == 'D' && buf[85] == 'X' && buf[86] == 'T');
        if (!bc || (width % 4 == 0 && height % 4 == 0)) return {};
        const std::uint32_t pw = (width + 3) & ~3u, ph = (height + 3) & ~3u;
        std::memcpy(buf.data() + 12, &ph, 4);
        std::memcpy(buf.data() + 16, &pw, 4);
        // Only the top mip is guaranteed to be laid out as we assume; mip levels of a
        // non-multiple-of-4 chain are unreliable, so declare a single level.
        std::uint32_t one = 1;
        std::memcpy(buf.data() + 28, &one, 4);
        std::uint32_t flags; std::memcpy(&flags, buf.data() + 8, 4);
        flags &= ~0x20000u;   // DDSD_MIPMAPCOUNT
        std::memcpy(buf.data() + 8, &flags, 4);
        std::uint32_t caps; std::memcpy(&caps, buf.data() + 108, 4);
        caps &= ~0x400008u;   // DDSCAPS_MIPMAP | DDSCAPS_COMPLEX
        std::memcpy(buf.data() + 108, &caps, 4);
        std::error_code ec;
        const auto dir = std::filesystem::current_path() / "Data/SKSE/Plugins/LilithWidget/texcache";
        std::filesystem::create_directories(dir, ec);
        const std::string rel = std::format("Data/SKSE/Plugins/LilithWidget/texcache/{}.dds", name);
        std::ofstream o(std::filesystem::current_path() / rel, std::ios::binary | std::ios::trunc);
        if (!o) return {};
        o.write(buf.data(), static_cast<std::streamsize>(buf.size()));
        srcSize = { static_cast<float>(width), static_cast<float>(height) };
        SKSE::log::info("HudUI - texture {}: {}x{} block-compressed, padded to {}x{} via {}", name, width, height, pw, ph, rel);
        return rel;
    }

    ImGuiMCP::ImTextureID TryLoad(const char* name, bool optional = false) {
        char p[128];
        std::snprintf(p, sizeof(p), "Data/Interface/HUDWidgets/lilith/%s.dds", name);
        std::error_code ec;
        const auto full = std::filesystem::current_path() / p;
        if (optional && !std::filesystem::exists(full, ec)) return nullptr;
        ImVec2 srcSize{ 0, 0 };
        std::string padded;
        if (std::filesystem::exists(full, ec)) padded = PadBlockCompressed(full, name, srcSize);
        auto t = SKSEMenuFramework::LoadTexture(padded.empty() ? std::string(p) : padded);
        if (t) {
            const ImVec2 sz = QueryTexSize(t);
            if (srcSize.x > 0 && sz.x > 0) {
                g_texSize[t] = srcSize;
                g_texUv[t] = { srcSize.x / sz.x, srcSize.y / sz.y };
            } else {
                g_texSize[t] = sz;
            }
            SKSE::log::info("HudUI - texture {}: loaded ({}x{})", p, static_cast<int>(g_texSize[t].x), static_cast<int>(g_texSize[t].y));
        } else {
            SKSE::log::info("HudUI - texture {}: not loaded ({}); {}", p, ExplainDDS(p),
                            optional ? "base art + tint used for this state" : "flat shapes used for this piece");
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
        g_texIcon = TryLoad("icon");
        for (int s = 0; s < kStates; ++s) {
            char n[32];
            const bool optional = s > 0;   // _drain / _death variants are extras; silence "not loaded"
            std::snprintf(n, sizeof(n), "bar_bg%s", kStateSuffix[s]);    g_texBg[s]    = TryLoad(n, optional);
            std::snprintf(n, sizeof(n), "bar_fill%s", kStateSuffix[s]);  g_texFill[s]  = TryLoad(n, optional);
            std::snprintf(n, sizeof(n), "bar_frame%s", kStateSuffix[s]); g_texFrame[s] = TryLoad(n, optional);
            int loaded = 0;
            for (int i = 0; i < kStages; ++i) {
                std::snprintf(n, sizeof(n), "energy%d%s", i, kStateSuffix[s]);
                g_stage[s][i] = TryLoad(n, optional);
                if (g_stage[s][i]) ++loaded;
            }
            if (s == 0) {
                g_stagesComplete = (loaded == kStages);
                SKSE::log::info("HudUI::Register - stage frames {}/{} -> stage mode {}", loaded, kStages,
                                g_stagesComplete ? "available" : "unavailable (auto uses crop/flat)");
            } else {
                SKSE::log::info("HudUI::Register - '{}' art: stage frames {}/{}, crop fill {} (missing pieces use the base art + tint)",
                                kStateSuffix[s], loaded, kStages, g_texFill[s] ? "yes" : "no");
            }
        }

        SKSEMenuFramework::AddHudElement(Render);
        g_registered = true;
        SKSE::log::info("HudUI::Register - HUD element registered");
    }
}
