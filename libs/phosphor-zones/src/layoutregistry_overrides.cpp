// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Per-algorithm autotile layout overrides.
// Autotile entries are NOT separate from manual layouts: they live in the same
// layout-settings.json sidecar (LayoutSettingsStore), keyed by the canonical
// "autotile:<id>" form so manual and autotile per-id settings share one store.
// This file owns the autotile-facing accessors plus the one-time fold of the
// legacy standalone autotile-overrides.json into that unified store.

#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutSettingsStore.h>

#include <PhosphorLayoutApi/LayoutId.h>

#include "zoneslogging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace PhosphorZones {

namespace {
// Legacy standalone sidecar, retired in favour of layout-settings.json. Read
// once by migrateLegacyAutotileOverrides() and then deleted.
const QString kLegacyAutotileOverridesFile = QStringLiteral("/autotile-overrides.json");
} // namespace

QJsonObject LayoutRegistry::loadAutotileOverrides(const QString& algorithmId) const
{
    return m_layoutSettings.settingsFor(PhosphorLayout::LayoutId::makeAutotileId(algorithmId));
}

void LayoutRegistry::saveAutotileOverrides(const QString& algorithmId, const QJsonObject& overrides)
{
    // setSettingsFor clears the entry when overrides is empty, so a fully-reset
    // algorithm collapses back to its defaults rather than persisting an empty
    // object.
    m_layoutSettings.setSettingsFor(PhosphorLayout::LayoutId::makeAutotileId(algorithmId), overrides);
    if (!m_layoutSettings.saveToFile(layoutSettingsFilePath())) {
        qCWarning(lcZonesLib) << "Failed to persist autotile overrides for" << algorithmId;
    }
}

void LayoutRegistry::seedDefaultLayoutSettingsIfFresh(const QJsonObject& defaults)
{
    if (defaults.isEmpty()) {
        return;
    }
    // "Fresh" = neither the unified sidecar nor the legacy autotile sidecar
    // exists yet. A user with either is an existing install and keeps every
    // layout/algorithm visible — the curated default seeds new configs only,
    // and never clobbers migrated legacy overrides.
    const QString settingsPath = layoutSettingsFilePath();
    if (QFile::exists(settingsPath) || QFile::exists(m_layoutDirectory + kLegacyAutotileOverridesFile)) {
        return;
    }
    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) {
        if (it.value().isObject()) {
            m_layoutSettings.setSettingsFor(it.key(), it.value().toObject());
        }
    }
    // Seeding runs at daemon init, before anything else has created the config
    // directory — ensure it exists so the QSaveFile write doesn't silently fail.
    // Persistence is disk-mediated: the caller's loadLayouts() reloads the
    // sidecar from disk immediately after (clearing the in-memory map), so the
    // on-disk copy written here is authoritative. A failed write degrades
    // benignly to "everything visible" (logged below), not a crash.
    QDir().mkpath(QFileInfo(settingsPath).absolutePath());
    if (!m_layoutSettings.saveToFile(settingsPath)) {
        qCWarning(lcZonesLib) << "Failed to seed default layout-settings.json";
    }
}

void LayoutRegistry::migrateLegacyAutotileOverrides()
{
    const QString legacyPath = m_layoutDirectory + kLegacyAutotileOverridesFile;
    QFile legacy(legacyPath);
    if (!legacy.exists()) {
        return;
    }
    if (!legacy.open(QIODevice::ReadOnly)) {
        qCWarning(lcZonesLib) << "Autotile-overrides migration: cannot read" << legacyPath;
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(legacy.readAll(), &parseError);
    legacy.close();

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // Don't delete an unreadable/corrupt legacy file — leave it in place so
        // the user's overrides survive for inspection and the fold can retry on
        // a future launch, rather than silently dropping data.
        qCWarning(lcZonesLib) << "Autotile-overrides migration: skipping unparseable" << legacyPath << ":"
                              << parseError.errorString();
        return;
    }

    const QJsonObject all = doc.object();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QString key = PhosphorLayout::LayoutId::makeAutotileId(it.key());
        // A store entry that already exists (a seeded default or a
        // post-migration user toggle) wins over the legacy value.
        if (m_layoutSettings.settingsFor(key).isEmpty()) {
            m_layoutSettings.setSettingsFor(key, it.value().toObject());
        }
    }

    // Retire the legacy file only after its contents are safely folded in. A
    // failed write keeps it in place so the fold retries next launch instead of
    // permanently losing the user's autotile overrides.
    if (!m_layoutSettings.saveToFile(layoutSettingsFilePath())) {
        qCWarning(lcZonesLib) << "Autotile-overrides migration: failed to write unified sidecar — "
                                 "keeping legacy file for retry";
        return;
    }
    QFile::remove(legacyPath);
}

} // namespace PhosphorZones
