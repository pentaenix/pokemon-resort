#pragma once

#include <string>

namespace pr::title_screen {

enum class SectionKind {
    Resort,
    Trade
};

class SectionScreenController {
public:
    SectionKind currentSection() const { return current_section_; }
    SectionKind pendingSection() const { return pending_section_; }

    void queueSection(SectionKind section);
    void commitPendingSection();
    void resetToResort();
    void selectTrade();
    std::string currentTitle() const;

private:
    SectionKind current_section_ = SectionKind::Resort;
    SectionKind pending_section_ = SectionKind::Resort;
};

} // namespace pr::title_screen
