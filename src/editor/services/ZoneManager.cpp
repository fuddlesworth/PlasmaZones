// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ZoneManager.h"
#include "ZoneAutoFiller.h"
#include "../../core/constants.h"

#include <QUuid>
#include <QtMath>
#include <QCoreApplication>
#include <QLatin1String>
#include <algorithm>
#include "../../core/logging.h"

using namespace PlasmaZones;

// ═══════════════════════════════════════════════════════════════════════════════
// HELPER METHOD IMPLEMENTATIONS
// ═══════════════════════════════════════════════════════════════════════════════

QRectF ZoneManager::extractZoneGeometry(const QVariantMap& zone) const
{
    return QRectF(zone[::PhosphorZones::ZoneJsonKeys::X].toDouble(), zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble(),
                  zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble(),
                  zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble());
}

QRectF ZoneManager::extractFixedGeometry(const QVariantMap& zone) const
{
    return QRectF(zone.value(::PhosphorZones::ZoneJsonKeys::FixedX, 0.0).toDouble(),
                  zone.value(::PhosphorZones::ZoneJsonKeys::FixedY, 0.0).toDouble(),
                  zone.value(::PhosphorZones::ZoneJsonKeys::FixedWidth, 0.0).toDouble(),
                  zone.value(::PhosphorZones::ZoneJsonKeys::FixedHeight, 0.0).toDouble());
}

bool ZoneManager::isFixedMode(const QVariantMap& zone)
{
    return zone.value(::PhosphorZones::ZoneJsonKeys::GeometryMode, 0).toInt()
        == static_cast<int>(ZoneGeometryMode::Fixed);
}

QSizeF ZoneManager::effectiveScreenSizeF() const
{
    return QSizeF(qMax(1.0, static_cast<qreal>(m_referenceScreenSize.width())),
                  qMax(1.0, static_cast<qreal>(m_referenceScreenSize.height())));
}

std::optional<QVariantMap> ZoneManager::getValidatedZone(const QString& zoneId) const
{
    if (zoneId.isEmpty()) {
        return std::nullopt;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        return std::nullopt;
    }

    return m_zones[index].toMap();
}

ZoneManager::ValidatedGeometry ZoneManager::validateAndClampGeometry(qreal x, qreal y, qreal width, qreal height) const
{
    ValidatedGeometry result;

    // Input validation
    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
        result.isValid = false;
        return result;
    }

    // Minimum size
    result.width = qMax(EditorConstants::MinZoneSize, width);
    result.height = qMax(EditorConstants::MinZoneSize, height);

    // Clamp to screen bounds
    result.x = qBound(0.0, x, 1.0 - result.width);
    result.y = qBound(0.0, y, 1.0 - result.height);
    result.isValid = true;

    return result;
}

ZoneManager::ValidatedFixedGeometry ZoneManager::validateAndClampFixedGeometry(qreal x, qreal y, qreal width,
                                                                               qreal height) const
{
    ValidatedFixedGeometry result;

    // Reject NaN/Inf inputs (consistent with relative validateAndClampGeometry)
    if (!qIsFinite(x) || !qIsFinite(y) || !qIsFinite(width) || !qIsFinite(height)) {
        result.isValid = false;
        return result;
    }

    // Fixed geometry: pixel values — position >= 0, size >= MinFixedZoneSize
    result.width = qMax(static_cast<qreal>(EditorConstants::MinFixedZoneSize), width);
    result.height = qMax(static_cast<qreal>(EditorConstants::MinFixedZoneSize), height);
    result.x = qMax(0.0, x);
    result.y = qMax(0.0, y);
    result.isValid = true;

    return result;
}

void ZoneManager::syncFixedFromRelative(QVariantMap& zone) const
{
    QSizeF ss = effectiveScreenSizeF();
    zone[::PhosphorZones::ZoneJsonKeys::FixedX] = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble() * ss.width();
    zone[::PhosphorZones::ZoneJsonKeys::FixedY] = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble() * ss.height();
    zone[::PhosphorZones::ZoneJsonKeys::FixedWidth] =
        zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble() * ss.width();
    zone[::PhosphorZones::ZoneJsonKeys::FixedHeight] =
        zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble() * ss.height();
}

void ZoneManager::syncRelativeFromFixed(QVariantMap& zone) const
{
    QSizeF ss = effectiveScreenSizeF();
    zone[::PhosphorZones::ZoneJsonKeys::X] = zone[::PhosphorZones::ZoneJsonKeys::FixedX].toDouble() / ss.width();
    zone[::PhosphorZones::ZoneJsonKeys::Y] = zone[::PhosphorZones::ZoneJsonKeys::FixedY].toDouble() / ss.height();
    zone[::PhosphorZones::ZoneJsonKeys::Width] =
        zone[::PhosphorZones::ZoneJsonKeys::FixedWidth].toDouble() / ss.width();
    zone[::PhosphorZones::ZoneJsonKeys::Height] =
        zone[::PhosphorZones::ZoneJsonKeys::FixedHeight].toDouble() / ss.height();
}

bool ZoneManager::applyGeometryToZoneMap(QVariantMap& zone, qreal x, qreal y, qreal width, qreal height) const
{
    if (isFixedMode(zone)) {
        ValidatedFixedGeometry geom = validateAndClampFixedGeometry(x, y, width, height);
        if (!geom.isValid) {
            return false;
        }
        zone[::PhosphorZones::ZoneJsonKeys::FixedX] = geom.x;
        zone[::PhosphorZones::ZoneJsonKeys::FixedY] = geom.y;
        zone[::PhosphorZones::ZoneJsonKeys::FixedWidth] = geom.width;
        zone[::PhosphorZones::ZoneJsonKeys::FixedHeight] = geom.height;
        // Update relative fallback so previews render correctly
        QSizeF ss = effectiveScreenSizeF();
        zone[::PhosphorZones::ZoneJsonKeys::X] = geom.x / ss.width();
        zone[::PhosphorZones::ZoneJsonKeys::Y] = geom.y / ss.height();
        zone[::PhosphorZones::ZoneJsonKeys::Width] = geom.width / ss.width();
        zone[::PhosphorZones::ZoneJsonKeys::Height] = geom.height / ss.height();
    } else {
        ValidatedGeometry geom = validateAndClampGeometry(x, y, width, height);
        if (!geom.isValid) {
            return false;
        }
        zone[::PhosphorZones::ZoneJsonKeys::X] = geom.x;
        zone[::PhosphorZones::ZoneJsonKeys::Y] = geom.y;
        zone[::PhosphorZones::ZoneJsonKeys::Width] = geom.width;
        zone[::PhosphorZones::ZoneJsonKeys::Height] = geom.height;
    }
    return true;
}

void ZoneManager::emitZoneSignal(SignalType type, const QString& zoneId, bool includeModified)
{
    if (m_batchUpdateDepth > 0) {
        // Defer signals until batch completes
        switch (type) {
        case SignalType::ZoneAdded:
            m_pendingZoneAdded.insert(zoneId);
            break;
        case SignalType::ZoneRemoved:
            m_pendingZoneRemoved.insert(zoneId);
            break;
        case SignalType::GeometryChanged:
            m_pendingGeometryChanges.insert(zoneId);
            break;
        case SignalType::ColorChanged:
            m_pendingColorChanges.insert(zoneId);
            break;
        default:
            break;
        }
        m_pendingZonesChanged = true;
        if (includeModified) {
            m_pendingZonesModified = true;
        }
    } else {
        // Immediate signal emission
        switch (type) {
        case SignalType::ZoneAdded:
            Q_EMIT zoneAdded(zoneId);
            break;
        case SignalType::ZoneRemoved:
            Q_EMIT zoneRemoved(zoneId);
            break;
        case SignalType::GeometryChanged:
            Q_EMIT zoneGeometryChanged(zoneId);
            break;
        case SignalType::NameChanged:
            Q_EMIT zoneNameChanged(zoneId);
            break;
        case SignalType::NumberChanged:
            Q_EMIT zoneNumberChanged(zoneId);
            break;
        case SignalType::ColorChanged:
            Q_EMIT zoneColorChanged(zoneId);
            break;
        case SignalType::ZOrderChanged:
            Q_EMIT zoneZOrderChanged(zoneId);
            break;
        }
        Q_EMIT zonesChanged();
        if (includeModified) {
            Q_EMIT zonesModified();
        }
    }
}

void ZoneManager::updateAllZOrderValues()
{
    for (int i = 0; i < m_zones.size(); ++i) {
        QVariantMap zoneMap = m_zones[i].toMap();
        zoneMap[::PhosphorZones::ZoneJsonKeys::ZOrder] = i;
        m_zones[i] = zoneMap;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// CONSTRUCTOR
// ═══════════════════════════════════════════════════════════════════════════════

ZoneManager::ZoneManager(QObject* parent)
    : QObject(parent)
    , m_defaultHighlightColor(QString::fromLatin1(EditorConstants::DefaultHighlightColor))
    , m_defaultInactiveColor(QString::fromLatin1(EditorConstants::DefaultInactiveColor))
    , m_defaultBorderColor(QString::fromLatin1(EditorConstants::DefaultBorderColor))
    , m_autoFiller(std::make_unique<ZoneAutoFiller>(this))
{
}

QVariantMap ZoneManager::createZone(const QString& name, int number, qreal x, qreal y, qreal width, qreal height)
{
    QVariantMap zone;
    zone[::PhosphorZones::ZoneJsonKeys::Id] = QUuid::createUuid().toString();
    zone[::PhosphorZones::ZoneJsonKeys::Name] = name;
    zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber] = number;
    zone[::PhosphorZones::ZoneJsonKeys::X] = x;
    zone[::PhosphorZones::ZoneJsonKeys::Y] = y;
    zone[::PhosphorZones::ZoneJsonKeys::Width] = width;
    zone[::PhosphorZones::ZoneJsonKeys::Height] = height;
    zone[::PhosphorZones::ZoneJsonKeys::ZOrder] = m_zones.size(); // New zones go on top
    // Use settable defaults (theme-based if set, otherwise fallback to constants)
    zone[::PhosphorZones::ZoneJsonKeys::HighlightColor] = m_defaultHighlightColor.isEmpty()
        ? QString::fromLatin1(EditorConstants::DefaultHighlightColor)
        : m_defaultHighlightColor;
    zone[::PhosphorZones::ZoneJsonKeys::InactiveColor] = m_defaultInactiveColor.isEmpty()
        ? QString::fromLatin1(EditorConstants::DefaultInactiveColor)
        : m_defaultInactiveColor;
    zone[::PhosphorZones::ZoneJsonKeys::BorderColor] = m_defaultBorderColor.isEmpty()
        ? QString::fromLatin1(EditorConstants::DefaultBorderColor)
        : m_defaultBorderColor;
    // Initialize appearance properties with defaults
    zone[::PhosphorZones::ZoneJsonKeys::ActiveOpacity] = ::PhosphorZones::ZoneDefaults::Opacity;
    zone[::PhosphorZones::ZoneJsonKeys::InactiveOpacity] = ::PhosphorZones::ZoneDefaults::InactiveOpacity;
    zone[::PhosphorZones::ZoneJsonKeys::BorderWidth] = ::PhosphorZones::ZoneDefaults::BorderWidth;
    zone[::PhosphorZones::ZoneJsonKeys::BorderRadius] = ::PhosphorZones::ZoneDefaults::BorderRadius;
    zone[::PhosphorZones::ZoneJsonKeys::UseCustomColors] = false; // New zones use theme colors by default
    zone[::PhosphorZones::ZoneJsonKeys::GeometryMode] = 0; // Default to Relative geometry mode
    return zone;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ZONE CRUD OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════════

QString ZoneManager::addZone(qreal x, qreal y, qreal width, qreal height)
{
    ValidatedGeometry geom = validateAndClampGeometry(x, y, width, height);
    if (!geom.isValid) {
        qCWarning(lcEditorZone) << "Invalid zone geometry:" << x << y << width << height;
        return QString();
    }

    int zoneNumber = m_zones.size() + 1;
    QString zoneName = QStringLiteral("Zone %1").arg(zoneNumber);
    QVariantMap zone = createZone(zoneName, zoneNumber, geom.x, geom.y, geom.width, geom.height);
    QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();

    m_zones.append(zone);
    emitZoneSignal(SignalType::ZoneAdded, zoneId);

    return zoneId;
}

void ZoneManager::updateZoneGeometry(const QString& zoneId, qreal x, qreal y, qreal width, qreal height)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for geometry update";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for geometry update:" << zoneId;
        return;
    }

    QVariantMap zone = m_zones[index].toMap();

    if (!applyGeometryToZoneMap(zone, x, y, width, height)) {
        qCWarning(lcEditorZone) << "Invalid zone geometry:" << x << y << width << height;
        return;
    }

    m_zones[index] = zone;
    emitZoneSignal(SignalType::GeometryChanged, zoneId);
}

void ZoneManager::updateZoneGeometryDirect(const QString& zoneId, qreal x, qreal y, qreal width, qreal height)
{
    // Direct update without undo commands - used for multi-zone drag preview
    if (zoneId.isEmpty()) {
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        return;
    }

    QVariantMap zone = m_zones[index].toMap();

    if (!applyGeometryToZoneMap(zone, x, y, width, height)) {
        return;
    }

    m_zones[index] = zone;

    // No zonesModified - this is a preview, not a user action
    emitZoneSignal(SignalType::GeometryChanged, zoneId, false);
}

void ZoneManager::updateZoneName(const QString& zoneId, const QString& name)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for name update";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for name update:" << zoneId;
        return;
    }

    QVariantMap zone = m_zones[index].toMap();
    zone[::PhosphorZones::ZoneJsonKeys::Name] = name;
    m_zones.replace(index, zone);

    emitZoneSignal(SignalType::NameChanged, zoneId);
}

void ZoneManager::updateZoneNumber(const QString& zoneId, int number)
{
    if (zoneId.isEmpty() || number < 1) {
        qCWarning(lcEditorZone) << "Invalid zone ID or number:" << zoneId << number;
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for number update:" << zoneId;
        return;
    }

    QVariantMap zone = m_zones[index].toMap();
    zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber] = number;
    m_zones.replace(index, zone);

    emitZoneSignal(SignalType::NumberChanged, zoneId);
}

void ZoneManager::updateZoneColor(const QString& zoneId, const QString& colorType, const QString& color)
{
    if (zoneId.isEmpty() || colorType.isEmpty() || color.isEmpty()) {
        qCWarning(lcEditorZone) << "Invalid parameters for color update";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for color update:" << zoneId;
        return;
    }

    QVariantMap zone = m_zones[index].toMap();
    zone[colorType] = color;
    m_zones.replace(index, zone);

    emitZoneSignal(SignalType::ColorChanged, zoneId);
}

void ZoneManager::updateZoneAppearance(const QString& zoneId, const QString& propertyName, const QVariant& value)
{
    if (zoneId.isEmpty() || propertyName.isEmpty()) {
        qCWarning(lcEditorZone) << "Invalid parameters for appearance update";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for appearance update:" << zoneId;
        return;
    }

    QVariantMap zone = m_zones[index].toMap();

    // Normalize property name to use JsonKeys constants (must match save/load)
    QString normalizedKey = propertyName;
    QString useCustomColorsKey = QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::UseCustomColors);

    // Normalize known appearance property names to use JsonKeys constants
    if (propertyName.compare(QLatin1String("useCustomColors"), Qt::CaseInsensitive) == 0
        || propertyName == useCustomColorsKey) {
        normalizedKey = useCustomColorsKey;
    } else if (propertyName.compare(QLatin1String("activeOpacity"), Qt::CaseInsensitive) == 0) {
        normalizedKey = QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::ActiveOpacity);
    } else if (propertyName.compare(QLatin1String("inactiveOpacity"), Qt::CaseInsensitive) == 0) {
        normalizedKey = QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::InactiveOpacity);
    } else if (propertyName.compare(QLatin1String("borderWidth"), Qt::CaseInsensitive) == 0) {
        normalizedKey = QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::BorderWidth);
    } else if (propertyName.compare(QLatin1String("borderRadius"), Qt::CaseInsensitive) == 0) {
        normalizedKey = QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::BorderRadius);
    } else if (propertyName.compare(QLatin1String("overlayDisplayMode"), Qt::CaseInsensitive) == 0) {
        normalizedKey = QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode);
    }

    zone[normalizedKey] = value;

    // When enabling useCustomColors, initialize zone colors to theme defaults
    // so they match what the KCM/global settings show (not the hardcoded constants)
    if (normalizedKey == useCustomColorsKey && value.toBool()) {
        if (!m_defaultHighlightColor.isEmpty())
            zone[QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::HighlightColor)] = m_defaultHighlightColor;
        if (!m_defaultInactiveColor.isEmpty())
            zone[QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::InactiveColor)] = m_defaultInactiveColor;
        if (!m_defaultBorderColor.isEmpty())
            zone[QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::BorderColor)] = m_defaultBorderColor;
    }

    m_zones.replace(index, zone);

    // Reuse ColorChanged signal for all appearance updates
    emitZoneSignal(SignalType::ColorChanged, zoneId);
}

void ZoneManager::deleteZone(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for deletion";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for deletion:" << zoneId;
        return;
    }

    m_zones.removeAt(index);
    renumberZones();

    emitZoneSignal(SignalType::ZoneRemoved, zoneId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BULK OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════════

void ZoneManager::clearAllZones()
{
    m_zones.clear();
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
}

void ZoneManager::setZones(const QVariantList& zones)
{
    m_zones = zones;
    Q_EMIT zonesChanged();
}

QVariantList ZoneManager::zones() const
{
    return m_zones;
}

int ZoneManager::findZoneIndex(const QString& zoneId) const
{
    for (int i = 0; i < m_zones.size(); ++i) {
        QVariantMap zone = m_zones[i].toMap();
        if (zone[::PhosphorZones::ZoneJsonKeys::Id].toString() == zoneId) {
            return i;
        }
    }
    return -1;
}

void ZoneManager::renumberZones()
{
    for (int i = 0; i < m_zones.size(); ++i) {
        QVariantMap zone = m_zones[i].toMap();
        QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
        int oldNumber = zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber].toInt();
        int newNumber = i + 1;

        if (oldNumber != newNumber) {
            zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber] = newNumber;
            m_zones[i] = zone;
            Q_EMIT zoneNumberChanged(zoneId);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// AUTO-FILL OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

QVariantMap ZoneManager::findAdjacentZones(const QString& zoneId, qreal threshold)
{
    return m_autoFiller->findAdjacentZones(zoneId, threshold);
}

bool ZoneManager::expandToFillSpace(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    return m_autoFiller->expandToFillSpace(zoneId, mouseX, mouseY);
}

QVariantMap ZoneManager::calculateFillRegion(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    return m_autoFiller->calculateFillRegion(zoneId, mouseX, mouseY);
}

void ZoneManager::deleteZoneWithFill(const QString& zoneId, bool autoFill)
{
    m_autoFiller->deleteZoneWithFill(zoneId, autoFill);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BATCH UPDATE SUPPORT
// ═══════════════════════════════════════════════════════════════════════════════

void ZoneManager::beginBatchUpdate()
{
    ++m_batchUpdateDepth;
}

void ZoneManager::endBatchUpdate()
{
    if (m_batchUpdateDepth > 0) {
        --m_batchUpdateDepth;
        if (m_batchUpdateDepth == 0) {
            emitDeferredSignals();
        }
    }
}

void ZoneManager::emitDeferredSignals()
{
    // Emit zone added signals
    for (const QString& zoneId : std::as_const(m_pendingZoneAdded)) {
        Q_EMIT zoneAdded(zoneId);
    }
    m_pendingZoneAdded.clear();

    // Emit zone removed signals
    for (const QString& zoneId : std::as_const(m_pendingZoneRemoved)) {
        Q_EMIT zoneRemoved(zoneId);
    }
    m_pendingZoneRemoved.clear();

    // Emit geometry change signals for each affected zone
    for (const QString& zoneId : std::as_const(m_pendingGeometryChanges)) {
        Q_EMIT zoneGeometryChanged(zoneId);
    }
    m_pendingGeometryChanges.clear();

    // Emit color change signals for each affected zone
    for (const QString& zoneId : std::as_const(m_pendingColorChanges)) {
        Q_EMIT zoneColorChanged(zoneId);
    }
    m_pendingColorChanges.clear();

    // Emit aggregate signals once at the end
    if (m_pendingZonesChanged) {
        Q_EMIT zonesChanged();
        m_pendingZonesChanged = false;
    }

    if (m_pendingZonesModified) {
        Q_EMIT zonesModified();
        m_pendingZonesModified = false;
    }
}
