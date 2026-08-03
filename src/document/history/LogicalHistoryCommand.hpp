#pragma once

#include "document/history/HistoryMemory.hpp"
#include "document/history/HistoryTypes.hpp"

#include <QUndoCommand>

namespace wobble
{
namespace history
{

// Base for every entry DocumentUndoStack owns.
//
// `preflight` must build the complete target state and return false if it
// cannot, before `redo`/`undo` touch anything; the stack only moves its cursor
// after a successful preflight, so a rejected movement leaves the document and
// its UI side effects untouched. `clearPreflight` releases whatever preflight
// staged, and must be safe to call whether or not preflight succeeded.
class LogicalHistoryCommand : public QUndoCommand
{
public:
    using QUndoCommand::QUndoCommand;

    virtual bool preflight(bool forward) = 0;
    virtual void clearPreflight() = 0;
    virtual StorageStats storageStats() const = 0;
    virtual void accountStorage(MemoryFootprint &footprint) const = 0;
};

}

}
