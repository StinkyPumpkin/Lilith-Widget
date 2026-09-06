#pragma once
#include <string>

namespace JsonStore {
    std::string Load();
    bool        Save(const std::string& json);
    std::string GetStatePath(); // Data/SKSE/Plugins/HUDWidgets/config.json
}
