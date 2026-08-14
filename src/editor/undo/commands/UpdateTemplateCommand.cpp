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
    const auto* cmd = dynamic_cast<const UpdateTemplateCommand*>(other);
    if (!cmd || m_mergeKey.isEmpty() || cmd->m_mergeKey != m_mergeKey) {
        return false;
    }
    // Keep this command's old state (the gesture's origin) and take the
    // newer command's new state (the latest drag position).
    m_newState = cmd->m_newState;
    return true;
}

} // namespace PlasmaZones
