#pragma once
// The watch snapshot as text. A monitor view reads the snapshot struct directly;
// this exists for the CLI, for bug reports and for tests, where a pasted trace
// has to be reproducible instead of retyped.
//
// The format is deliberately boring, one row per line, columns separated by a
// single tab:
//
//   scan=42<TAB>now_ms=420<TAB>faulted=0
//   tag<TAB>motor<TAB>BOOL<TAB>TRUE<TAB>1<TAB>0
//   force<TAB>motor<TAB>TRUE
//
// Columns after `tag` are name, type, value, present, forced. An absent tag
// prints `-` for its type and value, because a variable the program does not
// declare has no type to report. Neither a tag name nor a type name can contain
// a tab, so splitting on tabs needs no quoting and no escaping.
#include "app/application.hpp"

#include <string>

namespace sc {

// Renders one snapshot. The result always ends with a newline, so consecutive
// scans can simply be appended.
std::string watch_to_text(const Application::WatchSnapshot& snapshot);

}  // namespace sc
