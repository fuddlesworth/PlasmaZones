// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Layout JSON serialization / deserialization.
// Part of Layout class — split from layout.cpp for SRP.

#include <PhosphorZones/Layout.h>
#include "constants.h"
#include <PhosphorZones/LayoutUtils.h>
#include "../zoneslogging.h"
#include <PhosphorZones/Zone.h>
#include <QJsonArray>

namespace PlasmaZones {

QVariantList Layout::appRulesVariant() const
{
    QVariantList result;
    for (const auto& rule : m_appRules) {
        QVariantMap map;
        map[QStringLiteral("pattern")] = rule.pattern;
        map[QStringLiteral("zoneNumber")] = rule.zoneNumber;
        if (!rule.targetScreen.isEmpty()) {
            map[QStringLiteral("targetScreen")] = rule.targetScreen;
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
        rule.pattern = map.value(QStringLiteral("pattern")).toString();
        rule.zoneNumber = map.value(QStringLiteral("zoneNumber")).toInt();
        rule.targetScreen = map.value(QStringLiteral("targetScreen")).toString();
        if (!rule.pattern.isEmpty() && rule.zoneNumber > 0) {
            newRules.append(rule);
        }
    }
    setAppRules(newRules);
}

QJsonObject AppRule::toJson() const
{
    QJsonObject obj;
    obj[JsonKeys::Pattern] = pattern;
    obj[JsonKeys::ZoneNumber] = zoneNumber;
    if (!targetScreen.isEmpty()) {
        obj[JsonKeys::TargetScreen] = targetScreen;
    }
    return obj;
}

AppRule AppRule::fromJson(const QJsonObject& obj)
{
    AppRule rule;
    rule.pattern = obj[JsonKeys::Pattern].toString();
    rule.zoneNumber = obj[JsonKeys::ZoneNumber].toInt();
    rule.targetScreen = obj[JsonKeys::TargetScreen].toString();
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
    json[JsonKeys::Id] = m_id.toString();
    json[JsonKeys::Name] = m_name;
    if (!m_description.isEmpty()) {
        json[JsonKeys::Description] = m_description;
    }
    // Only serialize gap overrides if they're set (>= 0)
    if (m_zonePadding >= 0) {
        json[JsonKeys::ZonePadding] = m_zonePadding;
    }
    if (m_outerGap >= 0) {
        json[JsonKeys::OuterGap] = m_outerGap;
    }
    // Per-side outer gap overrides — serialize toggle whenever enabled so user intent is preserved
    if (m_usePerSideOuterGap) {
        json[JsonKeys::UsePerSideOuterGap] = true;
        if (m_outerGapTop >= 0)
            json[JsonKeys::OuterGapTop] = m_outerGapTop;
        if (m_outerGapBottom >= 0)
            json[JsonKeys::OuterGapBottom] = m_outerGapBottom;
        if (m_outerGapLeft >= 0)
            json[JsonKeys::OuterGapLeft] = m_outerGapLeft;
        if (m_outerGapRight >= 0)
            json[JsonKeys::OuterGapRight] = m_outerGapRight;
    }
    json[JsonKeys::ShowZoneNumbers] = m_showZoneNumbers;
    if (m_overlayDisplayMode >= 0) {
        json[JsonKeys::OverlayDisplayMode] = m_overlayDisplayMode;
    }
    if (m_defaultOrder != 999) {
        json[JsonKeys::DefaultOrder] = m_defaultOrder;
    }
    // Note: isBuiltIn is no longer serialized - it's determined by source path at load time

    // Persist system origin path so user overrides can be restored on deletion
    if (!m_systemSourcePath.isEmpty()) {
        json[JsonKeys::SystemSourcePath] = m_systemSourcePath;
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
        json[JsonKeys::ShaderId] = m_shaderId;
    }
    if (!m_shaderParams.isEmpty()) {
        json[JsonKeys::ShaderParams] = QJsonObject::fromVariantMap(m_shaderParams);
    }

    // App-to-zone rules - only serialize if non-empty
    if (!m_appRules.isEmpty()) {
        QJsonArray rulesArray;
        for (const auto& rule : m_appRules) {
            rulesArray.append(rule.toJson());
        }
        json[JsonKeys::AppRules] = rulesArray;
    }

    // Auto-assign - only serialize if true
    if (m_autoAssign) {
        json[JsonKeys::AutoAssign] = true;
    }

    // Full screen geometry mode - only serialize if true
    if (m_useFullScreenGeometry) {
        json[JsonKeys::UseFullScreenGeometry] = true;
    }

    // Aspect ratio classification - only serialize non-default values
    if (m_aspectRatioClass != AspectRatioClass::Any) {
        json[JsonKeys::AspectRatioClassKey] = ScreenClassification::toString(m_aspectRatioClass);
    }
    if (m_minAspectRatio > 0.0) {
        json[JsonKeys::MinAspectRatio] = m_minAspectRatio;
    }
    if (m_maxAspectRatio > 0.0) {
        json[JsonKeys::MaxAspectRatio] = m_maxAspectRatio;
    }

    // Visibility filtering - only serialize non-default values
    if (m_hiddenFromSelector) {
        json[JsonKeys::HiddenFromSelector] = true;
    }
    LayoutUtils::serializeAllowLists(json, m_allowedScreens, m_allowedDesktops, m_allowedActivities);

    QJsonArray zonesArray;
    for (const auto* zone : m_zones) {
        zonesArray.append(zone->toJson(m_lastRecalcGeometry));
    }
    json[JsonKeys::Zones] = zonesArray;

    return json;
}

Layout* Layout::fromJson(const QJsonObject& json, QObject* parent)
{
    auto layout = new Layout(parent);

    layout->m_id = QUuid::fromString(json[JsonKeys::Id].toString());
    if (layout->m_id.isNull()) {
        layout->m_id = QUuid::createUuid();
    }

    layout->m_name = json[JsonKeys::Name].toString();
    // Note: "type" key is silently ignored for backward compatibility
    layout->m_description = json[JsonKeys::Description].toString();
    // Gap overrides: -1 means use global setting (key absent = no override)
    layout->m_zonePadding = json.contains(JsonKeys::ZonePadding) ? json[JsonKeys::ZonePadding].toInt(-1) : -1;
    layout->m_outerGap = json.contains(JsonKeys::OuterGap) ? json[JsonKeys::OuterGap].toInt(-1) : -1;
    // Per-side outer gap overrides
    layout->m_usePerSideOuterGap = json[JsonKeys::UsePerSideOuterGap].toBool(false);
    layout->m_outerGapTop = json.contains(JsonKeys::OuterGapTop) ? json[JsonKeys::OuterGapTop].toInt(-1) : -1;
    layout->m_outerGapBottom = json.contains(JsonKeys::OuterGapBottom) ? json[JsonKeys::OuterGapBottom].toInt(-1) : -1;
    layout->m_outerGapLeft = json.contains(JsonKeys::OuterGapLeft) ? json[JsonKeys::OuterGapLeft].toInt(-1) : -1;
    layout->m_outerGapRight = json.contains(JsonKeys::OuterGapRight) ? json[JsonKeys::OuterGapRight].toInt(-1) : -1;
    layout->m_showZoneNumbers = json[JsonKeys::ShowZoneNumbers].toBool(true);
    layout->m_overlayDisplayMode =
        json.contains(JsonKeys::OverlayDisplayMode) ? json[JsonKeys::OverlayDisplayMode].toInt(-1) : -1;
    layout->m_defaultOrder = json[JsonKeys::DefaultOrder].toInt(999);
    // Note: sourcePath is set by LayoutManager after loading, not from JSON
    // But systemSourcePath IS persisted in user JSON for system override restoration
    layout->m_systemSourcePath = json[JsonKeys::SystemSourcePath].toString();

    // Shader support
    layout->m_shaderId = json[JsonKeys::ShaderId].toString();
    if (json.contains(JsonKeys::ShaderParams)) {
        layout->m_shaderParams = json[JsonKeys::ShaderParams].toObject().toVariantMap();
    }

    // App-to-zone rules
    if (json.contains(JsonKeys::AppRules)) {
        layout->m_appRules = AppRule::fromJsonArray(json[JsonKeys::AppRules].toArray());
    }

    // Auto-assign
    layout->m_autoAssign = json[JsonKeys::AutoAssign].toBool(false);

    // Full screen geometry mode
    layout->m_useFullScreenGeometry = json[JsonKeys::UseFullScreenGeometry].toBool(false);

    // Aspect ratio classification
    layout->m_aspectRatioClass = ScreenClassification::fromString(json[JsonKeys::AspectRatioClassKey].toString());
    layout->m_minAspectRatio = json[JsonKeys::MinAspectRatio].toDouble(0.0);
    layout->m_maxAspectRatio = json[JsonKeys::MaxAspectRatio].toDouble(0.0);

    // Visibility filtering
    layout->m_hiddenFromSelector = json[JsonKeys::HiddenFromSelector].toBool(false);
    LayoutUtils::deserializeAllowLists(json, layout->m_allowedScreens, layout->m_allowedDesktops,
                                       layout->m_allowedActivities);

    // Note: allowedScreens may contain either connector names ("DP-2") or
    // EDID-based screen IDs ("LG:Model:115107"). Resolution between the
    // two forms happens at visibility-check time via screensMatch — this
    // load path stores the strings verbatim. (Previously this loop did an
    // eager connector-name → screen-ID translation via QGuiApplication,
    // which forced a daemon-side dependency that prevented zones from
    // running headless in the editor / settings preview path.)

    const auto zonesArray = json[JsonKeys::Zones].toArray();
    for (const auto& zoneValue : zonesArray) {
        auto zone = Zone::fromJson(zoneValue.toObject(), layout);
        layout->m_zones.append(zone);
    }

    return layout;
}

} // namespace PlasmaZones
