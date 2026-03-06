// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {

class KCMPlasmaZones;
class Settings;

/**
 * @brief Manages screen/desktop/activity assignments, quick layout slots, and app-to-zone rules
 *
 * Owns all assignment caches, pending state, and save/load logic.
 * Uses the back-pointer pattern to access KCM's Settings, screens, and D-Bus helpers.
 */
class AssignmentManager : public QObject
{
    Q_OBJECT

public:
    explicit AssignmentManager(KCMPlasmaZones* kcm, Settings* settings, QObject* parent = nullptr);

    // ── Accessors ──────────────────────────────────────────────────────────
    const QVariantMap& screenAssignments() const
    {
        return m_screenAssignments;
    }
    const QVariantMap& tilingScreenAssignments() const
    {
        return m_tilingScreenAssignments;
    }
    int assignmentViewMode() const
    {
        return m_assignmentViewMode;
    }

    // ── Screen assignments (snapping) ──────────────────────────────────────
    void assignLayoutToScreen(const QString& screenName, const QString& layoutId);
    void clearScreenAssignment(const QString& screenName);
    QString getLayoutForScreen(const QString& screenName) const;

    // ── Tiling screen assignments ──────────────────────────────────────────
    void assignTilingLayoutToScreen(const QString& screenName, const QString& layoutId);
    void clearTilingScreenAssignment(const QString& screenName);
    QString getTilingLayoutForScreen(const QString& screenName) const;

    // ── Per-desktop screen assignments (daemon-backed) ─────────────────────
    void assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop, const QString& layoutId);
    void clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop);
    QString getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    bool hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const;

    // ── Tiling per-desktop screen assignments ──────────────────────────────
    void assignTilingLayoutToScreenDesktop(const QString& screenName, int virtualDesktop, const QString& layoutId);
    void clearTilingScreenDesktopAssignment(const QString& screenName, int virtualDesktop);
    QString getTilingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const;
    bool hasExplicitTilingAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const;

    // ── Per-activity screen assignments (daemon-backed) ────────────────────
    void assignLayoutToScreenActivity(const QString& screenName, const QString& activityId, const QString& layoutId);
    void clearScreenActivityAssignment(const QString& screenName, const QString& activityId);
    QString getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    bool hasExplicitAssignmentForScreenActivity(const QString& screenName, const QString& activityId) const;

    // ── Tiling per-activity screen assignments ─────────────────────────────
    void assignTilingLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                            const QString& layoutId);
    void clearTilingScreenActivityAssignment(const QString& screenName, const QString& activityId);
    QString getTilingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const;
    bool hasExplicitTilingAssignmentForScreenActivity(const QString& screenName, const QString& activityId) const;

    // ── Quick layout slots ─────────────────────────────────────────────────
    QString getQuickLayoutSlot(int slotNumber) const;
    void setQuickLayoutSlot(int slotNumber, const QString& layoutId);
    QString getQuickLayoutShortcut(int slotNumber) const;
    QString getTilingQuickLayoutSlot(int slotNumber) const;
    void setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId);

    // ── Assignment view mode ───────────────────────────────────────────────
    void setAssignmentViewMode(int mode);

    // ── App-to-zone rules ──────────────────────────────────────────────────
    QVariantList getAppRulesForLayout(const QString& layoutId) const;
    void setAppRulesForLayout(const QString& layoutId, const QVariantList& rules);
    void addAppRuleToLayout(const QString& layoutId, const QString& pattern, int zoneNumber,
                            const QString& targetScreen = QString());
    void removeAppRuleFromLayout(const QString& layoutId, int index);

public Q_SLOTS:
    // ── D-Bus event handlers ───────────────────────────────────────────────
    void onScreenLayoutChanged(const QString& screenName, const QString& layoutId);
    void onQuickLayoutSlotsChanged();

public:
    // ── Save/load/defaults integration ─────────────────────────────────────
    void save(QStringList& failedOperations);
    void load();
    void resetToDefaults();
    void clearPendingStates();

    // ── Autotile ID sync ───────────────────────────────────────────────────
    QString resolveAutotileAlgorithm(const QString& key, bool isScreenAssignment) const;
    bool syncAutotileAssignmentIds(QVariantMap& assignments, bool isScreenAssignment);
    bool syncAutotileAssignmentIds(QMap<QString, QString>& assignments, bool isScreenAssignment);

Q_SIGNALS:
    void screenAssignmentsChanged();
    void tilingScreenAssignmentsChanged();
    void tilingActivityAssignmentsChanged();
    void tilingDesktopAssignmentsChanged();
    void assignmentViewModeChanged();
    void quickLayoutSlotsChanged();
    void tilingQuickLayoutSlotsChanged();
    void activityAssignmentsChanged();
    void appRulesRefreshed();
    void needsSave();
    void refreshScreensRequested();

private:
    QDBusMessage callDaemon(const QString& interface, const QString& method, const QVariantList& args = {}) const;

    KCMPlasmaZones* m_kcm = nullptr;
    Settings* m_settings = nullptr;

    // Screens and assignments
    QVariantMap m_screenAssignments;
    QVariantMap m_tilingScreenAssignments;
    QMap<int, QString> m_quickLayoutSlots;
    QMap<int, QString> m_tilingQuickLayoutSlots;
    QMap<QString, QString> m_tilingActivityAssignments;
    QMap<QString, QString> m_tilingDesktopAssignments;
    int m_assignmentViewMode = 0;

    // Pending per-desktop assignments
    QMap<QString, QString> m_pendingDesktopAssignments;
    QSet<QString> m_clearedDesktopAssignments;

    // Pending per-activity assignments
    QMap<QString, QString> m_pendingActivityAssignments;
    QSet<QString> m_clearedActivityAssignments;

    // Dirty flags for tiling per-desktop/per-activity
    bool m_tilingDesktopAssignmentsDirty = false;
    bool m_tilingActivityAssignmentsDirty = false;

    // Pending app-to-zone rules
    QHash<QString, QVariantList> m_pendingAppRules;
};

} // namespace PlasmaZones
