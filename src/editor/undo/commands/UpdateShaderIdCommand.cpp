// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateShaderIdCommand.h"

#include <KLocalizedString>

#include "../../EditorController.h"
#include "CommandId.h"

using namespace PlasmaZones;

UpdateShaderIdCommand::UpdateShaderIdCommand(QPointer<EditorController> editorController, const QString& oldId,
                                             const QString& newId, const QString& text, QUndoCommand* parent)
    : QUndoCommand(text.isEmpty() ? i18nc("@action", "Change Shader Effect") : text, parent)
    , m_editorController(editorController)
    , m_oldId(oldId)
    , m_newId(newId)
{
}

void UpdateShaderIdCommand::undo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->setCurrentShaderIdDirect(m_oldId);
}

void UpdateShaderIdCommand::redo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->setCurrentShaderIdDirect(m_newId);
}

bool UpdateShaderIdCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }

    const auto* cmd = static_cast<const UpdateShaderIdCommand*>(other);

    // Merge: keep old ID, update new ID
    // QUndoStack does not call redo() on the merged command, so we must apply
    // the new value to the model so it matches the merged state.
    UpdateShaderIdCommand* self = const_cast<UpdateShaderIdCommand*>(this);
    self->m_newId = cmd->m_newId;
    if (m_editorController) {
        m_editorController->setCurrentShaderIdDirect(m_newId);
    }
    return true;
}
