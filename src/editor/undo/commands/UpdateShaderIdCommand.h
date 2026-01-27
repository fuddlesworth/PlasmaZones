// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QString>
#include <QUndoCommand>

#include "CommandId.h"

namespace PlasmaZones {

class EditorController;

/**
 * @brief Command for updating the current shader effect
 *
 * Enables undo/redo for shader selection changes in the layout editor.
 */
class UpdateShaderIdCommand : public QUndoCommand
{
public:
    explicit UpdateShaderIdCommand(QPointer<EditorController> editorController, const QString& oldId,
                                   const QString& newId, const QString& text = QString(),
                                   QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return CommandId::UpdateShaderId;
    }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QPointer<EditorController> m_editorController;
    QString m_oldId;
    QString m_newId;
};

} // namespace PlasmaZones
