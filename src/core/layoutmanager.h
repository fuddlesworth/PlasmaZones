// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorZones/Layout.h>
#include "interfaces.h"
#include "assignmententry.h"

#include <PhosphorConfig/IBackend.h>

#include <QHash>
#include <QSet>
#include <QUuid>
#include <QString>
#include <memory>

namespace PlasmaZones {

/**
 * @brief Manages all layouts and their assignments to screens/desktops
 *
 * Central component for zone layouts: loading/saving, screen/desktop/activity
 * assignment, keyboard shortcuts, and built-in templates.
 *
 * Inherits QObject for signals and PhosphorZones::ILayoutManager for the interface contract.
 * PhosphorZones::ILayoutManager avoids QObject to prevent signal shadowing issues.
 */
class PLASMAZONES_EXPORT LayoutManager : public QObject, public PhosphorZones::ILayoutManager
{
    Q_OBJECT

    Q_PROPERTY(int layoutCount READ layoutCount NOTIFY layoutsChanged)
    Q_PROPERTY(PhosphorZones::Layout* activeLayout READ activeLayout NOTIFY activeLayoutChanged)
    Q_PROPERTY(QString layoutDirectory READ layoutDirectory WRITE setLayoutDirectory NOTIFY layoutDirectoryChanged)

public:
    explicit LayoutManager(QObject* parent = nullptr);
    LayoutManager(PhosphorConfig::IBackend* backend, QObject* parent);
    ~LayoutManager();

    // No singleton - use dependency injection instead

    // PhosphorZones::ILayoutManager interface implementation
    QString layoutDirectory() const override
    {
        return m_layoutDirectory;
    }
    void setLayoutDirectory(const QString& directory) override;

    int layoutCount() const override
    {
        return m_layouts.size();
    }
    QVector<PhosphorZones::Layout*> layouts() const override
    {
        return m_layouts;
    }
    PhosphorZones::Layout* layout(int index) const override;
    PhosphorZones::Layout* layoutById(const QUuid& id) const override;
    PhosphorZones::Layout* layoutByName(const QString& name) const override;

    Q_INVOKABLE void addLayout(PhosphorZones::Layout* layout) override;
    Q_INVOKABLE void removeLayout(PhosphorZones::Layout* layout) override;
    Q_INVOKABLE void removeLayoutById(const QUuid& id) override;
    Q_INVOKABLE PhosphorZones::Layout* duplicateLayout(PhosphorZones::Layout* source) override;

    PhosphorZones::Layout* activeLayout() const override
    {
        return m_activeLayout;
    }
    PhosphorZones::Layout* defaultLayout() const override;
    /**
     * @brief Get the layout that was active before the most recent switch
     * @return Previous layout. On first setActiveLayout, equals activeLayout.
     * @note Used for resnap-to-new-layout: windows from previous layout are remapped to current
     */
    PhosphorZones::Layout* previousLayout() const
    {
        return m_previousLayout;
    }
    Q_INVOKABLE void setActiveLayout(PhosphorZones::Layout* layout) override;
    Q_INVOKABLE void setActiveLayoutById(const QUuid& id) override;

    Q_INVOKABLE void assignLayout(const QString& screenId, int virtualDesktop, const QString& activity,
                                  PhosphorZones::Layout* layout) override;
    Q_INVOKABLE void assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                      const QString& layoutId) override;

    /**
     * @brief Set a full AssignmentEntry directly (from KCM via D-Bus)
     *
     * Stores the entry in m_assignments regardless of isValid() — mode-only
     * entries are valid when explicitly set by the KCM.
     */
    void setAssignmentEntryDirect(const QString& screenId, int virtualDesktop, const QString& activity,
                                  const AssignmentEntry& entry);
    Q_INVOKABLE PhosphorZones::Layout* layoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                                       const QString& activity = QString()) const override;
    Q_INVOKABLE void clearAssignment(const QString& screenId, int virtualDesktop = 0,
                                     const QString& activity = QString()) override;

    /**
     * @brief Check if there's an explicit assignment (no fallback)
     * @param screenId Stable EDID-based screen ID (or connector name fallback)
     * @param virtualDesktop Desktop number (0 = all desktops)
     * @param activity Activity ID (empty = all activities)
     * @return true if explicit assignment exists for this exact key
     */
    Q_INVOKABLE bool hasExplicitAssignment(const QString& screenId, int virtualDesktop = 0,
                                           const QString& activity = QString()) const override;

    /**
     * @brief Get the raw assignment ID for a screen (may be UUID or autotile ID)
     *
     * Same fallback cascade as layoutForScreen() but returns the raw assignment
     * string instead of resolving it to a PhosphorZones::Layout*. Use this when you need to
     * distinguish autotile assignments from manual ones.
     *
     * @param screenId Stable EDID-based screen ID (or connector name fallback)
     * @param virtualDesktop Desktop number (0 = all desktops)
     * @param activity Activity ID (empty = all activities)
     * @return Raw assignment string (UUID or "autotile:*"), or empty if no assignment
     */
    QString assignmentIdForScreen(const QString& screenId, int virtualDesktop = 0,
                                  const QString& activity = QString()) const;

    /**
     * @brief Get the full AssignmentEntry for a screen/desktop/activity
     * @note Same fallback cascade as layoutForScreen() but returns the full entry
     */
    AssignmentEntry assignmentEntryForScreen(const QString& screenId, int virtualDesktop = 0,
                                             const QString& activity = QString()) const;

    /**
     * @brief Get the mode (Snapping or Autotile) for a context
     */
    AssignmentEntry::Mode modeForScreen(const QString& screenId, int virtualDesktop = 0,
                                        const QString& activity = QString()) const;

    /**
     * @brief Get the snapping layout UUID for a context (regardless of mode)
     */
    QString snappingLayoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                    const QString& activity = QString()) const;

    /**
     * @brief Get the tiling algorithm ID for a context (regardless of mode)
     */
    QString tilingAlgorithmForScreen(const QString& screenId, int virtualDesktop = 0,
                                     const QString& activity = QString()) const;

    /**
     * @brief Clear all autotile assignments (when feature gate is disabled)
     *
     * Flips mode to Snapping for all entries that are in Autotile mode.
     * Preserves snappingLayout field. Emits layoutAssigned for each
     * affected screen and saves once at end.
     */
    void clearAutotileAssignments();

    /**
     * @brief Batch set all screen assignments (base assignments, desktop=0, no activity)
     * @param assignments Map of screenId -> layoutId
     * Clears existing base assignments and sets new ones, saves once at end
     */
    void setAllScreenAssignments(const QHash<QString, QString>& assignments) override;

    /**
     * @brief Batch set all per-desktop assignments
     * @param assignments Map of (screenId, virtualDesktop) -> layoutId
     * Clears existing per-desktop assignments and sets new ones, saves once at end
     */
    void setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments) override;

    /**
     * @brief Batch set all per-activity assignments
     * @param assignments Map of (screenId, activityId) -> layoutId
     * Clears existing per-activity assignments and sets new ones, saves once at end
     */
    void setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments) override;

    Q_INVOKABLE PhosphorZones::Layout* layoutForShortcut(int number) const override;
    Q_INVOKABLE void applyQuickLayout(int number, const QString& screenId) override;
    void setQuickLayoutSlot(int number, const QString& layoutId) override;
    void setAllQuickLayoutSlots(const QHash<int, QString>& slots) override; // Batch set - saves once
    QHash<int, QString> quickLayoutSlots() const override
    {
        return m_quickLayoutShortcuts;
    }

    // PhosphorZones::Layout cycling
    Q_INVOKABLE void cycleToPreviousLayout(const QString& screenId);
    Q_INVOKABLE void cycleToNextLayout(const QString& screenId);

    // Context for visibility-filtered cycling and per-screen layout lookups
    int currentVirtualDesktop() const override
    {
        return m_currentVirtualDesktop;
    }
    QString currentActivity() const override
    {
        return m_currentActivity;
    }
    void setCurrentVirtualDesktop(int desktop)
    {
        m_currentVirtualDesktop = desktop;
    }
    void setCurrentActivity(const QString& activity)
    {
        m_currentActivity = activity;
    }

    Q_INVOKABLE void createBuiltInLayouts() override;
    QVector<PhosphorZones::Layout*> builtInLayouts() const override;

    Q_INVOKABLE void loadLayouts() override;
    void setSettings(ISettings* settings);
    Q_INVOKABLE void saveLayouts() override;
    void saveLayout(PhosphorZones::Layout* layout) override;
    Q_INVOKABLE void loadAssignments() override;
    Q_INVOKABLE void saveAssignments() override;
    Q_INVOKABLE void importLayout(const QString& filePath) override;
    Q_INVOKABLE void exportLayout(PhosphorZones::Layout* layout, const QString& filePath) override;

    // Autotile layout overrides (per-algorithm gap, visibility, shader settings)
    void saveAutotileOverrides(const QString& algorithmId, const QJsonObject& overrides);
    QJsonObject loadAutotileOverrides(const QString& algorithmId) const;

    /**
     * @brief Get all per-desktop assignments (virtualDesktop > 0, no activity)
     * @return Map of (screenId, virtualDesktop) -> layoutId
     */
    QHash<QPair<QString, int>, QString> desktopAssignments() const;

    /**
     * @brief Get all per-activity assignments (activity non-empty, any desktop)
     * @return Map of (screenId, activityId) -> layoutId
     */
    QHash<QPair<QString, QString>, QString> activityAssignments() const;

Q_SIGNALS:
    // Signals declared here only (PhosphorZones::ILayoutManager is a pure interface without signals)
    void layoutsChanged();
    void layoutAdded(PhosphorZones::Layout* layout);
    void layoutRemoved(PhosphorZones::Layout* layout);
    void activeLayoutChanged(PhosphorZones::Layout* layout);
    void layoutAssigned(const QString& screenId, int virtualDesktop, PhosphorZones::Layout* layout);
    void layoutDirectoryChanged();
    void layoutsLoaded();
    void layoutsSaved();

private:
    void ensureLayoutDirectory();
    void loadLayoutsFromDirectory(const QString& directory);
    PhosphorZones::Layout* restoreSystemLayout(const QUuid& id, const QString& systemPath);
    QString layoutFilePath(const QUuid& id) const;
    void readAssignmentGroups(PhosphorConfig::IBackend* backend);
    void readQuickLayouts(PhosphorConfig::IBackend* backend);
    PhosphorZones::Layout* cycleLayoutImpl(const QString& screenId, int direction);
    bool shouldSkipLayoutAssignment(const QString& layoutId, const QString& context) const;
    void emitLayoutAssigned(const QString& screenId, int virtualDesktop, const QString& layoutId);
    QJsonObject loadAllAutotileOverrides() const;
    void saveAllAutotileOverrides(const QJsonObject& all);
    ISettings* m_settings = nullptr;
    std::unique_ptr<PhosphorConfig::IBackend> m_ownedBackend;
    PhosphorConfig::IBackend* m_configBackend = nullptr; // always valid after construction
    QString m_layoutDirectory;
    QVector<PhosphorZones::Layout*> m_layouts;
    PhosphorZones::Layout* m_activeLayout = nullptr;
    PhosphorZones::Layout* m_previousLayout =
        nullptr; ///< PhosphorZones::Layout active before last setActiveLayout (for resnap)
    QHash<LayoutAssignmentKey, AssignmentEntry> m_assignments;
    QHash<int, QString> m_quickLayoutShortcuts; // number -> layout ID
    int m_currentVirtualDesktop = 1;
    QString m_currentActivity;
};

} // namespace PlasmaZones
