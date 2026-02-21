// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QRectF>
#include <QUndoCommand>

namespace PlasmaZones {

class EditorController;

/**
 * @brief Command for toggling per-zone geometry mode (Relative <-> Fixed)
 *
 * Stores both old and new mode, relative geometry, and fixed geometry
 * so that undo/redo correctly restores the zone's complete geometry state.
 */
class ToggleGeometryModeCommand : public QUndoCommand
{
public:
    explicit ToggleGeometryModeCommand(QPointer<EditorController> editorController,
                                        const QString& zoneId,
                                        int oldMode, int newMode,
                                        const QRectF& oldRelativeGeo, const QRectF& newRelativeGeo,
                                        const QRectF& oldFixedGeo, const QRectF& newFixedGeo,
                                        const QString& text = QString(),
                                        QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QPointer<EditorController> m_editorController;
    QString m_zoneId;
    int m_oldMode;
    int m_newMode;
    QRectF m_oldRelativeGeo;
    QRectF m_newRelativeGeo;
    QRectF m_oldFixedGeo;
    QRectF m_newFixedGeo;
};

} // namespace PlasmaZones
