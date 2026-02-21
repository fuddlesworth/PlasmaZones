// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zone.h"
#include "constants.h"
#include <QJsonArray>
#include <cmath>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Macros for setter patterns
// Reduces boilerplate for zone property setters
// ═══════════════════════════════════════════════════════════════════════════════

// Simple setter: if changed, update member and emit signal
#define ZONE_SETTER(Type, name, member, signal) \
    void Zone::set##name(Type value) \
    { \
        if (member != value) { \
            member = value; \
            Q_EMIT signal(); \
        } \
    }

// Clamped int setter with minimum of 0
#define ZONE_SETTER_MIN_ZERO(name, member, signal) \
    void Zone::set##name(int value) \
    { \
        value = qMax(0, value); \
        if (member != value) { \
            member = value; \
            Q_EMIT signal(); \
        } \
    }

// Clamped qreal setter for opacity (0.0-1.0) with fuzzy compare
#define ZONE_SETTER_OPACITY(name, member, signal) \
    void Zone::set##name(qreal opacity) \
    { \
        opacity = qBound(0.0, opacity, 1.0); \
        if (!qFuzzyCompare(member, opacity)) { \
            member = opacity; \
            Q_EMIT signal(); \
        } \
    }

Zone::Zone(QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
{
}

Zone::Zone(const QRectF& geometry, QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_geometry(geometry)
{
}

Zone* Zone::clone(QObject* parent) const
{
    auto* newZone = new Zone(parent);
    newZone->copyPropertiesFrom(*this);
    return newZone;
}

void Zone::copyPropertiesFrom(const Zone& other)
{
    // Note: ID is NOT copied - the clone gets a new unique ID
    m_name = other.m_name;
    m_geometry = other.m_geometry;
    m_relativeGeometry = other.m_relativeGeometry;
    m_zoneNumber = other.m_zoneNumber;
    m_highlightColor = other.m_highlightColor;
    m_inactiveColor = other.m_inactiveColor;
    m_borderColor = other.m_borderColor;
    m_activeOpacity = other.m_activeOpacity;
    m_inactiveOpacity = other.m_inactiveOpacity;
    m_borderWidth = other.m_borderWidth;
    m_borderRadius = other.m_borderRadius;
    m_isHighlighted = other.m_isHighlighted;
    m_useCustomColors = other.m_useCustomColors;
    m_geometryMode = other.m_geometryMode;
    m_fixedGeometry = other.m_fixedGeometry;
}

bool Zone::operator==(const Zone& other) const
{
    return m_id == other.m_id && m_geometryMode == other.m_geometryMode && m_fixedGeometry == other.m_fixedGeometry;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Zone Property Setters
// ═══════════════════════════════════════════════════════════════════════════════

// Simple property setters
ZONE_SETTER(const QString&, Name, m_name, nameChanged)
ZONE_SETTER(const QRectF&, Geometry, m_geometry, geometryChanged)
ZONE_SETTER(const QRectF&, RelativeGeometry, m_relativeGeometry, relativeGeometryChanged)
ZONE_SETTER(int, ZoneNumber, m_zoneNumber, zoneNumberChanged)

// Color setters
ZONE_SETTER(const QColor&, HighlightColor, m_highlightColor, highlightColorChanged)
ZONE_SETTER(const QColor&, InactiveColor, m_inactiveColor, inactiveColorChanged)
ZONE_SETTER(const QColor&, BorderColor, m_borderColor, borderColorChanged)

// Opacity setters (clamped 0.0-1.0 with fuzzy compare)
ZONE_SETTER_OPACITY(ActiveOpacity, m_activeOpacity, activeOpacityChanged)
ZONE_SETTER_OPACITY(InactiveOpacity, m_inactiveOpacity, inactiveOpacityChanged)

// Border setters (clamped min 0)
ZONE_SETTER_MIN_ZERO(BorderWidth, m_borderWidth, borderWidthChanged)
ZONE_SETTER_MIN_ZERO(BorderRadius, m_borderRadius, borderRadiusChanged)

// Bool setters
ZONE_SETTER(bool, Highlighted, m_isHighlighted, highlightedChanged)
ZONE_SETTER(bool, UseCustomColors, m_useCustomColors, useCustomColorsChanged)

// Geometry mode setters
void Zone::setGeometryMode(ZoneGeometryMode mode)
{
    if (m_geometryMode != mode) {
        m_geometryMode = mode;
        Q_EMIT geometryModeChanged();
    }
}

void Zone::setGeometryModeInt(int mode)
{
    setGeometryMode(static_cast<ZoneGeometryMode>(mode));
}

ZONE_SETTER(const QRectF&, FixedGeometry, m_fixedGeometry, fixedGeometryChanged)

bool Zone::containsPoint(const QPointF& point) const
{
    return m_geometry.contains(point);
}

qreal Zone::distanceToPoint(const QPointF& point) const
{
    if (containsPoint(point)) {
        return 0.0;
    }

    // Calculate minimum distance to zone edges
    qreal dx = 0.0;
    qreal dy = 0.0;

    if (point.x() < m_geometry.left()) {
        dx = m_geometry.left() - point.x();
    } else if (point.x() > m_geometry.right()) {
        dx = point.x() - m_geometry.right();
    }

    if (point.y() < m_geometry.top()) {
        dy = m_geometry.top() - point.y();
    } else if (point.y() > m_geometry.bottom()) {
        dy = point.y() - m_geometry.bottom();
    }

    return std::sqrt(dx * dx + dy * dy);
}

QRectF Zone::calculateAbsoluteGeometry(const QRectF& screenGeometry) const
{
    if (m_geometryMode == ZoneGeometryMode::Fixed) {
        // Fixed mode: pixel coords relative to screen origin
        return QRectF(screenGeometry.x() + m_fixedGeometry.x(),
                      screenGeometry.y() + m_fixedGeometry.y(),
                      m_fixedGeometry.width(),
                      m_fixedGeometry.height());
    }
    // Relative mode: multiply by screen dimensions
    return QRectF(screenGeometry.x() + m_relativeGeometry.x() * screenGeometry.width(),
                  screenGeometry.y() + m_relativeGeometry.y() * screenGeometry.height(),
                  m_relativeGeometry.width() * screenGeometry.width(),
                  m_relativeGeometry.height() * screenGeometry.height());
}

QRectF Zone::normalizedGeometry(const QRectF& referenceGeometry) const
{
    if (m_geometryMode == ZoneGeometryMode::Fixed && referenceGeometry.width() > 0 && referenceGeometry.height() > 0) {
        return QRectF(m_fixedGeometry.x() / referenceGeometry.width(),
                      m_fixedGeometry.y() / referenceGeometry.height(),
                      m_fixedGeometry.width() / referenceGeometry.width(),
                      m_fixedGeometry.height() / referenceGeometry.height());
    }
    return m_relativeGeometry;
}

QRectF Zone::applyPadding(int padding) const
{
    return m_geometry.adjusted(padding, padding, -padding, -padding);
}

QJsonObject Zone::toJson() const
{
    using namespace JsonKeys;

    QJsonObject json;
    json[Id] = m_id.toString();
    json[Name] = m_name;
    json[ZoneNumber] = m_zoneNumber;

    // Relative geometry for resolution independence (always written for backward compat)
    QJsonObject relGeo;
    relGeo[X] = m_relativeGeometry.x();
    relGeo[Y] = m_relativeGeometry.y();
    relGeo[Width] = m_relativeGeometry.width();
    relGeo[Height] = m_relativeGeometry.height();
    json[RelativeGeometry] = relGeo;

    // Per-zone geometry mode (only write when Fixed to maintain backward compat)
    if (m_geometryMode == ZoneGeometryMode::Fixed) {
        json[GeometryMode] = static_cast<int>(m_geometryMode);

        QJsonObject fixedGeo;
        fixedGeo[X] = m_fixedGeometry.x();
        fixedGeo[Y] = m_fixedGeometry.y();
        fixedGeo[Width] = m_fixedGeometry.width();
        fixedGeo[Height] = m_fixedGeometry.height();
        json[FixedGeometry] = fixedGeo;
    }

    // Appearance
    QJsonObject appearance;
    appearance[HighlightColor] = m_highlightColor.name(QColor::HexArgb);
    appearance[InactiveColor] = m_inactiveColor.name(QColor::HexArgb);
    appearance[BorderColor] = m_borderColor.name(QColor::HexArgb);
    appearance[ActiveOpacity] = m_activeOpacity;
    appearance[InactiveOpacity] = m_inactiveOpacity;
    appearance[BorderWidth] = m_borderWidth;
    appearance[BorderRadius] = m_borderRadius;
    appearance[UseCustomColors] = m_useCustomColors;
    json[Appearance] = appearance;

    return json;
}

Zone* Zone::fromJson(const QJsonObject& json, QObject* parent)
{
    using namespace JsonKeys;

    auto zone = new Zone(parent);

    zone->m_id = QUuid::fromString(json[Id].toString());
    if (zone->m_id.isNull()) {
        zone->m_id = QUuid::createUuid();
    }

    zone->m_name = json[Name].toString();
    zone->m_zoneNumber = json[ZoneNumber].toInt();

    // Relative geometry
    const auto relGeo = json[RelativeGeometry].toObject();
    zone->m_relativeGeometry =
        QRectF(relGeo[X].toDouble(), relGeo[Y].toDouble(), relGeo[Width].toDouble(), relGeo[Height].toDouble());

    // Per-zone geometry mode (default Relative if missing)
    zone->m_geometryMode = static_cast<ZoneGeometryMode>(json[GeometryMode].toInt(0));

    // Fixed geometry (only present when mode is Fixed)
    if (json.contains(FixedGeometry)) {
        const auto fixedGeo = json[FixedGeometry].toObject();
        zone->m_fixedGeometry =
            QRectF(fixedGeo[X].toDouble(), fixedGeo[Y].toDouble(), fixedGeo[Width].toDouble(), fixedGeo[Height].toDouble());
    }

    // Appearance
    const auto appearance = json[Appearance].toObject();
    if (!appearance.isEmpty()) {
        zone->m_highlightColor = QColor(appearance[HighlightColor].toString());
        zone->m_inactiveColor = QColor(appearance[InactiveColor].toString());
        zone->m_borderColor = QColor(appearance[BorderColor].toString());
        zone->m_activeOpacity = appearance[ActiveOpacity].toDouble(Defaults::Opacity);
        zone->m_inactiveOpacity = appearance[InactiveOpacity].toDouble(Defaults::InactiveOpacity);
        zone->m_borderWidth = appearance[BorderWidth].toInt(Defaults::BorderWidth);
        zone->m_borderRadius = appearance[BorderRadius].toInt(Defaults::BorderRadius);
        // Check if useCustomColors exists in JSON, default to false if missing
        zone->m_useCustomColors = appearance.contains(UseCustomColors) ? appearance[UseCustomColors].toBool() : false;
    }

    return zone;
}

} // namespace PlasmaZones
