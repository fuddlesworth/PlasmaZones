// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/phosphorlayer_export.h>

#include <QJsonObject>
#include <QString>

namespace PhosphorLayer {

/**
 * @brief Key-value JSON store for surface-related persistence.
 *
 * Layer-shell surfaces have no per-instance runtime state that the
 * compositor doesn't already manage (position comes from anchors/margins,
 * size from configure events). What consumers DO want to persist is
 * application state: "which surfaces were visible", "which shader preset
 * was last selected in the modal dialog", etc.
 *
 * This interface is deliberately thin — a glorified QJsonObject bag
 * keyed by string. The library does not automatically persist anything
 * through it; consumers decide what to save and when.
 *
 * Implementations:
 * - @ref JsonSurfaceStore — file-backed, atomic write via QSaveFile
 * - consumer-written: wrap KConfig, QSettings, a database, …
 */
class PHOSPHORLAYER_EXPORT ISurfaceStore
{
public:
    virtual ~ISurfaceStore() = default;

    /// Persist @p data under @p key. Overwrites any existing entry.
    /// Returns false on I/O failure (the implementation logs the reason).
    virtual bool save(const QString& key, const QJsonObject& data) = 0;

    /// Load the JSON object previously stored under @p key. Returns an
    /// empty object if the key does not exist or the stored data cannot
    /// be parsed.
    virtual QJsonObject load(const QString& key) const = 0;

    /// True if @p key exists in the store.
    virtual bool has(const QString& key) const = 0;

    /// Remove the entry for @p key (no-op if absent).
    virtual void remove(const QString& key) = 0;
};

} // namespace PhosphorLayer
