#include "mapmaker/document/DocumentCommands.hpp"

#include <iostream>
#include <stdexcept>

int main() {
    try {
        auto document = pr::mapmaker::OwmapDocument::create(
            2, 2, 16.0f, pr::parseJsonText(R"({"id":"test","unknown":null})"));
        pr::mapmaker::CommandStack commands;
        int changes = 0;
        const bool executed = commands.execute(pr::mapmaker::makeOwmapMutationCommand(
            document, "Raise terrain", [](auto& candidate) { candidate.heightAt(1, 1) = 7; },
            [&changes] { ++changes; }));
        if (!executed || document.heightAt(1, 1) != 7 || changes != 1) {
            throw std::runtime_error("document mutation should apply exactly once");
        }
        if (!commands.undo() || document.heightAt(1, 1) != 0 || changes != 2) {
            throw std::runtime_error("undo should restore the exact original document");
        }
        if (!commands.redo() || document.heightAt(1, 1) != 7 ||
            !document.metadata().get("unknown")->isNull() || changes != 3) {
            throw std::runtime_error("redo should restore edit without dropping unknown metadata");
        }
        std::cout << "document_commands_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "document_commands_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
