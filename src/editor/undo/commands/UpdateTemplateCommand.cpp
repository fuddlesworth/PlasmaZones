// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateTemplateCommand.h"

#include "../../EditorTemplateModel.h"

namespace PlasmaZones {

UpdateTemplateCommand::UpdateTemplateCommand(QPointer<EditorTemplateModel> model, const QVariantMap& oldState,
                                             const QVariantMap& newState, const QString& text, const QString& mergeKey,
                                             QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_model(model)
    , m_oldState(oldState)
    , m_newState(newState)
    , m_mergeKey(mergeKey)
{
}

void UpdateTemplateCommand::undo()
{
    if (m_model) {
        m_model->applyState(m_oldState);
    }
}

void UpdateTemplateCommand::redo()
{
    if (m_model) {
        m_model->applyState(m_newState);
    }
}

bool UpdateTemplateCommand::mergeWith(const QUndoCommand* other)
{
    // QUndoStack only calls mergeWith on equal non-negative ids, so after
    // the id guard the static_cast is safe — the guard-then-cast shape every
    // sibling command uses (see UpdateGapOverrideCommand).
    if (!other || other->id() != id()) {
        return false;
    }
    const auto* cmd = static_cast<const UpdateTemplateCommand*>(other);
    if (m_mergeKey.isEmpty() || cmd->m_mergeKey != m_mergeKey) {
        return false;
    }
    // Keep this command's old state (the gesture's origin) and take the
    // newer command's new state (the latest position).
    m_newState = cmd->m_newState;
    // A gesture that returns exactly to its origin (held right then left
    // back to the start) collapses to a no-op; marking the merged entry
    // obsolete lets push() drop it instead of leaving a dead undo step. The
    // incoming command's redo() already ran, so the model holds the origin
    // state either way.
    if (m_newState == m_oldState) {
        setObsolete(true);
    }
    return true;
}

} // namespace PlasmaZones
