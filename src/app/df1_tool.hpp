#pragma once
// One-shot DF1 reads and writes for bring-up against a real controller. The
// scan driver is shaped for a VM and a tag map; commissioning a MicroLogix
// needs a way to look at one N7 word before trusting either. This tool speaks
// the same PCCC client with no VM in the way, but a write is still allowlisted
// and session-gated through CommissioningPolicy: typing --write is the
// operator's opt-in, not a bypass.
#include "core/types.hpp"
#include "io/df1.hpp"
#include "io/df1_link.hpp"
#include "io/df1_pccc.hpp"

#include <string>
#include <string_view>

namespace sc::app {

struct Df1Address {
    sc::u8 file = 7;
    sc::u8 file_type = sc::io::kPcccFileTypeInteger;
    sc::u16 element = 0;
    sc::u8 bit = 0;
    bool has_bit = false;
};

// N7:0, B3:5 or N7:0/3. Integer and binary files are the two the driver owns.
bool parse_df1_address(std::string_view text, Df1Address& address, std::string& error);
std::string df1_address_text(const Df1Address& address);

// A word is decimal or 0x-prefixed hex in 0..65535; a bit also accepts the
// boolean words the rest of the tooling uses.
bool parse_df1_word_value(std::string_view text, sc::u16& value, std::string& error);
bool parse_df1_bit_value(std::string_view text, bool& value, std::string& error);

struct Df1ToolConfig {
    sc::u8 host = 0;
    sc::u8 node = 1;
    sc::io::Df1Checksum checksum = sc::io::Df1Checksum::Crc16;
    Df1Address address;
};

struct Df1ToolResult {
    bool ok = false;
    sc::u16 word = 0;
    bool bit = false;
    std::string error;
};

Df1ToolResult df1_read_word(const Df1ToolConfig& config, sc::io::Df1Link& link);
Df1ToolResult df1_write_word(const Df1ToolConfig& config, sc::io::Df1Link& link,
                             sc::u16 value);
Df1ToolResult df1_read_bit(const Df1ToolConfig& config, sc::io::Df1Link& link);
Df1ToolResult df1_write_bit(const Df1ToolConfig& config, sc::io::Df1Link& link, bool value);

}  // namespace sc::app
