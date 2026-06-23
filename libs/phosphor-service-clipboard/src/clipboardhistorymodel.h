// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) clipboard-history model. It watches an IClipboardSource,
// reads each new selection's preferred MIME type, and records it as a de-duplicated,
// capped list (most-recent first). Sensitive selections (password-manager hints)
// are dropped entirely: never read, never kept. Pure logic over the seam, so it is
// unit-tested with a fake source and no live compositor.

#include "clipboardentry.h"
#include "iclipboardsource.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPointer>

namespace PhosphorServiceClipboard {

class ClipboardHistoryModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        PreviewRole = Qt::UserRole + 1,
        MimeTypeRole,
        OfferedTypesRole,
        TimestampRole,
    };

    explicit ClipboardHistoryModel(QObject* parent = nullptr);
    ~ClipboardHistoryModel() override;

    /// Watch @p source for selection changes; reads each new selection and
    /// records it. Passing nullptr detaches from the current source.
    void setSource(IClipboardSource* source);

    /// Maximum number of entries kept; the oldest are evicted past this. Default
    /// 100. Lowering it evicts immediately.
    void setMaxEntries(int max);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// The full entry at @p row, or a default-constructed entry when out of range
    /// (used by the copy path to re-apply content).
    [[nodiscard]] ClipboardEntry entryAt(int row) const;
    /// The full entry list (most-recent first), for persistence.
    [[nodiscard]] QList<ClipboardEntry> entries() const;
    /// Replace the entries (most-recent first), e.g. seeding from disk on
    /// startup. Trims to the cap. A reset, not a user mutation: it updates the
    /// count but does NOT emit `historyChanged` (so a load does not trigger a
    /// re-save).
    void setEntries(const QList<ClipboardEntry>& entries);

    /// Remove the entry at @p row. Out-of-range rows are ignored.
    void removeAt(int row);
    /// Remove every entry.
    void clear();

Q_SIGNALS:
    void countChanged();
    /// The persisted history changed (a new capture, dedup re-order, removal,
    /// clear, or cap eviction). The host saves on this; not emitted by a load.
    void historyChanged();

private:
    void onSelectionChanged(const QStringList& mimeTypes);
    void onDataReceived(const QString& mimeType, const QByteArray& data);
    void startNextRead();
    void recordEntry(const QByteArray& content, const QString& mimeType, const QStringList& offeredTypes);
    [[nodiscard]] int indexOfContent(const QByteArray& content, const QString& mimeType) const;
    void enforceCap();

    [[nodiscard]] static QString preferredMimeType(const QStringList& mimeTypes);
    [[nodiscard]] static bool isSensitive(const QStringList& mimeTypes);
    [[nodiscard]] static QString makePreview(const QByteArray& content, const QString& mimeType);

    QPointer<IClipboardSource> m_source;
    QList<ClipboardEntry> m_entries;
    int m_maxEntries = 100;

    // Reads are serialized so an async delivery is never mis-attributed: at most
    // one receive() is outstanding at a time. `m_latestTypes` is the most recent
    // selection to capture; `m_readTypes` / `m_readMime` are the snapshot the
    // outstanding read belongs to (so its bytes are recorded with ITS own offered
    // types, even if the selection changed mid-read).
    QStringList m_latestTypes;
    QStringList m_readTypes;
    QString m_readMime;
    bool m_reading = false;
    bool m_selectionChangedWhileReading = false;
};

} // namespace PhosphorServiceClipboard
