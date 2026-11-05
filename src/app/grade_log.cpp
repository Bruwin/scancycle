#include "app/grade_log.hpp"

#include <fstream>
#include <string_view>
#include <utility>

namespace sc::app {
namespace {

constexpr char kFieldSeparator = '\t';

std::string encode_field(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        const bool plain = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                           (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' ||
                           byte == '-';
        if (plain) {
            out += character;
        } else {
            const char* hex = "0123456789ABCDEF";
            out += '%';
            out += hex[(byte >> 4) & 0xF];
            out += hex[byte & 0xF];
        }
    }
    return out;
}

int hex_digit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

std::string decode_field(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int high = i + 2 < text.size() && text[i] == '%' ? hex_digit(text[i + 1]) : -1;
        const int low = high >= 0 ? hex_digit(text[i + 2]) : -1;
        if (high >= 0 && low >= 0) {
            out += static_cast<char>(high * 16 + low);
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::vector<std::string_view> split_on(std::string_view line, char separator) {
    std::vector<std::string_view> fields;
    std::size_t pos = 0;
    while (true) {
        const std::size_t found = line.find(separator, pos);
        if (found == std::string_view::npos) {
            fields.push_back(line.substr(pos));
            return fields;
        }
        fields.push_back(line.substr(pos, found - pos));
        pos = found + 1;
    }
}

bool parse_unsigned(std::string_view text, std::size_t& out) {
    if (text.empty()) {
        return false;
    }
    std::size_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10 + static_cast<std::size_t>(character - '0');
    }
    out = value;
    return true;
}

// Each check is `passed,description,detail` with both texts percent-encoded,
// which is what stops a description holding a comma or a newline from splitting
// the entry apart.
bool parse_checks(std::string_view text, std::vector<GradeCheck>& out) {
    out.clear();
    if (text.empty()) {
        return true;
    }
    for (const std::string_view entry : split_on(text, ';')) {
        const std::vector<std::string_view> parts = split_on(entry, ',');
        if (parts.size() != 3) {
            return false;
        }
        std::size_t passed = 0;
        if (!parse_unsigned(parts[0], passed) || passed > 1) {
            return false;
        }
        GradeCheck check;
        check.passed = passed == 1;
        check.description = decode_field(parts[1]);
        check.detail = decode_field(parts[2]);
        out.push_back(std::move(check));
    }
    return true;
}

}  // namespace

std::string format_grade_record(const GradeRecord& record) {
    std::string line = std::to_string(record.at);
    line += kFieldSeparator;
    line += encode_field(record.lesson_id);
    line += kFieldSeparator;
    line += record.passed ? '1' : '0';
    line += kFieldSeparator;
    line += std::to_string(record.passed_count);
    line += kFieldSeparator;
    line += std::to_string(record.total);
    line += kFieldSeparator;
    line += encode_field(record.program);
    // The name is an eighth field, written after the checks so a row from before
    // names existed still parses. An unnamed attempt with checks stays at seven.
    if (!record.checks.empty() || !record.student.empty()) {
        line += kFieldSeparator;
        for (std::size_t i = 0; i < record.checks.size(); ++i) {
            if (i != 0) {
                line += ';';
            }
            const GradeCheck& check = record.checks[i];
            line += check.passed ? '1' : '0';
            line += ',';
            line += encode_field(check.description);
            line += ',';
            line += encode_field(check.detail);
        }
    }
    if (!record.student.empty()) {
        line += kFieldSeparator;
        line += encode_field(record.student);
    }
    return line;
}

bool parse_grade_record(std::string_view line, GradeRecord& out) {
    const std::vector<std::string_view> fields = split_on(line, kFieldSeparator);
    // Six fields is a record from before per-check detail was stored, and seven
    // from before a student name was. Both still open; they just cannot show
    // what they never kept.
    if (fields.size() < 6 || fields.size() > 8) {
        return false;
    }

    std::size_t at = 0;
    std::size_t passed = 0;
    std::size_t passed_count = 0;
    std::size_t total = 0;
    if (!parse_unsigned(fields[0], at) || !parse_unsigned(fields[2], passed) ||
        !parse_unsigned(fields[3], passed_count) || !parse_unsigned(fields[4], total)) {
        return false;
    }
    if (passed > 1) {
        return false;
    }

    out.at = static_cast<std::int64_t>(at);
    out.lesson_id = decode_field(fields[1]);
    out.passed = passed == 1;
    out.passed_count = passed_count;
    out.total = total;
    out.program = decode_field(fields[5]);
    out.checks.clear();
    if (fields.size() >= 7 && !parse_checks(fields[6], out.checks)) {
        return false;
    }
    out.student.clear();
    if (fields.size() == 8) {
        out.student = decode_field(fields[7]);
    }
    return true;
}

GradeLog::GradeLog(std::string path) : path_(std::move(path)) {}

bool GradeLog::append(const GradeRecord& record, std::string& error) const {
    error.clear();
    std::ofstream file(path_, std::ios::app);
    if (!file) {
        error = "could not open the grade log '" + path_ + "'";
        return false;
    }
    file << format_grade_record(record) << '\n';
    file.flush();
    if (!file) {
        error = "could not write to the grade log '" + path_ + "'";
        return false;
    }
    return true;
}

std::vector<GradeRecord> GradeLog::entries(std::string& error) const {
    error.clear();
    std::vector<GradeRecord> out;

    std::ifstream file(path_);
    if (!file) {
        return out;
    }
    std::string line;
    while (std::getline(file, line)) {
        GradeRecord record;
        if (!line.empty() && parse_grade_record(line, record)) {
            out.push_back(std::move(record));
        }
    }
    return out;
}

}  // namespace sc::app
