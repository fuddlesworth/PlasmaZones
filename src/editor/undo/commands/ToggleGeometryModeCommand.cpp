// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ToggleGeometryModeCommand.h"

#include <KLocalizedString>

#include "../../EditorController.h"

using namespace PlasmaZones;

ToggleGeometryModeCommand::ToggleGeometryModeCommand(QPointer<EditorController> editorController,
                                                       const QString& zoneId,
                                                       int oldMode, int newMode,
                                                       const QRectF& oldRelativeGeo, const QRectF& newRelativeGeo,
                                                       const QRectF& oldFixedGeo, const QRectF& newFixedGeo,
                                                       const QString& text, QUndoCommand* parent)
    : QUndoCommand(text.isEmpty() ? i18nc("@action", "Toggle Zone Geometry Mode") : text, parent)
    , m_editorController(editorController)
    , m_zoneId(zoneId)
    , m_oldMode(oldMode)
    , m_newMode(newMode)
    , m_oldRelativeGeo(oldRelativeGeo)
    , m_newRelativeGeo(newRelativeGeo)
    , m_oldFixedGeo(oldFixedGeo)
    , m_newFixedGeo(newFixedGeo)
{
}

void ToggleGeometryModeCommand::undo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->applyZoneGeometryMode(m_zoneId, m_oldMode, m_oldRelativeGeo, m_oldFixedGeo);
}

void ToggleGeometryModeCommand::redo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->applyZoneGeometryMode(m_zoneId, m_newMode, m_newRelativeGeo, m_newFixedGeo);
}
