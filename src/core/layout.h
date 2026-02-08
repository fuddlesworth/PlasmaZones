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
#include <QJsonArray>
#include <QJsonObject>
#include <memory>

namespace PlasmaZones {

/**
 * @brief App-to-zone auto-snap rule
 *
 * Maps a window class pattern to a zone number within a layout.
 * Patterns are case-insensitive substring matches against the window class.
 */
struct PLASMAZONES_EXPORT AppRule {
    QString pattern;       // Window class or app name pattern (case-insensitive substring match)
    int zoneNumber = 0;    // 1-based zone number to snap to
    QString targetScreen;  // Optional: snap to zone on this screen instead of current

    bool operator==(const AppRule& other) const {
        return pattern == other.pattern
            && zoneNumber == other.zoneNumber
            && targetScreen == other.targetScreen;
    }
    bool operator!=(const AppRule& other) const { return !(*this == other); }

    // Serialization helpers (centralized to avoid DRY violations)
    QJsonObject toJson() const;
    static AppRule fromJson(const QJsonObject& obj);
    static QVector<AppRule> fromJsonArray(const QJsonArray& array);
};

/**
 * @brief Result of matching a window class against app rules
 */
struct PLASMAZONES_EXPORT AppRuleMatch {
    int zoneNumber = 0;
    QString targetScreen;
    bool matched() const { return zoneNumber > 0; }
};

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
 * @brief Category for layout type (manual zone-based layouts only)
 *
 * QML Note: Passed as int to QML. Value: 0 = Manual
 */
enum class LayoutCategory {
    Manual = 0   ///< Traditional zone-based layout
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
    Q_PROPERTY(int zonePadding READ zonePadding WRITE setZonePadding NOTIFY zonePaddingChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(bool hasZonePaddingOverride READ hasZonePaddingOverride NOTIFY zonePaddingChanged)
    Q_PROPERTY(bool hasOuterGapOverride READ hasOuterGapOverride NOTIFY outerGapChanged)
    Q_PROPERTY(bool showZoneNumbers READ showZoneNumbers WRITE setShowZoneNumbers NOTIFY showZoneNumbersChanged)
    Q_PROPERTY(int zoneCount READ zoneCount NOTIFY zonesChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath WRITE setSourcePath NOTIFY sourcePathChanged)
    Q_PROPERTY(bool isSystemLayout READ isSystemLayout NOTIFY sourcePathChanged)
    Q_PROPERTY(QString shaderId READ shaderId WRITE setShaderId NOTIFY shaderIdChanged)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams WRITE setShaderParams NOTIFY shaderParamsChanged)

    // App-to-zone rules
    Q_PROPERTY(QVariantList appRules READ appRulesVariant WRITE setAppRulesVariant NOTIFY appRulesChanged)

    // Visibility filtering
    Q_PROPERTY(bool hiddenFromSelector READ hiddenFromSelector WRITE setHiddenFromSelector NOTIFY hiddenFromSelectorChanged)
    Q_PROPERTY(QStringList allowedScreens READ allowedScreens WRITE setAllowedScreens NOTIFY allowedScreensChanged)
    Q_PROPERTY(QList<int> allowedDesktops READ allowedDesktops WRITE setAllowedDesktops NOTIFY allowedDesktopsChanged)
    Q_PROPERTY(QStringList allowedActivities READ allowedActivities WRITE setAllowedActivities NOTIFY allowedActivitiesChanged)

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

    // Layout settings (per-layout gap overrides, -1 = use global setting)
    int zonePadding() const
    {
        return m_zonePadding;
    }
    void setZonePadding(int padding);
    bool hasZonePaddingOverride() const
    {
        return m_zonePadding >= 0;
    }
    void clearZonePaddingOverride();

    int outerGap() const
    {
        return m_outerGap;
    }
    void setOuterGap(int gap);
    bool hasOuterGapOverride() const
    {
        return m_outerGap >= 0;
    }
    void clearOuterGapOverride();

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
    QString shaderId() const
    {
        return m_shaderId;
    }
    void setShaderId(const QString& id);
    QVariantMap shaderParams() const
    {
        return m_shaderParams;
    }
    void setShaderParams(const QVariantMap& params);

    // Visibility filtering
    bool hiddenFromSelector() const
    {
        return m_hiddenFromSelector;
    }
    void setHiddenFromSelector(bool hidden);
    QStringList allowedScreens() const
    {
        return m_allowedScreens;
    }
    void setAllowedScreens(const QStringList& screens);
    QList<int> allowedDesktops() const
    {
        return m_allowedDesktops;
    }
    void setAllowedDesktops(const QList<int>& desktops);
    QStringList allowedActivities() const
    {
        return m_allowedActivities;
    }
    void setAllowedActivities(const QStringList& activities);

    // App-to-zone rules
    QVector<AppRule> appRules() const { return m_appRules; }
    void setAppRules(const QVector<AppRule>& rules);
    QVariantList appRulesVariant() const;
    void setAppRulesVariant(const QVariantList& rules);
    AppRuleMatch matchAppRule(const QString& windowClass) const;

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
    void zonePaddingChanged();
    void outerGapChanged();
    void showZoneNumbersChanged();
    void sourcePathChanged();
    void shaderIdChanged();
    void shaderParamsChanged();
    void hiddenFromSelectorChanged();
    void allowedScreensChanged();
    void allowedDesktopsChanged();
    void allowedActivitiesChanged();
    void appRulesChanged();
    void zonesChanged();
    void zoneAdded(Zone* zone);
    void zoneRemoved(Zone* zone);
    void layoutModified();

private:
    QUuid m_id;
    QString m_name;
    LayoutType m_type = LayoutType::Custom;
    QString m_description;
    int m_zonePadding = -1;  // -1 = use global setting
    int m_outerGap = -1;     // -1 = use global setting
    bool m_showZoneNumbers = true;
    QString m_sourcePath; // Path where layout was loaded from (empty for new layouts)
    int m_defaultOrder = 999; // Optional: lower values appear first when choosing default (999 = not set)
    QVector<Zone*> m_zones;

    // App-to-zone rules
    QVector<AppRule> m_appRules;

    // Shader support
    QString m_shaderId; // Shader effect ID (empty = no shader)
    QVariantMap m_shaderParams; // Shader-specific parameters

    // Visibility filtering
    bool m_hiddenFromSelector = false;
    QStringList m_allowedScreens;    // empty = all screens
    QList<int> m_allowedDesktops;    // empty = all desktops
    QStringList m_allowedActivities; // empty = all activities

    // Cache last geometry used for recalculation to avoid redundant work
    mutable QRectF m_lastRecalcGeometry;
};

} // namespace PlasmaZones

Q_DECLARE_METATYPE(PlasmaZones::LayoutType)
Q_DECLARE_METATYPE(PlasmaZones::LayoutCategory)
