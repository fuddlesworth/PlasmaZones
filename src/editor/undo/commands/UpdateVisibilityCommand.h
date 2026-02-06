// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QPointer>
#include <QStringList>
#include <QUndoCommand>

namespace PlasmaZones {

class EditorController;

/**
 * @brief Command for updating layout visibility allow-lists
 *
 * Enables undo/redo for per-screen/desktop/activity visibility changes.
 * Stores old and new state for all three allow-lists.
 */
class UpdateVisibilityCommand : public QUndoCommand
{
public:
    explicit UpdateVisibilityCommand(QPointer<EditorController> editorController,
                                     const QStringList& oldScreens, const QStringList& newScreens,
                                     const QList<int>& oldDesktops, const QList<int>& newDesktops,
                                     const QStringList& oldActivities, const QStringList& newActivities,
                                     const QString& text = QString(), QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QPointer<EditorController> m_editorController;
    QStringList m_oldScreens;
    QStringList m_newScreens;
    QList<int> m_oldDesktops;
    QList<int> m_newDesktops;
    QStringList m_oldActivities;
    QStringList m_newActivities;
};

} // namespace PlasmaZones
