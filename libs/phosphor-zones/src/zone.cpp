// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneJsonKeys.h>
#include <PhosphorZones/ZoneDefaults.h>
#include "zoneslogging.h"
#include <QJsonArray>
#include <cmath>

namespace PhosphorZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Macros for setter patterns
// Reduces boilerplate for zone property setters
// ═══════════════════════════════════════════════════════════════════════════════

// Simple setter: if changed, update member and emit signal
#define ZONE_SETTER(Type, name, member, signal)                                                                        \
    void Zone::set##name(Type value)                                                                                   \
    {                                                                                                                  \
        if (member != value) {                                                                                         \
            member = value;                                                                                            \
            Q_EMIT signal();                                                                                           \
        }                                                                                                              \
    }

// Clamped int setter with minimum of 0
#define ZONE_SETTER_MIN_ZERO(name, member, signal)                                                                     \
    void Zone::set##name(int value)                                                                                    \
    {                                                                                                                  \
        value = qMax(0, value);                                                                                        \
        if (member != value) {                                                                                         \
            member = value;                                                                                            \
            Q_EMIT signal();                                                                                           \
        }                                                                                                              \
    }

// Clamped qreal setter for opacity (0.0-1.0) with fuzzy compare
#define ZONE_SETTER_OPACITY(name, member, signal)                                                                      \
    void Zone::set##name(qreal opacity)                                                                                \
    {                                                                                                                  \
        opacity = qBound(0.0, opacity, 1.0);                                                                           \
        if (!qFuzzyCompare(1.0 + member, 1.0 + opacity)) {                                                             \
            member = opacity;                                                                                          \
            Q_EMIT signal();                                                                                           \
        }                                                                                                              \
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
    m_overlayDisplayMode = other.m_overlayDisplayMode;
    m_geometryMode = other.m_geometryMode;
    m_fixedGeometry = other.m_fixedGeometry;
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

// Overlay display mode setter (allows -1 for "use layout/global")
void Zone::setOverlayDisplayMode(int mode)
{
    if (mode < -1)
        mode = -1;
    if (m_overlayDisplayMode != mode) {
        m_overlayDisplayMode = mode;
        Q_EMIT overlayDisplayModeChanged();
    }
}

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
    if (mode < 0 || mode > static_cast<int>(ZoneGeometryMode::Fixed))
        return;
    setGeometryMode(static_cast<ZoneGeometryMode>(mode));
}

ZONE_SETTER(const QRectF&, FixedGeometry, m_fixedGeometry, fixedGeometryChanged)

QRectF Zone::sanitizeFixedGeometry(const QRectF& geometry)
{
    const qreal x = std::isfinite(geometry.x()) ? geometry.x() : 0.0;
    const qreal y = std::isfinite(geometry.y()) ? geometry.y() : 0.0;
    const qreal width = std::isfinite(geometry.width()) ? qMax(0.0, geometry.width()) : 0.0;
    const qreal height = std::isfinite(geometry.height()) ? qMax(0.0, geometry.height()) : 0.0;
    return QRectF(x, y, width, height);
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

QRectF Zone::computeAbsoluteGeometry(ZoneGeometryMode mode, const QRectF& relativeGeometry, const QRectF& fixedGeometry,
                                     const QRectF& screenGeometry)
{
    if (mode == ZoneGeometryMode::Fixed) {
        // Fixed mode: pixel coords relative to screen origin
        return QRectF(screenGeometry.x() + fixedGeometry.x(), screenGeometry.y() + fixedGeometry.y(),
                      fixedGeometry.width(), fixedGeometry.height());
    }
    // Relative mode: multiply by screen dimensions
    return QRectF(screenGeometry.x() + relativeGeometry.x() * screenGeometry.width(),
                  screenGeometry.y() + relativeGeometry.y() * screenGeometry.height(),
                  relativeGeometry.width() * screenGeometry.width(),
                  relativeGeometry.height() * screenGeometry.height());
}

QRectF Zone::calculateAbsoluteGeometry(const QRectF& screenGeometry) const
{
    return computeAbsoluteGeometry(m_geometryMode, m_relativeGeometry, m_fixedGeometry, screenGeometry);
}

QRectF Zone::normalizedGeometry(const QRectF& referenceGeometry) const
{
    if (m_geometryMode == ZoneGeometryMode::Fixed && referenceGeometry.width() > 0 && referenceGeometry.height() > 0) {
        return QRectF(m_fixedGeometry.x() / referenceGeometry.width(), m_fixedGeometry.y() / referenceGeometry.height(),
                      m_fixedGeometry.width() / referenceGeometry.width(),
                      m_fixedGeometry.height() / referenceGeometry.height());
    }
    return m_relativeGeometry;
}

QJsonObject Zone::toJson(const QRectF& referenceGeometry) const
{
    using namespace ::PhosphorZones::ZoneJsonKeys;

    QJsonObject json;
    json[Id] = m_id.toString();
    json[Name] = m_name;
    json[ZoneNumber] = m_zoneNumber;

    // Relative geometry for resolution independence (always written for backward compat)
    // For fixed-mode zones, compute correct 0-1 coords from pixel geometry so that
    // consumers reading only relativeGeometry (previews, KCM thumbnails) show correct positions.
    //
    // Clamped to 0–1, the range the key is defined over and the same range
    // fromJson clamps to on the way back in, so the two ends of the round-trip
    // agree. This is the ONLY thing that keeps the emitted value inside the
    // schema, and the schema gate drops the WHOLE layout when it fails: a fixed
    // zone can sit at a negative offset (the format allows it, see fromJson) or
    // overflow the screen it is normalized against, and either way the raw
    // quotient lands outside 0–1. Clamping costs nothing real — a fixed zone
    // renders from fixedGeometry below, which round-trips untouched, so the
    // clamp only bounds the derived hint the preview consumers read, and it
    // bounds it to the closest true statement (the zone's edge of the screen).
    const QRectF normGeo = normalizedGeometry(referenceGeometry);
    QJsonObject relGeo;
    relGeo[X] = qBound(0.0, normGeo.x(), 1.0);
    relGeo[Y] = qBound(0.0, normGeo.y(), 1.0);
    relGeo[Width] = qBound(0.0, normGeo.width(), 1.0);
    relGeo[Height] = qBound(0.0, normGeo.height(), 1.0);
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
    if (m_overlayDisplayMode >= 0) {
        appearance[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode] = m_overlayDisplayMode;
    }
    json[Appearance] = appearance;

    return json;
}

Zone* Zone::fromJson(const QJsonObject& json, QObject* parent)
{
    using namespace ::PhosphorZones::ZoneJsonKeys;

    auto zone = new Zone(parent);

    zone->m_id = QUuid::fromString(json[Id].toString());
    if (zone->m_id.isNull()) {
        zone->m_id = QUuid::createUuid();
    }

    zone->m_name = json[Name].toString();
    zone->m_zoneNumber = json[ZoneNumber].toInt();

    // Relative geometry — clamp 0..1 to defend against malformed input.
    const auto relGeo = json[RelativeGeometry].toObject();
    zone->m_relativeGeometry =
        QRectF(qBound(0.0, relGeo[X].toDouble(), 1.0), qBound(0.0, relGeo[Y].toDouble(), 1.0),
               qBound(0.0, relGeo[Width].toDouble(), 1.0), qBound(0.0, relGeo[Height].toDouble(), 1.0));

    // Per-zone geometry mode (default Relative if missing). Routed through the
    // validating setter rather than a raw cast: the cast accepts any int, and
    // the schema does not constrain the key, so "geometryMode": 7 would survive
    // every gate and then stick forever — toJson only writes the key when the
    // mode is Fixed, so the garbage would never be re-emitted and never
    // repaired. setGeometryModeInt leaves an out-of-range value at the default.
    const int rawMode = json[GeometryMode].toInt(0);
    zone->setGeometryModeInt(rawMode);
    if (rawMode != static_cast<int>(zone->m_geometryMode)) {
        qCWarning(lcZonesLib) << "Zone::fromJson: out-of-range GeometryMode" << rawMode << "for zone"
                              << zone->m_id.toString() << "— falling back to" << static_cast<int>(zone->m_geometryMode);
    }

    // Fixed geometry (only present when mode is Fixed). X/Y are pixel offsets
    // (negative allowed for off-screen positioning); width/height must be > 0.
    if (json.contains(FixedGeometry)) {
        const auto fixedGeo = json[FixedGeometry].toObject();
        zone->m_fixedGeometry = sanitizeFixedGeometry(QRectF(fixedGeo[X].toDouble(), fixedGeo[Y].toDouble(),
                                                             fixedGeo[Width].toDouble(), fixedGeo[Height].toDouble()));
    }
    // GeometryMode=Fixed with no usable FixedGeometry — the key is absent, or
    // it carries a payload that cannot describe a zone that renders (a negative
    // or non-finite extent). A hand-edited / partial JSON would otherwise
    // silently produce an invisible zone anchored at the screen origin that
    // swallows snap targets, and a zero-extent one would additionally normalize
    // to a zero-extent relativeGeometry that the schema rejects on the next
    // read, taking the whole layout with it. Drop back to Relative so the zone
    // renders with its authored relativeGeometry instead.
    if (zone->m_geometryMode == ZoneGeometryMode::Fixed && zone->m_fixedGeometry.isEmpty()) {
        qCWarning(lcZonesLib) << "Zone::fromJson: GeometryMode=Fixed but FixedGeometry missing or degenerate for zone"
                              << zone->m_id.toString() << "— downgrading to Relative";
        zone->m_geometryMode = ZoneGeometryMode::Relative;
    }

    // Appearance — colours fall back to ZoneDefaults when missing or invalid so
    // a partial appearance block (one good colour, one malformed) still yields a
    // fully-populated zone instead of a default-constructed QColor for the bad key.
    const auto appearance = json[Appearance].toObject();
    if (!appearance.isEmpty()) {
        const QColor highlight(appearance[HighlightColor].toString());
        zone->m_highlightColor = highlight.isValid() ? highlight : ::PhosphorZones::ZoneDefaults::HighlightColor;
        const QColor inactive(appearance[InactiveColor].toString());
        zone->m_inactiveColor = inactive.isValid() ? inactive : ::PhosphorZones::ZoneDefaults::InactiveColor;
        const QColor border(appearance[BorderColor].toString());
        zone->m_borderColor = border.isValid() ? border : ::PhosphorZones::ZoneDefaults::BorderColor;
        zone->m_activeOpacity = appearance[ActiveOpacity].toDouble(::PhosphorZones::ZoneDefaults::Opacity);
        zone->m_inactiveOpacity = appearance[InactiveOpacity].toDouble(::PhosphorZones::ZoneDefaults::InactiveOpacity);
        zone->m_borderWidth = appearance[BorderWidth].toInt(::PhosphorZones::ZoneDefaults::BorderWidth);
        zone->m_borderRadius = appearance[BorderRadius].toInt(::PhosphorZones::ZoneDefaults::BorderRadius);
        // Check if useCustomColors exists in JSON, default to false if missing
        zone->m_useCustomColors = appearance.contains(UseCustomColors) ? appearance[UseCustomColors].toBool() : false;
        zone->m_overlayDisplayMode = appearance.contains(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode)
            ? appearance[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode].toInt(-1)
            : -1;
    }

    return zone;
}

} // namespace PhosphorZones
