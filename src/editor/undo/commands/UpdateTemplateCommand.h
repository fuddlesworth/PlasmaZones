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
 * calls mergeWith on consecutive same-id commands). Held-key autorepeat
 * column resizes key this as "columnWidth:<column>:<gesture>" and the
 * default-width spin as "defaultWidthValue:<gesture>", so one gesture is one
 * undo entry and two separate gestures stay two entries (the mouse drag
 * commits once on release with no key and never merges). A merge whose
 * result is a no-op (new state equals the origin) marks itself obsolete so
 * push() drops the entry. Commands with an empty mergeKey never merge.
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
