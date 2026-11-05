#include "app/grade_log.hpp"
#include "app/grades_csv.hpp"
#include "app/grades_page.hpp"
#include "app/grades_pager.hpp"
#include "app/lesson_api.hpp"
#include "app/term_logs.hpp"
#include "lesson/lesson.hpp"
#include "net/http.hpp"
#include "net/http_server.hpp"
#include "net/portal_server.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

// The teacher portal. The router is pure request-to-response mapping and lives
// in sc_net; this binary binds it to loopback and prints the addresses a teacher
// can open. Ctrl+C stops it.
int main(int argc, char** argv) {
    unsigned port = 8080;
    long requests = 0;  // 0 serves until the process is stopped
    std::string records_path = "scancycle-grades.tsv";
    bool records_given = false;
    // Term mode: every <term>.tsv in one directory, with `active_term` taking the
    // new attempts. Empty logs_path keeps the original single-file behaviour.
    std::string logs_path;
    std::string active_term = "current";
    bool term_given = false;

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
            records_given = true;
            if (records_path.empty()) {
                std::fprintf(stderr, "portal: --records= needs a file path\n");
                return 2;
            }
        } else if (arg.rfind("--logs=", 0) == 0) {
            logs_path = arg.substr(7);
            if (logs_path.empty()) {
                std::fprintf(stderr, "portal: --logs= needs a directory path\n");
                return 2;
            }
        } else if (arg.rfind("--term=", 0) == 0) {
            active_term = arg.substr(7);
            term_given = true;
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    if (!sc::app::valid_term(active_term)) {
        std::fprintf(stderr, "portal: '%s' is not a term name\n", active_term.c_str());
        return 2;
    }
    if (!logs_path.empty() && records_given) {
        std::fprintf(stderr, "portal: --logs and --records cannot be combined\n");
        return 2;
    }
    if (logs_path.empty() && term_given) {
        std::fprintf(stderr, "portal: --term needs --logs\n");
        return 2;
    }

    const std::vector<sc::lesson::Lesson> lessons = sc::lesson::make_builtin_lessons();
    // The coverage table needs the catalog, not just what the log remembers, so a
    // lesson nobody has tried still appears as a column of dashes.
    std::vector<std::string> lesson_ids;
    lesson_ids.reserve(lessons.size());
    for (const sc::lesson::Lesson& one : lessons) {
        lesson_ids.push_back(one.id);
    }

    // Term mode keeps the directory's terms in memory; the active term can be
    // switched from the page, which is what starting a new class needs. A file
    // created outside the portal appears on the next restart.
    std::optional<sc::app::TermLogs> term_logs;
    sc::app::GradeLog single_log(records_path);
    if (!logs_path.empty()) {
        std::filesystem::path logs_directory = logs_path;
        std::error_code error;
        std::filesystem::create_directories(logs_directory, error);
        if (error) {
            std::fprintf(stderr, "portal: could not create '%s'\n", logs_path.c_str());
            return 1;
        }
        term_logs.emplace(logs_directory, active_term);
    }

    const auto log_for = [&](const std::string& requested) {
        if (term_logs) {
            return term_logs->log(requested);
        }
        return single_log;
    };
    const auto term_for = [&](const std::string& requested) {
        return term_logs ? term_logs->resolve(requested) : std::string();
    };
    const auto term_list = [&]() -> std::span<const std::string> {
        return term_logs ? std::span<const std::string>(term_logs->terms())
                         : std::span<const std::string>();
    };
    const auto active_name = [&]() {
        return term_logs ? term_logs->active() : std::string();
    };

    sc::net::PortalRouter router = sc::net::make_default_router(lessons);
    // A grade always writes to the term that is active at that moment, whichever
    // term a teacher happens to be reading.
    router.route(sc::net::HttpMethod::Post, "/api/grade",
                 [&lessons, &log_for](const sc::net::Request& request) {
                     sc::app::GradeLog log = log_for("");
                     return sc::app::grade_lesson(lessons, request, &log);
                 });

    router.route(sc::net::HttpMethod::Post, "/grade",
                 [&lessons, &log_for](const sc::net::Request& request) {
                     sc::app::GradeLog log = log_for("");
                     return sc::app::grade_lesson_page(lessons, request, &log);
                 });

    // Starting a new term and switching to it is one action, so a new class
    // becomes a new file without restarting the portal. A name that cannot be a
    // term is refused rather than silently ignored.
    if (term_logs) {
        router.route(sc::net::HttpMethod::Post, "/grades/active",
                     [&](const sc::net::Request& request) {
                         const std::string wanted = sc::net::form_field(request.body, "term");
                         if (!term_logs->set_active(wanted)) {
                             sc::net::Response response;
                             response.status = 400;
                             response.content_type = "text/html; charset=utf-8";
                             response.body =
                                 sc::net::page_head("Scan Cycle term") +
                                 "<h1>Could not switch term</h1><p><code>" +
                                 sc::net::html_escape(wanted) +
                                 "</code> is not a term name. Use letters, digits, dots, "
                                 "dashes or underscores.</p><p><a href=\"/grades\">Back to the "
                                 "log</a></p></main></body></html>";
                             return response;
                         }
                         sc::app::GradeLog log = term_logs->log("");
                         return sc::app::grades_page(log, "", "", 1, lesson_ids, "",
                                                     term_logs->active(), term_logs->terms(),
                                                     term_logs->active());
                     });
    }

    // `?lesson=<id>`, `?student=<name>` and `?result=pass|fail` narrow the view;
    // an absent field is the whole log, and `?term=` reads another term's file.
    router.route(sc::net::HttpMethod::Get, "/grades",
                 [&](const sc::net::Request& request) {
                     const std::string lesson = sc::net::form_field(request.query, "lesson");
                     const std::string student = sc::net::form_field(request.query, "student");
                     const std::string result = sc::net::form_field(request.query, "result");
                     const std::string requested = sc::net::form_field(request.query, "term");
                     const std::size_t page = sc::app::parse_attempt_page(
                         sc::net::form_field(request.query, "page"));
                     sc::app::GradeLog log = log_for(requested);
                     return sc::app::grades_page(log, lesson, student, page, lesson_ids, result,
                                                 term_for(requested), term_list(),
                                                 active_name());
                 });

    router.route(sc::net::HttpMethod::Get, "/grades.csv",
                 [&](const sc::net::Request& request) {
                     const std::string lesson = sc::net::form_field(request.query, "lesson");
                     const std::string student = sc::net::form_field(request.query, "student");
                     const std::string result = sc::net::form_field(request.query, "result");
                     sc::app::GradeLog log = log_for(sc::net::form_field(request.query, "term"));
                     return sc::app::grades_csv(log, lesson, student, result);
                 });

    // The log grows while the server runs, so one route per attempt cannot be
    // registered at startup; the prefix route resolves the ordinal per request.
    router.route_prefix(sc::net::HttpMethod::Get, "/grades/",
                        [&](const sc::net::Request& request) {
                            const std::string lesson = sc::net::form_field(request.query, "lesson");
                            const std::string student = sc::net::form_field(request.query, "student");
                            const std::string result = sc::net::form_field(request.query, "result");
                            const std::string requested = sc::net::form_field(request.query, "term");
                            sc::app::GradeLog log = log_for(requested);
                            return sc::app::attempt_page(log, request.path.substr(8), lesson,
                                                         student, result, term_for(requested));
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
