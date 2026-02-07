// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QHash>
#include <optional>

namespace PlasmaZones {

class LayoutManager; // Concrete type needed for signal connections
class VirtualDesktopManager;
class ActivityManager;
class Layout;

/**
 * @brief D-Bus adaptor for layout management operations
 *
 * Provides D-Bus interface: org.plasmazones.LayoutManager
 *  Layout CRUD and assignment operations
 */
class PLASMAZONES_EXPORT LayoutAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.LayoutManager")

public:
    explicit LayoutAdaptor(LayoutManager* manager, QObject* parent = nullptr);
    explicit LayoutAdaptor(LayoutManager* manager, VirtualDesktopManager* vdm, QObject* parent = nullptr);
    ~LayoutAdaptor() override = default;

    void setVirtualDesktopManager(VirtualDesktopManager* vdm);
    void setActivityManager(ActivityManager* am);

public Q_SLOTS:
    // Layout queries
    QString getActiveLayout();
    QStringList getLayoutList();
    QString getLayout(const QString& id);

    // Layout management
    void setActiveLayout(const QString& id);
    void applyQuickLayout(int number, const QString& screenName);
    QString createLayout(const QString& name, const QString& type);
    void deleteLayout(const QString& id);
    QString duplicateLayout(const QString& id);

    // Import/Export
    QString importLayout(const QString& filePath);
    void exportLayout(const QString& layoutId, const QString& filePath);

    // Visibility filtering
    void setLayoutHidden(const QString& layoutId, bool hidden);

    // Editor support
    bool updateLayout(const QString& layoutJson);
    QString createLayoutFromJson(const QString& layoutJson);

    // Editor launch
    void openEditor();
    void openEditorForScreen(const QString& screenName);
    void openEditorForLayout(const QString& layoutId);
    void openEditorForLayoutOnScreen(const QString& layoutId, const QString& screenName);

    // Screen assignments
    QString getLayoutForScreen(const QString& screenName);
    void assignLayoutToScreen(const QString& screenName, const QString& layoutId);
    void clearAssignment(const QString& screenName);
    void setAllScreenAssignments(const QVariantMap& assignments);  // Batch set - saves once

    // Per-virtual-desktop screen assignments
    QString getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop);
    void assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop, const QString& layoutId);
    void clearAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop);
    bool hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop);
    void setAllDesktopAssignments(const QVariantMap& assignments);  // Batch set - key: "screen:desktop", value: layoutId

    // Virtual desktop information
    int getVirtualDesktopCount();
    QStringList getVirtualDesktopNames();
    QString getAllScreenAssignments();
    QVariantMap getAllDesktopAssignments();   // Get all per-desktop assignments as key -> layoutId
    QVariantMap getAllActivityAssignments();  // Get all per-activity assignments as key -> layoutId

    // Quick layout slots (1-9)
    QString getQuickLayoutSlot(int slotNumber);
    void setQuickLayoutSlot(int slotNumber, const QString& layoutId);
    void setAllQuickLayoutSlots(const QVariantMap& slots);  // Batch set - saves once
    QVariantMap getAllQuickLayoutSlots();

    // ═══════════════════════════════════════════════════════════════════════════════
    // KDE Activities Support
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if KDE Activities support is available
     * @return true if KActivities is running
     */
    bool isActivitiesAvailable();

    /**
     * @brief Get list of all activity IDs
     * @return List of activity UUIDs
     */
    QStringList getActivities();

    /**
     * @brief Get the current activity ID
     * @return Current activity UUID, or empty if unavailable
     */
    QString getCurrentActivity();

    /**
     * @brief Get activity info as JSON
     * @param activityId Activity UUID
     * @return JSON with id, name, icon fields
     */
    QString getActivityInfo(const QString& activityId);

    /**
     * @brief Get all activities as JSON array
     * @return JSON array with activity info objects
     */
    QString getAllActivitiesInfo();

    // Per-activity layout assignments (screen + activity, any desktop)
    QString getLayoutForScreenActivity(const QString& screenName, const QString& activityId);
    void assignLayoutToScreenActivity(const QString& screenName, const QString& activityId, const QString& layoutId);
    void clearAssignmentForScreenActivity(const QString& screenName, const QString& activityId);
    bool hasExplicitAssignmentForScreenActivity(const QString& screenName, const QString& activityId);
    void setAllActivityAssignments(const QVariantMap& assignments);  // Batch set - key: "screen:activity", value: layoutId

    // Full assignment (screen + desktop + activity)
    QString getLayoutForScreenDesktopActivity(const QString& screenName, int virtualDesktop, const QString& activityId);
    void assignLayoutToScreenDesktopActivity(const QString& screenName, int virtualDesktop, const QString& activityId, const QString& layoutId);
    void clearAssignmentForScreenDesktopActivity(const QString& screenName, int virtualDesktop, const QString& activityId);

Q_SIGNALS:
    /**
     * @brief Emitted when the daemon has fully initialized and is ready
     * @note KCM should wait for this signal before querying layouts
     */
    void daemonReady();

    void layoutChanged(const QString& layoutJson);
    void layoutListChanged();
    void screenLayoutChanged(const QString& screenName, const QString& layoutId);
    void virtualDesktopCountChanged(int count);

    /**
     * @brief Emitted when the active layout changes (for KCM UI sync)
     * @param layoutId The ID of the newly active layout
     * @note This allows the settings panel to update its selection when
     *       the layout is changed externally (e.g., via quick layout hotkey)
     */
    void activeLayoutIdChanged(const QString& layoutId);

    /**
     * @brief Emitted when quick layout slots are modified
     * @note This allows the settings panel to refresh its quick layout slot assignments
     */
    void quickLayoutSlotsChanged();

    /**
     * @brief Emitted when the current KDE Activity changes
     * @param activityId The new activity ID
     */
    void currentActivityChanged(const QString& activityId);

    /**
     * @brief Emitted when the list of KDE Activities changes (added/removed)
     */
    void activitiesChanged();

private Q_SLOTS:
    // String-based connection slots for LayoutManager signals
    // (LayoutManager redeclares signals for Q_PROPERTY, so we use string-based connections)
    void onActiveLayoutChanged(Layout* layout);
    void onLayoutsChanged();
    void onLayoutAssigned(const QString& screen, Layout* layout);

private:
    void invalidateCache();
    void connectLayoutManagerSignals();
    void connectVirtualDesktopSignals();
    void connectActivitySignals();

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper Methods
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Parse UUID string with validation and logging
     * @param id UUID string to parse
     * @param operation Description for error logging (e.g., "setActiveLayout")
     * @return Valid QUuid or std::nullopt on failure (logs warning)
     */
    std::optional<QUuid> parseAndValidateUuid(const QString& id, const QString& operation) const;

    /**
     * @brief Get layout by ID string with full validation
     * @param id UUID string
     * @param operation Description for error logging
     * @return Layout pointer or nullptr on failure (logs warning)
     *
     * Consolidates parseUuid + layoutById + error logging.
     */
    Layout* getValidatedLayout(const QString& id, const QString& operation);

    /**
     * @brief Validate that a required string parameter is not empty
     * @param value The value to check
     * @param paramName Parameter name for error message (e.g., "layout ID")
     * @param operation Operation name for error message
     * @return true if valid (non-empty), false if empty (logs warning)
     */
    bool validateNonEmpty(const QString& value, const QString& paramName, const QString& operation) const;

    /**
     * @brief Parse JSON string to QJsonObject with validation
     * @param jsonString JSON string to parse
     * @param operation Operation name for error messages
     * @return QJsonObject if valid, std::nullopt on parse error or non-object
     */
    std::optional<QJsonObject> parseJsonObject(const QString& jsonString, const QString& operation) const;

    /**
     * @brief Launch the editor with given arguments
     * @param args Command line arguments for the editor
     * @param description Description for logging (e.g., "for screen: DP-1")
     */
    void launchEditor(const QStringList& args, const QString& description);

    /**
     * @brief Build activity info JSON object
     * @param activityId The activity ID
     * @return QJsonObject with id, name, icon fields
     */
    QJsonObject buildActivityInfoJson(const QString& activityId) const;

    LayoutManager* m_layoutManager; // Concrete type for signal connections
    VirtualDesktopManager* m_virtualDesktopManager = nullptr;
    ActivityManager* m_activityManager = nullptr;

    // JSON caching for performance
    QString m_cachedActiveLayoutJson;
    QUuid m_cachedActiveLayoutId;
    QHash<QUuid, QString> m_cachedLayoutJson; // Cache for individual layouts
};

} // namespace PlasmaZones
