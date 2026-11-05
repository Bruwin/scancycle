#pragma once
// One grade log per class or term, kept as <term>.tsv in one directory. A term
// name arrives in a URL, so it is validated against a filename-safe alphabet and
// never joined into a path unchecked; a term that does not exist reads as an
// empty log, the same answer as a term with no attempts yet.
#include "app/grade_log.hpp"

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

// The classroom's terms and the one new attempts go to. Kept as an object rather
// than a value so a teacher can switch the active term while the portal runs,
// which is what starting a new class needs. The list lives in memory; a file
// added by another process appears on the next restart.
class TermLogs {
public:
    // Lists the directory and keeps `active` in the list even before its file
    // exists, since that is the term the next attempt will create.
    TermLogs(std::filesystem::path directory, std::string active);

    const std::string& active() const { return active_; }
    const std::vector<std::string>& terms() const { return terms_; }

    // Switches where new attempts go. A name that cannot be a term changes
    // nothing; a new but valid one joins the list, so no restart is needed.
    bool set_active(std::string_view term);

    // The log a view reads: an invalid name falls back to the active term, while
    // a valid unknown one is simply an empty file.
    GradeLog log(std::string_view requested) const;
    std::string resolve(std::string_view requested) const;

private:
    std::filesystem::path directory_;
    std::string active_;
    std::vector<std::string> terms_;
};

}  // namespace sc::app
