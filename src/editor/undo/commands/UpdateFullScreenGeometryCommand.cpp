// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateFullScreenGeometryCommand.h"

#include <KLocalizedString>

#include "../../EditorController.h"

using namespace PlasmaZones;

UpdateFullScreenGeometryCommand::UpdateFullScreenGeometryCommand(QPointer<EditorController> editorController,
                                                                 bool oldValue, bool newValue, const QString& text,
                                                                 QUndoCommand* parent)
    : QUndoCommand(text.isEmpty() ? i18nc("@action", "Toggle Use Full Screen Area") : text, parent)
    , m_editorController(editorController)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

void UpdateFullScreenGeometryCommand::undo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->setUseFullScreenGeometryDirect(m_oldValue);
}

void UpdateFullScreenGeometryCommand::redo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->setUseFullScreenGeometryDirect(m_newValue);
}
