// Presets.h — factory chains: which modules, in what order, with which
// settings. Anything not listed keeps its default. The plugin adds user
// presets saved as files next to the settings file.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace planish
{
struct FactoryPreset
{
    const char* name;
    const char* note;                                   // one line: what it is for
    std::vector<int> order;                             // ModuleIds, signal order
    std::vector<std::pair<const char*, float>> values;  // parameter id, plain value
};
const std::vector<FactoryPreset>& factoryPresets();

// Module presets: settings for one module.
struct ModulePreset
{
    int module;
    const char* name;
    const char* note;
    std::vector<std::pair<const char*, float>> values;
};
const std::vector<ModulePreset>& factoryModulePresets();
} // namespace planish
