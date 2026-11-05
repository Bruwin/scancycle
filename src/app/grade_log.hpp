#pragma once
// Where the portal keeps a graded attempt. One line per submission: a classroom
// log has to survive a crash mid-write and has to open in a spreadsheet, so the
// fields are percent-encoded and a program full of tabs and newlines still fits
// on one line.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sc::app {

struct GradeRecord {
    std::int64_t at = 0;  // unix seconds, supplied by the caller
    std::string lesson_id;
    bool passed = false;
    std::size_t passed_count = 0;
    std::size_t total = 0;
    std::string program;  // what was submitted; empty means the reference
};

// An append-only log. Appending opens the file per record, so two portal
// processes pointed at one path interleave whole lines instead of corrupting
// each other's buffer.
class GradeLog {
public:
    explicit GradeLog(std::string path);

    bool append(const GradeRecord& record, std::string& error) const;

    // Oldest first, the order the file is written in. A missing file is an empty
    // log rather than an error: on the first day there is no history.
    std::vector<GradeRecord> entries(std::string& error) const;

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// One line without the trailing newline, exposed so a test can prove the format
// round-trips.
std::string format_grade_record(const GradeRecord& record);
bool parse_grade_record(std::string_view line, GradeRecord& out);

}  // namespace sc::app
