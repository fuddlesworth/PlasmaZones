// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QString>
#include <QUndoCommand>
#include <QVariantMap>

#include "CommandId.h"

namespace PlasmaZones {

class EditorController;

/**
 * @brief Command for updating shader parameters (single parameter with merge support)
 *
 * Enables undo/redo for shader parameter changes. Supports merging consecutive
 * changes to the same parameter (e.g., slider drags) into a single undo step.
 */
class UpdateShaderParamsCommand : public QUndoCommand
{
public:
    /**
     * @brief Construct for single parameter change (supports merging)
     */
    explicit UpdateShaderParamsCommand(QPointer<EditorController> editorController, const QString& paramKey,
                                       const QVariant& oldValue, const QVariant& newValue,
                                       const QString& text = QString(), QUndoCommand* parent = nullptr);

    /**
     * @brief Construct for batch/reset operations (no merging)
     */
    explicit UpdateShaderParamsCommand(QPointer<EditorController> editorController, const QVariantMap& oldParams,
                                       const QVariantMap& newParams, const QString& text = QString(),
                                       QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return m_isSingleParam ? CommandId::UpdateShaderParams : -1;
    }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QPointer<EditorController> m_editorController;

    // For single parameter mode (supports merging)
    bool m_isSingleParam = false;
    QString m_paramKey;
    QVariant m_oldValue;
    QVariant m_newValue;

    // For batch mode (no merging)
    QVariantMap m_oldParams;
    QVariantMap m_newParams;
};

} // namespace PlasmaZones
