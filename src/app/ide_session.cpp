#include "app/ide_session.hpp"

#include "plc/compiler.hpp"
#include "plc/parser.hpp"
#include "sim/plant.hpp"

#include <utility>

namespace sc::app {

IdeSession::IdeSession(std::string source) { compile(std::move(source)); }

bool IdeSession::compile(std::string source) {
    editor_.set_text(std::move(source));
    editor_.set_diagnostics({});
    application_.reset();
    error_.clear();

    plc::ParseResult parsed = plc::parse_program(editor_.text());
    plc::Diagnostics diagnostics = parsed.diagnostics;
    if (!parsed.program) {
        editor_.set_diagnostics(std::move(diagnostics));
        error_ = editor_.diagnostics().empty() ? "failed to parse the program"
                                               : editor_.diagnostics().front().message;
        return false;
    }

    plc::CompileResult compiled = plc::compile(*parsed.program);
    diagnostics.insert(diagnostics.end(), compiled.diagnostics.begin(),
                       compiled.diagnostics.end());
    editor_.set_diagnostics(std::move(diagnostics));
    if (!compiled.ok) {
        error_ = editor_.diagnostics().empty() ? "failed to compile the program"
                                               : editor_.diagnostics().front().message;
        return false;
    }

    AppConfig config;
    config.program_source = editor_.text();
    auto application = std::make_unique<Application>(
        std::move(config), sim::make_vertical_slice_plant());
    if (!application->initialize()) {
        error_ = application->error().empty() ? "failed to start the cell"
                                              : application->error();
        return false;
    }
    application_ = std::move(application);
    return true;
}

bool IdeSession::step() {
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

bool IdeSession::set_force(std::string name, bool value) {
    if (!application_) {
        error_ = "compile a program before forcing a tag";
        return false;
    }
    if (!application_->set_force(std::move(name), value)) {
        error_ = application_->error();
        if (error_.empty()) {
            error_ = "the force was refused";
        }
        return false;
    }
    error_.clear();
    return true;
}

bool IdeSession::clear_force(std::string_view name) {
    if (!application_) {
        error_ = "compile a program before clearing a force";
        return false;
    }
    if (!application_->clear_force(name)) {
        error_ = application_->error();
        if (error_.empty()) {
            error_ = "the force was refused";
        }
        return false;
    }
    error_.clear();
    return true;
}

bool IdeSession::faulted() const { return application_ != nullptr && application_->faulted(); }

Application::WatchSnapshot IdeSession::snapshot() const {
    if (!application_) {
        return {};
    }
    return application_->snapshot(watch_names());
}

std::vector<std::string> IdeSession::watch_names() const {
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

std::vector<std::string> IdeSession::unbound_devices() const {
    if (!application_) {
        return {};
    }
    return application_->unbound_devices();
}

}  // namespace sc::app
