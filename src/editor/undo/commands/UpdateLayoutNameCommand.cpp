// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateLayoutNameCommand.h"
#include "../../EditorController.h"
#include "CommandId.h"
#include <KLocalizedString>

using namespace PlasmaZones;

UpdateLayoutNameCommand::UpdateLayoutNameCommand(QPointer<EditorController> editorController, const QString& oldName,
                                                 const QString& newName, const QString& text, QUndoCommand* parent)
    : QUndoCommand(text.isEmpty() ? i18nc("@action", "Rename Layout") : text, parent)
    , m_editorController(editorController)
    , m_oldName(oldName)
    , m_newName(newName)
{
}

void UpdateLayoutNameCommand::undo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->setLayoutNameDirect(m_oldName);
}

void UpdateLayoutNameCommand::redo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->setLayoutNameDirect(m_newName);
}

bool UpdateLayoutNameCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }

    const auto* cmd = static_cast<const UpdateLayoutNameCommand*>(other);

    // Merge: keep old name, update new name
    const_cast<UpdateLayoutNameCommand*>(this)->m_newName = cmd->m_newName;
    return true;
}
