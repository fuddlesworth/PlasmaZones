// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <functional>
#include <memory>

namespace PlasmaZones {

class AssignmentManager;
class LayoutManager;
class ScreenHelper;
class Settings;

/**
 * @brief Assignments sub-KCM -- Screen, desktop, activity, and app-to-zone layout assignments
 */
class KCMAssignments : public KQuickConfigModule
{
    Q_OBJECT

    // Assignment view mode
    Q_PROPERTY(
        int assignmentViewMode READ assignmentViewMode WRITE setAssignmentViewMode NOTIFY assignmentViewModeChanged)

    // Layout list (for combo boxes)
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY layoutsChanged)
    Q_PROPERTY(QString defaultLayoutId READ defaultLayoutId NOTIFY defaultLayoutIdChanged)
    Q_PROPERTY(bool autotileEnabled READ autotileEnabled NOTIFY autotileEnabledChanged)
    Q_PROPERTY(QString autotileAlgorithm READ autotileAlgorithm NOTIFY autotileAlgorithmChanged)

    // Screens
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)

    // Virtual desktop support
    Q_PROPERTY(int virtualDesktopCount READ virtualDesktopCount NOTIFY virtualDesktopCountChanged)
    Q_PROPERTY(QStringList virtualDesktopNames READ virtualDesktopNames NOTIFY virtualDesktopNamesChanged)

    // KDE Activities support
    Q_PROPERTY(bool activitiesAvailable READ activitiesAvailable NOTIFY activitiesAvailableChanged)
    Q_PROPERTY(QVariantList activities READ activities NOTIFY activitiesChanged)
    Q_PROPERTY(QString currentActivity READ currentActivity NOTIFY currentActivityChanged)

    // Disabled monitors (for MonitorAssignmentsCard)
    Q_PROPERTY(QStringList disabledMonitors READ disabledMonitors NOTIFY disabledMonitorsChanged)

public:
    KCMAssignments(QObject* parent, const KPluginMetaData& data);
    ~KCMAssignments() override;

    // Assignment view mode
    int assignmentViewMode() const;
    void setAssignmentViewMode(int mode);

    // Layout list
    QVariantList layouts() const;
    QString defaultLayoutId() const;
    bool autotileEnabled() const;
    QString autotileAlgorithm() const;

    // Screens
    QVariantList screens() const;

    // Virtual desktops
    int virtualDesktopCount() const;
    QStringList virtualDesktopNames() const;

    // KDE Activities
    bool activitiesAvailable() const;
    QVariantList activities() const;
    QString currentActivity() const;

    // Disabled monitors
    QStringList disabledMonitors() const;

    // ── Screen assignments (snapping) ──────────────────────────────────────
    Q_INVOKABLE void assignLayoutToScreen(const QString& screenName, const QString& layoutId);
    Q_INVOKABLE void clearScreenAssignment(const QString& screenName);
    Q_INVOKABLE QString getLayoutForScreen(const QString& screenName) const;

    // ── Tiling screen assignments ──────────────────────────────────────────
    Q_INVOKABLE void assignTilingLayoutToScreen(const QString& screenName, const QString& layoutId);
    Q_INVOKABLE void clearTilingScreenAssignment(const QString& screenName);
    Q_INVOKABLE QString getTilingLayoutForScreen(const QString& screenName) const;

    // ── Per-desktop screen assignments (snapping) ──────────────────────────
    Q_INVOKABLE void assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                 const QString& layoutId);
    Q_INVOKABLE void clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop);
    Q_INVOKABLE QString getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE QString getSnappingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE bool hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const;

    // ── Tiling per-desktop screen assignments ──────────────────────────────
    Q_INVOKABLE void assignTilingLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                       const QString& layoutId);
    Q_INVOKABLE void clearTilingScreenDesktopAssignment(const QString& screenName, int virtualDesktop);
    Q_INVOKABLE QString getTilingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    Q_INVOKABLE bool hasExplicitTilingAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const;

    // ── Per-activity screen assignments (snapping) ─────────────────────────
    Q_INVOKABLE void assignLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                  const QString& layoutId);
    Q_INVOKABLE void clearScreenActivityAssignment(const QString& screenName, const QString& activityId);
    Q_INVOKABLE QString getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE QString getSnappingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE bool hasExplicitAssignmentForScreenActivity(const QString& screenName, const QString& activityId) const;

    // ── Tiling per-activity screen assignments ─────────────────────────────
    Q_INVOKABLE void assignTilingLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                        const QString& layoutId);
    Q_INVOKABLE void clearTilingScreenActivityAssignment(const QString& screenName, const QString& activityId);
    Q_INVOKABLE QString getTilingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    Q_INVOKABLE bool hasExplicitTilingAssignmentForScreenActivity(const QString& screenName,
                                                                  const QString& activityId) const;

    // ── Monitor disable ────────────────────────────────────────────────────
    Q_INVOKABLE bool isMonitorDisabled(const QString& screenName) const;
    Q_INVOKABLE void setMonitorDisabled(const QString& screenName, bool disabled);

    // ── Layout lock ─────────────────────────────────────────────────────────
    // mode: 0 = snapping, 1 = tiling (locks are independent per mode)
    Q_INVOKABLE bool isScreenLocked(const QString& screenName, int mode) const;
    Q_INVOKABLE void toggleScreenLock(const QString& screenName, int mode);
    Q_INVOKABLE bool isContextLocked(const QString& screenName, int virtualDesktop, const QString& activity,
                                     int mode) const;
    Q_INVOKABLE void toggleContextLock(const QString& screenName, int virtualDesktop, const QString& activity,
                                       int mode);

    // ── Quick layout slots ─────────────────────────────────────────────────
    Q_INVOKABLE QString getQuickLayoutSlot(int slotNumber) const;
    Q_INVOKABLE void setQuickLayoutSlot(int slotNumber, const QString& layoutId);
    Q_INVOKABLE QString getQuickLayoutShortcut(int slotNumber) const;
    Q_INVOKABLE QString getTilingQuickLayoutSlot(int slotNumber) const;
    Q_INVOKABLE void setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId);

    // ── Running windows (for WindowPickerDialog) ──────────────────────────
    Q_INVOKABLE QVariantList getRunningWindows() const;

    // ── App-to-zone rules ──────────────────────────────────────────────────
    Q_INVOKABLE QVariantList getAppRulesForLayout(const QString& layoutId) const;
    Q_INVOKABLE void setAppRulesForLayout(const QString& layoutId, const QVariantList& rules);
    Q_INVOKABLE void addAppRuleToLayout(const QString& layoutId, const QString& pattern, int zoneNumber,
                                        const QString& targetScreen = QString());
    Q_INVOKABLE void removeAppRuleFromLayout(const QString& layoutId, int index);

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;
    void refreshScreens();
    void refreshVirtualDesktops();
    void refreshActivities();
    void onExternalSettingsChanged();

Q_SIGNALS:
    void assignmentViewModeChanged();
    void layoutsChanged();
    void defaultLayoutIdChanged();
    void autotileEnabledChanged();
    void autotileAlgorithmChanged();
    void screensChanged();
    void virtualDesktopCountChanged();
    void virtualDesktopNamesChanged();
    void activitiesAvailableChanged();
    void activitiesChanged();
    void currentActivityChanged();
    void disabledMonitorsChanged();
    void lockedScreensChanged();
    void screenAssignmentsChanged();
    void tilingScreenAssignmentsChanged();
    void tilingActivityAssignmentsChanged();
    void tilingDesktopAssignmentsChanged();
    void quickLayoutSlotsChanged();
    void tilingQuickLayoutSlotsChanged();
    void activityAssignmentsChanged();
    void appRulesRefreshed();

private:
    void emitAllChanged();
    void onCurrentActivityChanged(const QString& activityId);
    void onActivitiesChanged();

    Settings* m_settings = nullptr;
    bool m_saving = false;
    std::unique_ptr<AssignmentManager> m_assignmentManager;
    std::unique_ptr<LayoutManager> m_layoutManager;
    std::unique_ptr<ScreenHelper> m_screenHelper;

    // Virtual desktops
    int m_virtualDesktopCount = 1;
    QStringList m_virtualDesktopNames;

    // KDE Activities
    bool m_activitiesAvailable = false;
    QVariantList m_activities;
    QString m_currentActivity;
};

} // namespace PlasmaZones
