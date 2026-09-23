// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/ISurfaceStore.h>
#include <PhosphorLayer/phosphorlayer_export.h>

#include <memory>

QT_BEGIN_NAMESPACE
class QString;
QT_END_NAMESPACE

namespace PhosphorLayer {

/**
 * @brief File-backed ISurfaceStore — one JSON document on disk.
 *
 * The entire store is serialised as a single JSON object keyed by
 * key-name. Loads happen lazily on first access; saves use QSaveFile
 * for atomicity (write-to-temp + rename). Suitable for modest amounts
 * of state (hundreds of keys); for heavier workloads wrap KConfig or
 * a database.
 *
 * The file path is consumer-chosen. Typical usage:
 *
 * @code
 *     JsonSurfaceStore store(QStandardPaths::writableLocation(
 *                                QStandardPaths::AppDataLocation)
 *                            + "/surface-state.json");
 * @endcode
 *
 * @note **Thread safety.** This store is NOT thread-safe — `save()` and
 * `load()` share a single in-memory `QJsonObject` without locks. All
 * calls must originate from the same thread (typically the GUI thread).
 * Consumers that need cross-thread access must serialise calls
 * externally, or wrap the store in their own mutex. Every `save()` also
 * rewrites the full file; batching on the caller side is recommended if
 * many keys are mutated together.
 */
class PHOSPHORLAYER_EXPORT JsonSurfaceStore : public ISurfaceStore
{
public:
    explicit JsonSurfaceStore(QString filePath);
    ~JsonSurfaceStore() override;

    bool save(const QString& key, const QJsonObject& data) override;
    QJsonObject load(const QString& key) const override;
    bool has(const QString& key) const override;
    void remove(const QString& key) override;

    /// File path this store reads from / writes to.
    QString filePath() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorLayer
