#include "harness.hpp"

#include "app/grade_events.hpp"
#include "app/grade_log.hpp"
#include "app/grades_page.hpp"
#include "app/lesson_api.hpp"
#include "lesson/lesson.hpp"
#include "net/http_server.hpp"
#include "net/websocket.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

using namespace sc;

TEST(websocket_accept_key_matches_the_rfc_example) {
    CHECK(net::websocket_accept_key("dGhlIHNhbXBsZSBub25jZQ==") ==
          "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(websocket_text_frame_marks_a_final_text_frame) {
    const std::string frame = net::websocket_text_frame("hi");
    const char expected[] = {static_cast<char>(0x81), 2, 'h', 'i'};
    CHECK(frame == std::string(expected, sizeof(expected)));
}

TEST(websocket_text_frame_uses_the_extended_length_for_long_payloads) {
    const std::string frame = net::websocket_text_frame(std::string(130, 'a'));
    CHECK(frame.size() == 134);
    CHECK(static_cast<unsigned char>(frame[0]) == 0x81);
    CHECK(static_cast<unsigned char>(frame[1]) == 126);
    CHECK(static_cast<unsigned char>(frame[2]) == 0);
    CHECK(static_cast<unsigned char>(frame[3]) == 130);
}

TEST(grade_event_is_small_and_escaped) {
    app::GradeRecord record;
    record.at = 1700000000;
    record.lesson_id = "press\"batch";
    record.student = "Ada\nLovelace";
    record.passed = true;
    record.passed_count = 3;
    record.total = 4;

    const std::string json = app::grade_recorded_json(record);
    CHECK(json.find("\"type\":\"grade\"") != std::string::npos);
    CHECK(json.find("press\\\"batch") != std::string::npos);
    CHECK(json.find("Ada\\nLovelace") != std::string::npos);
    CHECK(json.find("\"passed\":true") != std::string::npos);
    CHECK(json.find("\"passed_count\":3") != std::string::npos);

    const std::string term = app::term_changed_json("fall\nterm");
    CHECK(term == "{\"type\":\"term\",\"term\":\"fall\\nterm\"}");
}

TEST(grades_page_offers_a_live_channel) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-live-page-test.tsv";
    std::filesystem::remove(path);
    const app::GradeLog log(path.string());

    const net::Response page = app::grades_page(log);
    CHECK(page.status == 200);
    CHECK(page.body.find("id=\"live-status\"") != std::string::npos);
    CHECK(page.body.find("/api/live") != std::string::npos);
    CHECK(page.body.find("new WebSocket") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(grade_route_publishes_only_after_a_recorded_attempt) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-live-event-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/grade";
    request.body = "lesson=press-batch&student=Ada";

    int events = 0;
    std::string observed;
    const net::Response page = app::grade_lesson_page(
        lessons, request, &log, [&](const app::GradeRecord& record) {
            ++events;
            observed = record.student;
        });

    CHECK(page.status == 200);
    CHECK(events == 1);
    CHECK(observed == "Ada");

    std::filesystem::remove(path);
}

TEST(websocket_server_accepts_the_rfc_handshake_and_broadcasts) {
    net::WebSocketServer server;
    const bool listening = server.listen(0);
    CHECK(listening);
    if (!listening) {
        return;
    }

    const std::string request =
        "GET /live HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    std::string answer;
    std::string error;
    std::thread client([&] { answer = net::http_round_trip(server.port(), request, error); });

    for (int attempt = 0; attempt < 200 && server.client_count() == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(server.client_count() == 1);
    server.broadcast("hello");
    server.stop();
    client.join();

    CHECK(server.client_count() == 0);
    CHECK(error.empty());
    CHECK(answer.find("HTTP/1.1 101 Switching Protocols") != std::string::npos);
    CHECK(answer.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") !=
          std::string::npos);
    const char expected[] = {static_cast<char>(0x81), 5, 'h', 'e', 'l', 'l', 'o'};
    CHECK(answer.find(std::string(expected, sizeof(expected))) != std::string::npos);
}
