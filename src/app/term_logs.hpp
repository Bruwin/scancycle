#pragma once
// One grade log per class or term, kept as <term>.tsv in one directory. A term
// name arrives in a URL, so it is validated against a filename-safe alphabet and
// never joined into a path unchecked; a term that does not exist reads as an
// empty log, the same answer as a term with no attempts yet.
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sc::app {

// Letters, digits, '.', '_' and '-', at most 64 of them. This is what keeps
// `?term=../../secrets` from naming a file outside the directory.
bool valid_term(std::string_view term);

// Every <term>.tsv in `directory`, sorted, skipping names that cannot be terms.
// A missing directory is no terms, not an error: the first attempt creates it.
std::vector<std::string> list_terms(const std::filesystem::path& directory);

// The log path for a validated term. Callers check valid_term first.
std::filesystem::path term_path(const std::filesystem::path& directory,
                                std::string_view term);

}  // namespace sc::app
