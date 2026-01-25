// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QUndoCommand>
#include <QPointer>
#include <QString>
#include "CommandId.h"

namespace PlasmaZones {

class EditorController;

/**
 * @brief Command for updating layout name
 *
 * Note: This command operates on EditorController (not ZoneManager)
 * because layout name is stored in EditorController.
 */
class UpdateLayoutNameCommand : public QUndoCommand
{
public:
    explicit UpdateLayoutNameCommand(QPointer<EditorController> editorController, const QString& oldName,
                                     const QString& newName, const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return CommandId::UpdateLayoutName;
    }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QPointer<EditorController> m_editorController;
    QString m_oldName;
    QString m_newName;
};

} // namespace PlasmaZones
