// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateShaderParamsCommand.h"

#include <KLocalizedString>

#include "../../EditorController.h"
#include "CommandId.h"

using namespace PlasmaZones;

UpdateShaderParamsCommand::UpdateShaderParamsCommand(QPointer<EditorController> editorController,
                                                     const QString& paramKey, const QVariant& oldValue,
                                                     const QVariant& newValue, const QString& text,
                                                     QUndoCommand* parent)
    : QUndoCommand(text.isEmpty() ? i18nc("@action", "Change Shader Parameter") : text, parent)
    , m_editorController(editorController)
    , m_isSingleParam(true)
    , m_paramKey(paramKey)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

UpdateShaderParamsCommand::UpdateShaderParamsCommand(QPointer<EditorController> editorController,
                                                     const QVariantMap& oldParams, const QVariantMap& newParams,
                                                     const QString& text, QUndoCommand* parent)
    : QUndoCommand(text.isEmpty() ? i18nc("@action", "Change Shader Parameters") : text, parent)
    , m_editorController(editorController)
    , m_isSingleParam(false)
    , m_oldParams(oldParams)
    , m_newParams(newParams)
{
}

void UpdateShaderParamsCommand::undo()
{
    if (!m_editorController) {
        return;
    }

    if (m_isSingleParam) {
        m_editorController->setShaderParameterDirect(m_paramKey, m_oldValue);
    } else {
        m_editorController->setCurrentShaderParamsDirect(m_oldParams);
    }
}

void UpdateShaderParamsCommand::redo()
{
    if (!m_editorController) {
        return;
    }

    if (m_isSingleParam) {
        m_editorController->setShaderParameterDirect(m_paramKey, m_newValue);
    } else {
        m_editorController->setCurrentShaderParamsDirect(m_newParams);
    }
}

bool UpdateShaderParamsCommand::mergeWith(const QUndoCommand* other)
{
    // Only single-param mode supports merging
    if (!m_isSingleParam) {
        return false;
    }

    if (other->id() != id()) {
        return false;
    }

    const auto* cmd = static_cast<const UpdateShaderParamsCommand*>(other);

    // Only merge if same parameter key
    if (!cmd->m_isSingleParam || cmd->m_paramKey != m_paramKey) {
        return false;
    }

    // Merge: keep old value, update new value
    // QUndoStack does not call redo() on the merged command, so we must apply
    // the new value to the model so it matches the merged state.
    UpdateShaderParamsCommand* self = const_cast<UpdateShaderParamsCommand*>(this);
    self->m_newValue = cmd->m_newValue;
    if (m_editorController) {
        m_editorController->setShaderParameterDirect(m_paramKey, m_newValue);
    }
    return true;
}
