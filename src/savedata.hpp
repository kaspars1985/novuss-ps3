#pragma once

#include <string>

namespace Novuss {
namespace Savedata {

// Attempt to load settings from native PS3 savedata utility.
// Returns true if successfully loaded and populates outIni.
bool load(std::string& outIni);

// Save settings to native PS3 savedata utility.
// Returns true if successfully saved.
bool save(const std::string& iniContent);

} // namespace Savedata
} // namespace Novuss
