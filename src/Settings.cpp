#include "Settings.h"
#include "JsonStore.h"

#include <SKSE/SKSE.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {
    std::mutex        g_mutex;
    Settings::Config  g_config{};
    std::atomic<bool> g_dirty{false};

    const char* B(bool v) { return v ? "true" : "false"; }

    std::string Serialize(const Settings::Config& c) {
        char buf[900];
        std::snprintf(buf, sizeof(buf),
            "{\n"
            "  \"enabled\": %s,\n  \"x\": %.2f,\n  \"y\": %.2f,\n"
            "  \"width\": %.2f,\n  \"height\": %.2f,\n  \"alpha\": %.3f,\n"
            "  \"showText\": %s,\n  \"showLevel\": %s,\n  \"textSizePx\": %.2f,\n"
            "  \"xpBarHeight\": %.2f,\n  \"useTextures\": %s,\n"
            "  \"readCadenceMs\": %d,\n  \"hideHotkeyDX\": %d,\n  \"followCompassHide\": %s\n"
            "}\n",
            B(c.enabled), c.x, c.y, c.width, c.height, c.alpha,
            B(c.showText), B(c.showLevel), c.textSizePx,
            c.xpBarHeight, B(c.useTextures),
            c.readCadenceMs, c.hideHotkeyDX, B(c.followCompassHide));
        return buf;
    }

    std::string FindValue(const std::string& json, const std::string& key) {
        std::string needle = "\"" + key + "\"";
        size_t p = json.find(needle);
        if (p == std::string::npos) return "";
        p += needle.size();
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == ':')) ++p;
        size_t end = p;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n') ++end;
        return json.substr(p, end - p);
    }

    void ReadF(const std::string& j, const char* k, float& out) {
        auto v = FindValue(j, k); if (!v.empty()) out = std::strtof(v.c_str(), nullptr);
    }
    void ReadI(const std::string& j, const char* k, int& out) {
        auto v = FindValue(j, k); if (!v.empty()) out = std::atoi(v.c_str());
    }
    void ReadB(const std::string& j, const char* k, bool& out) {
        auto v = FindValue(j, k); if (!v.empty()) out = (v.find("true") != std::string::npos);
    }
}

namespace Settings {

    std::unique_lock<std::mutex> Lock() { return std::unique_lock<std::mutex>(g_mutex); }
    Config& Get() { return g_config; }

    void Load() {
        std::string txt = JsonStore::Load();
        if (txt.empty()) {
            SKSE::log::info("Settings::Load - no config.json yet, using defaults");
            return;
        }
        auto lk = Lock();
        auto& c = g_config;
        ReadB(txt, "enabled", c.enabled);
        ReadF(txt, "x", c.x);               ReadF(txt, "y", c.y);
        ReadF(txt, "width", c.width);       ReadF(txt, "height", c.height);
        ReadF(txt, "alpha", c.alpha);
        ReadB(txt, "showText", c.showText); ReadB(txt, "showLevel", c.showLevel);
        ReadF(txt, "textSizePx", c.textSizePx);
        ReadF(txt, "xpBarHeight", c.xpBarHeight);
        ReadB(txt, "useTextures", c.useTextures);
        ReadI(txt, "readCadenceMs", c.readCadenceMs);
        ReadI(txt, "hideHotkeyDX", c.hideHotkeyDX);
        ReadB(txt, "followCompassHide", c.followCompassHide);
        if (c.readCadenceMs < 100) c.readCadenceMs = 100;
        SKSE::log::info("Settings::Load - applied config from disk");
    }

    void Save() {
        std::string txt;
        { auto lk = Lock(); txt = Serialize(g_config); }
        if (JsonStore::Save(txt)) {
            SKSE::log::info("Settings::Save - wrote config.json ({} bytes)", txt.size());
        }
    }

    void MarkDirty() { g_dirty.store(true, std::memory_order_relaxed); }
    bool TakeDirty() { return g_dirty.exchange(false, std::memory_order_relaxed); }
}
