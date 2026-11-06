#include "app/ladder_session.hpp"

#include "sim/plant.hpp"

#include <utility>

namespace sc::app {

LadderSession::LadderSession(std::string source) { load(std::move(source)); }

bool LadderSession::load(std::string source) {
    text_ = std::move(source);
    application_.reset();
    error_.clear();

    plc::LadderParseResult parsed = plc::parse_ladder_source(text_);
    source_ = std::move(parsed.source);
    parsed_ = parsed.ok;
    diagnostics_ = std::move(parsed.diagnostics);
    if (!parsed_) {
        error_ = diagnostics_.empty() ? "failed to parse the ladder program"
                                      : diagnostics_.front().message;
        return false;
    }

    // Compile once here for diagnostics the application would only summarise as
    // one error string. The application then owns the running copy.
    plc::NetlistResult compiled =
        plc::compile_netlist("ladder", source_.rungs, source_.vars);
    diagnostics_ = std::move(compiled.diagnostics);
    if (!compiled.ok) {
        error_ = diagnostics_.empty() ? "failed to compile the ladder program"
                                      : diagnostics_.front().message;
        return false;
    }

    AppConfig config;
    config.program_source = text_;
    config.program_language = ProgramLanguage::Ladder;
    auto application = std::make_unique<Application>(
        std::move(config), sim::make_vertical_slice_plant());
    if (!application->initialize()) {
        error_ = application->error();
        return false;
    }
    application_ = std::move(application);
    return true;
}

bool LadderSession::faulted() const {
    return application_ != nullptr && application_->faulted();
}

bool LadderSession::step() {
    if (!application_) {
        error_ = "compile a program before stepping";
        return false;
    }
    if (!application_->step_scan()) {
        error_ = application_->error();
        if (error_.empty()) {
            error_ = "the scan failed";
        }
        return false;
    }
    error_.clear();
    return true;
}

bool LadderSession::set_force(std::string name, bool value) {
    if (!application_) {
        error_ = "compile a program before forcing a tag";
        return false;
    }
    if (!application_->set_force(std::move(name), value)) {
        error_ = application_->error();
        return false;
    }
    error_.clear();
    return true;
}

bool LadderSession::clear_force(std::string_view name) {
    if (!application_) {
        error_ = "compile a program before clearing a force";
        return false;
    }
    if (!application_->clear_force(name)) {
        error_ = application_->error();
        return false;
    }
    error_.clear();
    return true;
}

Application::WatchSnapshot LadderSession::snapshot() const {
    if (!application_) {
        return {};
    }
    return application_->snapshot(watch_names());
}

std::vector<std::string> LadderSession::watch_names() const {
    std::vector<std::string> names;
    if (!application_ || application_->plc() == nullptr) {
        return names;
    }
    for (const plc::VarInfo& var : application_->plc()->program().vars) {
        if (var.fb_index < 0) {
            names.push_back(var.name);
        }
    }
    return names;
}

bool LadderSession::edit(std::size_t rung, const RungEdit& edit_rung) {
    if (!parsed_) {
        error_ = "fix the parse error before editing";
        return false;
    }

    // Work on a copy: a refused edit must not touch the chart the page shows.
    plc::LadderParseResult copy =
        plc::parse_ladder_source(plc::format_ladder_source(source_));
    if (!copy.ok || rung >= copy.source.rungs.size()) {
        error_ = "the chart could not be copied for editing";
        return false;
    }

    const plc::NetEditResult result = edit_rung(copy.source.rungs[rung]);
    if (!result.ok) {
        error_ = result.error;
        return false;
    }
    return load(plc::format_ladder_source(copy.source));
}

bool LadderSession::add_rung(std::string coil) {
    if (!parsed_) {
        error_ = "fix the parse error before editing";
        return false;
    }
    if (coil.empty()) {
        error_ = "a rung needs a coil tag";
        return false;
    }

    plc::Rung rung;
    rung.coils.push_back(plc::Coil{std::move(coil), false});
    source_.rungs.push_back(std::move(rung));
    return load(plc::format_ladder_source(source_));
}

bool LadderSession::remove_rung(std::size_t rung) {
    if (!parsed_) {
        error_ = "fix the parse error before editing";
        return false;
    }
    if (rung >= source_.rungs.size()) {
        error_ = "no such rung";
        return false;
    }

    source_.rungs.erase(source_.rungs.begin() + static_cast<std::ptrdiff_t>(rung));
    return load(plc::format_ladder_source(source_));
}

}  // namespace sc::app
