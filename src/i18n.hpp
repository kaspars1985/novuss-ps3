#pragma once

#include <map>
#include <string>
#include <vector>

namespace Novuss {
namespace I18n {

// Look up `key` in the given language ("en" or "lv"), falling back to English,
// then to the key itself. Replaces {n}, {w}, {name}, {list}, ... placeholders
// with values from `params` (matching t() in reference/main.js).
std::string tr(const std::string& lang,
               const std::string& key,
               const std::map<std::string, std::string>& params = {});

// Structured rules sections for the Rules screen.
struct RulesSection {
    std::string heading;
    std::string body;
};

// Returns the ordered list of rules sections for the given language.
const std::vector<RulesSection>& rules(const std::string& lang);

} // namespace I18n
} // namespace Novuss
