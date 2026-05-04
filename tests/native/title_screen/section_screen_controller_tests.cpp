#include "ui/title_screen/SectionScreenController.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using pr::title_screen::SectionKind;
    using pr::title_screen::SectionScreenController;

    SectionScreenController sections;
    expect(sections.currentSection() == SectionKind::Resort, "default current section is RESORT");
    expect(sections.pendingSection() == SectionKind::Resort, "default pending section is RESORT");
    expect(sections.currentTitle() == "RESORT", "default title is RESORT");

    sections.queueSection(SectionKind::Trade);
    expect(sections.currentSection() == SectionKind::Resort, "queueing TRADE does not change current section before fade commit");
    expect(sections.pendingSection() == SectionKind::Trade, "queueing TRADE updates pending section");
    expect(sections.currentTitle() == "RESORT", "queued section should not affect current title before commit");

    sections.commitPendingSection();
    expect(sections.currentSection() == SectionKind::Trade, "committing pending section enters TRADE");
    expect(sections.currentTitle() == "TRADE", "TRADE title is exposed after commit");

    sections.resetToResort();
    expect(sections.currentSection() == SectionKind::Resort, "reset returns current section to RESORT");
    expect(sections.pendingSection() == SectionKind::Resort, "reset returns pending section to RESORT");

    sections.selectTrade();
    expect(sections.currentSection() == SectionKind::Trade, "selectTrade sets current section to TRADE");
    expect(sections.pendingSection() == SectionKind::Trade, "selectTrade keeps pending section aligned");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
