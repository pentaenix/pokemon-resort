#include "mapmaker/logging/StructuredLogger.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) throw TestFailure(message);
}

fs::path temporaryRoot(const std::string& label) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path path = fs::temp_directory_path() /
        ("pokemon_resort_logger_" + label + "_" + std::to_string(suffix));
    fs::create_directories(path);
    return path;
}

std::string readText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void testWritesStructuredJsonAndEscapesMessages() {
    const fs::path root = temporaryRoot("json");
    {
        pr::mapmaker::StructuredLogger logger({root, "editor.log", 4096, 2, 10});
        expect(logger.log(pr::mapmaker::LogLevel::Warning, "save", "line one\n\"quoted\""),
            "structured record writes");
        logger.flush();
        expect(logger.lastError().empty(), "successful write clears logger error");
    }
    const std::string contents = readText(root / "editor.log");
    expect(contents.find("\"level\":\"warning\"") != std::string::npos,
        "record contains stable level field");
    expect(contents.find("\"category\":\"save\"") != std::string::npos,
        "record contains category field");
    expect(contents.find("line one\\n\\\"quoted\\\"") != std::string::npos,
        "message is JSON escaped on one line");
    expect(contents.find("\"timestamp\":\"") != std::string::npos,
        "record contains UTC timestamp");
    fs::remove_all(root);
}

void testRotationAndMemoryRingAreBounded() {
    const fs::path root = temporaryRoot("rotation");
    {
        pr::mapmaker::StructuredLogger logger({root, "map_maker.log", 170, 2, 2});
        for (int index = 0; index < 8; ++index) {
            expect(logger.log(pr::mapmaker::LogLevel::Info, "edit",
                "record-" + std::to_string(index) + "-with-enough-payload-to-rotate"),
                "rotating logger keeps accepting records");
        }
        logger.flush();
        const auto recent = logger.recent();
        expect(recent.size() == 2U, "memory ring retains configured number of records");
        expect(recent.front().message.find("record-6") != std::string::npos &&
            recent.back().message.find("record-7") != std::string::npos,
            "memory ring retains newest records");
    }
    expect(fs::is_regular_file(root / "map_maker.log"), "current log exists after rotation");
    expect(fs::is_regular_file(root / "map_maker.1.log"), "first retained log exists");
    expect(fs::is_regular_file(root / "map_maker.2.log"), "second retained log exists");
    expect(!fs::exists(root / "map_maker.3.log"), "rotation does not exceed retention limit");
    fs::remove_all(root);
}

void testConcurrentWritersRemainUsable() {
    const fs::path root = temporaryRoot("threads");
    {
        pr::mapmaker::StructuredLogger logger({root, "threads.log", 1024 * 1024, 1, 200});
        std::vector<std::thread> writers;
        for (int worker = 0; worker < 4; ++worker) {
            writers.emplace_back([&logger, worker] {
                for (int index = 0; index < 25; ++index) {
                    logger.log(pr::mapmaker::LogLevel::Debug, "worker",
                        std::to_string(worker) + ":" + std::to_string(index));
                }
            });
        }
        for (std::thread& writer : writers) writer.join();
        logger.flush();
        expect(logger.recent().size() == 100U, "mutex-protected ring receives every thread record");
        expect(logger.lastError().empty(), "concurrent writes leave no file error");
    }
    std::size_t lines = 0;
    for (char character : readText(root / "threads.log")) {
        if (character == '\n') ++lines;
    }
    expect(lines == 100U, "concurrent writes emit one JSON line per record");
    fs::remove_all(root);
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"writes structured JSON and escapes", testWritesStructuredJsonAndEscapesMessages},
        {"rotation and memory ring bounded", testRotationAndMemoryRingAreBounded},
        {"concurrent writers remain usable", testConcurrentWritersRemainUsable},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
