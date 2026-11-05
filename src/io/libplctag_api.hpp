#pragma once
// The real libplctag binding is optional. When the project is configured
// without it, this factory returns nullptr and an actionable diagnostic instead
// of making the headless build depend on an external package.
#include "io/plc_tag_driver.hpp"

#include <memory>
#include <string>

namespace sc::io {

std::unique_ptr<PlcTagApi> make_libplctag_api(std::string& error);

}  // namespace sc::io
