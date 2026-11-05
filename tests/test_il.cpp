#include "harness.hpp"

#include "plc/compiler.hpp"
#include "plc/il.hpp"
#include "plc/parser.hpp"
#include "plc/vm.hpp"

#include <initializer_list>
#include <string>
#include <vector>

namespace {

using namespace sc::plc;

std::vector<TagDecl> bool_tags(std::initializer_list<const char*> names) {
    std::vector<TagDecl> out;
    for (const char* name : names) {
        out.push_back(TagDecl{name, ValueType::Bool});
    }
    return out;
}

std::string truth_table(Vm& vm, const std::vector<std::string>& inputs,
                        const std::string& output) {
    std::string column;
    const std::size_t rows = std::size_t{1} << inputs.size();
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t bit = 0; bit < inputs.size(); ++bit) {
            vm.set(inputs[bit], Value::boolean(((row >> bit) & 1u) != 0));
        }
        vm.scan(static_cast<sc::i64>(row) * 10);
        column += vm.get(output).as_bool() ? '1' : '0';
    }
    return column;
}

}  // namespace

TEST(il_loads_and_stores) {
    const char* source = R"(// a start button straight to a motor
LD start
ST motor
)";
    const IlResult built = compile_il("Drive", source, bool_tags({"start", "motor"}));
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.scan(0));
    CHECK(!vm.get("motor").as_bool());
    CHECK(vm.set("start", Value::boolean(true)));
    CHECK(vm.scan(10));
    CHECK(vm.get("motor").as_bool());
}

TEST(il_negated_instructions_invert) {
    const IlResult built = compile_il("Invert", "LDN a\nSTN b\n", bool_tags({"a", "b"}));
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(truth_table(vm, {"a"}, "b") == "01");
}

TEST(il_combinators_chain_left_to_right) {
    const IlResult built =
        compile_il("Chain", "LD a\nAND b\nOR c\nST d\n", bool_tags({"a", "b", "c", "d"}));
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    // (a AND b) OR c
    CHECK(truth_table(vm, {"a", "b", "c"}, "d") == "00011111");
}

TEST(il_agrees_with_structured_text) {
    const IlResult il = compile_il("Same", "ldn a\nanD b\nST d\n", bool_tags({"a", "b", "d"}));
    CHECK(il.ok);
    if (!il.ok) {
        return;
    }

    const char* st_source = R"st(PROGRAM Same
VAR
    a : BOOL;
    b : BOOL;
    d : BOOL;
END_VAR
    d := NOT a AND b;
END_PROGRAM
)st";
    ParseResult parsed = parse_program(st_source);
    CHECK(parsed.program != nullptr);
    if (!parsed.program) {
        return;
    }
    CompileResult compiled = compile(*parsed.program);
    CHECK(compiled.ok);
    if (!compiled.ok) {
        return;
    }

    Vm il_vm(il.program);
    Vm st_vm(compiled.program);
    const std::string il_column = truth_table(il_vm, {"a", "b"}, "d");
    CHECK(il_column == truth_table(st_vm, {"a", "b"}, "d"));
    CHECK(il_column == "0010");
}

TEST(il_reports_an_unknown_tag_with_its_line) {
    const IlResult built =
        compile_il("Typo", "LD a\nAND missing\nST out\n", bool_tags({"a", "out"}));
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].line == 2);
    CHECK(built.diagnostics[0].message.find("missing") != std::string::npos);
}

TEST(il_refuses_to_combine_without_a_load) {
    const IlResult built = compile_il("Lost", "AND a\n", bool_tags({"a"}));
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].line == 1);
    CHECK(built.diagnostics[0].message.find("combine") != std::string::npos);
}

TEST(il_refuses_to_store_an_empty_accumulator) {
    const IlResult built = compile_il("Lost", "ST a\n", bool_tags({"a"}));
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("store") != std::string::npos);
}

TEST(il_rejects_an_unknown_instruction) {
    const IlResult built = compile_il("Odd", "MOV a\n", bool_tags({"a"}));
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("MOV") != std::string::npos);
}
