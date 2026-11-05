#include "io/df1_slave.hpp"

#include <utility>

namespace sc::io {
namespace {

u16 read_u16_at(std::span<const u8> bytes, std::size_t at) {
    return static_cast<u16>(bytes[at] | (static_cast<u16>(bytes[at + 1]) << 8));
}

}  // namespace

void Df1Slave::add_word_file(u8 file, u8 file_type, std::size_t elements) {
    if (File* existing = find(file, file_type)) {
        existing->words.assign(elements, 0);
        return;
    }
    File entry;
    entry.file = file;
    entry.type = file_type;
    entry.words.assign(elements, 0);
    files_.push_back(std::move(entry));
}

bool Df1Slave::open(std::string& error) {
    error.clear();
    return true;
}

bool Df1Slave::exchange(std::span<const u8> request, std::vector<u8>& response,
                        std::string& error) {
    error.clear();
    response.clear();
    ++requests_;
    if (silent_) {
        return true;
    }

    std::vector<u8> body;
    std::size_t consumed = 0;
    if (!df1_decode(request, checksum_, body, consumed, error)) {
        return false;
    }
    if (consumed != request.size()) {
        error = "DF1 request carries trailing bytes";
        return false;
    }

    std::vector<u8> reply;
    if (!handle(body, reply, error)) {
        return false;
    }
    return df1_encode(reply, checksum_, response, error);
}

bool Df1Slave::handle(std::span<const u8> body, std::vector<u8>& reply, std::string& error) {
    error.clear();
    Df1Header header;
    std::size_t at = 0;
    if (!df1_parse_header(body, header, at, error)) {
        return false;
    }

    const u8 function = body.size() > at ? body[at] : 0;
    const bool known = header.command == kDf1CmdProtectedRead ||
                       header.command == kDf1CmdProtectedWrite;
    if (!known || body.size() < at + 7) {
        reply = error_reply(header, function);
        return true;
    }

    const u8 size = body[at + 1];
    const PcccAddress address{body[at + 2], body[at + 3], read_u16_at(body, at + 4),
                              body[at + 6]};
    const bool read = header.command == kDf1CmdProtectedRead;
    const std::size_t extra = read ? 0 : size;
    File* file = find(address.file, address.file_type);
    if (size == 0 || (size % 2) != 0 || body.size() != at + 7 + extra || file == nullptr) {
        reply = error_reply(header, function);
        return true;
    }

    const std::size_t words = size / 2;
    if (static_cast<std::size_t>(address.element) + words > file->words.size()) {
        reply = error_reply(header, function);
        return true;
    }

    Df1Header reply_header = header;
    reply_header.destination = header.source;
    reply_header.source = header.destination;

    if (read) {
        reply_header.command = kDf1CmdReadReply;
        const std::vector<u16> data(
            file->words.begin() + address.element,
            file->words.begin() + address.element + static_cast<std::ptrdiff_t>(words));
        reply = df1_body(reply_header);
        const std::vector<u8> payload = build_pccc_read_reply(address, data);
        reply.insert(reply.end(), payload.begin(), payload.end());
        return true;
    }

    for (std::size_t index = 0; index < words; ++index) {
        file->words[static_cast<std::size_t>(address.element) + index] =
            read_u16_at(body, at + 7 + index * 2);
    }
    ++writes_;
    reply_header.command = kDf1CmdWriteReply;
    reply = df1_body(reply_header);
    const std::vector<u8> payload = build_pccc_write_reply(address);
    reply.insert(reply.end(), payload.begin(), payload.end());
    return true;
}

std::vector<u8> Df1Slave::error_reply(const Df1Header& request, u8 function) const {
    Df1Header header = request;
    header.destination = request.source;
    header.source = request.destination;
    header.command = static_cast<u8>(request.command + 0x40);
    header.status = kPcccStatusError;
    std::vector<u8> body = df1_body(header);
    const std::vector<u8> payload = build_pccc_error_reply(function);
    body.insert(body.end(), payload.begin(), payload.end());
    return body;
}

Df1Slave::File* Df1Slave::find(u8 file, u8 type) {
    for (File& entry : files_) {
        if (entry.file == file && entry.type == type) {
            return &entry;
        }
    }
    return nullptr;
}

const Df1Slave::File* Df1Slave::find(u8 file, u8 type) const {
    for (const File& entry : files_) {
        if (entry.file == file && entry.type == type) {
            return &entry;
        }
    }
    return nullptr;
}

bool Df1Slave::set_word(u8 file, u16 element, u16 value) {
    File* entry = find(file, kPcccFileTypeInteger);
    if (entry == nullptr) {
        entry = find(file, kPcccFileTypeBinary);
    }
    if (entry == nullptr || element >= entry->words.size()) {
        return false;
    }
    entry->words[element] = value;
    return true;
}

u16 Df1Slave::word(u8 file, u16 element) const {
    const File* entry = find(file, kPcccFileTypeInteger);
    if (entry == nullptr) {
        entry = find(file, kPcccFileTypeBinary);
    }
    if (entry == nullptr || element >= entry->words.size()) {
        return 0;
    }
    return entry->words[element];
}

bool Df1Slave::set_bit(u8 file, u16 element, u8 bit, bool value) {
    if (bit > 15) {
        return false;
    }
    const u16 current = word(file, element);
    const u16 mask = static_cast<u16>(1u << bit);
    return set_word(file, element, value ? static_cast<u16>(current | mask)
                                         : static_cast<u16>(current & ~mask));
}

bool Df1Slave::bit(u8 file, u16 element, u8 bit) const {
    if (bit > 15) {
        return false;
    }
    return ((word(file, element) >> bit) & 1u) != 0;
}

}  // namespace sc::io
