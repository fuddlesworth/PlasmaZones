// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateGapOverrideCommand.h"

#include <KLocalizedString>

#include "../../EditorController.h"

using namespace PlasmaZones;

UpdateGapOverrideCommand::UpdateGapOverrideCommand(QPointer<EditorController> editorController,
                                                   GapType type, int oldValue, int newValue,
                                                   const QString& text, QUndoCommand* parent)
    : QUndoCommand(text.isEmpty()
                       ? (type == GapType::ZonePadding
                              ? i18nc("@action", "Change Zone Padding")
                              : i18nc("@action", "Change Edge Gap"))
                       : text,
                   parent)
    , m_editorController(editorController)
    , m_type(type)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

void UpdateGapOverrideCommand::undo()
{
    applyValue(m_oldValue);
}

void UpdateGapOverrideCommand::redo()
{
    applyValue(m_newValue);
}

void UpdateGapOverrideCommand::applyValue(int value)
{
    if (!m_editorController) {
        return;
    }

    switch (m_type) {
    case GapType::ZonePadding:
        m_editorController->setZonePaddingDirect(value);
        break;
    case GapType::OuterGap:
        m_editorController->setOuterGapDirect(value);
        break;
    }
}

bool UpdateGapOverrideCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }

    const auto* cmd = static_cast<const UpdateGapOverrideCommand*>(other);

    // Only merge if same gap type
    if (cmd->m_type != m_type) {
        return false;
    }

    // Merge: keep old value, update new value.
    // QUndoStack does not call redo() on the merged command, so we must apply
    // the new value to the model so it matches the merged state.
    m_newValue = cmd->m_newValue;
    applyValue(m_newValue);
    return true;
}
