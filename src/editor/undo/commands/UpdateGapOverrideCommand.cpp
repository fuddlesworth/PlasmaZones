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
                              : (type == GapType::UsePerSideOuterGap
                                     ? i18nc("@action", "Toggle Per-Side Edge Gap")
                                     : i18nc("@action", "Change Edge Gap")))
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
    case GapType::OuterGapTop:
        m_editorController->setOuterGapTopDirect(value);
        break;
    case GapType::OuterGapBottom:
        m_editorController->setOuterGapBottomDirect(value);
        break;
    case GapType::OuterGapLeft:
        m_editorController->setOuterGapLeftDirect(value);
        break;
    case GapType::OuterGapRight:
        m_editorController->setOuterGapRightDirect(value);
        break;
    case GapType::UsePerSideOuterGap:
        m_editorController->setUsePerSideOuterGapDirect(static_cast<bool>(value));
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

    // Merge: keep our old value (for undo), adopt the other command's new value.
    // Qt6 calls redo() on the incoming command before mergeWith() in both the
    // normal push path and inside beginMacro/endMacro blocks, so the model
    // already reflects cmd->m_newValue. We only need to update our stored value.
    m_newValue = cmd->m_newValue;
    return true;
}
