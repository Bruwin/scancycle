#include "app/sfc_session.hpp"

#include "sim/plant.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace sc::app {
namespace {

constexpr std::string_view kInternalPrefix = "__sfc_";

bool internal_tag(std::string_view name) {
    return name.size() >= kInternalPrefix.size() &&
           name.substr(0, kInternalPrefix.size()) == kInternalPrefix;
}

}  // namespace

SfcSession::SfcSession(std::string source) { load(std::move(source)); }

bool SfcSession::load(std::string source) {
    text_ = std::move(source);
    application_.reset();
    error_.clear();

    plc::SfcParseResult parsed = plc::parse_sfc_source(text_);
    chart_ = std::move(parsed.chart);
    parsed_ = parsed.ok;
    diagnostics_ = std::move(parsed.diagnostics);
    if (!parsed_) {
        error_ = diagnostics_.empty() ? "failed to parse the SFC chart"
                                      : diagnostics_.front().message;
        return false;
    }

    // Compile once here for a diagnostic the application would only summarise as
    // one string. The application then owns the running copy.
    plc::SfcResult compiled = plc::compile_sfc("sfc", chart_);
    diagnostics_ = std::move(compiled.diagnostics);
    if (!compiled.ok) {
        error_ = diagnostics_.empty() ? "failed to compile the SFC chart"
                                      : diagnostics_.front().message;
        return false;
    }

    AppConfig config;
    config.program_source = text_;
    config.program_language = ProgramLanguage::Sfc;
    auto application = std::make_unique<Application>(std::move(config),
                                                     sim::make_vertical_slice_plant());
    if (!application->initialize()) {
        error_ = application->error();
        return false;
    }
    application_ = std::move(application);
    return true;
}

bool SfcSession::faulted() const {
    return application_ != nullptr && application_->faulted();
}

bool SfcSession::step() {
    if (!application_) {
        error_ = "compile a chart before stepping";
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

Application::WatchSnapshot SfcSession::snapshot() const {
    if (!application_) {
        return {};
    }
    return application_->snapshot(watch_names());
}

std::vector<bool> SfcSession::active_steps() const {
    std::vector<bool> active(chart_.steps.size(), false);
    if (application_ == nullptr || application_->plc() == nullptr) {
        return active;
    }
    for (std::size_t i = 0; i < chart_.steps.size(); ++i) {
        active[i] = application_->plc()->get(chart_.steps[i].name).as_bool();
    }
    return active;
}

std::vector<std::string> SfcSession::watch_names() const {
    std::vector<std::string> names;
    if (application_ == nullptr || application_->plc() == nullptr) {
        return names;
    }
    for (const plc::VarInfo& var : application_->plc()->program().vars) {
        if (var.fb_index < 0 && !internal_tag(var.name)) {
            names.push_back(var.name);
        }
    }
    return names;
}

}  // namespace sc::app
