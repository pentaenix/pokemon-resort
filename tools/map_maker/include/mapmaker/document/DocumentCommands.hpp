#pragma once

#include "mapmaker/commands/CommandStack.hpp"
#include "mapmaker/document/OwmapDocument.hpp"

#include <functional>
#include <string>

namespace pr::mapmaker {

// Lossless document command. It snapshots the encoded OWMAP envelope before
// and after one gesture, so undo restores terrain and arbitrary metadata with
// no schema-specific reconstruction.
class OwmapMutationCommand final : public EditorCommand {
public:
    using Mutation = std::function<void(OwmapDocument&)>;

    OwmapMutationCommand(
        OwmapDocument& document,
        std::string label,
        Mutation mutation,
        std::function<void()> after_change = {});

    const std::string& label() const override { return label_; }
    void apply() override;
    void revert() override;
    bool isNoop() const override { return before_ == after_; }

private:
    OwmapDocument* document_ = nullptr;
    std::string label_;
    std::vector<std::uint8_t> before_;
    std::vector<std::uint8_t> after_;
    std::function<void()> after_change_;
};

std::unique_ptr<EditorCommand> makeOwmapMutationCommand(
    OwmapDocument& document,
    std::string label,
    OwmapMutationCommand::Mutation mutation,
    std::function<void()> after_change = {});

} // namespace pr::mapmaker
