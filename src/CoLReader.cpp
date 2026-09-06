#include "CoLReader.h"

#include <RE/O/Object.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESQuest.h>
#include <RE/V/Variable.h>
#include <RE/V/VirtualMachine.h>

#include <chrono>
#include <mutex>

namespace {
    constexpr const char* kEsp = "ChildrenOfLilith.esp";

    // Local FormIDs in ChildrenOfLilith.esp 5.2.0 (verified from the plugin).
    constexpr RE::FormID kQuestPlayerSuccubus = 0x000D64;  // carries CoL_Mechanic_EnergyHandler_Script
    constexpr RE::FormID kQuestWidget         = 0x341362;  // carries CoL_UI_Widget_Script
    constexpr RE::FormID kGlobIsSuccubus      = 0x000D63;  // CoL_IsPlayerSuccubus
    constexpr RE::FormID kGlobLevel           = 0x13B614;  // CoL_playerSuccubusLevel
    constexpr RE::FormID kGlobLevelRatio      = 0x282A20;  // CoL_playerSuccubusLevelRatio

    constexpr const char* kEnergyScript = "CoL_Mechanic_EnergyHandler_Script";
    constexpr const char* kWidgetScript = "CoL_UI_Widget_Script";

    RE::TESQuest*  g_playerQuest = nullptr;
    RE::TESQuest*  g_widgetQuest = nullptr;
    RE::TESGlobal* g_isSuccubus  = nullptr;
    RE::TESGlobal* g_level       = nullptr;
    RE::TESGlobal* g_levelRatio  = nullptr;

    std::mutex       g_mutex;
    CoLReader::State g_state{};
    std::chrono::steady_clock::time_point g_next{};
    bool g_warnedNoScript = false;

    // Read a numeric script variable off the named script bound to a quest.
    bool ReadScriptNumber(RE::TESQuest* quest, const char* script, const char* var, float& out) {
        if (!quest) return false;
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return false;
        auto* policy = vm->GetObjectHandlePolicy();
        if (!policy) return false;

        const auto typeID = static_cast<RE::VMTypeID>(RE::FormType::Quest);
        const auto handle = policy->GetHandleForObject(typeID, quest);
        if (handle == policy->EmptyHandle()) return false;

        RE::BSTSmartPointer<RE::BSScript::Object> obj;
        if (!vm->FindBoundObject(handle, script, obj) || !obj) return false;

        auto* v = obj->GetVariable(RE::BSFixedString(var));
        if (!v) return false;
        if (v->IsFloat()) { out = v->GetFloat(); return true; }
        if (v->IsInt())   { out = static_cast<float>(v->GetSInt()); return true; }
        return false;
    }
}

namespace CoLReader {

    void Detect() {
        auto* dh = RE::TESDataHandler::GetSingleton();
        State st{};
        if (dh && dh->LookupModByName(kEsp)) {
            g_playerQuest = dh->LookupForm<RE::TESQuest>(kQuestPlayerSuccubus, kEsp);
            g_widgetQuest = dh->LookupForm<RE::TESQuest>(kQuestWidget, kEsp);
            g_isSuccubus  = dh->LookupForm<RE::TESGlobal>(kGlobIsSuccubus, kEsp);
            g_level       = dh->LookupForm<RE::TESGlobal>(kGlobLevel, kEsp);
            g_levelRatio  = dh->LookupForm<RE::TESGlobal>(kGlobLevelRatio, kEsp);
            st.available  = g_playerQuest && g_isSuccubus && g_level && g_levelRatio;
            SKSE::log::info("CoLReader::Detect - {} found; playerQuest={} widgetQuest={} globals={}/{}/{} -> available={}",
                            kEsp, static_cast<void*>(g_playerQuest), static_cast<void*>(g_widgetQuest),
                            static_cast<void*>(g_isSuccubus), static_cast<void*>(g_level),
                            static_cast<void*>(g_levelRatio), st.available);
        } else {
            SKSE::log::info("CoLReader::Detect - {} not in load order; widget idle", kEsp);
        }
        std::lock_guard lk(g_mutex);
        g_state = st;
        g_next  = {};
    }

    void Invalidate() {
        std::lock_guard lk(g_mutex);
        g_next = {};
        g_warnedNoScript = false;
    }

    void Refresh(int cadenceMs) {
        using clock = std::chrono::steady_clock;
        const auto now = clock::now();
        {
            std::lock_guard lk(g_mutex);
            if (!g_state.available) return;
            if (now < g_next) return;
            g_next = now + std::chrono::milliseconds(cadenceMs < 100 ? 100 : cadenceMs);
        }

        State st{};
        st.available  = true;
        st.succubus   = g_isSuccubus && static_cast<int>(g_isSuccubus->value) == 1;
        st.level      = g_level ? static_cast<int>(g_level->value) : 0;
        st.levelRatio = g_levelRatio ? g_levelRatio->value : 0.0f;
        if (st.levelRatio < 0.0f) st.levelRatio = 0.0f;
        if (st.levelRatio > 1.0f) st.levelRatio = 1.0f;

        float cur = 0.0f, mx = 0.0f;
        const bool okCur = ReadScriptNumber(g_playerQuest, kEnergyScript, "playerEnergyCurrent_var", cur);
        const bool okMax = ReadScriptNumber(g_playerQuest, kEnergyScript, "playerEnergyMax_var", mx);
        st.energyOk = okCur && okMax;
        if (st.energyOk) {
            st.energy    = cur;
            st.energyMax = mx > 0.0f ? mx : 100.0f;
            if (st.energy < 0.0f) st.energy = 0.0f;
            if (st.energy > st.energyMax) st.energy = st.energyMax;
        } else if (st.succubus && !g_warnedNoScript) {
            g_warnedNoScript = true;
            SKSE::log::warn("CoLReader - could not read {} variables on quest {:08X} (cur={} max={}); "
                            "bar hidden until the script is bound", kEnergyScript,
                            g_playerQuest ? g_playerQuest->GetFormID() : 0, okCur, okMax);
        }

        float dc = 0.0f;
        if (ReadScriptNumber(g_widgetQuest, kWidgetScript, "drainCode", dc)) {
            st.drainCode = static_cast<int>(dc);
            if (st.drainCode < 0 || st.drainCode > 2) st.drainCode = 0;
        }

        std::lock_guard lk(g_mutex);
        g_state = st;
    }

    State Get() {
        std::lock_guard lk(g_mutex);
        return g_state;
    }
}
