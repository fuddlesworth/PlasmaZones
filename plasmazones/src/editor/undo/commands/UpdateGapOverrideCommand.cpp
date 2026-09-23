// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateGapOverrideCommand.h"

#include "phosphor_i18n.h"

#include "../../EditorController.h"
#include "../../EditorGapsModel.h"

using namespace PlasmaZones;

UpdateGapOverrideCommand::UpdateGapOverrideCommand(QPointer<EditorController> editorController, GapType type,
                                                   int oldValue, int newValue, const QString& text,
                                                   QUndoCommand* parent)
    : QUndoCommand(text.isEmpty() ? (type == GapType::ZonePadding
                                         ? PhosphorI18n::tr("Change Zone Padding", "@action")
                                         : (type == GapType::UsePerSideOuterGap
                                                ? PhosphorI18n::tr("Toggle Per-Side Edge Gap", "@action")
                                                : (type == GapType::OverlayDisplayMode
                                                       ? PhosphorI18n::tr("Change Overlay Style", "@action")
                                                       : PhosphorI18n::tr("Change Edge Gap", "@action"))))
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

    // Zone-padding / edge-gap state moved to the gap sub-model; overlay display
    // mode is still owned by the controller directly.
    EditorGapsModel* gaps = m_editorController->gaps();

    switch (m_type) {
    case GapType::ZonePadding:
        gaps->setZonePaddingDirect(value);
        break;
    case GapType::OuterGap:
        gaps->setOuterGapDirect(value);
        break;
    case GapType::OuterGapTop:
        gaps->setOuterGapTopDirect(value);
        break;
    case GapType::OuterGapBottom:
        gaps->setOuterGapBottomDirect(value);
        break;
    case GapType::OuterGapLeft:
        gaps->setOuterGapLeftDirect(value);
        break;
    case GapType::OuterGapRight:
        gaps->setOuterGapRightDirect(value);
        break;
    case GapType::UsePerSideOuterGap:
        gaps->setUsePerSideOuterGapDirect(static_cast<bool>(value));
        break;
    case GapType::OverlayDisplayMode:
        m_editorController->setOverlayDisplayModeDirect(value);
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
