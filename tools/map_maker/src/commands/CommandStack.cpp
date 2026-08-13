#include "mapmaker/commands/CommandStack.hpp"

#include <algorithm>
#include <utility>

namespace pr::mapmaker {
namespace {

class CompositeCommand final : public EditorCommand {
public:
    CompositeCommand(std::string label, std::vector<std::unique_ptr<EditorCommand>> commands)
        : label_(std::move(label)), commands_(std::move(commands)) {}

    const std::string& label() const override { return label_; }

    void apply() override {
        for (const auto& command : commands_) command->apply();
    }

    void revert() override {
        for (auto command = commands_.rbegin(); command != commands_.rend(); ++command) {
            (*command)->revert();
        }
    }

    bool isNoop() const override { return commands_.empty(); }

private:
    std::string label_;
    std::vector<std::unique_ptr<EditorCommand>> commands_;
};

void appendCoalescing(
    std::vector<std::unique_ptr<EditorCommand>>& commands,
    std::unique_ptr<EditorCommand> incoming) {
    if (!commands.empty() && commands.back()->coalesceWith(*incoming)) {
        if (commands.back()->isNoop()) commands.pop_back();
        return;
    }
    commands.push_back(std::move(incoming));
}

} // namespace

struct CommandStack::Impl {
    struct Entry {
        std::unique_ptr<EditorCommand> command;
        std::uint64_t before_revision = 0;
        std::uint64_t after_revision = 0;
    };

    struct Transaction {
        std::string label;
        std::vector<std::unique_ptr<EditorCommand>> commands;
        bool executed_anything = false;
    };

    explicit Impl(std::size_t limit) : maximum_entries(std::max<std::size_t>(1, limit)) {}

    void trimUndoHistory() {
        while (undo.size() > maximum_entries) undo.erase(undo.begin());
    }

    std::size_t maximum_entries = 512;
    std::vector<Entry> undo;
    std::vector<Entry> redo;
    std::unique_ptr<Transaction> transaction;
    std::uint64_t current_revision = 0;
    std::uint64_t clean_revision = 0;
    std::uint64_t next_revision = 1;
};

CommandStack::CommandStack(std::size_t maximum_entries)
    : impl_(std::make_unique<Impl>(maximum_entries)) {}

CommandStack::~CommandStack() = default;
CommandStack::CommandStack(CommandStack&&) noexcept = default;
CommandStack& CommandStack::operator=(CommandStack&&) noexcept = default;

bool CommandStack::execute(std::unique_ptr<EditorCommand> command) {
    if (!command || command->isNoop()) return false;
    command->apply();

    if (impl_->transaction) {
        impl_->transaction->executed_anything = true;
        appendCoalescing(impl_->transaction->commands, std::move(command));
        return true;
    }

    impl_->redo.clear();
    if (!impl_->undo.empty() && impl_->undo.back().command->coalesceWith(*command)) {
        Impl::Entry& previous = impl_->undo.back();
        if (previous.command->isNoop()) {
            impl_->current_revision = previous.before_revision;
            impl_->undo.pop_back();
        } else {
            previous.after_revision = impl_->next_revision++;
            impl_->current_revision = previous.after_revision;
        }
        return true;
    }

    const std::uint64_t after = impl_->next_revision++;
    impl_->undo.push_back({std::move(command), impl_->current_revision, after});
    impl_->current_revision = after;
    impl_->trimUndoHistory();
    return true;
}

bool CommandStack::undo() {
    if (impl_->transaction || impl_->undo.empty()) return false;
    Impl::Entry entry = std::move(impl_->undo.back());
    impl_->undo.pop_back();
    entry.command->revert();
    impl_->current_revision = entry.before_revision;
    impl_->redo.push_back(std::move(entry));
    return true;
}

bool CommandStack::redo() {
    if (impl_->transaction || impl_->redo.empty()) return false;
    Impl::Entry entry = std::move(impl_->redo.back());
    impl_->redo.pop_back();
    entry.command->apply();
    impl_->current_revision = entry.after_revision;
    impl_->undo.push_back(std::move(entry));
    impl_->trimUndoHistory();
    return true;
}

bool CommandStack::beginTransaction(std::string label) {
    if (impl_->transaction) return false;
    impl_->transaction = std::make_unique<Impl::Transaction>();
    impl_->transaction->label = label.empty() ? "Edit" : std::move(label);
    return true;
}

bool CommandStack::commitTransaction() {
    if (!impl_->transaction) return false;
    std::unique_ptr<Impl::Transaction> transaction = std::move(impl_->transaction);
    if (transaction->commands.empty()) return transaction->executed_anything;

    impl_->redo.clear();
    const std::uint64_t after = impl_->next_revision++;
    auto composite = std::make_unique<CompositeCommand>(
        std::move(transaction->label), std::move(transaction->commands));
    impl_->undo.push_back({std::move(composite), impl_->current_revision, after});
    impl_->current_revision = after;
    impl_->trimUndoHistory();
    return true;
}

bool CommandStack::cancelTransaction() {
    if (!impl_->transaction) return false;
    for (auto command = impl_->transaction->commands.rbegin();
         command != impl_->transaction->commands.rend(); ++command) {
        (*command)->revert();
    }
    impl_->transaction.reset();
    return true;
}

bool CommandStack::transactionOpen() const { return impl_->transaction != nullptr; }

void CommandStack::clear() {
    if (impl_->transaction) cancelTransaction();
    impl_->undo.clear();
    impl_->redo.clear();
}

void CommandStack::markClean() {
    if (!impl_->transaction) impl_->clean_revision = impl_->current_revision;
}

bool CommandStack::isDirty() const {
    return (impl_->transaction && impl_->transaction->executed_anything) ||
        impl_->current_revision != impl_->clean_revision;
}

bool CommandStack::canUndo() const { return !impl_->transaction && !impl_->undo.empty(); }
bool CommandStack::canRedo() const { return !impl_->transaction && !impl_->redo.empty(); }
std::size_t CommandStack::undoCount() const { return impl_->undo.size(); }
std::size_t CommandStack::redoCount() const { return impl_->redo.size(); }

std::string CommandStack::undoLabel() const {
    return canUndo() ? impl_->undo.back().command->label() : std::string{};
}

std::string CommandStack::redoLabel() const {
    return canRedo() ? impl_->redo.back().command->label() : std::string{};
}

std::uint64_t CommandStack::currentRevision() const { return impl_->current_revision; }

} // namespace pr::mapmaker
