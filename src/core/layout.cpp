// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layout.h"
#include "constants.h"
#include "logging.h"
#include "shaderregistry.h"
#include <QJsonArray>
#include <QStandardPaths>
#include <algorithm>

namespace PlasmaZones {

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
    , m_author(other.m_author)
    , m_shortcut(other.m_shortcut)
    , m_zonePadding(other.m_zonePadding)
    , m_showZoneNumbers(other.m_showZoneNumbers)
    , m_sourcePath() // Copies have no source path (will be saved to user directory)
    , m_defaultOrder(other.m_defaultOrder)
    , m_shaderId(other.m_shaderId)
    , m_shaderParams(other.m_shaderParams)
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
        m_name = other.m_name;
        m_type = other.m_type;
        m_description = other.m_description;
        m_author = other.m_author;
        m_shortcut = other.m_shortcut;
        m_zonePadding = other.m_zonePadding;
        m_showZoneNumbers = other.m_showZoneNumbers;
        m_defaultOrder = other.m_defaultOrder;
        m_sourcePath.clear(); // Assignment creates a user copy (will be saved to user directory)
        m_shaderId = other.m_shaderId;
        m_shaderParams = other.m_shaderParams;

        // Deep copy zones using clone() method
        qDeleteAll(m_zones);
        m_zones.clear();
        for (const auto* zone : other.m_zones) {
            auto* newZone = zone->clone(this);
            m_zones.append(newZone);
        }
        m_lastRecalcGeometry = QRectF(); // Invalidate geometry cache
        Q_EMIT zonesChanged();
    }
    return *this;
}

void Layout::setName(const QString& name)
{
    if (m_name != name) {
        m_name = name;
        Q_EMIT nameChanged();
        Q_EMIT layoutModified();
    }
}

void Layout::setType(LayoutType type)
{
    if (m_type != type) {
        m_type = type;
        Q_EMIT typeChanged();
        Q_EMIT layoutModified();
    }
}

void Layout::setDescription(const QString& description)
{
    if (m_description != description) {
        m_description = description;
        Q_EMIT descriptionChanged();
        Q_EMIT layoutModified();
    }
}

void Layout::setAuthor(const QString& author)
{
    if (m_author != author) {
        m_author = author;
        Q_EMIT authorChanged();
        Q_EMIT layoutModified();
    }
}

void Layout::setShortcut(const QString& shortcut)
{
    if (m_shortcut != shortcut) {
        m_shortcut = shortcut;
        Q_EMIT shortcutChanged();
        Q_EMIT layoutModified();
    }
}

void Layout::setZonePadding(int padding)
{
    padding = qMax(0, padding);
    if (m_zonePadding != padding) {
        m_zonePadding = padding;
        Q_EMIT zonePaddingChanged();
        Q_EMIT layoutModified();
    }
}

void Layout::setShowZoneNumbers(bool show)
{
    if (m_showZoneNumbers != show) {
        m_showZoneNumbers = show;
        Q_EMIT showZoneNumbersChanged();
        Q_EMIT layoutModified();
    }
}

void Layout::setSourcePath(const QString& path)
{
    if (m_sourcePath != path) {
        m_sourcePath = path;
        Q_EMIT sourcePathChanged();
    }
}

void Layout::setShaderId(const QString& id)
{
    if (m_shaderId != id) {
        m_shaderId = id;
        Q_EMIT shaderIdChanged();
        Q_EMIT layoutModified();
    }
}

void Layout::setShaderParams(const QVariantMap& params)
{
    if (m_shaderParams != params) {
        m_shaderParams = params;
        Q_EMIT shaderParamsChanged();
        Q_EMIT layoutModified();
    }
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
        Q_EMIT layoutModified();
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
        Q_EMIT layoutModified();
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
        Q_EMIT layoutModified();
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
        Q_EMIT layoutModified();
    }
}

void Layout::moveZone(int fromIndex, int toIndex)
{
    if (fromIndex >= 0 && fromIndex < m_zones.size() && toIndex >= 0 && toIndex < m_zones.size()
        && fromIndex != toIndex) {
        m_zones.move(fromIndex, toIndex);
        renumberZones();
        Q_EMIT zonesChanged();
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

    qCDebug(lcLayout) << "recalculateZoneGeometries for" << m_name << "with screenGeometry:" << screenGeometry;
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
    json[JsonKeys::Description] = m_description;
    json[JsonKeys::Author] = m_author;
    json[JsonKeys::Shortcut] = m_shortcut;
    json[JsonKeys::ZonePadding] = m_zonePadding;
    json[JsonKeys::ShowZoneNumbers] = m_showZoneNumbers;
    if (m_defaultOrder != 999) {
        json[JsonKeys::DefaultOrder] = m_defaultOrder;
    }
    // Note: isBuiltIn is no longer serialized - it's determined by source path at load time

    // Shader support
    if (!ShaderRegistry::isNoneShader(m_shaderId)) {
        json[JsonKeys::ShaderId] = m_shaderId;
    }
    if (!m_shaderParams.isEmpty()) {
        json[JsonKeys::ShaderParams] = QJsonObject::fromVariantMap(m_shaderParams);
    }

    QJsonArray zonesArray;
    for (const auto* zone : m_zones) {
        zonesArray.append(zone->toJson());
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
    layout->m_author = json[JsonKeys::Author].toString();
    layout->m_shortcut = json[JsonKeys::Shortcut].toString();
    layout->m_zonePadding = json[JsonKeys::ZonePadding].toInt(Defaults::ZonePadding);
    layout->m_showZoneNumbers = json[JsonKeys::ShowZoneNumbers].toBool(true);
    layout->m_defaultOrder = json[JsonKeys::DefaultOrder].toInt(999);
    // Note: sourcePath is set by LayoutManager after loading, not from JSON

    // Shader support
    layout->m_shaderId = json[JsonKeys::ShaderId].toString();
    if (json.contains(JsonKeys::ShaderParams)) {
        layout->m_shaderParams = json[JsonKeys::ShaderParams].toObject().toVariantMap();
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
