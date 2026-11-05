#include "app/ide_page.hpp"
#include "app/ide_session.hpp"
#include "net/http_server.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace {

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void print_usage() {
    std::printf(
        "scancycle_ide - Scan Cycle PLC IDE\n"
        "\n"
        "usage: scancycle_ide [--port=N] [--requests=N] [--program=PATH]\n"
        "\n"
        "  --port=N       loopback port (default 8090)\n"
        "  --requests=N   stop after N requests; 0 serves until interrupted\n"
        "  --program=PATH Structured Text file to load (default examples/press_cell.st)\n");
}

}  // namespace

int main(int argc, char** argv) {
    unsigned port = 8090;
    long requests = 0;
    std::string program_path = "examples/press_cell.st";
    bool program_given = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            print_usage();
            return 0;
        }
        if (arg.rfind("--port=", 0) == 0) {
            const long value = std::strtol(arg.c_str() + 7, nullptr, 10);
            if (value <= 0 || value > 65535) {
                std::fprintf(stderr, "ide: '%s' is not a port\n", arg.c_str());
                return 2;
            }
            port = static_cast<unsigned>(value);
        } else if (arg.rfind("--requests=", 0) == 0) {
            requests = std::strtol(arg.c_str() + 11, nullptr, 10);
            if (requests < 0) {
                std::fprintf(stderr, "ide: '%s' is not a request count\n", arg.c_str());
                return 2;
            }
        } else if (arg.rfind("--program=", 0) == 0) {
            program_path = arg.substr(10);
            program_given = true;
            if (program_path.empty()) {
                std::fprintf(stderr, "ide: --program= needs a file path\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::string source = read_file(program_path);
    if (source.empty() && program_given) {
        std::fprintf(stderr, "ide: cannot read '%s'\n", program_path.c_str());
        return 1;
    }
    if (source.empty()) {
        std::fprintf(stderr, "ide: no program at '%s'; starting with an empty editor\n",
                     program_path.c_str());
    }

    sc::app::IdeSession session(std::move(source));
    sc::net::PortalRouter router = sc::app::make_ide_router(session);
    sc::net::HttpServer server(router);
    if (!server.listen(static_cast<sc::u16>(port))) {
        std::fprintf(stderr, "ide: %s\n", server.error().c_str());
        return 1;
    }

    const unsigned bound = static_cast<unsigned>(server.port());
    std::printf("Scan Cycle IDE\n");
    std::printf("  http://127.0.0.1:%u/\n", bound);
    std::printf("  program %s\n", program_path.c_str());
    std::printf("  Ctrl+C stops the server\n\n");
    std::fflush(stdout);

    long served = 0;
    while (requests == 0 || served < requests) {
        if (!server.serve_one()) {
            std::fprintf(stderr, "ide: %s\n", server.error().c_str());
            return 1;
        }
        ++served;
    }
    return 0;
}
