// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Per-layout settings sidecar (layout-settings.json) — split/merge + I/O.

#include <PhosphorZones/LayoutSettingsStore.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include "zoneslogging.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <array>

namespace PhosphorZones {

namespace {

// Root key holding the schema-version stamp in layout-settings.json.
constexpr QLatin1String kVersionKey{"_version"};

// Key under a layout's settings object that holds the per-zone appearance map
// (zone UUID → appearance object). Distinct from the structural "zones" array
// so the two never collide in the merged document.
constexpr QLatin1String kZoneAppearanceMapKey{"zoneAppearance"};

// The per-LAYOUT setting keys that move out of the layout file. The per-ZONE
// appearance block is handled separately (see extract/strip/merge below).
const std::array<QLatin1String, 13> layoutSettingKeys{{
    ZoneJsonKeys::ZonePadding,
    ZoneJsonKeys::OuterGap,
    ZoneJsonKeys::UsePerSideOuterGap,
    ZoneJsonKeys::OuterGapTop,
    ZoneJsonKeys::OuterGapBottom,
    ZoneJsonKeys::OuterGapLeft,
    ZoneJsonKeys::OuterGapRight,
    ZoneJsonKeys::ShowZoneNumbers,
    ZoneJsonKeys::OverlayDisplayMode,
    ZoneJsonKeys::AutoAssign,
    ZoneJsonKeys::UseFullScreenGeometry,
    ZoneJsonKeys::ShaderId,
    ZoneJsonKeys::ShaderParams,
}};

} // namespace

QJsonObject LayoutSettingsStore::extractSettings(const QJsonObject& fullLayout)
{
    QJsonObject settings;
    for (const QLatin1String key : layoutSettingKeys) {
        if (fullLayout.contains(key)) {
            settings.insert(key, fullLayout.value(key));
        }
    }

    QJsonObject zoneAppearance;
    const QJsonArray zones = fullLayout.value(ZoneJsonKeys::Zones).toArray();
    for (const QJsonValue& zoneVal : zones) {
        const QJsonObject zone = zoneVal.toObject();
        const QString zoneId = zone.value(ZoneJsonKeys::Id).toString();
        if (!zoneId.isEmpty() && zone.contains(ZoneJsonKeys::Appearance)) {
            zoneAppearance.insert(zoneId, zone.value(ZoneJsonKeys::Appearance));
        }
    }
    if (!zoneAppearance.isEmpty()) {
        settings.insert(kZoneAppearanceMapKey, zoneAppearance);
    }
    return settings;
}

QJsonObject LayoutSettingsStore::stripSettings(const QJsonObject& fullLayout)
{
    QJsonObject structural = fullLayout;
    for (const QLatin1String key : layoutSettingKeys) {
        structural.remove(key);
    }

    QJsonArray zones = structural.value(ZoneJsonKeys::Zones).toArray();
    for (int i = 0; i < zones.size(); ++i) {
        QJsonObject zone = zones.at(i).toObject();
        zone.remove(ZoneJsonKeys::Appearance);
        zones.replace(i, zone);
    }
    structural.insert(QString(ZoneJsonKeys::Zones), zones);
    return structural;
}

QJsonObject LayoutSettingsStore::mergeSettings(QJsonObject structural, const QJsonObject& settings)
{
    QJsonObject zoneAppearance;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        if (it.key() == kZoneAppearanceMapKey) {
            zoneAppearance = it.value().toObject();
            continue;
        }
        structural.insert(it.key(), it.value());
    }

    if (!zoneAppearance.isEmpty()) {
        QJsonArray zones = structural.value(ZoneJsonKeys::Zones).toArray();
        for (int i = 0; i < zones.size(); ++i) {
            QJsonObject zone = zones.at(i).toObject();
            const QString zoneId = zone.value(ZoneJsonKeys::Id).toString();
            if (zoneAppearance.contains(zoneId)) {
                zone.insert(QString(ZoneJsonKeys::Appearance), zoneAppearance.value(zoneId));
                zones.replace(i, zone);
            }
        }
        structural.insert(QString(ZoneJsonKeys::Zones), zones);
    }
    return structural;
}

bool LayoutSettingsStore::loadFromFile(const QString& path)
{
    m_byLayout.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return true; // a missing sidecar is an empty store, not an error
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcZonesLib) << "LayoutSettingsStore: failed to parse" << path << err.errorString();
        return false;
    }

    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key() == kVersionKey || !it.value().isObject()) {
            continue;
        }
        m_byLayout.insert(it.key(), it.value().toObject());
    }
    return true;
}

bool LayoutSettingsStore::saveToFile(const QString& path) const
{
    QJsonObject root;
    root.insert(QString(kVersionKey), SchemaVersion);
    for (auto it = m_byLayout.constBegin(); it != m_byLayout.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            root.insert(it.key(), it.value());
        }
    }

    // QSaveFile gives atomic temp-write + rename — a crash mid-write never
    // leaves a truncated sidecar behind.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcZonesLib) << "LayoutSettingsStore: failed to open for writing" << path << file.errorString();
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        qCWarning(lcZonesLib) << "LayoutSettingsStore: failed to write" << path << file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        qCWarning(lcZonesLib) << "LayoutSettingsStore: failed to commit" << path << file.errorString();
        return false;
    }
    return true;
}

QJsonObject LayoutSettingsStore::settingsFor(const QString& layoutId) const
{
    return m_byLayout.value(layoutId);
}

void LayoutSettingsStore::setSettingsFor(const QString& layoutId, const QJsonObject& settings)
{
    if (settings.isEmpty()) {
        m_byLayout.remove(layoutId);
    } else {
        m_byLayout.insert(layoutId, settings);
    }
}

void LayoutSettingsStore::removeLayout(const QString& layoutId)
{
    m_byLayout.remove(layoutId);
}

} // namespace PhosphorZones
