// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QUndoCommand>

namespace PlasmaZones {

class EditorController;

/**
 * @brief Command for toggling the per-layout unit of fixed-size zones
 *
 * Enables undo/redo for the fixedGeometryInDevicePixels setting. When enabled,
 * a fixed zone's numbers count the output's device pixels rather than logical
 * ones, so the zone keeps its size on the panel at any display scaling.
 */
class UpdateFixedGeometryUnitCommand : public QUndoCommand
{
public:
    explicit UpdateFixedGeometryUnitCommand(QPointer<EditorController> editorController, bool oldValue, bool newValue,
                                            const QString& text = QString(), QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QPointer<EditorController> m_editorController;
    bool m_oldValue;
    bool m_newValue;
};

} // namespace PlasmaZones
