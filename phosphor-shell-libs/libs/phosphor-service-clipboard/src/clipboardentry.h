// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) value type: one entry in the clipboard history.

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace PhosphorServiceClipboard {

struct ClipboardEntry
{
    /// The raw bytes that were on the clipboard for `mimeType`.
    QByteArray content;
    /// The MIME type the `content` was materialized as.
    QString mimeType;
    /// Every MIME type the selection offered (the full menu, even though only
    /// `mimeType` was read).
    QStringList offeredTypes;
    /// A short, single-line, human-readable preview (decoded text, or a
    /// `[type, N bytes]` placeholder for binary content).
    QString preview;
    /// When the entry was captured.
    QDateTime timestamp;
    /// True when the selection carried a sensitivity hint (e.g. a password
    /// manager). Sensitive entries are surfaced live but never persisted.
    bool sensitive = false;
};

} // namespace PhosphorServiceClipboard
