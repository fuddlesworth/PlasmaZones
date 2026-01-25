// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zone.h"
#include "constants.h"
#include <QJsonArray>
#include <cmath>

namespace PlasmaZones {

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
    m_shortcut = other.m_shortcut;
    m_highlightColor = other.m_highlightColor;
    m_inactiveColor = other.m_inactiveColor;
    m_borderColor = other.m_borderColor;
    m_activeOpacity = other.m_activeOpacity;
    m_inactiveOpacity = other.m_inactiveOpacity;
    m_borderWidth = other.m_borderWidth;
    m_borderRadius = other.m_borderRadius;
    m_isHighlighted = other.m_isHighlighted;
    m_useCustomColors = other.m_useCustomColors;
}

bool Zone::operator==(const Zone& other) const
{
    return m_id == other.m_id;
}

void Zone::setName(const QString& name)
{
    if (m_name != name) {
        m_name = name;
        Q_EMIT nameChanged();
    }
}

void Zone::setGeometry(const QRectF& geometry)
{
    if (m_geometry != geometry) {
        m_geometry = geometry;
        Q_EMIT geometryChanged();
    }
}

void Zone::setRelativeGeometry(const QRectF& relativeGeometry)
{
    if (m_relativeGeometry != relativeGeometry) {
        m_relativeGeometry = relativeGeometry;
        Q_EMIT relativeGeometryChanged();
    }
}

void Zone::setZoneNumber(int number)
{
    if (m_zoneNumber != number) {
        m_zoneNumber = number;
        Q_EMIT zoneNumberChanged();
    }
}

void Zone::setShortcut(const QString& shortcut)
{
    if (m_shortcut != shortcut) {
        m_shortcut = shortcut;
        Q_EMIT shortcutChanged();
    }
}

void Zone::setHighlightColor(const QColor& color)
{
    if (m_highlightColor != color) {
        m_highlightColor = color;
        Q_EMIT highlightColorChanged();
    }
}

void Zone::setInactiveColor(const QColor& color)
{
    if (m_inactiveColor != color) {
        m_inactiveColor = color;
        Q_EMIT inactiveColorChanged();
    }
}

void Zone::setBorderColor(const QColor& color)
{
    if (m_borderColor != color) {
        m_borderColor = color;
        Q_EMIT borderColorChanged();
    }
}

void Zone::setActiveOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (!qFuzzyCompare(m_activeOpacity, opacity)) {
        m_activeOpacity = opacity;
        Q_EMIT activeOpacityChanged();
    }
}

void Zone::setInactiveOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (!qFuzzyCompare(m_inactiveOpacity, opacity)) {
        m_inactiveOpacity = opacity;
        Q_EMIT inactiveOpacityChanged();
    }
}

void Zone::setBorderWidth(int width)
{
    width = qMax(0, width);
    if (m_borderWidth != width) {
        m_borderWidth = width;
        Q_EMIT borderWidthChanged();
    }
}

void Zone::setBorderRadius(int radius)
{
    radius = qMax(0, radius);
    if (m_borderRadius != radius) {
        m_borderRadius = radius;
        Q_EMIT borderRadiusChanged();
    }
}

void Zone::setHighlighted(bool highlighted)
{
    if (m_isHighlighted != highlighted) {
        m_isHighlighted = highlighted;
        Q_EMIT highlightedChanged();
    }
}

void Zone::setUseCustomColors(bool useCustom)
{
    if (m_useCustomColors != useCustom) {
        m_useCustomColors = useCustom;
        Q_EMIT useCustomColorsChanged();
    }
}

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
    return QRectF(screenGeometry.x() + m_relativeGeometry.x() * screenGeometry.width(),
                  screenGeometry.y() + m_relativeGeometry.y() * screenGeometry.height(),
                  m_relativeGeometry.width() * screenGeometry.width(),
                  m_relativeGeometry.height() * screenGeometry.height());
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
    json[Shortcut] = m_shortcut;

    // Relative geometry for resolution independence
    QJsonObject relGeo;
    relGeo[X] = m_relativeGeometry.x();
    relGeo[Y] = m_relativeGeometry.y();
    relGeo[Width] = m_relativeGeometry.width();
    relGeo[Height] = m_relativeGeometry.height();
    json[RelativeGeometry] = relGeo;

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
    zone->m_shortcut = json[Shortcut].toString();

    // Relative geometry
    const auto relGeo = json[RelativeGeometry].toObject();
    zone->m_relativeGeometry =
        QRectF(relGeo[X].toDouble(), relGeo[Y].toDouble(), relGeo[Width].toDouble(), relGeo[Height].toDouble());

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
