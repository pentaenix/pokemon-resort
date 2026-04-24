#include "ui/title_screen/SectionScreenController.hpp"

namespace pr::title_screen {

void SectionScreenController::queueSection(SectionKind section) {
    pending_section_ = section;
}

void SectionScreenController::commitPendingSection() {
    current_section_ = pending_section_;
}

void SectionScreenController::resetToResort() {
    current_section_ = SectionKind::Resort;
    pending_section_ = SectionKind::Resort;
}

void SectionScreenController::selectTrade() {
    current_section_ = SectionKind::Trade;
    pending_section_ = SectionKind::Trade;
}

std::string SectionScreenController::currentTitle() const {
    switch (current_section_) {
        case SectionKind::Resort:
            return "RESORT";
        case SectionKind::Trade:
            return "TRADE";
    }
    return "SECTION";
}

} // namespace pr::title_screen
