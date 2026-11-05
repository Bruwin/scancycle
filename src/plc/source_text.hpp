#pragma once
// Scanning shared by the line-oriented front ends. Structured Text has a real
// lexer; an instruction list or a ladder file needs only three things before its
// own grammar starts: comments gone, lines trimmed, and the VAR block that names
// the tags.
//
// Comments are blanked rather than removed. Length and newlines stay where the
// author put them, so a diagnostic still quotes the line and column they can see
// in their file instead of a number invented by a stripper.
#include "plc/declaration.hpp"
#include "plc/diagnostic.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sc::plc {

std::string_view trim_text(std::string_view text);
bool text_equals_ci(std::string_view a, std::string_view b);

// Removes `(* ... *)` and `//` comments. Returns false when a block comment is
// left open, because the rest of the file would otherwise be read as code.
bool strip_comments(std::string_view source, std::string& out);

struct VarBlock {
    bool present = false;
    std::size_t begin = 0;  // first byte after END_VAR
    int next_line = 1;      // line number of the first line after the block
    std::vector<TagDecl> vars;
};

// Reads an optional VAR block at the top of `text`. A file without one is legal:
// the caller may own the tag list, which is what an editor with a tag database
// does. Type names are matched case-insensitively, since IEC keywords are.
bool parse_var_block(const std::string& text, VarBlock& block, Diagnostics& diagnostics);

}  // namespace sc::plc
