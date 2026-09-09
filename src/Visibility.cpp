#include "Visibility.h"
#include "Settings.h"
#include "iHUDBridge.h"

#include <RE/G/GFxMovieView.h>
#include <RE/G/GFxValue.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/U/UI.h>

#include <atomic>

namespace {
    std::atomic<bool> g_manuallyHidden{false};

    // Paths to MovieClip objects (NOT the _alpha property). We resolve the
    // object via GetVariable, then read alpha/visible via GetDisplayInfo.
    static constexpr const char* kCompassPaths[] = {
        "_root.HUDMovieBaseInstance.CompassShoutMeterHolder",
        "_root.HUDMovieBaseInstance.Compass",
        "_root.HUDMovieBaseInstance.HUDComponent_Compass",
        "_root.HUDMovieBaseInstance.SkyUI_HUDComponent_Compass",
        "_root.HUDMovieBaseInstance.compass_mc",
        "_root.CompassShoutMeterHolder",
        "_root.Compass",
        "HUDMovieBaseInstance.CompassShoutMeterHolder",
        "_root.HUDMovieBaseInstance",
    };

    int  g_resolvedPathIdx  = -1;
    bool g_lastCompassHidden = false;
    bool g_didFirstDump     = false;

    struct CompassRead {
        bool   hidden    = false;
        double alpha     = -1.0;
        bool   visible   = true;
        int    pathIdx   = -1;
    };

    // Try one path. Returns true if the variable resolved AND was a DisplayObject
    // we could call GetDisplayInfo on; out filled with alpha + visible.
    bool TryPath(RE::GFxMovieView* view, const char* path,
                  double& alphaOut, bool& visibleOut) {
        RE::GFxValue obj;
        if (!view->GetVariable(&obj, path)) return false;
        if (!obj.IsObject() && !obj.IsDisplayObject()) {
            // The path may have pointed to a number directly (the old _alpha
            // suffix). Accept that as a degraded read.
            if (obj.IsNumber()) {
                alphaOut   = obj.GetNumber();
                visibleOut = (alphaOut > 0.0);
                return true;
            }
            return false;
        }
        RE::GFxValue::DisplayInfo info;
        if (!obj.GetDisplayInfo(&info)) {
            // No display info but we DID resolve the variable — still partial success
            alphaOut   = -1.0;
            visibleOut = true;
            return false;
        }
        alphaOut   = info.GetAlpha();
        visibleOut = info.GetVisible();
        return true;
    }

    CompassRead ReadCompassAlpha() {
        CompassRead r;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return r;
        // CRITICAL: Skyrim's HUD menu is registered as "HUD Menu" (space).
        // Constant comes from RE::HUDMenu::MENU_NAME.
        auto view = ui->GetMovieView("HUD Menu");
        if (!view) {
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
                SKSE::log::warn("Visibility: GetMovieView(\"HUD Menu\") returned null");
            }
            return r;
        }

        // First-call dump: try EVERY path, log each result. Helps us find the
        // right path on this user's particular HUDMenu / SkyUI / Edge UI build.
        if (!g_didFirstDump) {
            g_didFirstDump = true;
            SKSE::log::info("Visibility: ===== first-time compass path dump =====");
            for (int i = 0; i < static_cast<int>(std::size(kCompassPaths)); ++i) {
                double a = -1.0;
                bool   v = true;
                bool ok  = TryPath(view.get(), kCompassPaths[i], a, v);
                SKSE::log::info("  [{}] {} -> resolved={} alpha={:.1f} visible={}",
                                i, kCompassPaths[i], ok, a, v);
            }
            SKSE::log::info("Visibility: ===== end dump =====");
        }

        for (int i = 0; i < static_cast<int>(std::size(kCompassPaths)); ++i) {
            double a = -1.0;
            bool   v = true;
            if (TryPath(view.get(), kCompassPaths[i], a, v)) {
                r.alpha   = a;
                r.visible = v;
                r.hidden  = (!v) || (a >= 0.0 && a < 5.0);
                r.pathIdx = i;
                return r;
            }
        }
        return r;
    }

    // v0.1.5 (same fix as Aroused Widget 0.3.7): the compass read used to run inside
    // ShouldRender(), i.e. inside SKSE Menu Framework's render callback (the D3D Present
    // hook). GetVariable() walks the HUD movie's ActionScript objects and AddRefs whatever
    // it finds; around load transitions the main thread is tearing down / rebuilding that
    // movie while Present keeps firing, so the AddRef landed on a freed object
    // (SkyrimSE.exe+10EA1D9 `inc [rax+0x20]` under HudManager::Render). Now the read runs
    // ONLY on the main thread (SKSE task, queued from the save loop every 250 ms) and the
    // render path reads these atomics.
    std::atomic<bool> g_compassHidden{false};
    std::atomic<bool> g_compassResolved{false};
    std::atomic<bool> g_pollQueued{false};

    void LogCompassChange(const CompassRead& cur) {
        if (cur.pathIdx != g_resolvedPathIdx) {
            if (cur.pathIdx >= 0) {
                SKSE::log::info("Visibility: compass path resolved -> [{}] '{}'",
                                cur.pathIdx, kCompassPaths[cur.pathIdx]);
            } else {
                SKSE::log::warn("Visibility: NO compass path resolved this tick");
            }
            g_resolvedPathIdx = cur.pathIdx;
        }
        if (cur.hidden != g_lastCompassHidden) {
            SKSE::log::info("Visibility: compass {} (alpha={:.1f} visible={})",
                            cur.hidden ? "hidden" : "visible", cur.alpha, cur.visible);
            g_lastCompassHidden = cur.hidden;
        }
    }
}

namespace Visibility {

    void ToggleManualHide() {
        g_manuallyHidden.store(!g_manuallyHidden.load(), std::memory_order_relaxed);
    }
    bool IsManuallyHidden() {
        return g_manuallyHidden.load(std::memory_order_relaxed);
    }

    bool ShouldRender() {
        // One-shot: log every reason we'd return early on the first call,
        // plus the actual followCompassHide value seen.
        static bool s_dumpedFlow = false;
        if (!s_dumpedFlow) {
            s_dumpedFlow = true;
            bool follow0 = true;
            { auto lk = Settings::Lock(); follow0 = Settings::Get().followCompassHide; }
            SKSE::log::info("Visibility::ShouldRender FIRST CALL - manualHidden={} followCompassHide={}",
                            g_manuallyHidden.load(), follow0);
        }

        if (g_manuallyHidden.load(std::memory_order_relaxed)) return false;

        // External hide via iHUDClaude's "Universal Hide" (Smart or Nuclear).
        // Modulated by RespectArousalThreshold if iHUDClaude sent one.
        if (iHUDBridge::IsHiddenByExternal()) return false;

        auto* ui = RE::UI::GetSingleton();
        if (!ui) return true;

        if (ui->GameIsPaused()) return false;

        // Follow the game's own "show menus" flag. This is what the `tm` console command
        // and po3's Photo Mode "Hide UI" flip (UI::ShowMenus); it hides every Scaleform
        // menu but not an ImGui overlay, so mirror it here.
        if (!ui->IsShowingMenus()) return false;

        // Load screens: the menu-name/pause checks below can miss transitions
        // (field report: widget visible during a loading screen). The player's
        // 3D is unloaded during every load — a reliable catch-all.
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded()) return false;

        // Verified against CommonLibSSE-NG MENU_NAME constants. Note: spaces
        // and capitalisation are wildly inconsistent in Bethesda's naming.
        static constexpr const char* kBlockMenus[] = {
            // v0.3.3: Fader/Mist/LoadWaitSpinner cover the load-adjacent transitions the
            // Loading Menu check alone misses (field report: widget visible during loads).
            "Fader Menu",       "Mist Menu",
            "LoadWaitSpinner",
            "Main Menu",        "Loading Menu",
            "Console",          "MessageBoxMenu",
            "Crafting Menu",    "BarterMenu",
            "ContainerMenu",    "GiftMenu",
            "InventoryMenu",    "MagicMenu",
            "MapMenu",          "FavoritesMenu",
            "Dialogue Menu",    "Journal Menu",
            "Book Menu",        "TweenMenu",
            "Tutorial Menu",    "RaceSex Menu",
            "Sleep/Wait Menu",
        };
        for (const char* name : kBlockMenus) {
            if (ui->IsMenuOpen(name)) return false;
        }

        auto* pc = RE::PlayerCamera::GetSingleton();
        if (pc) {
            const auto idx = static_cast<size_t>(RE::CameraStates::kAutoVanity);
            const auto& vanityState = pc->cameraStates[idx];
            const auto& cur = pc->currentState;
            if (cur && vanityState && cur.get() == vanityState.get()) return false;
        }

        bool follow = true;
        { auto lk = Settings::Lock(); follow = Settings::Get().followCompassHide; }
        if (follow) {
            // Main-thread poll result only - never touch Scaleform from the render path.
            if (g_compassResolved.load(std::memory_order_relaxed) &&
                g_compassHidden.load(std::memory_order_relaxed)) return false;
        }

        return true;
    }

    void QueueCompassPoll() {
        bool follow = true;
        { auto lk = Settings::Lock(); follow = Settings::Get().followCompassHide; }
        if (!follow) {
            g_compassResolved.store(false, std::memory_order_relaxed);
            g_compassHidden.store(false, std::memory_order_relaxed);
            return;
        }
        // One outstanding task at a time: during a load the main thread does not drain
        // SKSE tasks for seconds, and we must not pile up hundreds of polls behind it.
        if (g_pollQueued.exchange(true, std::memory_order_acq_rel)) return;
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) { g_pollQueued.store(false, std::memory_order_relaxed); return; }
        tasks->AddTask([]() {
            g_pollQueued.store(false, std::memory_order_relaxed);
            auto* ui = RE::UI::GetSingleton();
            auto* player = RE::PlayerCharacter::GetSingleton();
            // Nothing to read while the HUD movie may be mid-rebuild; keep the last state
            // but mark it unresolved so a stale "hidden" can't outlive a load.
            if (!ui || !player || !player->Is3DLoaded() ||
                ui->IsMenuOpen("Loading Menu") || ui->IsMenuOpen("Fader Menu") ||
                ui->IsMenuOpen("Main Menu") || !ui->IsMenuOpen("HUD Menu")) {
                g_compassResolved.store(false, std::memory_order_relaxed);
                return;
            }
            auto rd = ReadCompassAlpha();
            LogCompassChange(rd);
            g_compassHidden.store(rd.pathIdx >= 0 && rd.hidden, std::memory_order_relaxed);
            g_compassResolved.store(rd.pathIdx >= 0, std::memory_order_relaxed);
        });
    }
}
