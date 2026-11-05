#pragma once
// A software SLC data table on the far end of a DF1 link. It exists so the
// driver's framing, typed read/write, latching and commissioning rules can be
// tested with no serial port and no PLC, and so a classroom can point a real
// driver at a known table. exchange() answers on the caller's thread: nothing
// here is asynchronous, which keeps the tests deterministic.
#include "io/df1.hpp"
#include "io/df1_link.hpp"
#include "io/df1_pccc.hpp"

#include <span>
#include <string>
#include <vector>

namespace sc::io {

class Df1Slave final : public Df1Link {
public:
    // Registers a data file of 16-bit words. Re-registering a file replaces its
    // contents. File number and type are both part of the identity, so N7 and
    // B7 cannot collide.
    void add_word_file(u8 file, u8 file_type, std::size_t elements);
    void set_checksum(Df1Checksum checksum) { checksum_ = checksum; }

    bool open(std::string& error) override;
    void close() override {}
    bool exchange(std::span<const u8> request, std::vector<u8>& response,
                  std::string& error) override;

    bool set_word(u8 file, u16 element, u16 value);
    u16 word(u8 file, u16 element) const;
    bool set_bit(u8 file, u16 element, u8 bit, bool value);
    bool bit(u8 file, u16 element, u8 bit) const;

    // A controller that never answers, for the driver's no-reply path.
    void set_silent(bool silent) { silent_ = silent; }

    i64 requests() const { return requests_; }
    i64 writes() const { return writes_; }

private:
    struct File {
        u8 file = 0;
        u8 type = 0;
        std::vector<u16> words;
    };

    File* find(u8 file, u8 type);
    const File* find(u8 file, u8 type) const;
    bool handle(std::span<const u8> body, std::vector<u8>& reply, std::string& error);
    std::vector<u8> error_reply(const Df1Header& request, u8 function) const;

    std::vector<File> files_;
    Df1Checksum checksum_ = Df1Checksum::Bcc;
    bool silent_ = false;
    i64 requests_ = 0;
    i64 writes_ = 0;
};

}  // namespace sc::io
