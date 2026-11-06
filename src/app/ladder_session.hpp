#pragma once
// One ladder editing session: the parsed chart, the generated text, the last
// compile and the running application. Edits are applied to a copied chart and
// only adopted when they compile, so a click can never leave a half-mutated
// network behind. The page and its routes drive this without a browser.
#include "app/application.hpp"
#include "plc/ladder_text.hpp"
#include "plc/netlist_edit.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sc::app {

class LadderSession {
public:
    explicit LadderSession(std::string source = {});

    // Replaces the chart, compiles it and resets the scan. A parse or compile
    // failure leaves the last good drawing in source() but sets parsed() false.
    bool load(std::string source);

    const std::string& text() const { return text_; }
    const plc::LadderSource& source() const { return source_; }
    bool parsed() const { return parsed_; }
    const plc::Diagnostics& diagnostics() const { return diagnostics_; }
    const std::string& error() const { return error_; }

    bool compiled() const { return application_ != nullptr; }
    bool faulted() const;
    bool step();
    bool set_force(std::string name, bool value);
    bool clear_force(std::string_view name);

    Application::WatchSnapshot snapshot() const;
    std::vector<std::string> watch_names() const;

    using RungEdit = std::function<plc::NetEditResult(plc::Rung&)>;

    // Applies an edit to one rung and recompiles. A refused edit or a failed
    // compile leaves the text as it was before the call.
    bool edit(std::size_t rung, const RungEdit& edit);
    bool add_rung(std::string coil);
    bool remove_rung(std::size_t rung);

private:
    plc::LadderSource source_;
    std::string text_;
    plc::Diagnostics diagnostics_;
    std::unique_ptr<Application> application_;
    std::string error_;
    bool parsed_ = false;
};

}  // namespace sc::app
