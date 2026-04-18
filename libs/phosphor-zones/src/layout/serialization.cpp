// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Layout JSON serialization / deserialization.
// Part of Layout class — split from layout.cpp for SRP.

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/ZoneJsonKeys.h>
#include <PhosphorZones/LayoutUtils.h>
#include "../zoneslogging.h"
#include <PhosphorZones/Zone.h>
#include <QJsonArray>
#include <QStandardPaths>

namespace PhosphorZones {

QVariantList Layout::appRulesVariant() const
{
    QVariantList result;
    for (const auto& rule : m_appRules) {
        QVariantMap map;
        map[::PhosphorZones::ZoneJsonKeys::Pattern] = rule.pattern;
        map[::PhosphorZones::ZoneJsonKeys::ZoneNumber] = rule.zoneNumber;
        if (!rule.targetScreen.isEmpty()) {
            map[::PhosphorZones::ZoneJsonKeys::TargetScreen] = rule.targetScreen;
        }
        result.append(map);
    }
    return result;
}

void Layout::setAppRulesVariant(const QVariantList& rules)
{
    QVector<AppRule> newRules;
    for (const auto& item : rules) {
        QVariantMap map = item.toMap();
        AppRule rule;
        rule.pattern = map.value(::PhosphorZones::ZoneJsonKeys::Pattern).toString();
        rule.zoneNumber = map.value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt();
        rule.targetScreen = map.value(::PhosphorZones::ZoneJsonKeys::TargetScreen).toString();
        if (!rule.pattern.isEmpty() && rule.zoneNumber > 0) {
            newRules.append(rule);
        }
    }
    setAppRules(newRules);
}

QJsonObject AppRule::toJson() const
{
    QJsonObject obj;
    obj[::PhosphorZones::ZoneJsonKeys::Pattern] = pattern;
    obj[::PhosphorZones::ZoneJsonKeys::ZoneNumber] = zoneNumber;
    if (!targetScreen.isEmpty()) {
        obj[::PhosphorZones::ZoneJsonKeys::TargetScreen] = targetScreen;
    }
    return obj;
}

AppRule AppRule::fromJson(const QJsonObject& obj)
{
    AppRule rule;
    rule.pattern = obj[::PhosphorZones::ZoneJsonKeys::Pattern].toString();
    rule.zoneNumber = obj[::PhosphorZones::ZoneJsonKeys::ZoneNumber].toInt();
    rule.targetScreen = obj[::PhosphorZones::ZoneJsonKeys::TargetScreen].toString();
    return rule;
}

QVector<AppRule> AppRule::fromJsonArray(const QJsonArray& array)
{
    QVector<AppRule> rules;
    rules.reserve(array.size());
    for (const auto& value : array) {
        AppRule rule = AppRule::fromJson(value.toObject());
        if (!rule.pattern.isEmpty() && rule.zoneNumber > 0) {
            rules.append(rule);
        }
    }
    return rules;
}

QJsonObject Layout::toJson() const
{
    QJsonObject json;
    json[::PhosphorZones::ZoneJsonKeys::Id] = m_id.toString();
    json[::PhosphorZones::ZoneJsonKeys::Name] = m_name;
    if (!m_description.isEmpty()) {
        json[::PhosphorZones::ZoneJsonKeys::Description] = m_description;
    }
    // Only serialize gap overrides if they're set (>= 0)
    if (m_zonePadding >= 0) {
        json[::PhosphorZones::ZoneJsonKeys::ZonePadding] = m_zonePadding;
    }
    if (m_outerGap >= 0) {
        json[::PhosphorZones::ZoneJsonKeys::OuterGap] = m_outerGap;
    }
    // Per-side outer gap overrides — serialize toggle whenever enabled so user intent is preserved
    if (m_usePerSideOuterGap) {
        json[::PhosphorZones::ZoneJsonKeys::UsePerSideOuterGap] = true;
        if (m_outerGapTop >= 0)
            json[::PhosphorZones::ZoneJsonKeys::OuterGapTop] = m_outerGapTop;
        if (m_outerGapBottom >= 0)
            json[::PhosphorZones::ZoneJsonKeys::OuterGapBottom] = m_outerGapBottom;
        if (m_outerGapLeft >= 0)
            json[::PhosphorZones::ZoneJsonKeys::OuterGapLeft] = m_outerGapLeft;
        if (m_outerGapRight >= 0)
            json[::PhosphorZones::ZoneJsonKeys::OuterGapRight] = m_outerGapRight;
    }
    json[::PhosphorZones::ZoneJsonKeys::ShowZoneNumbers] = m_showZoneNumbers;
    if (m_overlayDisplayMode >= 0) {
        json[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode] = m_overlayDisplayMode;
    }
    if (m_defaultOrder != 999) {
        json[::PhosphorZones::ZoneJsonKeys::DefaultOrder] = m_defaultOrder;
    }
    // Note: isBuiltIn is no longer serialized - it's determined by source path at load time

    // Persist system origin path so user overrides can be restored on deletion
    if (!m_systemSourcePath.isEmpty()) {
        json[::PhosphorZones::ZoneJsonKeys::SystemSourcePath] = m_systemSourcePath;
    }

    // Shader support — empty shaderId means "no shader", we only persist the
    // key when populated. Layout is a pure data holder: it persists whatever
    // m_shaderParams was last set to, without reaching into a UI-side
    // validator. Stale-param cleanup (stripping values whose keys don't
    // belong to the current shader) is the editor's responsibility on the
    // edit boundary — see EditorController::stripStaleShaderParams and the
    // shader-refresh path that calls it when the active shader changes.
    // Keeping Layout decoupled from ShaderRegistry lets the core data type
    // eventually live in a standalone phosphor-zones library without
    // pulling phosphor-shell into the dependency graph.
    if (!m_shaderId.isEmpty()) {
        json[::PhosphorZones::ZoneJsonKeys::ShaderId] = m_shaderId;
    }
    // Don't persist params when no shader is bound — stale params without a
    // shaderId are meaningless to consumers and just bloat the file.
    if (!m_shaderId.isEmpty() && !m_shaderParams.isEmpty()) {
        json[::PhosphorZones::ZoneJsonKeys::ShaderParams] = QJsonObject::fromVariantMap(m_shaderParams);
    }

    // App-to-zone rules - only serialize if non-empty
    if (!m_appRules.isEmpty()) {
        QJsonArray rulesArray;
        for (const auto& rule : m_appRules) {
            rulesArray.append(rule.toJson());
        }
        json[::PhosphorZones::ZoneJsonKeys::AppRules] = rulesArray;
    }

    // Auto-assign - only serialize if true
    if (m_autoAssign) {
        json[::PhosphorZones::ZoneJsonKeys::AutoAssign] = true;
    }

    // Full screen geometry mode - only serialize if true
    if (m_useFullScreenGeometry) {
        json[::PhosphorZones::ZoneJsonKeys::UseFullScreenGeometry] = true;
    }

    // Aspect ratio classification - only serialize non-default values
    if (m_aspectRatioClass != ::PhosphorLayout::AspectRatioClass::Any) {
        json[::PhosphorZones::ZoneJsonKeys::AspectRatioClassKey] =
            ::PhosphorLayout::ScreenClassification::toString(m_aspectRatioClass);
    }
    if (m_minAspectRatio > 0.0) {
        json[::PhosphorZones::ZoneJsonKeys::MinAspectRatio] = m_minAspectRatio;
    }
    if (m_maxAspectRatio > 0.0) {
        json[::PhosphorZones::ZoneJsonKeys::MaxAspectRatio] = m_maxAspectRatio;
    }

    // Visibility filtering - only serialize non-default values
    if (m_hiddenFromSelector) {
        json[::PhosphorZones::ZoneJsonKeys::HiddenFromSelector] = true;
    }
    LayoutUtils::serializeAllowLists(json, m_allowedScreens, m_allowedDesktops, m_allowedActivities);

    QJsonArray zonesArray;
    for (const auto* zone : m_zones) {
        zonesArray.append(zone->toJson(m_lastRecalcGeometry));
    }
    json[::PhosphorZones::ZoneJsonKeys::Zones] = zonesArray;

    return json;
}

Layout* Layout::fromJson(const QJsonObject& json, QObject* parent)
{
    // Allocate a blank Layout and delegate population to a private member
    // method. This scopes the raw-member pokes to the class's own
    // implementation so future setter validation (e.g. name trimming)
    // lands naturally without needing to extend friendship to this TU.
    auto* layout = new Layout(parent);
    layout->initFromJson(json);
    return layout;
}

void Layout::initFromJson(const QJsonObject& json)
{
    m_id = QUuid::fromString(json[::PhosphorZones::ZoneJsonKeys::Id].toString());
    if (m_id.isNull()) {
        m_id = QUuid::createUuid();
    }

    m_name = json[::PhosphorZones::ZoneJsonKeys::Name].toString();
    // Note: "type" key is silently ignored for backward compatibility
    m_description = json[::PhosphorZones::ZoneJsonKeys::Description].toString();
    // Gap overrides: -1 means use global setting (key absent = no override)
    m_zonePadding = json.contains(::PhosphorZones::ZoneJsonKeys::ZonePadding)
        ? json[::PhosphorZones::ZoneJsonKeys::ZonePadding].toInt(-1)
        : -1;
    m_outerGap = json.contains(::PhosphorZones::ZoneJsonKeys::OuterGap)
        ? json[::PhosphorZones::ZoneJsonKeys::OuterGap].toInt(-1)
        : -1;
    // Per-side outer gap overrides
    m_usePerSideOuterGap = json[::PhosphorZones::ZoneJsonKeys::UsePerSideOuterGap].toBool(false);
    m_outerGapTop = json.contains(::PhosphorZones::ZoneJsonKeys::OuterGapTop)
        ? json[::PhosphorZones::ZoneJsonKeys::OuterGapTop].toInt(-1)
        : -1;
    m_outerGapBottom = json.contains(::PhosphorZones::ZoneJsonKeys::OuterGapBottom)
        ? json[::PhosphorZones::ZoneJsonKeys::OuterGapBottom].toInt(-1)
        : -1;
    m_outerGapLeft = json.contains(::PhosphorZones::ZoneJsonKeys::OuterGapLeft)
        ? json[::PhosphorZones::ZoneJsonKeys::OuterGapLeft].toInt(-1)
        : -1;
    m_outerGapRight = json.contains(::PhosphorZones::ZoneJsonKeys::OuterGapRight)
        ? json[::PhosphorZones::ZoneJsonKeys::OuterGapRight].toInt(-1)
        : -1;
    m_showZoneNumbers = json[::PhosphorZones::ZoneJsonKeys::ShowZoneNumbers].toBool(true);
    m_overlayDisplayMode = json.contains(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode)
        ? json[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode].toInt(-1)
        : -1;
    m_defaultOrder = json[::PhosphorZones::ZoneJsonKeys::DefaultOrder].toInt(999);
    // Note: sourcePath is set by LayoutManager after loading, not from JSON
    // But systemSourcePath IS persisted in user JSON for system override restoration
    m_systemSourcePath = json[::PhosphorZones::ZoneJsonKeys::SystemSourcePath].toString();

    // Sanity-check the persisted systemSourcePath: it must resolve under a
    // known system data prefix (the OS-level GenericDataLocation list,
    // minus the writable user dir). If the value doesn't match any prefix,
    // a stale or hand-edited config can leak an arbitrary path into the
    // "restore system original" code path — drop it with a warning.
    //
    // Skip the check entirely when no prefixes are configured (headless
    // test environments), so fixture tests stay portable. Also skip when
    // the path is empty (no override tracked).
    if (!m_systemSourcePath.isEmpty()) {
        const QStringList systemPrefixes = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        const QString userDataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        bool valid = false;
        bool anyPrefix = false;
        for (const QString& prefix : systemPrefixes) {
            if (prefix.isEmpty() || prefix == userDataPath) {
                continue; // user dir isn't a "system" prefix
            }
            anyPrefix = true;
            if (m_systemSourcePath.startsWith(prefix)) {
                valid = true;
                break;
            }
        }
        if (anyPrefix && !valid) {
            qCWarning(lcLayoutLib) << "dropping invalid systemSourcePath" << m_systemSourcePath;
            m_systemSourcePath.clear();
        }
    }

    // Shader support
    m_shaderId = json[::PhosphorZones::ZoneJsonKeys::ShaderId].toString();
    if (json.contains(::PhosphorZones::ZoneJsonKeys::ShaderParams)) {
        m_shaderParams = json[::PhosphorZones::ZoneJsonKeys::ShaderParams].toObject().toVariantMap();
    }

    // App-to-zone rules
    if (json.contains(::PhosphorZones::ZoneJsonKeys::AppRules)) {
        m_appRules = AppRule::fromJsonArray(json[::PhosphorZones::ZoneJsonKeys::AppRules].toArray());
    }

    // Auto-assign
    m_autoAssign = json[::PhosphorZones::ZoneJsonKeys::AutoAssign].toBool(false);

    // Full screen geometry mode
    m_useFullScreenGeometry = json[::PhosphorZones::ZoneJsonKeys::UseFullScreenGeometry].toBool(false);

    // Aspect ratio classification
    m_aspectRatioClass = ::PhosphorLayout::ScreenClassification::fromString(
        json[::PhosphorZones::ZoneJsonKeys::AspectRatioClassKey].toString());
    m_minAspectRatio = json[::PhosphorZones::ZoneJsonKeys::MinAspectRatio].toDouble(0.0);
    m_maxAspectRatio = json[::PhosphorZones::ZoneJsonKeys::MaxAspectRatio].toDouble(0.0);

    // Visibility filtering
    m_hiddenFromSelector = json[::PhosphorZones::ZoneJsonKeys::HiddenFromSelector].toBool(false);
    LayoutUtils::deserializeAllowLists(json, m_allowedScreens, m_allowedDesktops, m_allowedActivities);

    // Translate any legacy connector names ("DP-2") in allowedScreens to the
    // application's stable screen identifier ("LG:Model:Serial") if a
    // resolver is installed. Daemon / editor / settings install a resolver
    // that walks QGuiApplication::screens(); headless contexts leave the
    // resolver unset and the strings pass through verbatim. Equality-path
    // matches (same connector on both sides) still work without a resolver.
    if (const auto& resolver = Layout::screenIdResolver(); resolver) {
        for (int i = 0; i < m_allowedScreens.size(); ++i) {
            const QString resolved = resolver(m_allowedScreens[i]);
            if (!resolved.isEmpty()) {
                m_allowedScreens[i] = resolved;
            }
        }
    }

    // Route zones through addZone() so zoneAdded / zonesChanged fire
    // naturally during deserialization. addZone respects a pre-set
    // zoneNumber (>0) — the number was read by Zone::fromJson — and
    // only auto-assigns the next slot when the incoming zone lacks one.
    //
    // Wrap the loop in batchModify so the individual per-zone
    // emitModifiedIfNotBatched() calls coalesce into a single
    // layoutModified after construction — the layout was just loaded
    // from disk, not edited, so any listener that wires up after
    // fromJson returns sees a clean-but-populated container.
    const auto zonesArray = json[::PhosphorZones::ZoneJsonKeys::Zones].toArray();
    beginBatchModify();
    for (const auto& zoneValue : zonesArray) {
        auto* zone = Zone::fromJson(zoneValue.toObject(), this);
        addZone(zone);
    }
    endBatchModify();
    // Drop the dirty flag: deserialization isn't a user edit, and a stale
    // dirty flag would trick any future isDirty() probe into saving the
    // just-loaded contents back to disk.
    clearDirty();
}

} // namespace PhosphorZones
