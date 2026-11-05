#include "app/df1_tool.hpp"

#include "io/commissioning.hpp"
#include "io/df1_client.hpp"

#include <charconv>
#include <cctype>
#include <system_error>

namespace sc::app {
namespace {

bool parse_number(std::string_view text, unsigned long maximum, unsigned long& out) {
    if (text.empty()) {
        return false;
    }
    unsigned long parsed = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() ||
        parsed > maximum) {
        return false;
    }
    out = parsed;
    return true;
}

// A write is only attempted while a session is open and this exact address is
// allowlisted. The session is bounded: a forgotten terminal cannot leave the
// gate open indefinitely.
bool allow_write(const Df1Address& address, io::CommissioningPolicy& policy,
                 std::string& error) {
    const std::string text = df1_address_text(address);
    policy.allow_address(text);
    policy.set_min_write_interval_ms(0);
    if (!policy.begin_session(0, 60'000)) {
        error = "could not open a commissioning session";
        return false;
    }
    if (!policy.can_write(text, 0)) {
        error = "commissioning gate denied the write";
        return false;
    }
    return true;
}

}  // namespace

bool parse_df1_address(std::string_view text, Df1Address& address, std::string& error) {
    error.clear();
    if (text.size() < 4) {
        error = "expected an address like N7:0, B3:5 or N7:0/3";
        return false;
    }

    sc::u8 file_type = 0;
    switch (text[0]) {
        case 'N':
        case 'n':
            file_type = io::kPcccFileTypeInteger;
            break;
        case 'B':
        case 'b':
            file_type = io::kPcccFileTypeBinary;
            break;
        default:
            error = "only N (integer) and B (binary) files are supported";
            return false;
    }

    const std::size_t colon = text.find(':', 1);
    if (colon == std::string_view::npos) {
        error = "address is missing ':'";
        return false;
    }
    unsigned long file = 0;
    if (!parse_number(text.substr(1, colon - 1), 255, file)) {
        error = "address has a bad file number";
        return false;
    }

    const std::string_view rest = text.substr(colon + 1);
    const std::size_t slash = rest.find('/');
    const std::string_view element_text =
        slash == std::string_view::npos ? rest : rest.substr(0, slash);
    unsigned long element = 0;
    if (!parse_number(element_text, 65535, element)) {
        error = "address has a bad element number";
        return false;
    }

    Df1Address parsed;
    parsed.file = static_cast<sc::u8>(file);
    parsed.file_type = file_type;
    parsed.element = static_cast<sc::u16>(element);
    if (slash != std::string_view::npos) {
        unsigned long bit = 0;
        if (!parse_number(rest.substr(slash + 1), 15, bit)) {
            error = "address has a bad bit number (0..15)";
            return false;
        }
        parsed.has_bit = true;
        parsed.bit = static_cast<sc::u8>(bit);
    }
    address = parsed;
    return true;
}

std::string df1_address_text(const Df1Address& address) {
    std::string text;
    text.push_back(address.file_type == io::kPcccFileTypeBinary ? 'B' : 'N');
    text += std::to_string(address.file);
    text.push_back(':');
    text += std::to_string(address.element);
    if (address.has_bit) {
        text.push_back('/');
        text += std::to_string(address.bit);
    }
    return text;
}

bool parse_df1_word_value(std::string_view text, sc::u16& value, std::string& error) {
    error.clear();
    std::string_view digits = text;
    int base = 10;
    if (digits.size() > 2 && digits[0] == '0' &&
        (digits[1] == 'x' || digits[1] == 'X')) {
        digits.remove_prefix(2);
        base = 16;
    }

    unsigned long parsed = 0;
    const auto result =
        std::from_chars(digits.data(), digits.data() + digits.size(), parsed, base);
    if (digits.empty() || result.ec != std::errc() ||
        result.ptr != digits.data() + digits.size() || parsed > 65535) {
        error = "expected a word value in 0..65535 (decimal or 0x hex)";
        return false;
    }
    value = static_cast<sc::u16>(parsed);
    return true;
}

bool parse_df1_bit_value(std::string_view text, bool& value, std::string& error) {
    error.clear();
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    if (lowered == "1" || lowered == "true" || lowered == "on") {
        value = true;
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "off") {
        value = false;
        return true;
    }
    error = "expected a bit value: 0, 1, true, false, on or off";
    return false;
}

Df1ToolResult df1_read_word(const Df1ToolConfig& config, io::Df1Link& link) {
    Df1ToolResult result;
    io::Df1Client client(config.host, config.node, config.checksum, link);
    if (!client.open(result.error)) {
        return result;
    }
    if (!client.read_word(config.address.file, config.address.file_type,
                          config.address.element, result.word, result.error)) {
        return result;
    }
    result.ok = true;
    return result;
}

Df1ToolResult df1_write_word(const Df1ToolConfig& config, io::Df1Link& link,
                             sc::u16 value) {
    Df1ToolResult result;
    io::CommissioningPolicy policy;
    if (!allow_write(config.address, policy, result.error)) {
        return result;
    }
    io::Df1Client client(config.host, config.node, config.checksum, link);
    if (!client.open(result.error)) {
        return result;
    }
    if (!client.write_word(config.address.file, config.address.file_type,
                           config.address.element, value, result.error)) {
        return result;
    }
    result.word = value;
    result.ok = true;
    return result;
}

Df1ToolResult df1_read_bit(const Df1ToolConfig& config, io::Df1Link& link) {
    Df1ToolResult result;
    io::Df1Client client(config.host, config.node, config.checksum, link);
    if (!client.open(result.error)) {
        return result;
    }
    if (!client.read_bit(config.address.file, config.address.file_type,
                         config.address.element, config.address.bit, result.bit,
                         result.error)) {
        return result;
    }
    result.ok = true;
    return result;
}

Df1ToolResult df1_write_bit(const Df1ToolConfig& config, io::Df1Link& link,
                            bool value) {
    Df1ToolResult result;
    io::CommissioningPolicy policy;
    if (!allow_write(config.address, policy, result.error)) {
        return result;
    }
    io::Df1Client client(config.host, config.node, config.checksum, link);
    if (!client.open(result.error)) {
        return result;
    }
    if (!client.write_bit(config.address.file, config.address.file_type,
                          config.address.element, config.address.bit, value,
                          result.error)) {
        return result;
    }
    result.bit = value;
    result.ok = true;
    return result;
}

}  // namespace sc::app
