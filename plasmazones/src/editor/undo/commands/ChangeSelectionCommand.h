// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QUndoCommand>
#include <QPointer>
#include <QStringList>

namespace PlasmaZones {

class EditorController;

/**
 * @brief Undo command for selection changes
 *
 * Allows undoing/redoing selection state changes.
 * Consecutive selection changes can be merged for better UX.
 */
class ChangeSelectionCommand : public QUndoCommand
{
public:
    /**
     * @brief Construct a selection change command
     * @param controller EditorController to modify
     * @param oldSelection Previous selection state
     * @param newSelection New selection state
     * @param text Description for undo menu
     */
    ChangeSelectionCommand(QPointer<EditorController> controller, const QStringList& oldSelection,
                           const QStringList& newSelection, const QString& text = QString());

    void undo() override;
    void redo() override;
    int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    QPointer<EditorController> m_controller;
    QStringList m_oldSelection;
    QStringList m_newSelection;
    bool m_firstRedo = true; // Skip first redo (already applied)
};

} // namespace PlasmaZones
