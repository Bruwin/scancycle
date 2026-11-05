#include "app/grade_log.hpp"
#include "app/grades_csv.hpp"
#include "app/grades_page.hpp"
#include "app/grades_pager.hpp"
#include "app/lesson_api.hpp"
#include "lesson/lesson.hpp"
#include "net/http.hpp"
#include "net/http_server.hpp"
#include "net/portal_server.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// The teacher portal. The router is pure request-to-response mapping and lives
// in sc_net; this binary binds it to loopback and prints the addresses a teacher
// can open. Ctrl+C stops it.
int main(int argc, char** argv) {
    unsigned port = 8080;
    long requests = 0;  // 0 serves until the process is stopped
    std::string records_path = "scancycle-grades.tsv";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--port=", 0) == 0) {
            const long value = std::strtol(arg.c_str() + 7, nullptr, 10);
            if (value <= 0 || value > 65535) {
                std::fprintf(stderr, "portal: '%s' is not a port\n", arg.c_str());
                return 2;
            }
            port = static_cast<unsigned>(value);
        } else if (arg.rfind("--requests=", 0) == 0) {
            requests = std::strtol(arg.c_str() + 11, nullptr, 10);
        } else if (arg.rfind("--records=", 0) == 0) {
            records_path = arg.substr(10);
            if (records_path.empty()) {
                std::fprintf(stderr, "portal: --records= needs a file path\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    const std::vector<sc::lesson::Lesson> lessons = sc::lesson::make_builtin_lessons();
    // The coverage table needs the catalog, not just what the log remembers, so a
    // lesson nobody has tried still appears as a column of dashes.
    std::vector<std::string> lesson_ids;
    lesson_ids.reserve(lessons.size());
    for (const sc::lesson::Lesson& one : lessons) {
        lesson_ids.push_back(one.id);
    }
    sc::app::GradeLog grade_log(records_path);

    sc::net::PortalRouter router = sc::net::make_default_router(lessons);
    router.route(sc::net::HttpMethod::Post, "/api/grade",
                 [&lessons, &grade_log](const sc::net::Request& request) {
                     return sc::app::grade_lesson(lessons, request, &grade_log);
                 });

    router.route(sc::net::HttpMethod::Post, "/grade",
                 [&lessons, &grade_log](const sc::net::Request& request) {
                     return sc::app::grade_lesson_page(lessons, request, &grade_log);
                 });

    // `?lesson=<id>` and `?student=<name>` narrow either view; an absent field is
    // the whole log, an empty value is treated as absent, and both together
    // narrow further.
    router.route(sc::net::HttpMethod::Get, "/grades",
                 [&grade_log, &lesson_ids](const sc::net::Request& request) {
                     const std::string lesson = sc::net::form_field(request.query, "lesson");
                     const std::string student = sc::net::form_field(request.query, "student");
                     const std::size_t page = sc::app::parse_attempt_page(
                         sc::net::form_field(request.query, "page"));
                     return sc::app::grades_page(grade_log, lesson, student, page, lesson_ids);
                 });

    router.route(sc::net::HttpMethod::Get, "/grades.csv",
                 [&grade_log](const sc::net::Request& request) {
                     const std::string lesson = sc::net::form_field(request.query, "lesson");
                     const std::string student = sc::net::form_field(request.query, "student");
                     return sc::app::grades_csv(grade_log, lesson, student);
                 });

    // The log grows while the server runs, so one route per attempt cannot be
    // registered at startup; the prefix route resolves the ordinal per request.
    router.route_prefix(sc::net::HttpMethod::Get, "/grades/",
                        [&grade_log](const sc::net::Request& request) {
                            const std::string lesson = sc::net::form_field(request.query, "lesson");
                            const std::string student = sc::net::form_field(request.query, "student");
                            return sc::app::attempt_page(grade_log, request.path.substr(8), lesson,
                                                         student);
                        });

    sc::net::HttpServer server(router);
    if (!server.listen(static_cast<sc::u16>(port))) {
        std::fprintf(stderr, "portal: %s\n", server.error().c_str());
        return 1;
    }

    const unsigned bound = static_cast<unsigned>(server.port());
    std::printf("Scan Cycle teacher portal\n");
    std::printf("  http://127.0.0.1:%u/\n", bound);
    std::printf("  http://127.0.0.1:%u/api/health\n", bound);
    std::printf("  http://127.0.0.1:%u/api/lessons\n", bound);
    std::printf("  POST http://127.0.0.1:%u/api/grade  (JSON)\n", bound);
    std::printf("  POST http://127.0.0.1:%u/grade      (page)\n", bound);
    std::printf("  http://127.0.0.1:%u/grades   (printable log)\n", bound);
    std::printf("  http://127.0.0.1:%u/grades/<n> (one attempt)\n", bound);
    std::printf("  http://127.0.0.1:%u/grades.csv (spreadsheet)\n", bound);
    std::printf("  records %s\n\n", records_path.c_str());
    for (const sc::lesson::Lesson& lesson : lessons) {
        std::printf("  lesson %-20s %s\n", lesson.id.c_str(), lesson.title.c_str());
    }
    std::printf("\n");
    std::fflush(stdout);

    long served = 0;
    while (requests == 0 || served < requests) {
        if (!server.serve_one()) {
            std::fprintf(stderr, "portal: %s\n", server.error().c_str());
            return 1;
        }
        ++served;
    }
    return 0;
}
