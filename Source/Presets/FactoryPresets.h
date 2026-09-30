#pragma once

#include "PresetState.h"

namespace nedd
{
struct FactoryPreset
{
    const char* name;
    const char* category;
    const char* description;
    std::function<void (PresetState&)> build;
};

/** The built-in sound library. Always available, independent of files on disk. */
const std::vector<FactoryPreset>& getFactoryPresets();

/** Builds a complete preset state from a factory recipe. */
PresetState makeFactoryPreset (int index);

} // namespace nedd
