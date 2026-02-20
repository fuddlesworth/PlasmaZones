// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QUndoCommand>

namespace PlasmaZones {

class EditorController;

/**
 * @brief Command for toggling per-layout full screen geometry mode
 *
 * Enables undo/redo for the useFullScreenGeometry setting.
 * When enabled, zones span the entire screen including areas behind
 * panels and taskbars.
 */
class UpdateFullScreenGeometryCommand : public QUndoCommand
{
public:
    explicit UpdateFullScreenGeometryCommand(QPointer<EditorController> editorController,
                                              bool oldValue, bool newValue,
                                              const QString& text = QString(),
                                              QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QPointer<EditorController> m_editorController;
    bool m_oldValue;
    bool m_newValue;
};

} // namespace PlasmaZones
