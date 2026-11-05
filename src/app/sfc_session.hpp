#pragma once
// One chart session: the parsed chart, the last compile and the running
// application. A chart is state, so the session also reports which step is
// active right now: that is the only question a chart drawing has to answer
// differently from a rung drawing.
#include "app/application.hpp"
#include "plc/sfc.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sc::app {

class SfcSession {
public:
    explicit SfcSession(std::string source = {});

    // Replaces the chart, compiles it and resets the scan. A parse or compile
    // failure leaves the text in place and sets parsed() false.
    bool load(std::string source);

    const std::string& text() const { return text_; }
    const plc::SfcChart& chart() const { return chart_; }
    bool parsed() const { return parsed_; }
    const plc::Diagnostics& diagnostics() const { return diagnostics_; }
    const std::string& error() const { return error_; }

    bool compiled() const { return application_ != nullptr; }
    bool faulted() const;
    bool step();

    Application::WatchSnapshot snapshot() const;
    // One entry per step, in chart order: whether that step is active. Empty when
    // the chart has not compiled, so a drawing shows no highlight rather than
    // claiming step one is running.
    std::vector<bool> active_steps() const;
    std::vector<std::string> watch_names() const;

private:
    plc::SfcChart chart_;
    std::string text_;
    plc::Diagnostics diagnostics_;
    std::unique_ptr<Application> application_;
    std::string error_;
    bool parsed_ = false;
};

}  // namespace sc::app
