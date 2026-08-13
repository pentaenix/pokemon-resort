#include "mapmaker/document/DocumentCommands.hpp"

#include <utility>

namespace pr::mapmaker {

OwmapMutationCommand::OwmapMutationCommand(
    OwmapDocument& document,
    std::string label,
    Mutation mutation,
    std::function<void()> after_change)
    : document_(&document),
      label_(std::move(label)),
      before_(document.serialize()),
      after_change_(std::move(after_change)) {
    OwmapDocument candidate = OwmapDocument::fromBytes(before_);
    mutation(candidate);
    after_ = candidate.serialize();
}

void OwmapMutationCommand::apply() {
    *document_ = OwmapDocument::fromBytes(after_);
    if (after_change_) after_change_();
}

void OwmapMutationCommand::revert() {
    *document_ = OwmapDocument::fromBytes(before_);
    if (after_change_) after_change_();
}

std::unique_ptr<EditorCommand> makeOwmapMutationCommand(
    OwmapDocument& document,
    std::string label,
    OwmapMutationCommand::Mutation mutation,
    std::function<void()> after_change) {
    return std::make_unique<OwmapMutationCommand>(
        document, std::move(label), std::move(mutation), std::move(after_change));
}

} // namespace pr::mapmaker
