#pragma once
// A synchronous PCCC client over any DF1 link. It owns transaction numbering,
// DLE framing and reply matching, so the scan driver and the bring-up tool
// cannot disagree about the wire format. Safety stays outside: writing to a
// field still requires a CommissioningPolicy at the call site.
#include "core/types.hpp"
#include "io/df1.hpp"
#include "io/df1_link.hpp"
#include "io/df1_pccc.hpp"

#include <span>
#include <string>
#include <vector>

namespace sc::io {

class Df1Client {
public:
    // `host` is this node, `destination` is the controller.
    Df1Client(u8 host, u8 destination, Df1Checksum checksum, Df1Link& link);
    ~Df1Client();

    Df1Client(const Df1Client&) = delete;
    Df1Client& operator=(const Df1Client&) = delete;

    bool open(std::string& error);
    void close();
    bool is_open() const { return open_; }

    bool read_word(u8 file, u8 file_type, u16 element, u16& value, std::string& error);
    bool write_word(u8 file, u8 file_type, u16 element, u16 value, std::string& error);

    // A bit is one bit of a 16-bit word. A write reads the word first and
    // changes only that bit, so the other fifteen survive.
    bool read_bit(u8 file, u8 file_type, u16 element, u8 bit, bool& value,
                  std::string& error);
    bool write_bit(u8 file, u8 file_type, u16 element, u8 bit, bool value,
                   std::string& error);

    i64 exchanges() const { return exchanges_; }

private:
    bool transact(u8 command, std::span<const u8> payload, Df1Header& request,
                  std::vector<u8>& reply, std::string& error);

    Df1Link& link_;
    u8 host_;
    u8 destination_;
    Df1Checksum checksum_;
    u16 transaction_ = 0;
    i64 exchanges_ = 0;
    bool open_ = false;
};

}  // namespace sc::io
