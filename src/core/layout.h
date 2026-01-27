// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "zone.h"
#include "plasmazones_export.h"
#include <QObject>
#include <QVariantMap>
#include <QVector>
#include <QUuid>
#include <QString>
#include <QJsonObject>
#include <memory>

namespace PlasmaZones {

/**
 * @brief Layout types matching FancyZones
 */
enum class LayoutType {
    Custom, // User-defined canvas layout
    Grid, // Grid-based layout
    Columns, // Vertical columns
    Rows, // Horizontal rows
    PriorityGrid, // Primary zone with grid
    Focus // Large center with sides
};

/**
 * @brief Represents a collection of zones that form a layout
 *
 * Layouts can be assigned to specific monitors, virtual desktops,
 * and activities. Supports both predefined templates and custom
 * canvas-style layouts.
 */
class PLASMAZONES_EXPORT Layout : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QUuid id READ id CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(LayoutType type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(QString author READ author WRITE setAuthor NOTIFY authorChanged)
    Q_PROPERTY(QString shortcut READ shortcut WRITE setShortcut NOTIFY shortcutChanged)
    Q_PROPERTY(int zonePadding READ zonePadding WRITE setZonePadding NOTIFY zonePaddingChanged)
    Q_PROPERTY(bool showZoneNumbers READ showZoneNumbers WRITE setShowZoneNumbers NOTIFY showZoneNumbersChanged)
    Q_PROPERTY(int zoneCount READ zoneCount NOTIFY zonesChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath WRITE setSourcePath NOTIFY sourcePathChanged)
    Q_PROPERTY(bool isSystemLayout READ isSystemLayout NOTIFY sourcePathChanged)
    Q_PROPERTY(QString shaderId READ shaderId WRITE setShaderId NOTIFY shaderIdChanged)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams WRITE setShaderParams NOTIFY shaderParamsChanged)

public:
    explicit Layout(QObject* parent = nullptr);
    explicit Layout(const QString& name, LayoutType type = LayoutType::Custom, QObject* parent = nullptr);
    Layout(const Layout& other);
    ~Layout() override;

    Layout& operator=(const Layout& other);

    // Identification
    QUuid id() const
    {
        return m_id;
    }
    QString name() const
    {
        return m_name;
    }
    void setName(const QString& name);

    LayoutType type() const
    {
        return m_type;
    }
    void setType(LayoutType type);

    QString description() const
    {
        return m_description;
    }
    void setDescription(const QString& description);

    QString author() const
    {
        return m_author;
    }
    void setAuthor(const QString& author);

    // Quick switch shortcut (Win+Ctrl+Alt+[number])
    QString shortcut() const
    {
        return m_shortcut;
    }
    void setShortcut(const QString& shortcut);

    // Layout settings
    int zonePadding() const
    {
        return m_zonePadding;
    }
    void setZonePadding(int padding);

    bool showZoneNumbers() const
    {
        return m_showZoneNumbers;
    }
    void setShowZoneNumbers(bool show);

    // Source path tracking - determines if layout is from system or user directory
    QString sourcePath() const
    {
        return m_sourcePath;
    }
    void setSourcePath(const QString& path);

    // Returns true if layout was loaded from a system directory (not user's .local)
    // This determines whether the layout can be edited/deleted in place
    bool isSystemLayout() const;

    // Shader support
    QString shaderId() const { return m_shaderId; }
    void setShaderId(const QString& id);
    QVariantMap shaderParams() const { return m_shaderParams; }
    void setShaderParams(const QVariantMap& params);

    // Optional load order for "default" layout when defaultLayoutId is not set (lower = first)
    int defaultOrder() const
    {
        return m_defaultOrder;
    }

    // Zone management
    int zoneCount() const
    {
        return m_zones.size();
    }
    QVector<Zone*> zones() const
    {
        return m_zones;
    }
    Zone* zone(int index) const;
    Zone* zoneById(const QUuid& id) const;
    Zone* zoneByNumber(int number) const;

    Q_INVOKABLE void addZone(Zone* zone);
    Q_INVOKABLE void removeZone(Zone* zone);
    Q_INVOKABLE void removeZoneAt(int index);
    Q_INVOKABLE void clearZones();
    Q_INVOKABLE void moveZone(int fromIndex, int toIndex);

    // Zone detection
    Q_INVOKABLE Zone* zoneAtPoint(const QPointF& point) const;
    Q_INVOKABLE Zone* nearestZone(const QPointF& point, qreal maxDistance = -1) const;
    Q_INVOKABLE QVector<Zone*> zonesInRect(const QRectF& rect) const;
    Q_INVOKABLE QVector<Zone*> adjacentZones(const QPointF& point, qreal threshold = 20) const;

    // Geometry calculations
    Q_INVOKABLE void recalculateZoneGeometries(const QRectF& screenGeometry);
    Q_INVOKABLE void renumberZones();

    // Serialization
    QJsonObject toJson() const;
    static Layout* fromJson(const QJsonObject& json, QObject* parent = nullptr);

    // Predefined layouts (templates)
    static Layout* createColumnsLayout(int columns, QObject* parent = nullptr);
    static Layout* createRowsLayout(int rows, QObject* parent = nullptr);
    static Layout* createGridLayout(int columns, int rows, QObject* parent = nullptr);
    static Layout* createPriorityGridLayout(QObject* parent = nullptr);
    static Layout* createFocusLayout(QObject* parent = nullptr);

Q_SIGNALS:
    void nameChanged();
    void typeChanged();
    void descriptionChanged();
    void authorChanged();
    void shortcutChanged();
    void zonePaddingChanged();
    void showZoneNumbersChanged();
    void sourcePathChanged();
    void shaderIdChanged();
    void shaderParamsChanged();
    void zonesChanged();
    void zoneAdded(Zone* zone);
    void zoneRemoved(Zone* zone);
    void layoutModified();

private:
    QUuid m_id;
    QString m_name;
    LayoutType m_type = LayoutType::Custom;
    QString m_description;
    QString m_author;
    QString m_shortcut;
    int m_zonePadding = 8;
    bool m_showZoneNumbers = true;
    QString m_sourcePath; // Path where layout was loaded from (empty for new layouts)
    int m_defaultOrder = 999; // Optional: lower values appear first when choosing default (999 = not set)
    QVector<Zone*> m_zones;

    // Shader support
    QString m_shaderId; // Shader effect ID (empty = no shader)
    QVariantMap m_shaderParams; // Shader-specific parameters

    // Cache last geometry used for recalculation to avoid redundant work
    mutable QRectF m_lastRecalcGeometry;
};

} // namespace PlasmaZones

Q_DECLARE_METATYPE(PlasmaZones::LayoutType)
