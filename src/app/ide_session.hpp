#pragma once
// One headless PLC editing session: the source buffer, the latest compile and
// the running application. The HTML page and the HTTP routes drive this, so the
// IDE stays testable without a browser or a socket.
#include "app/application.hpp"
#include "plc/diagnostic.hpp"
#include "ui/editor_model.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sc::app {

class IdeSession {
public:
    // The initial source is compiled immediately. A failure leaves the session
    // without a running application, so a step can never act on an older program
    // while the page displays the new diagnostics.
    explicit IdeSession(std::string source = {});

    // Replaces the buffer and compiles it. Always resets the scan state, even
    // when the new source is identical to the old one.
    bool compile(std::string source);

    bool step();

    // Forces apply to bound I/O tags only, matching a real rack: a local
    // variable has no field terminal to pin.
    bool set_force(std::string name, bool value);
    bool clear_force(std::string_view name);

    const std::string& source() const { return editor_.text(); }
    const plc::Diagnostics& diagnostics() const { return editor_.diagnostics(); }
    const std::string& error() const { return error_; }
    bool compiled() const { return application_ != nullptr; }
    bool faulted() const;

    // The monitor view at one instant. Empty when nothing has compiled.
    Application::WatchSnapshot snapshot() const;

    // Every declared variable except internal function-block instances, which
    // have no single scalar value to display.
    std::vector<std::string> watch_names() const;

    std::vector<std::string> unbound_devices() const;

private:
    ui::EditorModel editor_;
    std::unique_ptr<Application> application_;
    std::string error_;
};

}  // namespace sc::app
