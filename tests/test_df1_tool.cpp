#include "harness.hpp"

#include "app/df1_tool.hpp"
#include "io/df1_slave.hpp"

#include <string>

using namespace sc;

TEST(df1_address_parser_reads_integer_and_binary_files) {
    app::Df1Address address;
    std::string error;

    CHECK(app::parse_df1_address("N7:0", address, error));
    CHECK(error.empty());
    CHECK(address.file == 7);
    CHECK(address.file_type == io::kPcccFileTypeInteger);
    CHECK(address.element == 0);
    CHECK(!address.has_bit);

    CHECK(app::parse_df1_address("b3:12/5", address, error));
    CHECK(address.file == 3);
    CHECK(address.file_type == io::kPcccFileTypeBinary);
    CHECK(address.element == 12);
    CHECK(address.has_bit);
    CHECK(address.bit == 5);

    CHECK(app::parse_df1_address("B255:65535/15", address, error));
    CHECK(address.file == 255);
    CHECK(address.element == 65535);
    CHECK(address.bit == 15);
}

TEST(df1_address_parser_rejects_malformed_addresses) {
    app::Df1Address address;
    std::string error;

    CHECK(!app::parse_df1_address("", address, error));
    CHECK(!app::parse_df1_address("N7", address, error));
    CHECK(!app::parse_df1_address("N:0", address, error));
    CHECK(!app::parse_df1_address("X1:0", address, error));
    CHECK(!app::parse_df1_address("N256:0", address, error));
    CHECK(!app::parse_df1_address("N7:65536", address, error));
    CHECK(!app::parse_df1_address("N7:0/16", address, error));
    CHECK(!app::parse_df1_address("N7:0/x", address, error));
    CHECK(!app::parse_df1_address("N7:0/", address, error));
    CHECK(!error.empty());
}

TEST(df1_address_text_round_trips) {
    const char* kAddresses[] = {"N7:0", "B3:5", "N7:10/7", "B255:65535/15"};

    for (const char* text : kAddresses) {
        app::Df1Address address;
        std::string error;
        CHECK(app::parse_df1_address(text, address, error));
        CHECK(app::df1_address_text(address) == text);
    }
}

TEST(df1_tool_parses_word_and_bit_values) {
    sc::u16 word = 0;
    std::string error;

    CHECK(app::parse_df1_word_value("4660", word, error));
    CHECK(word == 4660);
    CHECK(app::parse_df1_word_value("0x1234", word, error));
    CHECK(word == 0x1234);
    CHECK(app::parse_df1_word_value("65535", word, error));
    CHECK(word == 65535);
    CHECK(!app::parse_df1_word_value("65536", word, error));
    CHECK(!app::parse_df1_word_value("-1", word, error));
    CHECK(!app::parse_df1_word_value("", word, error));

    bool bit = false;
    CHECK(app::parse_df1_bit_value("1", bit, error));
    CHECK(bit);
    CHECK(app::parse_df1_bit_value("ON", bit, error));
    CHECK(bit);
    CHECK(app::parse_df1_bit_value("false", bit, error));
    CHECK(!bit);
    CHECK(!app::parse_df1_bit_value("2", bit, error));
}

TEST(df1_tool_reads_and_writes_a_word) {
    io::Df1Slave slave;
    slave.add_word_file(7, io::kPcccFileTypeInteger, 4);
    slave.set_checksum(io::Df1Checksum::Crc16);
    CHECK(slave.set_word(7, 1, 0xBEEF));

    app::Df1ToolConfig config;
    std::string error;
    CHECK(app::parse_df1_address("N7:1", config.address, error));

    const app::Df1ToolResult read = app::df1_read_word(config, slave);
    CHECK(read.ok);
    CHECK(read.word == 0xBEEF);

    const app::Df1ToolResult write = app::df1_write_word(config, slave, 0x1234);
    CHECK(write.ok);
    CHECK(slave.word(7, 1) == 0x1234);
}

TEST(df1_tool_reads_and_writes_a_bit_without_disturbing_neighbours) {
    io::Df1Slave slave;
    slave.add_word_file(7, io::kPcccFileTypeInteger, 4);
    slave.set_checksum(io::Df1Checksum::Crc16);
    CHECK(slave.set_word(7, 2, 0x0004));

    app::Df1ToolConfig config;
    std::string error;
    CHECK(app::parse_df1_address("N7:2/2", config.address, error));

    const app::Df1ToolResult read = app::df1_read_bit(config, slave);
    CHECK(read.ok);
    CHECK(read.bit);

    // Flip bit 0 and check that the set bit 2 survives in both directions.
    CHECK(app::parse_df1_address("N7:2/0", config.address, error));

    const app::Df1ToolResult set = app::df1_write_bit(config, slave, true);
    CHECK(set.ok);
    CHECK(slave.word(7, 2) == 0x0005);

    const app::Df1ToolResult clear = app::df1_write_bit(config, slave, false);
    CHECK(clear.ok);
    CHECK(slave.word(7, 2) == 0x0004);
}

TEST(df1_tool_reports_a_missing_file) {
    io::Df1Slave slave;  // No data file registered.
    slave.set_checksum(io::Df1Checksum::Crc16);

    app::Df1ToolConfig config;
    std::string error;
    CHECK(app::parse_df1_address("N7:0", config.address, error));

    const app::Df1ToolResult read = app::df1_read_word(config, slave);
    CHECK(!read.ok);
    CHECK(read.error.find("status 0xF0") != std::string::npos);
}
