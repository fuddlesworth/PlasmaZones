// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/ILayoutManager.h>
#include <PhosphorZones/Layout.h>

#include <PhosphorConfig/IBackend.h>

#include <QHash>
#include <QSet>
#include <QUuid>
#include <QString>
#include <functional>
#include <memory>

namespace PhosphorZones {

/**
 * @brief Schema-key configuration for a LayoutManager instance.
 *
 * Decouples the manager from PlasmaZones-specific config-key strings
 * so the class can move into the @c phosphor-zones library without
 * dragging in @c ConfigDefaults / @c configbackends. Composition roots
 * supply the project's actual values (PlasmaZones uses
 * @c makePzLayoutManager() below; tests use @c testLayoutManagerConfig
 * from @c TestHelpers).
 *
 * Every field is required — the manager has no built-in defaults
 * because "default values" are inherently project-specific (the lib
 * has no opinion on what to call its assignment groups).
 */
struct LayoutManagerConfig
{
    /// Group-name prefix for per-context layout assignments
    /// (e.g. @c "Assignment:" so groups read @c "Assignment:eDP-1:Desktop:2").
    QString assignmentGroupPrefix;

    /// Group name for the quick-layout shortcut map.
    QString quickLayoutsGroup;

    /// Group name for the per-context mode-tracking map (snapping vs autotile).
    QString modeTrackingGroup;

    /// Default filesystem directory for layout JSON files. Composition
    /// roots typically pass @c "$XDG_DATA_HOME/<app>/layouts". The
    /// manager calls @c QDir::mkpath on this in its constructor; pass
    /// an empty string to defer until @c setLayoutDirectory() is called
    /// explicitly.
    QString defaultLayoutDirectory;

    /// One-shot factory for the LEGACY config backend the manager
    /// inspects on first launch when @c assignments.json is empty —
    /// migrates old @c [Assignment:*] / @c [QuickLayouts] groups out of
    /// the project's general @c config.json into the dedicated
    /// assignments backend. Optional. Empty function (the default)
    /// disables migration entirely; clean installs and non-PlasmaZones
    /// consumers simply skip it. PlasmaZones wires it to
    /// @c createDefaultConfigBackend() — the lib has no business
    /// knowing what that path resolves to.
    std::function<std::unique_ptr<PhosphorConfig::IBackend>()> legacyMigrationBackendFactory;
};

/**
 * @brief Manages all layouts and their assignments to screens/desktops
 *
 * Central component for zone layouts: loading/saving, screen/desktop/activity
 * assignment, keyboard shortcuts, and built-in templates.
 *
 * Inherits @c PhosphorZones::ILayoutManager, which brings in a single
 * QObject base via its @c IZoneLayoutRegistry sibling (which inherits
 * @c PhosphorLayout::ILayoutSourceRegistry → @c QObject). No direct
 * @c QObject base is needed — adding one would yield two QObject
 * subobjects and break @c moc. The concrete @c layoutsChanged /
 * @c activeLayoutChanged / etc. signals below are declared here; the
 * inherited @c contentsChanged signal (from @c ILayoutSourceRegistry)
 * is forwarded whenever @c layoutsChanged fires so @c ZonesLayoutSource
 * auto-refreshes.
 *
 * Construction takes a @ref LayoutManagerConfig (PZ-specific schema
 * names — kept out of the class so the class can move into the
 * @c phosphor-zones library) and an owned @c IBackend (the persistent
 * config store the manager reads/writes assignments from). PlasmaZones
 * composition roots and tests should prefer @ref makePzLayoutManager
 * instead of constructing directly.
 */
class PHOSPHORZONES_EXPORT LayoutManager : public ILayoutManager
{
    Q_OBJECT

    Q_PROPERTY(int layoutCount READ layoutCount NOTIFY layoutsChanged)
    Q_PROPERTY(PhosphorZones::Layout* activeLayout READ activeLayout NOTIFY activeLayoutChanged)
    Q_PROPERTY(QString layoutDirectory READ layoutDirectory WRITE setLayoutDirectory NOTIFY layoutDirectoryChanged)

public:
    /**
     * @param config Schema-key strings the manager reads/writes against.
     *               No defaults — the lib is project-agnostic.
     * @param backend Owned config-store backend. Must outlive every
     *                method call; ownership transfers to the manager.
     *                Pass @c nullptr only in narrow tests that don't
     *                exercise persistence (load/save methods will skip).
     * @param parent Qt parent.
     */
    LayoutManager(LayoutManagerConfig config, std::unique_ptr<PhosphorConfig::IBackend> backend,
                  QObject* parent = nullptr);
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
                                  const QString& activity = QString()) const override;

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

    /**
     * @brief Inject a callback that returns the user-configured default
     * layout id (or empty if unset / unavailable).
     *
     * The manager calls this every time @ref defaultLayout has to
     * resolve the active default — there's no caching, so the callback
     * is free to re-read settings on each call. Used by composition
     * roots that own a settings object the lib doesn't (and can't)
     * know about; PlasmaZones wires it to
     * @c [s]{return s->defaultLayoutId();}. Pass an empty function to
     * disable (the manager falls back to the first registered layout).
     */
    void setDefaultLayoutIdProvider(std::function<QString()> provider);

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
    std::function<QString()> m_defaultLayoutIdProvider; ///< Empty = no provider; falls back to first layout
    LayoutManagerConfig m_config;
    std::unique_ptr<PhosphorConfig::IBackend> m_ownedBackend;
    PhosphorConfig::IBackend* m_configBackend = nullptr; ///< Borrowed alias of @c m_ownedBackend.get()
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

} // namespace PhosphorZones
