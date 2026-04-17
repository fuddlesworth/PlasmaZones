// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorZones/Layout.h>
#include "constants.h"
#include <PhosphorZones/LayoutUtils.h>
#include "zoneslogging.h"
#include <window_id.h>
#include <QJsonArray>
#include <QStandardPaths>
#include <algorithm>
#include <limits>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Macros for setter patterns
// Reduces boilerplate for layout property setters
// ═══════════════════════════════════════════════════════════════════════════════

// Simple setter: if changed, update member, emit specific signal and layoutModified
#define LAYOUT_SETTER(Type, name, member, signal)                                                                      \
    void Layout::set##name(Type value)                                                                                 \
    {                                                                                                                  \
        if (member != value) {                                                                                         \
            member = value;                                                                                            \
            Q_EMIT signal();                                                                                           \
            emitModifiedIfNotBatched();                                                                                \
        }                                                                                                              \
    }

// Simple setter without layoutModified signal (for internal properties)
#define LAYOUT_SETTER_NO_MODIFIED(Type, name, member, signal)                                                          \
    void Layout::set##name(Type value)                                                                                 \
    {                                                                                                                  \
        if (member != value) {                                                                                         \
            member = value;                                                                                            \
            Q_EMIT signal();                                                                                           \
        }                                                                                                              \
    }

// Setter that allows -1 (use global setting) or any non-negative value
#define LAYOUT_SETTER_MIN_NEGATIVE_ONE(name, member, signal)                                                           \
    void Layout::set##name(int value)                                                                                  \
    {                                                                                                                  \
        if (value < -1) {                                                                                              \
            value = -1;                                                                                                \
        }                                                                                                              \
        if (member != value) {                                                                                         \
            member = value;                                                                                            \
            Q_EMIT signal();                                                                                           \
            emitModifiedIfNotBatched();                                                                                \
        }                                                                                                              \
    }

Layout::Layout(QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
{
}

Layout::Layout(const QString& name, QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name(name)
{
}

Layout::Layout(const Layout& other)
    : QObject(other.parent())
    , m_id(QUuid::createUuid()) // New layout gets new ID
    , m_name(other.m_name)
    , m_description(other.m_description)
    , m_zonePadding(other.m_zonePadding)
    , m_outerGap(other.m_outerGap)
    , m_usePerSideOuterGap(other.m_usePerSideOuterGap)
    , m_outerGapTop(other.m_outerGapTop)
    , m_outerGapBottom(other.m_outerGapBottom)
    , m_outerGapLeft(other.m_outerGapLeft)
    , m_outerGapRight(other.m_outerGapRight)
    , m_showZoneNumbers(other.m_showZoneNumbers)
    , m_overlayDisplayMode(other.m_overlayDisplayMode)
    , m_sourcePath() // Copies have no source path (will be saved to user directory)
    , m_defaultOrder(other.m_defaultOrder)
    , m_appRules(other.m_appRules)
    , m_autoAssign(other.m_autoAssign)
    , m_useFullScreenGeometry(other.m_useFullScreenGeometry)
    , m_shaderId(other.m_shaderId)
    , m_shaderParams(other.m_shaderParams)
    , m_aspectRatioClass(other.m_aspectRatioClass)
    , m_minAspectRatio(other.m_minAspectRatio)
    , m_maxAspectRatio(other.m_maxAspectRatio)
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
        m_description = other.m_description;
        m_zonePadding = other.m_zonePadding;
        m_outerGap = other.m_outerGap;
        m_usePerSideOuterGap = other.m_usePerSideOuterGap;
        m_outerGapTop = other.m_outerGapTop;
        m_outerGapBottom = other.m_outerGapBottom;
        m_outerGapLeft = other.m_outerGapLeft;
        m_outerGapRight = other.m_outerGapRight;
        m_showZoneNumbers = other.m_showZoneNumbers;
        m_overlayDisplayMode = other.m_overlayDisplayMode;
        m_defaultOrder = other.m_defaultOrder;
        m_sourcePath.clear(); // Assignment creates a user copy (will be saved to user directory)
        m_systemSourcePath.clear(); // New copy has no system origin
        m_shaderId = other.m_shaderId;
        m_shaderParams = other.m_shaderParams;
        bool rulesChanged = m_appRules != other.m_appRules;
        m_appRules = other.m_appRules;
        bool autoAssignDiff = m_autoAssign != other.m_autoAssign;
        m_autoAssign = other.m_autoAssign;
        bool fullScreenGeomDiff = m_useFullScreenGeometry != other.m_useFullScreenGeometry;
        m_useFullScreenGeometry = other.m_useFullScreenGeometry;
        bool arChanged = m_aspectRatioClass != other.m_aspectRatioClass
            || !qFuzzyCompare(1.0 + m_minAspectRatio, 1.0 + other.m_minAspectRatio)
            || !qFuzzyCompare(1.0 + m_maxAspectRatio, 1.0 + other.m_maxAspectRatio);
        m_aspectRatioClass = other.m_aspectRatioClass;
        m_minAspectRatio = other.m_minAspectRatio;
        m_maxAspectRatio = other.m_maxAspectRatio;
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
        if (hiddenChanged)
            Q_EMIT hiddenFromSelectorChanged();
        if (screensChanged)
            Q_EMIT allowedScreensChanged();
        if (desktopsChanged)
            Q_EMIT allowedDesktopsChanged();
        if (activitiesChanged)
            Q_EMIT allowedActivitiesChanged();
        if (rulesChanged)
            Q_EMIT appRulesChanged();
        if (autoAssignDiff)
            Q_EMIT autoAssignChanged();
        if (fullScreenGeomDiff)
            Q_EMIT useFullScreenGeometryChanged();
        if (arChanged)
            Q_EMIT aspectRatioClassChanged();

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
LAYOUT_SETTER(const QString&, Description, m_description, descriptionChanged)
LAYOUT_SETTER(bool, ShowZoneNumbers, m_showZoneNumbers, showZoneNumbersChanged)
LAYOUT_SETTER_MIN_NEGATIVE_ONE(OverlayDisplayMode, m_overlayDisplayMode, overlayDisplayModeChanged)
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

bool Layout::hasFixedGeometryZones() const
{
    for (Zone* zone : m_zones) {
        if (zone && zone->isFixedGeometry())
            return true;
    }
    return false;
}

// Aspect ratio classification setters
void Layout::setAspectRatioClass(::PhosphorLayout::AspectRatioClass cls)
{
    if (m_aspectRatioClass != cls) {
        m_aspectRatioClass = cls;
        Q_EMIT aspectRatioClassChanged();
        emitModifiedIfNotBatched();
    }
}

void Layout::setAspectRatioClassInt(int cls)
{
    if (cls < 0 || cls > static_cast<int>(::PhosphorLayout::AspectRatioClass::Portrait)) {
        cls = static_cast<int>(::PhosphorLayout::AspectRatioClass::Any);
    }
    setAspectRatioClass(static_cast<::PhosphorLayout::AspectRatioClass>(cls));
}

void Layout::setMinAspectRatio(qreal ratio)
{
    // Use 1.0+ shift to avoid qFuzzyCompare issues near zero
    if (!qFuzzyCompare(1.0 + m_minAspectRatio, 1.0 + ratio)) {
        m_minAspectRatio = ratio;
        Q_EMIT aspectRatioClassChanged();
        emitModifiedIfNotBatched();
    }
}

void Layout::setMaxAspectRatio(qreal ratio)
{
    // Use 1.0+ shift to avoid qFuzzyCompare issues near zero
    if (!qFuzzyCompare(1.0 + m_maxAspectRatio, 1.0 + ratio)) {
        m_maxAspectRatio = ratio;
        Q_EMIT aspectRatioClassChanged();
        emitModifiedIfNotBatched();
    }
}

bool Layout::matchesAspectRatio(qreal screenAspectRatio) const
{
    // Explicit min/max bounds take precedence over class matching
    if (m_minAspectRatio > 0.0 || m_maxAspectRatio > 0.0) {
        if (m_minAspectRatio > 0.0 && screenAspectRatio < m_minAspectRatio) {
            return false;
        }
        if (m_maxAspectRatio > 0.0 && screenAspectRatio > m_maxAspectRatio) {
            return false;
        }
        return true;
    }

    // Fall back to class matching
    auto screenClass = ::PhosphorLayout::ScreenClassification::classify(screenAspectRatio);
    return ::PhosphorLayout::ScreenClassification::matches(m_aspectRatioClass, screenClass);
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

AppRuleMatch Layout::matchAppRule(const QString& windowClass) const
{
    if (windowClass.isEmpty() || m_appRules.isEmpty()) {
        return {};
    }
    for (const auto& rule : m_appRules) {
        if (rule.pattern.isEmpty()) {
            continue;
        }
        // Segment-aware match: "firefox" matches "org.mozilla.firefox" (dot-boundary),
        // "org.mozilla.firefox" matches "firefox", exact match always works.
        // Prevents "fire" from matching "firefox" (no dot boundary).
        if (WindowIdUtils::appIdMatches(windowClass, rule.pattern)) {
            return {rule.zoneNumber, rule.targetScreen};
        }
    }
    return {};
}

void Layout::clearZonePaddingOverride()
{
    setZonePadding(-1);
}

void Layout::clearOverlayDisplayModeOverride()
{
    setOverlayDisplayMode(-1);
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
    const QString userDataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
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
    // When zones overlap, pick the smallest zone containing the point.
    // "Area covered" heuristic: the cursor covers
    // a larger proportion of a smaller zone, so it wins the overlap.
    Zone* best = nullptr;
    qreal bestArea = std::numeric_limits<qreal>::max();

    for (auto* zone : m_zones) {
        if (zone->containsPoint(point)) {
            qreal area = zone->geometry().width() * zone->geometry().height();
            if (area < bestArea) {
                bestArea = area;
                best = zone;
            }
        }
    }
    return best;
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

    qCDebug(PhosphorZones::lcLayoutLib) << "recalculateZoneGeometries layout=" << m_name
                                        << "screenGeometry=" << screenGeometry;
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

} // namespace PlasmaZones
