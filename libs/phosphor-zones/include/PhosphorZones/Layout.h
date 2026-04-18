// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorZones/Zone.h>
#include <phosphorzones_export.h>
#include <QObject>
#include <QVariantMap>
#include <QVector>
#include <QUuid>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <memory>

namespace PhosphorZones {

/**
 * @brief App-to-zone auto-snap rule
 *
 * Maps a window class pattern to a zone number within a layout.
 * Patterns are case-insensitive substring matches against the window class.
 */
struct PHOSPHORZONES_EXPORT AppRule
{
    QString pattern; // Window class or app name pattern (case-insensitive substring match)
    int zoneNumber = 0; // 1-based zone number to snap to
    QString targetScreen; // Optional: snap to zone on this screen instead of current

    bool operator==(const AppRule& other) const
    {
        return pattern == other.pattern && zoneNumber == other.zoneNumber && targetScreen == other.targetScreen;
    }
    bool operator!=(const AppRule& other) const
    {
        return !(*this == other);
    }

    // Serialization helpers (centralized to avoid DRY violations)
    QJsonObject toJson() const;
    static AppRule fromJson(const QJsonObject& obj);
    static QVector<AppRule> fromJsonArray(const QJsonArray& array);
};

/**
 * @brief Result of matching a window class against app rules
 */
struct PHOSPHORZONES_EXPORT AppRuleMatch
{
    int zoneNumber = 0;
    QString targetScreen;
    bool matched() const
    {
        return zoneNumber > 0;
    }
};

/**
 * @brief Category for layout type
 *
 * QML Note: Passed as int to QML. Values: 0 = Manual, 1 = Autotile
 */
enum class LayoutCategory {
    Manual = 0, ///< Traditional zone-based layout
    Autotile = 1 ///< Dynamic auto-tiling algorithm
};

/**
 * @brief Represents a collection of zones that form a layout
 *
 * Layouts can be assigned to specific monitors, virtual desktops,
 * and activities. Supports both predefined templates and custom
 * canvas-style layouts.
 */
class PHOSPHORZONES_EXPORT Layout : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QUuid id READ id CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(int zonePadding READ zonePadding WRITE setZonePadding NOTIFY zonePaddingChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(bool hasZonePaddingOverride READ hasZonePaddingOverride NOTIFY zonePaddingChanged)
    Q_PROPERTY(bool hasOuterGapOverride READ hasOuterGapOverride NOTIFY outerGapChanged)
    Q_PROPERTY(bool usePerSideOuterGap READ usePerSideOuterGap WRITE setUsePerSideOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(int outerGapTop READ outerGapTop WRITE setOuterGapTop NOTIFY outerGapChanged)
    Q_PROPERTY(int outerGapBottom READ outerGapBottom WRITE setOuterGapBottom NOTIFY outerGapChanged)
    Q_PROPERTY(int outerGapLeft READ outerGapLeft WRITE setOuterGapLeft NOTIFY outerGapChanged)
    Q_PROPERTY(int outerGapRight READ outerGapRight WRITE setOuterGapRight NOTIFY outerGapChanged)
    Q_PROPERTY(bool hasPerSideOuterGapOverride READ hasPerSideOuterGapOverride NOTIFY outerGapChanged)
    Q_PROPERTY(bool showZoneNumbers READ showZoneNumbers WRITE setShowZoneNumbers NOTIFY showZoneNumbersChanged)
    Q_PROPERTY(
        int overlayDisplayMode READ overlayDisplayMode WRITE setOverlayDisplayMode NOTIFY overlayDisplayModeChanged)
    Q_PROPERTY(bool hasOverlayDisplayModeOverride READ hasOverlayDisplayModeOverride NOTIFY overlayDisplayModeChanged)
    Q_PROPERTY(int zoneCount READ zoneCount NOTIFY zonesChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath WRITE setSourcePath NOTIFY sourcePathChanged)
    Q_PROPERTY(bool isSystemLayout READ isSystemLayout NOTIFY sourcePathChanged)
    Q_PROPERTY(QString shaderId READ shaderId WRITE setShaderId NOTIFY shaderIdChanged)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams WRITE setShaderParams NOTIFY shaderParamsChanged)

    // App-to-zone rules
    Q_PROPERTY(QVariantList appRules READ appRulesVariant WRITE setAppRulesVariant NOTIFY appRulesChanged)

    // Auto-assign: new windows fill first empty zone
    Q_PROPERTY(bool autoAssign READ autoAssign WRITE setAutoAssign NOTIFY autoAssignChanged)

    // Geometry mode: use full screen or available (panel-excluded) geometry
    Q_PROPERTY(bool useFullScreenGeometry READ useFullScreenGeometry WRITE setUseFullScreenGeometry NOTIFY
                   useFullScreenGeometryChanged)

    // Aspect ratio classification
    Q_PROPERTY(
        int aspectRatioClass READ aspectRatioClassInt WRITE setAspectRatioClassInt NOTIFY aspectRatioClassChanged)
    Q_PROPERTY(qreal minAspectRatio READ minAspectRatio WRITE setMinAspectRatio NOTIFY aspectRatioClassChanged)
    Q_PROPERTY(qreal maxAspectRatio READ maxAspectRatio WRITE setMaxAspectRatio NOTIFY aspectRatioClassChanged)

    // Visibility filtering
    Q_PROPERTY(
        bool hiddenFromSelector READ hiddenFromSelector WRITE setHiddenFromSelector NOTIFY hiddenFromSelectorChanged)
    Q_PROPERTY(QStringList allowedScreens READ allowedScreens WRITE setAllowedScreens NOTIFY allowedScreensChanged)
    Q_PROPERTY(QList<int> allowedDesktops READ allowedDesktops WRITE setAllowedDesktops NOTIFY allowedDesktopsChanged)
    Q_PROPERTY(
        QStringList allowedActivities READ allowedActivities WRITE setAllowedActivities NOTIFY allowedActivitiesChanged)

public:
    explicit Layout(QObject* parent = nullptr);
    explicit Layout(const QString& name, QObject* parent = nullptr);
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

    // Per-side outer gap overrides (-1 = use global setting)
    bool usePerSideOuterGap() const
    {
        return m_usePerSideOuterGap;
    }
    void setUsePerSideOuterGap(bool enabled);
    int outerGapTop() const
    {
        return m_outerGapTop;
    }
    void setOuterGapTop(int gap);
    int outerGapBottom() const
    {
        return m_outerGapBottom;
    }
    void setOuterGapBottom(int gap);
    int outerGapLeft() const
    {
        return m_outerGapLeft;
    }
    void setOuterGapLeft(int gap);
    int outerGapRight() const
    {
        return m_outerGapRight;
    }
    void setOuterGapRight(int gap);
    bool hasPerSideOuterGapOverride() const
    {
        return m_usePerSideOuterGap
            && (m_outerGapTop >= 0 || m_outerGapBottom >= 0 || m_outerGapLeft >= 0 || m_outerGapRight >= 0);
    }
    /**
     * @brief Raw per-side gap overrides. Values may be -1 (use global).
     * Callers should use GeometryUtils::getEffectiveOuterGaps() instead
     * for resolved pixel values.
     */
    ::PhosphorLayout::EdgeGaps rawOuterGaps() const
    {
        return {m_outerGapTop, m_outerGapBottom, m_outerGapLeft, m_outerGapRight};
    }

    bool showZoneNumbers() const
    {
        return m_showZoneNumbers;
    }
    void setShowZoneNumbers(bool show);

    int overlayDisplayMode() const
    {
        return m_overlayDisplayMode;
    }
    void setOverlayDisplayMode(int mode);
    bool hasOverlayDisplayModeOverride() const
    {
        return m_overlayDisplayMode >= 0;
    }
    void clearOverlayDisplayModeOverride();

    // Source path tracking - determines if layout is from system or user directory
    QString sourcePath() const
    {
        return m_sourcePath;
    }
    void setSourcePath(const QString& path);

    // Returns true if layout was loaded from a system directory (not user's .local)
    // This determines whether the layout can be edited/deleted in place
    bool isSystemLayout() const;

    // Original system layout path — set when a user override replaces a system layout.
    // Persisted in user JSON so deletion can restore the system original without scanning.
    QString systemSourcePath() const
    {
        return m_systemSourcePath;
    }
    void setSystemSourcePath(const QString& path)
    {
        m_systemSourcePath = path;
    }
    bool hasSystemOrigin() const
    {
        return !m_systemSourcePath.isEmpty();
    }

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

    // Aspect ratio classification
    ::PhosphorLayout::AspectRatioClass aspectRatioClass() const
    {
        return m_aspectRatioClass;
    }
    void setAspectRatioClass(::PhosphorLayout::AspectRatioClass cls);
    int aspectRatioClassInt() const
    {
        return static_cast<int>(m_aspectRatioClass);
    }
    void setAspectRatioClassInt(int cls);
    qreal minAspectRatio() const
    {
        return m_minAspectRatio;
    }
    void setMinAspectRatio(qreal ratio);
    qreal maxAspectRatio() const
    {
        return m_maxAspectRatio;
    }
    void setMaxAspectRatio(qreal ratio);

    /**
     * @brief Check if this layout is suitable for a screen with the given aspect ratio
     *
     * Uses explicit min/max bounds if set, otherwise falls back to class matching.
     */
    bool matchesAspectRatio(qreal screenAspectRatio) const;

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
    QVector<AppRule> appRules() const
    {
        return m_appRules;
    }
    void setAppRules(const QVector<AppRule>& rules);
    QVariantList appRulesVariant() const;
    void setAppRulesVariant(const QVariantList& rules);
    AppRuleMatch matchAppRule(const QString& windowClass) const;

    // Auto-assign: new windows fill first empty zone
    bool autoAssign() const
    {
        return m_autoAssign;
    }
    void setAutoAssign(bool enabled);

    // Geometry mode: when true, zones span the full screen including panel/taskbar areas
    bool useFullScreenGeometry() const
    {
        return m_useFullScreenGeometry;
    }
    void setUseFullScreenGeometry(bool enabled);

    /// Returns true if any zone uses fixed (pixel) geometry mode
    bool hasFixedGeometryZones() const;

    // Optional load order for "default" layout when defaultLayoutId is not set (lower = first)
    int defaultOrder() const
    {
        return m_defaultOrder;
    }
    void setDefaultOrder(int order)
    {
        m_defaultOrder = order;
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
    Q_INVOKABLE void renumberZones();
    QRectF lastRecalcGeometry() const
    {
        return m_lastRecalcGeometry;
    }
    void setLastRecalcGeometry(const QRectF& geom)
    {
        m_lastRecalcGeometry = geom;
    }

    // Serialization
    QJsonObject toJson() const;
    static Layout* fromJson(const QJsonObject& json, QObject* parent = nullptr);

    /// Screen-id resolver. Install a callback that maps a legacy connector
    /// name (e.g. "DP-2") to the application's stable screen identifier
    /// (EDID-based, "LG:Model:Serial"). The library calls it for every
    /// entry in `allowedScreens` during @c fromJson so in-memory layouts
    /// hold normalized IDs regardless of what the file stores.
    ///
    /// The callback typically requires a live QGuiApplication (to enumerate
    /// connected QScreens). Install from the daemon / editor / settings
    /// processes only; leave unset for headless / test code, in which case
    /// strings are stored verbatim.
    ///
    /// Passing a default-constructed function clears the resolver.
    using ScreenIdResolver = std::function<QString(const QString&)>;
    static void setScreenIdResolver(ScreenIdResolver resolver);
    static const ScreenIdResolver& screenIdResolver();

    // Predefined layouts (templates)
    static Layout* createColumnsLayout(int columns, QObject* parent = nullptr);
    static Layout* createRowsLayout(int rows, QObject* parent = nullptr);
    static Layout* createGridLayout(int columns, int rows, QObject* parent = nullptr);
    static Layout* createPriorityGridLayout(QObject* parent = nullptr);
    static Layout* createFocusLayout(QObject* parent = nullptr);

    // Dirty tracking for copy-on-write saving
    bool isDirty() const
    {
        return m_dirty;
    }
    void markDirty()
    {
        m_dirty = true;
    }
    void clearDirty()
    {
        m_dirty = false;
    }
    void beginBatchModify();
    void endBatchModify();

Q_SIGNALS:
    void nameChanged();
    void descriptionChanged();
    void zonePaddingChanged();
    void outerGapChanged();
    void showZoneNumbersChanged();
    void overlayDisplayModeChanged();
    void sourcePathChanged();
    void shaderIdChanged();
    void shaderParamsChanged();
    void aspectRatioClassChanged();
    void hiddenFromSelectorChanged();
    void allowedScreensChanged();
    void allowedDesktopsChanged();
    void allowedActivitiesChanged();
    void appRulesChanged();
    void autoAssignChanged();
    void useFullScreenGeometryChanged();
    void zonesChanged();
    void zoneAdded(Zone* zone);
    void zoneRemoved(Zone* zone);
    void layoutModified();

public:
    /// Recalculate every zone's absolute geometry against @p screenGeometry.
    ///
    /// The PlasmaZones application enforces its own "only LayoutComputeService
    /// calls this" coalescing discipline via its type system — that
    /// restriction is an application-layer concern, not a library one, so
    /// the method is public here.  Direct callers bypass the service's
    /// coalescing / threading contract and should only do so when they know
    /// they're running on the main thread with no pending compute requests.
    void recalculateZoneGeometries(const QRectF& screenGeometry);

private:
    void emitModifiedIfNotBatched();

    QUuid m_id;
    QString m_name;
    QString m_description;
    int m_zonePadding = -1; // -1 = use global setting
    int m_outerGap = -1; // -1 = use global setting
    bool m_usePerSideOuterGap = false;
    int m_outerGapTop = -1; // -1 = use global setting
    int m_outerGapBottom = -1;
    int m_outerGapLeft = -1;
    int m_outerGapRight = -1;
    bool m_showZoneNumbers = true;
    int m_overlayDisplayMode = -1; // -1 = use global setting
    QString m_sourcePath; // Path where layout was loaded from (empty for new layouts)
    QString m_systemSourcePath; // Original system path if this is a user override of a system layout
    int m_defaultOrder = 999; // Optional: lower values appear first when choosing default (999 = not set)
    QVector<Zone*> m_zones;

    // App-to-zone rules
    QVector<AppRule> m_appRules;

    // Auto-assign: new windows fill first empty zone
    bool m_autoAssign = false;

    // Geometry mode: zones use full screen (true) or available area excluding panels (false)
    bool m_useFullScreenGeometry = false;

    // Shader support
    QString m_shaderId; // Shader effect ID (empty = no shader)
    QVariantMap m_shaderParams; // Shader-specific parameters

    // Aspect ratio classification
    ::PhosphorLayout::AspectRatioClass m_aspectRatioClass = ::PhosphorLayout::AspectRatioClass::Any;
    qreal m_minAspectRatio = 0.0; // 0 = not set (use class matching)
    qreal m_maxAspectRatio = 0.0; // 0 = not set (use class matching)

    // Visibility filtering
    bool m_hiddenFromSelector = false;
    QStringList m_allowedScreens; // empty = all screens
    QList<int> m_allowedDesktops; // empty = all desktops
    QStringList m_allowedActivities; // empty = all activities

    // Cache last geometry used for recalculation to avoid redundant work
    mutable QRectF m_lastRecalcGeometry;

    // Dirty tracking for copy-on-write saving
    bool m_dirty = false;
    int m_batchModifyDepth = 0;
};

} // namespace PhosphorZones

Q_DECLARE_METATYPE(PhosphorZones::LayoutCategory)
