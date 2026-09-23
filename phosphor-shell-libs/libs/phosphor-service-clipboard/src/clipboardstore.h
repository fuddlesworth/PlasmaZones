// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) on-disk clipboard-history store. Persists the entry
// list as a JSON index plus per-entry content blobs (cliphist-style) under a
// directory, so history survives restarts. Sensitive entries are never written.
// The directory is injectable so the round-trip is unit-tested against a temp dir
// with no real user data.

#include "clipboardentry.h"

#include <QList>
#include <QString>

namespace PhosphorServiceClipboard {

class ClipboardStore
{
public:
    explicit ClipboardStore(QString directory);

    /// Load persisted entries (most-recent first). Returns an empty list on
    /// first run or when the index is missing / unreadable / corrupt.
    [[nodiscard]] QList<ClipboardEntry> load() const;

    /// Persist @p entries (most-recent first), replacing the prior contents.
    /// Sensitive entries are skipped. Writes are atomic (temp file + rename) and
    /// orphaned content blobs are pruned. Returns false on a write failure.
    bool save(const QList<ClipboardEntry>& entries) const;

    /// The default user data directory, `~/.local/share/phosphor-clipboard`.
    [[nodiscard]] static QString defaultDirectory();

private:
    [[nodiscard]] QString indexPath() const;
    [[nodiscard]] QString blobsDir() const;

    QString m_directory;
};

} // namespace PhosphorServiceClipboard
