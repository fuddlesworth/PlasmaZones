// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateFixedGeometryUnitCommand.h"

#include "phosphor_i18n.h"

#include "../../EditorController.h"

using namespace PlasmaZones;

UpdateFixedGeometryUnitCommand::UpdateFixedGeometryUnitCommand(QPointer<EditorController> editorController,
                                                               bool oldValue, bool newValue, const QString& text,
                                                               QUndoCommand* parent)
    : QUndoCommand(text.isEmpty() ? PhosphorI18n::tr("Toggle Physical Pixels for Fixed Sizes", "@action") : text,
                   parent)
    , m_editorController(editorController)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

void UpdateFixedGeometryUnitCommand::undo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->setFixedGeometryInDevicePixelsDirect(m_oldValue);
}

void UpdateFixedGeometryUnitCommand::redo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->setFixedGeometryInDevicePixelsDirect(m_newValue);
}
