#include "app/term_logs.hpp"

#include <algorithm>
#include <system_error>

namespace sc::app {
namespace {

constexpr std::size_t kMaxTermLength = 64;
constexpr std::string_view kSuffix = ".tsv";

bool is_term_character(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '.' || character == '_' ||
           character == '-';
}

}  // namespace

bool valid_term(std::string_view term) {
    if (term.empty() || term.size() > kMaxTermLength || term == "." || term == "..") {
        return false;
    }
    for (const char character : term) {
        if (!is_term_character(character)) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> list_terms(const std::filesystem::path& directory) {
    std::vector<std::string> terms;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        std::error_code type_error;
        if (!entry.is_regular_file(type_error) || type_error) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.size() <= kSuffix.size() ||
            name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
            continue;
        }
        const std::string term = name.substr(0, name.size() - kSuffix.size());
        if (valid_term(term)) {
            terms.push_back(term);
        }
    }
    // A directory is not ordered, and the switcher should not move under a
    // reader reloading the page.
    std::sort(terms.begin(), terms.end());
    return terms;
}

std::filesystem::path term_path(const std::filesystem::path& directory,
                                std::string_view term) {
    return directory / (std::string(term) + std::string(kSuffix));
}

}  // namespace sc::app
