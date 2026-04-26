// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>

#include <PhosphorConfig/IBackend.h>

#include <QHash>
#include <QSet>
#include <QString>
#include <QUuid>
#include <functional>
#include <memory>

namespace PhosphorZones {

/**
 * @brief Manual zone-layout registry + per-context assignment store.
 *
 * Concrete counterpart to @ref IZoneLayoutRegistry — mirrors the
 * PhosphorTiles @c AlgorithmRegistry shape (interface for the provider
 * contract, one concrete class for everything else). Composition roots
 * construct one instance per process and inject it into every consumer;
 * there is no process-global singleton.
 *
 * Responsibilities:
 *   - In-memory catalog of @ref Layout instances (enumeration, add,
 *     remove, duplicate, active-layout selection — inherited from
 *     @ref IZoneLayoutRegistry).
 *   - Per-context @ref AssignmentEntry table keyed by
 *     (screenId, virtualDesktop, activity), with cascading fallback
 *     (narrower keys beat wider keys; base screen is the widest).
 *   - Numbered quick-layout shortcut slots (1..9).
 *   - Built-in layout templates (columns, rows, grid, priority, focus).
 *   - JSON-file persistence for layouts; @ref PhosphorConfig::IBackend
 *     persistence for assignments + quick-slots.
 *
 * Schema strings (@c "Assignment:", @c "QuickLayouts", @c "ModeTracking")
 * are hardcoded lib-level constants inside @c layoutregistry_persistence.cpp.
 * They ARE the wire format — third-party compositors that reuse this
 * lib share them for free.
 */
class PHOSPHORZONES_EXPORT LayoutRegistry : public IZoneLayoutRegistry
{
    Q_OBJECT

    Q_PROPERTY(int layoutCount READ layoutCount NOTIFY layoutsChanged)
    Q_PROPERTY(Layout* activeLayout READ activeLayout NOTIFY activeLayoutChanged)
    Q_PROPERTY(QString layoutDirectory READ layoutDirectory WRITE setLayoutDirectory NOTIFY layoutDirectoryChanged)

public:
    /**
     * @param backend Owned config-store backend (typically
     *                @c createAssignmentsBackend()). Required — asserted
     *                non-null; every persistence method dereferences it.
     * @param layoutSubdirectory XDG-relative path used for layout JSON
     *                discovery (e.g. @c "plasmazones/layouts"). The registry
     *                writes to
     *                @c QStandardPaths::writableLocation(GenericDataLocation)/\<subdir\>
     *                and reads the union of every @c GenericDataLocation
     *                entry containing that subdirectory, so system copies
     *                (in @c /usr/share/<subdir>) provide built-ins while
     *                the user-writable copy overrides them. Required —
     *                asserted non-empty.
     * @param parent Qt parent.
     */
    LayoutRegistry(std::unique_ptr<PhosphorConfig::IBackend> backend, QString layoutSubdirectory,
                   QObject* parent = nullptr);
    ~LayoutRegistry() override;

    // ─── IZoneLayoutRegistry ──────────────────────────────────────────────

    int layoutCount() const override
    {
        return m_layouts.size();
    }
    QVector<Layout*> layouts() const override
    {
        return m_layouts;
    }
    Layout* layout(int index) const override;
    Layout* layoutById(const QUuid& id) const override;
    Layout* layoutByName(const QString& name) const override;

    Q_INVOKABLE void addLayout(Layout* layout) override;
    Q_INVOKABLE void removeLayout(Layout* layout) override;
    Q_INVOKABLE void removeLayoutById(const QUuid& id) override;
    Q_INVOKABLE Layout* duplicateLayout(Layout* source) override;

    Layout* activeLayout() const override
    {
        return m_activeLayout;
    }
    Q_INVOKABLE void setActiveLayout(Layout* layout) override;
    Q_INVOKABLE void setActiveLayoutById(const QUuid& id) override;

    // ─── Layout persistence (disk JSON) ───────────────────────────────────

    QString layoutDirectory() const
    {
        return m_layoutDirectory;
    }
    void setLayoutDirectory(const QString& directory);

    Q_INVOKABLE void loadLayouts();
    Q_INVOKABLE void saveLayouts();
    void saveLayout(Layout* layout);

    Q_INVOKABLE void importLayout(const QString& filePath);
    Q_INVOKABLE void exportLayout(Layout* layout, const QString& filePath);

    // ─── Assignment persistence (via config backend) ──────────────────────

    Q_INVOKABLE void loadAssignments();
    Q_INVOKABLE void saveAssignments();

    // ─── Default-layout resolution ────────────────────────────────────────

    /// Resolve the effective default layout. Returns, in order:
    ///   1. The layout whose id matches the default-id provider (if set
    ///      and non-empty).
    ///   2. The first registered layout (by @c defaultOrder).
    ///   3. nullptr if no layouts are registered.
    Layout* defaultLayout() const;

    /**
     * @brief Inject a callback that returns the user-configured default
     * layout id (or empty if unset).
     *
     * Used by composition roots that own a settings object the lib
     * doesn't know about. The registry invokes the callback on every
     * @ref defaultLayout call (no caching) so the callback can re-read
     * settings on each call. Pass an empty function to disable.
     */
    void setDefaultLayoutIdProvider(std::function<QString()> provider);

    /**
     * @brief Inject a callback that returns the user-configured default
     * assignment entry for screens with no explicit cascade hit.
     *
     * The library's cascade resolves (screen, desktop, activity) via
     * stored assignments. When *every* fallback misses (for example, a
     * brand-new virtual desktop the user never explicitly configured),
     * @ref assignmentEntryForScreen / @ref assignmentIdForScreen /
     * @ref layoutForScreen consult this provider as the final step.
     *
     * Composition roots are expected to synthesize the entry from the
     * settings layer — typically @c snappingEnabled / @c autotileEnabled
     * plus the configured default snap layout id and default autotile
     * algorithm. Callbacks should return a default-constructed entry
     * when the user has not configured a runtime mode; the registry
     * additionally guards against partial entries by gating the synth
     * on the same acceptance rule the cascade visitors use
     * (@c activeLayoutId() non-empty for entry/id queries; @c Snapping
     * mode with a resolvable layout for @ref layoutForScreen).
     *
     * Invoked on every cascade-miss with no caching, so providers can
     * re-read settings cheaply per call. Pass an empty function to
     * disable, restoring pre-368 cascade behaviour (no synthesized
     * fallback).
     *
     * Callers that need to inspect the user's *stored* state without
     * the synth fallback (e.g. KCM readbacks that round-trip through
     * persistence) should gate their query with @ref hasExplicitAssignment.
     *
     * Thread-safety: the provider is read on every cascade query and
     * swapped via this setter without synchronization; both must run
     * on the same thread (the LayoutRegistry's owner thread, typically
     * the main Qt thread).
     */
    void setDefaultAssignmentEntryProvider(std::function<AssignmentEntry()> provider);

    // ─── Assignments (per-context routing) ────────────────────────────────

    /// Get the previous active layout (before the most recent
    /// @ref setActiveLayout). On first call, equals @ref activeLayout.
    /// Used by resnap-to-new-layout.
    Layout* previousLayout() const
    {
        return m_previousLayout;
    }

    Q_INVOKABLE void assignLayout(const QString& screenId, int virtualDesktop, const QString& activity, Layout* layout);
    Q_INVOKABLE void assignLayoutById(const QString& screenId, int virtualDesktop, const QString& activity,
                                      const QString& layoutId);

    /// Store a full entry directly (from KCM via D-Bus). Stores
    /// regardless of @c isValid() — mode-only entries are valid when
    /// explicitly set.
    void setAssignmentEntryDirect(const QString& screenId, int virtualDesktop, const QString& activity,
                                  const AssignmentEntry& entry);

    Q_INVOKABLE Layout* layoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                        const QString& activity = QString()) const;

    /// Resolve layout for @p screenId using the current desktop/activity
    /// context. @ref layoutForScreen already falls back to
    /// @ref defaultLayout internally when no explicit assignment matches,
    /// so this helper is a thin context-filling forwarder.
    Layout* resolveLayoutForScreen(const QString& screenId) const
    {
        return layoutForScreen(screenId, m_currentVirtualDesktop, m_currentActivity);
    }

    Q_INVOKABLE void clearAssignment(const QString& screenId, int virtualDesktop = 0,
                                     const QString& activity = QString());
    Q_INVOKABLE bool hasExplicitAssignment(const QString& screenId, int virtualDesktop = 0,
                                           const QString& activity = QString()) const;

    /// Raw assignment id for a (screen, desktop, activity) context.
    /// Returns the stored string (manual-layout UUID or
    /// @c "autotile:<algorithmId>") without resolving to a @ref Layout*.
    /// Empty if no explicit assignment AND no synthesized default from
    /// @ref setDefaultAssignmentEntryProvider applies. Callers that
    /// need to distinguish "stored" from "synthesized fallback" must
    /// pair this with @ref hasExplicitAssignment.
    QString assignmentIdForScreen(const QString& screenId, int virtualDesktop = 0,
                                  const QString& activity = QString()) const;

    /// Full entry for a (screen, desktop, activity) context (same
    /// cascade as @ref layoutForScreen). When every cascade level
    /// misses, the entry returned by @ref setDefaultAssignmentEntryProvider
    /// is surfaced (gated on the same acceptance rule the cascade
    /// visitors use); a default-constructed entry otherwise. Callers
    /// that need raw stored state without the synth fallback must gate
    /// with @ref hasExplicitAssignment.
    AssignmentEntry assignmentEntryForScreen(const QString& screenId, int virtualDesktop = 0,
                                             const QString& activity = QString()) const;

    AssignmentEntry::Mode modeForScreen(const QString& screenId, int virtualDesktop = 0,
                                        const QString& activity = QString()) const;
    QString snappingLayoutForScreen(const QString& screenId, int virtualDesktop = 0,
                                    const QString& activity = QString()) const;
    QString tilingAlgorithmForScreen(const QString& screenId, int virtualDesktop = 0,
                                     const QString& activity = QString()) const;

    /// Flip mode to @c Snapping for every entry currently in @c Autotile
    /// (preserves @c snappingLayout + @c tilingAlgorithm). Emits
    /// @c layoutAssigned per affected screen; one save at end.
    void clearAutotileAssignments();

    /// Batch setters — clear existing, set new, save once at end.
    void setAllScreenAssignments(const QHash<QString, QString>& assignments);
    void setAllDesktopAssignments(const QHash<QPair<QString, int>, QString>& assignments);
    void setAllActivityAssignments(const QHash<QPair<QString, QString>, QString>& assignments);

    QHash<QPair<QString, int>, QString> desktopAssignments() const;
    QHash<QPair<QString, QString>, QString> activityAssignments() const;

    // ─── Quick-layout slots (1..9) ────────────────────────────────────────

    Q_INVOKABLE Layout* layoutForShortcut(int number) const;
    Q_INVOKABLE void applyQuickLayout(int number, const QString& screenId);
    void setQuickLayoutSlot(int number, const QString& layoutId);
    void setAllQuickLayoutSlots(const QHash<int, QString>& slots);
    QHash<int, QString> quickLayoutSlots() const
    {
        return m_quickLayoutShortcuts;
    }

    // ─── Layout cycling ───────────────────────────────────────────────────

    Q_INVOKABLE void cycleToPreviousLayout(const QString& screenId);
    Q_INVOKABLE void cycleToNextLayout(const QString& screenId);

    // ─── Built-in layouts ─────────────────────────────────────────────────

    Q_INVOKABLE void createBuiltInLayouts();
    QVector<Layout*> builtInLayouts() const;

    // ─── Context (desktop / activity) ─────────────────────────────────────

    int currentVirtualDesktop() const
    {
        return m_currentVirtualDesktop;
    }
    QString currentActivity() const
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

    // ─── Autotile layout overrides (per-algorithm user overrides) ─────────

    void saveAutotileOverrides(const QString& algorithmId, const QJsonObject& overrides);
    QJsonObject loadAutotileOverrides(const QString& algorithmId) const;

Q_SIGNALS:
    void layoutsChanged();
    void layoutAdded(Layout* layout);
    void layoutRemoved(Layout* layout);
    void activeLayoutChanged(Layout* layout);
    void layoutAssigned(const QString& screenId, int virtualDesktop, Layout* layout);
    void layoutDirectoryChanged();
    void layoutsLoaded();
    void layoutsSaved();

private:
    void ensureLayoutDirectory();
    void loadLayoutsFromDirectory(const QString& directory);
    Layout* restoreSystemLayout(const QUuid& id, const QString& systemPath);
    QString layoutFilePath(const QUuid& id) const;
    void readAssignmentGroups(PhosphorConfig::IBackend* backend);
    void readQuickLayouts(PhosphorConfig::IBackend* backend);
    Layout* cycleLayoutImpl(const QString& screenId, int direction);
    bool shouldSkipLayoutAssignment(const QString& layoutId, const QString& context) const;
    void emitLayoutAssigned(const QString& screenId, int virtualDesktop, const QString& layoutId);
    /**
     * @brief Commit a targeted per-screen layout switch (from a quick-slot
     * shortcut, layout cycle, or any caller that wants to change ONLY the
     * named screen — not fan activeLayoutChanged across every screen).
     *
     * Writes the per-desktop assignment for @p screenId with empty activity
     * (so D-Bus/KCM lookups using empty activity see the entry), clears any
     * stale activity-keyed assignment that would shadow it in the cascade,
     * then updates the global active-layout pointer under a QSignalBlocker
     * so @ref activeLayoutChanged does NOT fire — preventing resnap on the
     * other screens. Equivalent to the write+update pattern both
     * applyQuickLayout and cycleLayoutImpl were open-coding before the
     * extraction.
     */
    void applyLayoutToScreen(const QString& screenId, Layout* layout);
    QJsonObject loadAllAutotileOverrides() const;
    void saveAllAutotileOverrides(const QJsonObject& all);
    Layout* resolveConfiguredDefault() const;

    std::function<QString()> m_defaultLayoutIdProvider; ///< Empty = provider disabled; falls back to first layout
    /// Empty = provider disabled; cascade returns no entry when every fallback misses.
    /// When set, the provider's return value is the final cascade step for
    /// @ref assignmentEntryForScreen / @ref assignmentIdForScreen /
    /// @ref layoutForScreen — synthesizing a settings-derived default for
    /// contexts the user never explicitly configured (e.g. brand-new
    /// virtual desktops).
    std::function<AssignmentEntry()> m_defaultAssignmentEntryProvider;
    std::unique_ptr<PhosphorConfig::IBackend> m_ownedBackend;
    PhosphorConfig::IBackend* m_configBackend = nullptr; ///< Borrowed alias of m_ownedBackend.get(); always non-null
    QString m_layoutSubdirectory; ///< XDG-relative (e.g. "plasmazones/layouts") — drives locateAll discovery
    QString m_layoutDirectory; ///< Absolute user-writable path derived from m_layoutSubdirectory
    QVector<Layout*> m_layouts;
    Layout* m_activeLayout = nullptr;
    Layout* m_previousLayout = nullptr; ///< Active layout before last setActiveLayout (for resnap)
    QHash<LayoutAssignmentKey, AssignmentEntry> m_assignments;
    QHash<int, QString> m_quickLayoutShortcuts; ///< slot number (1..9) → layout ID
    int m_currentVirtualDesktop = 1;
    QString m_currentActivity;
};

} // namespace PhosphorZones
