#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pr::mapmaker {

class EditorCommand {
public:
    virtual ~EditorCommand() = default;

    virtual const std::string& label() const = 0;
    virtual void apply() = 0;
    virtual void revert() = 0;
    virtual bool isNoop() const { return false; }
    virtual bool coalesceWith(const EditorCommand&) { return false; }
};

class CommandStack {
public:
    explicit CommandStack(std::size_t maximum_entries = 512);
    ~CommandStack();

    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;
    CommandStack(CommandStack&&) noexcept;
    CommandStack& operator=(CommandStack&&) noexcept;

    bool execute(std::unique_ptr<EditorCommand> command);
    bool undo();
    bool redo();

    bool beginTransaction(std::string label);
    bool commitTransaction();
    bool cancelTransaction();
    bool transactionOpen() const;

    void clear();
    void markClean();
    bool isDirty() const;

    bool canUndo() const;
    bool canRedo() const;
    std::size_t undoCount() const;
    std::size_t redoCount() const;
    std::string undoLabel() const;
    std::string redoLabel() const;
    std::uint64_t currentRevision() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pr::mapmaker
