// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QString>
#include <QUndoCommand>
#include <QVariantMap>
#include "CommandId.h"

namespace PlasmaZones {

class EditorTemplateModel;

/**
 * @brief Snapshot command for scrolling-template edits
 *
 * A template's whole edit state (blueprint columns, description, later-column
 * defaults, preset vocabularies) is a handful of scalars and a list capped at
 * 16 entries, so one before/after snapshot pair covers every mutation without
 * a command class per field. undo()/redo() hand the snapshots to
 * EditorTemplateModel::applyState, which diffs per field and emits only the
 * signals that actually change.
 *
 * Merging: commands with a non-empty, equal mergeKey coalesce (QUndoStack
 * calls mergeWith on consecutive same-id commands). The width-drag path keys
 * this as "columnWidth:<column>:<gesture>" so one drag is one undo entry and
 * two separate drags on the same column stay two entries. Commands with an
 * empty mergeKey never merge.
 */
class UpdateTemplateCommand : public QUndoCommand
{
public:
    UpdateTemplateCommand(QPointer<EditorTemplateModel> model, const QVariantMap& oldState, const QVariantMap& newState,
                          const QString& text, const QString& mergeKey = QString(), QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return CommandId::UpdateTemplate;
    }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QPointer<EditorTemplateModel> m_model;
    QVariantMap m_oldState;
    QVariantMap m_newState;
    QString m_mergeKey;
};

} // namespace PlasmaZones
