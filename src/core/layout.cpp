// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layout.h"
#include "constants.h"
#include "layoututils.h"
#include "logging.h"
#include "shaderregistry.h"
#include "utils.h"
#include <QJsonArray>
#include <QStandardPaths>
#include <algorithm>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Macros for setter patterns
// Reduces boilerplate for layout property setters
// ═══════════════════════════════════════════════════════════════════════════════

// Simple setter: if changed, update member, emit specific signal and layoutModified
#define LAYOUT_SETTER(Type, name, member, signal) \
    void Layout::set##name(Type value) \
    { \
        if (member != value) { \
            member = value; \
            Q_EMIT signal(); \
            emitModifiedIfNotBatched(); \
        } \
    }

// Simple setter without layoutModified signal (for internal properties)
#define LAYOUT_SETTER_NO_MODIFIED(Type, name, member, signal) \
    void Layout::set##name(Type value) \
    { \
        if (member != value) { \
            member = value; \
            Q_EMIT signal(); \
        } \
    }

// Setter that allows -1 (use global setting) or any non-negative value
#define LAYOUT_SETTER_MIN_NEGATIVE_ONE(name, member, signal) \
    void Layout::set##name(int value) \
    { \
        if (value < -1) { \
            value = -1; \
        } \
        if (member != value) { \
            member = value; \
            Q_EMIT signal(); \
            emitModifiedIfNotBatched(); \
        } \
    }

Layout::Layout(QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
{
}

Layout::Layout(const QString& name, LayoutType type, QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name(name)
    , m_type(type)
{
}

Layout::Layout(const Layout& other)
    : QObject(other.parent())
    , m_id(QUuid::createUuid()) // New layout gets new ID
    , m_name(other.m_name)
    , m_type(other.m_type)
    , m_description(other.m_description)
    , m_zonePadding(other.m_zonePadding)
    , m_outerGap(other.m_outerGap)
    , m_usePerSideOuterGap(other.m_usePerSideOuterGap)
    , m_outerGapTop(other.m_outerGapTop)
    , m_outerGapBottom(other.m_outerGapBottom)
    , m_outerGapLeft(other.m_outerGapLeft)
    , m_outerGapRight(other.m_outerGapRight)
    , m_showZoneNumbers(other.m_showZoneNumbers)
    , m_sourcePath() // Copies have no source path (will be saved to user directory)
    , m_defaultOrder(other.m_defaultOrder)
    , m_appRules(other.m_appRules)
    , m_autoAssign(other.m_autoAssign)
    , m_useFullScreenGeometry(other.m_useFullScreenGeometry)
    , m_shaderId(other.m_shaderId)
    , m_shaderParams(other.m_shaderParams)
    , m_hiddenFromSelector(other.m_hiddenFromSelector)
    , m_allowedScreens(other.m_allowedScreens)
    , m_allowedDesktops(other.m_allowedDesktops)
    , m_allowedActivities(other.m_allowedActivities)
{
    // Deep copy zones using clone() method
    for (const auto* zone : other.m_zones) {
        auto* newZone = zone->clone(this);
        m_zones.append(newZone);
    }
}

Layout::~Layout()
{
    qDeleteAll(m_zones);
}

Layout& Layout::operator=(const Layout& other)
{
    if (this != &other) {
        beginBatchModify();

        // Track visibility changes for signal emission
        bool hiddenChanged = m_hiddenFromSelector != other.m_hiddenFromSelector;
        bool screensChanged = m_allowedScreens != other.m_allowedScreens;
        bool desktopsChanged = m_allowedDesktops != other.m_allowedDesktops;
        bool activitiesChanged = m_allowedActivities != other.m_allowedActivities;

        m_name = other.m_name;
        m_type = other.m_type;
        m_description = other.m_description;
        m_zonePadding = other.m_zonePadding;
        m_outerGap = other.m_outerGap;
        m_usePerSideOuterGap = other.m_usePerSideOuterGap;
        m_outerGapTop = other.m_outerGapTop;
        m_outerGapBottom = other.m_outerGapBottom;
        m_outerGapLeft = other.m_outerGapLeft;
        m_outerGapRight = other.m_outerGapRight;
        m_showZoneNumbers = other.m_showZoneNumbers;
        m_defaultOrder = other.m_defaultOrder;
        m_sourcePath.clear(); // Assignment creates a user copy (will be saved to user directory)
        m_shaderId = other.m_shaderId;
        m_shaderParams = other.m_shaderParams;
        bool rulesChanged = m_appRules != other.m_appRules;
        m_appRules = other.m_appRules;
        bool autoAssignDiff = m_autoAssign != other.m_autoAssign;
        m_autoAssign = other.m_autoAssign;
        bool fullScreenGeomDiff = m_useFullScreenGeometry != other.m_useFullScreenGeometry;
        m_useFullScreenGeometry = other.m_useFullScreenGeometry;
        m_hiddenFromSelector = other.m_hiddenFromSelector;
        m_allowedScreens = other.m_allowedScreens;
        m_allowedDesktops = other.m_allowedDesktops;
        m_allowedActivities = other.m_allowedActivities;

        // Deep copy zones using clone() method
        qDeleteAll(m_zones);
        m_zones.clear();
        for (const auto* zone : other.m_zones) {
            auto* newZone = zone->clone(this);
            m_zones.append(newZone);
        }
        m_lastRecalcGeometry = QRectF(); // Invalidate geometry cache
        Q_EMIT zonesChanged();

        // Emit visibility signals for changed properties
        if (hiddenChanged) Q_EMIT hiddenFromSelectorChanged();
        if (screensChanged) Q_EMIT allowedScreensChanged();
        if (desktopsChanged) Q_EMIT allowedDesktopsChanged();
        if (activitiesChanged) Q_EMIT allowedActivitiesChanged();
        if (rulesChanged) Q_EMIT appRulesChanged();
        if (autoAssignDiff) Q_EMIT autoAssignChanged();
        if (fullScreenGeomDiff) Q_EMIT useFullScreenGeometryChanged();

        m_dirty = true;
        endBatchModify();
    }
    return *this;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Layout Property Setters
// ═══════════════════════════════════════════════════════════════════════════════

// Simple property setters
LAYOUT_SETTER(const QString&, Name, m_name, nameChanged)
LAYOUT_SETTER(LayoutType, Type, m_type, typeChanged)
LAYOUT_SETTER(const QString&, Description, m_description, descriptionChanged)
LAYOUT_SETTER(bool, ShowZoneNumbers, m_showZoneNumbers, showZoneNumbersChanged)
LAYOUT_SETTER(const QString&, ShaderId, m_shaderId, shaderIdChanged)
LAYOUT_SETTER(const QVariantMap&, ShaderParams, m_shaderParams, shaderParamsChanged)
LAYOUT_SETTER(bool, HiddenFromSelector, m_hiddenFromSelector, hiddenFromSelectorChanged)
LAYOUT_SETTER(bool, AutoAssign, m_autoAssign, autoAssignChanged)
LAYOUT_SETTER(bool, UseFullScreenGeometry, m_useFullScreenGeometry, useFullScreenGeometryChanged)

void Layout::setAllowedScreens(const QStringList& screens)
{
    if (m_allowedScreens != screens) {
        m_allowedScreens = screens;
        Q_EMIT allowedScreensChanged();
        emitModifiedIfNotBatched();
    }
}

void Layout::setAllowedDesktops(const QList<int>& desktops)
{
    if (m_allowedDesktops != desktops) {
        m_allowedDesktops = desktops;
        Q_EMIT allowedDesktopsChanged();
        emitModifiedIfNotBatched();
    }
}

void Layout::setAllowedActivities(const QStringList& activities)
{
    if (m_allowedActivities != activities) {
        m_allowedActivities = activities;
        Q_EMIT allowedActivitiesChanged();
        emitModifiedIfNotBatched();
    }
}

// Gap setters (allow -1 for "use global" or non-negative values)
LAYOUT_SETTER_MIN_NEGATIVE_ONE(ZonePadding, m_zonePadding, zonePaddingChanged)
LAYOUT_SETTER_MIN_NEGATIVE_ONE(OuterGap, m_outerGap, outerGapChanged)
LAYOUT_SETTER_MIN_NEGATIVE_ONE(OuterGapTop, m_outerGapTop, outerGapChanged)
LAYOUT_SETTER_MIN_NEGATIVE_ONE(OuterGapBottom, m_outerGapBottom, outerGapChanged)
LAYOUT_SETTER_MIN_NEGATIVE_ONE(OuterGapLeft, m_outerGapLeft, outerGapChanged)
LAYOUT_SETTER_MIN_NEGATIVE_ONE(OuterGapRight, m_outerGapRight, outerGapChanged)

void Layout::setUsePerSideOuterGap(bool enabled)
{
    if (m_usePerSideOuterGap != enabled) {
        m_usePerSideOuterGap = enabled;
        Q_EMIT outerGapChanged();
        emitModifiedIfNotBatched();
    }
}

// Source path setter (no layoutModified - internal tracking property)
LAYOUT_SETTER_NO_MODIFIED(const QString&, SourcePath, m_sourcePath, sourcePathChanged)

// App-to-zone rules
void Layout::setAppRules(const QVector<AppRule>& rules)
{
    if (m_appRules != rules) {
        m_appRules = rules;
        Q_EMIT appRulesChanged();
        emitModifiedIfNotBatched();
    }
}

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

AppRuleMatch Layout::matchAppRule(const QString& windowClass) const
{
    if (windowClass.isEmpty() || m_appRules.isEmpty()) {
        return {};
    }
    for (const auto& rule : m_appRules) {
        if (windowClass.contains(rule.pattern, Qt::CaseInsensitive)) {
            return {rule.zoneNumber, rule.targetScreen};
        }
    }
    return {};
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

void Layout::clearZonePaddingOverride()
{
    setZonePadding(-1);
}

void Layout::clearOuterGapOverride()
{
    setOuterGap(-1);
    setUsePerSideOuterGap(false);
    setOuterGapTop(-1);
    setOuterGapBottom(-1);
    setOuterGapLeft(-1);
    setOuterGapRight(-1);
}

bool Layout::isSystemLayout() const
{
    if (m_sourcePath.isEmpty()) {
        return false; // New layouts (no source) are not system layouts
    }

    // System layouts are loaded from /usr/share or other system data directories
    // User layouts are in ~/.local/share
    // Check if source path is NOT under user's writable location
    // Use static to cache the user data path (it doesn't change during runtime)
    static const QString userDataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return !m_sourcePath.startsWith(userDataPath);
}

Zone* Layout::zone(int index) const
{
    if (index >= 0 && index < m_zones.size()) {
        return m_zones.at(index);
    }
    return nullptr;
}

Zone* Layout::zoneById(const QUuid& id) const
{
    auto it = std::find_if(m_zones.begin(), m_zones.end(), [&id](const Zone* z) {
        return z->id() == id;
    });
    return it != m_zones.end() ? *it : nullptr;
}

Zone* Layout::zoneByNumber(int number) const
{
    auto it = std::find_if(m_zones.begin(), m_zones.end(), [number](const Zone* z) {
        return z->zoneNumber() == number;
    });
    return it != m_zones.end() ? *it : nullptr;
}

void Layout::addZone(Zone* zone)
{
    if (zone && !m_zones.contains(zone)) {
        zone->setParent(this);
        zone->setZoneNumber(m_zones.size() + 1);
        m_zones.append(zone);
        m_lastRecalcGeometry = QRectF(); // Invalidate geometry cache
        Q_EMIT zoneAdded(zone);
        Q_EMIT zonesChanged();
        emitModifiedIfNotBatched();
    }
}

void Layout::removeZone(Zone* zone)
{
    if (zone && m_zones.removeOne(zone)) {
        m_lastRecalcGeometry = QRectF(); // Invalidate geometry cache
        Q_EMIT zoneRemoved(zone);
        zone->deleteLater();
        renumberZones();
        Q_EMIT zonesChanged();
        emitModifiedIfNotBatched();
    }
}

void Layout::removeZoneAt(int index)
{
    if (index >= 0 && index < m_zones.size()) {
        auto zone = m_zones.takeAt(index);
        m_lastRecalcGeometry = QRectF(); // Invalidate geometry cache
        Q_EMIT zoneRemoved(zone);
        zone->deleteLater();
        renumberZones();
        Q_EMIT zonesChanged();
        emitModifiedIfNotBatched();
    }
}

void Layout::clearZones()
{
    if (!m_zones.isEmpty()) {
        for (auto* zone : m_zones) {
            Q_EMIT zoneRemoved(zone);
            zone->deleteLater();
        }
        m_zones.clear();
        m_lastRecalcGeometry = QRectF(); // Invalidate geometry cache
        Q_EMIT zonesChanged();
        emitModifiedIfNotBatched();
    }
}

void Layout::moveZone(int fromIndex, int toIndex)
{
    if (fromIndex >= 0 && fromIndex < m_zones.size() && toIndex >= 0 && toIndex < m_zones.size()
        && fromIndex != toIndex) {
        m_zones.move(fromIndex, toIndex);
        renumberZones();
        Q_EMIT zonesChanged();
        emitModifiedIfNotBatched();
    }
}

void Layout::emitModifiedIfNotBatched()
{
    m_dirty = true;
    if (m_batchModifyDepth == 0) {
        Q_EMIT layoutModified();
    }
}

void Layout::beginBatchModify()
{
    ++m_batchModifyDepth;
}

void Layout::endBatchModify()
{
    if (m_batchModifyDepth > 0) {
        --m_batchModifyDepth;
    }
    if (m_batchModifyDepth == 0 && m_dirty) {
        Q_EMIT layoutModified();
    }
}

Zone* Layout::zoneAtPoint(const QPointF& point) const
{
    for (auto* zone : m_zones) {
        if (zone->containsPoint(point)) {
            return zone;
        }
    }
    return nullptr;
}

Zone* Layout::nearestZone(const QPointF& point, qreal maxDistance) const
{
    Zone* nearest = nullptr;
    qreal minDistance = std::numeric_limits<qreal>::max();

    for (auto* zone : m_zones) {
        qreal distance = zone->distanceToPoint(point);
        if (distance < minDistance) {
            minDistance = distance;
            nearest = zone;
        }
    }

    if (maxDistance >= 0 && minDistance > maxDistance) {
        return nullptr;
    }

    return nearest;
}

QVector<Zone*> Layout::zonesInRect(const QRectF& rect) const
{
    QVector<Zone*> result;
    for (auto* zone : m_zones) {
        if (zone->geometry().intersects(rect)) {
            result.append(zone);
        }
    }
    return result;
}

QVector<Zone*> Layout::adjacentZones(const QPointF& point, qreal threshold) const
{
    QVector<Zone*> result;
    for (auto* zone : m_zones) {
        if (zone->distanceToPoint(point) <= threshold) {
            result.append(zone);
        }
    }
    return result;
}

void Layout::recalculateZoneGeometries(const QRectF& screenGeometry)
{
    // Skip if geometry hasn't changed (prevents redundant recalculations)
    if (screenGeometry == m_lastRecalcGeometry) {
        return;
    }
    m_lastRecalcGeometry = screenGeometry;

    qCDebug(lcLayout) << "recalculateZoneGeometries layout= " << m_name << " screenGeometry= " << screenGeometry;
    for (auto* zone : m_zones) {
        QRectF absGeometry = zone->calculateAbsoluteGeometry(screenGeometry);
        zone->setGeometry(absGeometry);
    }
}

void Layout::renumberZones()
{
    for (int i = 0; i < m_zones.size(); ++i) {
        m_zones[i]->setZoneNumber(i + 1);
    }
}

QJsonObject Layout::toJson() const
{
    QJsonObject json;
    json[JsonKeys::Id] = m_id.toString();
    json[JsonKeys::Name] = m_name;
    json[JsonKeys::Type] = static_cast<int>(m_type);
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
    // Per-side outer gap overrides — only serialize if toggled on AND at least one side is set
    bool hasAnySide = (m_outerGapTop >= 0 || m_outerGapBottom >= 0
                       || m_outerGapLeft >= 0 || m_outerGapRight >= 0);
    if (m_usePerSideOuterGap && hasAnySide) {
        json[JsonKeys::UsePerSideOuterGap] = true;
        if (m_outerGapTop >= 0) json[JsonKeys::OuterGapTop] = m_outerGapTop;
        if (m_outerGapBottom >= 0) json[JsonKeys::OuterGapBottom] = m_outerGapBottom;
        if (m_outerGapLeft >= 0) json[JsonKeys::OuterGapLeft] = m_outerGapLeft;
        if (m_outerGapRight >= 0) json[JsonKeys::OuterGapRight] = m_outerGapRight;
    }
    json[JsonKeys::ShowZoneNumbers] = m_showZoneNumbers;
    if (m_defaultOrder != 999) {
        json[JsonKeys::DefaultOrder] = m_defaultOrder;
    }
    // Note: isBuiltIn is no longer serialized - it's determined by source path at load time

    // Shader support - only persist params belonging to the active shader
    if (!ShaderRegistry::isNoneShader(m_shaderId)) {
        json[JsonKeys::ShaderId] = m_shaderId;
    }
    if (!m_shaderParams.isEmpty()) {
        QVariantMap paramsToSave = m_shaderParams;

        // Strip stale params from other shaders and validate values
        auto* registry = ShaderRegistry::instance();
        if (registry && !ShaderRegistry::isNoneShader(m_shaderId)) {
            paramsToSave = registry->validateAndCoerceParams(m_shaderId, m_shaderParams);
        }

        if (!paramsToSave.isEmpty()) {
            json[JsonKeys::ShaderParams] = QJsonObject::fromVariantMap(paramsToSave);
        }
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
    layout->m_type = static_cast<LayoutType>(json[JsonKeys::Type].toInt());
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
    layout->m_defaultOrder = json[JsonKeys::DefaultOrder].toInt(999);
    // Note: sourcePath is set by LayoutManager after loading, not from JSON

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

    // Visibility filtering
    layout->m_hiddenFromSelector = json[JsonKeys::HiddenFromSelector].toBool(false);
    LayoutUtils::deserializeAllowLists(json, layout->m_allowedScreens, layout->m_allowedDesktops, layout->m_allowedActivities);

    // Migrate legacy connector names in allowedScreens to screen IDs
    for (int i = 0; i < layout->m_allowedScreens.size(); ++i) {
        if (Utils::isConnectorName(layout->m_allowedScreens[i])) {
            QString resolved = Utils::screenIdForName(layout->m_allowedScreens[i]);
            if (resolved != layout->m_allowedScreens[i]) {
                layout->m_allowedScreens[i] = resolved;
            } else {
                qCDebug(lcLayout) << "allowedScreens: could not resolve connector name"
                                  << layout->m_allowedScreens[i]
                                  << "to screen ID (monitor may be disconnected)";
            }
        }
    }

    const auto zonesArray = json[JsonKeys::Zones].toArray();
    for (const auto& zoneValue : zonesArray) {
        auto zone = Zone::fromJson(zoneValue.toObject(), layout);
        layout->m_zones.append(zone);
    }

    return layout;
}

// Static factory methods for predefined layouts

Layout* Layout::createColumnsLayout(int columns, QObject* parent)
{
    if (columns < 1) {
        columns = 1;
    }
    auto layout = new Layout(QStringLiteral("Columns (%1)").arg(columns), LayoutType::Columns, parent);
    layout->setDescription(QStringLiteral("Vertical columns layout"));

    qreal columnWidth = 1.0 / columns;
    for (int i = 0; i < columns; ++i) {
        auto zone = new Zone(layout);
        zone->setRelativeGeometry(QRectF(i * columnWidth, 0, columnWidth, 1.0));
        zone->setZoneNumber(i + 1);
        zone->setName(QStringLiteral("Column %1").arg(i + 1));
        layout->m_zones.append(zone);
    }

    return layout;
}

Layout* Layout::createRowsLayout(int rows, QObject* parent)
{
    if (rows < 1) {
        rows = 1;
    }
    auto layout = new Layout(QStringLiteral("Rows (%1)").arg(rows), LayoutType::Rows, parent);
    layout->setDescription(QStringLiteral("Horizontal rows layout"));

    qreal rowHeight = 1.0 / rows;
    for (int i = 0; i < rows; ++i) {
        auto zone = new Zone(layout);
        zone->setRelativeGeometry(QRectF(0, i * rowHeight, 1.0, rowHeight));
        zone->setZoneNumber(i + 1);
        zone->setName(QStringLiteral("Row %1").arg(i + 1));
        layout->m_zones.append(zone);
    }

    return layout;
}

Layout* Layout::createGridLayout(int columns, int rows, QObject* parent)
{
    if (columns < 1) {
        columns = 1;
    }
    if (rows < 1) {
        rows = 1;
    }
    auto layout = new Layout(QStringLiteral("Grid (%1x%2)").arg(columns).arg(rows), LayoutType::Grid, parent);
    layout->setDescription(QStringLiteral("Grid layout"));

    qreal columnWidth = 1.0 / columns;
    qreal rowHeight = 1.0 / rows;
    int zoneNum = 1;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < columns; ++col) {
            auto zone = new Zone(layout);
            zone->setRelativeGeometry(QRectF(col * columnWidth, row * rowHeight, columnWidth, rowHeight));
            zone->setZoneNumber(zoneNum++);
            zone->setName(QStringLiteral("Cell %1,%2").arg(row + 1).arg(col + 1));
            layout->m_zones.append(zone);
        }
    }

    return layout;
}

Layout* Layout::createPriorityGridLayout(QObject* parent)
{
    auto layout = new Layout(QStringLiteral("Priority Grid"), LayoutType::PriorityGrid, parent);
    layout->setDescription(QStringLiteral("Large primary zone with smaller secondary zones"));

    // Main zone (left 2/3)
    auto mainZone = new Zone(layout);
    mainZone->setRelativeGeometry(QRectF(0, 0, Defaults::PriorityGridMainRatio, 1.0));
    mainZone->setZoneNumber(1);
    mainZone->setName(QStringLiteral("Primary"));
    layout->m_zones.append(mainZone);

    // Top right
    auto topRight = new Zone(layout);
    topRight->setRelativeGeometry(
        QRectF(Defaults::PriorityGridMainRatio, 0, Defaults::PriorityGridSecondaryRatio, 0.5));
    topRight->setZoneNumber(2);
    topRight->setName(QStringLiteral("Secondary Top"));
    layout->m_zones.append(topRight);

    // Bottom right
    auto bottomRight = new Zone(layout);
    bottomRight->setRelativeGeometry(
        QRectF(Defaults::PriorityGridMainRatio, 0.5, Defaults::PriorityGridSecondaryRatio, 0.5));
    bottomRight->setZoneNumber(3);
    bottomRight->setName(QStringLiteral("Secondary Bottom"));
    layout->m_zones.append(bottomRight);

    return layout;
}

Layout* Layout::createFocusLayout(QObject* parent)
{
    auto layout = new Layout(QStringLiteral("Focus"), LayoutType::Focus, parent);
    layout->setDescription(QStringLiteral("Large center zone with side panels"));

    // Left panel
    auto left = new Zone(layout);
    left->setRelativeGeometry(QRectF(0, 0, Defaults::FocusSideRatio, 1.0));
    left->setZoneNumber(1);
    left->setName(QStringLiteral("Left Panel"));
    layout->m_zones.append(left);

    // Center (main focus)
    auto center = new Zone(layout);
    center->setRelativeGeometry(QRectF(Defaults::FocusSideRatio, 0, Defaults::FocusMainRatio, 1.0));
    center->setZoneNumber(2);
    center->setName(QStringLiteral("Focus"));
    layout->m_zones.append(center);

    // Right panel - starts after side + main
    constexpr qreal rightStart = Defaults::FocusSideRatio + Defaults::FocusMainRatio;
    auto right = new Zone(layout);
    right->setRelativeGeometry(QRectF(rightStart, 0, Defaults::FocusSideRatio, 1.0));
    right->setZoneNumber(3);
    right->setName(QStringLiteral("Right Panel"));
    layout->m_zones.append(right);

    return layout;
}

} // namespace PlasmaZones
