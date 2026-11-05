#include "app/watch_text.hpp"

namespace sc {

std::string watch_to_text(const Application::WatchSnapshot& snapshot) {
    std::string out;
    out += "scan=";
    out += std::to_string(snapshot.scan);
    out += "\tnow_ms=";
    out += std::to_string(snapshot.now_ms);
    out += snapshot.faulted ? "\tfaulted=1\n" : "\tfaulted=0\n";

    for (const Application::Watch& tag : snapshot.tags) {
        out += "tag\t";
        out += tag.name;
        out += '\t';
        if (tag.present) {
            out += plc::value_type_name(tag.value.type);
            out += '\t';
            out += tag.value.to_string();
            out += "\t1";
        } else {
            out += "-\t-\t0";
        }
        out += tag.forced ? "\t1\n" : "\t0\n";
    }

    for (const io::Force& force : snapshot.forces) {
        out += "force\t";
        out += force.name;
        out += force.value ? "\tTRUE\n" : "\tFALSE\n";
    }
    return out;
}

}  // namespace sc
