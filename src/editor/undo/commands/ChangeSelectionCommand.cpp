// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChangeSelectionCommand.h"
#include "CommandId.h"
#include "../../EditorController.h"

#include <KLocalizedString>

namespace PlasmaZones {

ChangeSelectionCommand::ChangeSelectionCommand(QPointer<EditorController> controller, const QStringList& oldSelection,
                                               const QStringList& newSelection, const QString& text)
    : QUndoCommand(text.isEmpty() ? i18nc("@action", "Change Selection") : text)
    , m_controller(controller)
    , m_oldSelection(oldSelection)
    , m_newSelection(newSelection)
{
}

void ChangeSelectionCommand::undo()
{
    if (!m_controller) {
        return;
    }
    m_controller->setSelectedZoneIdsDirect(m_oldSelection);
}

void ChangeSelectionCommand::redo()
{
    // Skip first redo - selection was already changed when command was created
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }

    if (!m_controller) {
        return;
    }
    m_controller->setSelectedZoneIdsDirect(m_newSelection);
}

int ChangeSelectionCommand::id() const
{
    return CommandId::ChangeSelection;
}

bool ChangeSelectionCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }

    const auto* otherCmd = static_cast<const ChangeSelectionCommand*>(other);

    // Merge by taking the new selection from the other command
    // This collapses rapid selection changes into one undo step
    m_newSelection = otherCmd->m_newSelection;

    return true;
}

} // namespace PlasmaZones
