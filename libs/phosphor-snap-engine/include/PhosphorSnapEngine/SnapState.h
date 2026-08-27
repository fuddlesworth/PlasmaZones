// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorsnapengine_export.h>
#include <PhosphorEngine/IPlacementState.h>
#include <PhosphorEngine/IWindowRegistry.h>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace PhosphorSnapEngine {

/// Per-screen snap placement state.
///
/// Owns the mutable state for manual zone-based snapping: which window
/// is assigned to which zone, floating state, pre-tile geometry for
/// restore, and pre-float zone memory for unfloat. Analogous to
/// PhosphorTiles::TilingState for automatic tiling.
///
/// Both SnapState and TilingState implement PhosphorEngine::IPlacementState
/// so the daemon's D-Bus adaptor can read state uniformly without branching on
/// mode. The state itself is within-session; what persists is per-window, in
/// the WindowPlacementStore.
class PHOSPHORSNAPENGINE_EXPORT SnapState : public QObject, public PhosphorEngine::IPlacementState
{
    Q_OBJECT

public:
    explicit SnapState(const QString& screenId, QObject* parent = nullptr);
    ~SnapState() override;

    SnapState(const SnapState&) = delete;
    SnapState& operator=(const SnapState&) = delete;

    /// Attach the daemon's shared window registry so every windowId-keyed
    /// accessor can canonicalize the incoming id to the stable first-seen
    /// composite (instanceId → first observed `appId|instanceId`). This makes
    /// the snap stores immune to the cross-process re-identification skew where
    /// the KWin effect restarts after a window's WM_CLASS mutated and re-derives
    /// a different composite for the same window (issue #628). Borrowed pointer,
    /// not owned (the daemon owns the registry); null in unit tests, in which
    /// case the accessors key on the raw id verbatim (today's behaviour).
    void setWindowRegistry(PhosphorEngine::IWindowRegistry* registry)
    {
        m_windowRegistry = registry;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // IPlacementState
    // ═══════════════════════════════════════════════════════════════════════════

    QString screenId() const override;
    int windowCount() const override;
    QStringList managedWindows() const override;
    bool containsWindow(const QString& windowId) const override;
    bool isFloating(const QString& windowId) const override;
    QStringList floatingWindows() const override;
    QString placementIdForWindow(const QString& windowId) const override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Assignment CRUD
    // ═══════════════════════════════════════════════════════════════════════════

    void assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                            int virtualDesktop);
    void assignWindowToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                             int virtualDesktop);
    struct UnassignResult
    {
        bool wasAssigned = false;
        bool lastUsedZoneCleared = false;
    };
    UnassignResult unassignWindow(const QString& windowId);
    /// Drop ONLY the screen/desktop residence entries, leaving the zone
    /// assignment, the floating bit and the pre-float capture untouched.
    ///
    /// For the cross-engine handoff of a window that was FLOATING but not
    /// snapped: unassignWindow never runs for it (no zone assignment to
    /// clear), so setFloatingOnScreen's residence writes survived in this
    /// store. They are invisible to reverse-map reads but NOT to raw scans
    /// like windowsOnScreenAndDesktop, which feeds cross-desktop focus — snap
    /// could then offer and activate a window another engine now owns.
    /// removeWindowData is the wrong primitive here: it also wipes the
    /// pre-float capture that handoffRelease deliberately preserves.
    /// @return true when an entry was removed.
    bool clearScreenAndDesktop(const QString& windowId);

    QString zoneForWindow(const QString& windowId) const;
    QStringList zonesForWindow(const QString& windowId) const;
    QStringList windowsInZone(const QString& zoneId) const;
    QStringList snappedWindows() const;
    bool isWindowSnapped(const QString& windowId) const;

    /// Layer-side focus memory for switch-focus-between-floating-and-tiling.
    ///
    /// SnapState holds no focus slot of its own (the engine reads the live
    /// focus from INavigationStateProvider on demand), so these remember
    /// only what noteFocused() classified from the engine's windowFocused
    /// reports: the last SNAPPED window focused and the last FLOATING
    /// window focused. Residence-only windows (screen recorded, no zone,
    /// no float bit) belong to neither layer and update neither memory.
    /// Cleared when the remembered window changes layer (setFloating and
    /// friends), leaves the store (removeWindowData, migrateWindowTo), and
    /// never serialized — focus history dies with the session. Consumers
    /// validate before use, so a stale value degrades to a fallback scan,
    /// never a wrong activation. Deliberate divergence from TilingState:
    /// the tiling twin RETAINS a non-focused window's old-side memory on a
    /// layer change (its transient floats round-trip), which needs a focus
    /// slot to condition on — snap holds none, so it clears unconditionally.
    void noteFocused(const QString& windowId);
    QString lastSnappedFocus() const
    {
        return m_lastSnappedFocus;
    }
    QString lastFloatingFocus() const
    {
        return m_lastFloatingFocus;
    }

    QString screenForWindow(const QString& windowId) const;

    /// Record screen + desktop residence WITHOUT a zone assignment and without
    /// touching the floating set.
    ///
    /// This is how a cross-engine handoff adopts a window as a plain FREE
    /// window (snapping's default for unmanaged windows): the arrival carries
    /// no resolvable zone and is not floating, but the adoption must still be
    /// VISIBLE — guardedHandoff verifies with isWindowTracked() after
    /// handoffReceive returns, and with no zone and no float only the
    /// screen-assignment arm can answer. Residence-only is an established
    /// state in this model (unsnapForFloat preserves it via
    /// clearZoneAssignment's preserve flag) and handoffRelease clears it
    /// symmetrically through clearScreenAndDesktop.
    void recordResidence(const QString& windowId, const QString& screenId, int virtualDesktop);
    int desktopForWindow(const QString& windowId) const;

    /// Re-stamp a snapped window's virtual-desktop membership to @p virtualDesktop,
    /// keeping its zone and screen. The one place a desktop is re-stamped on an
    /// EXISTING assignment without touching its zone or screen — used by
    /// cross-desktop directional move, where the window relocates to another
    /// desktop but keeps its snapped slot. (Full assignment writes can also carry
    /// a non-current desktop: RouteToDesktop pins and batch-resnap desktop
    /// preservation both route through assignWindowToZones.)
    /// No-op for a window that isn't currently assigned. Returns true on change.
    bool reassignDesktop(const QString& windowId, int virtualDesktop);

    /// Windows with a recorded desktop assignment (snapped, or floated-on-screen
    /// via setFloatingOnScreen / unsnapForFloat) on @p screenId whose desktop
    /// membership is @p virtualDesktop, sorted by id for deterministic
    /// entry-window choice. Iterates the desktop-assignment map; a window floated
    /// without a desktop slot is not in that map and is excluded. Used by
    /// cross-desktop directional focus to find a window to land on.
    QStringList windowsOnScreenAndDesktop(const QString& screenId, int virtualDesktop) const;

    const QHash<QString, QString>& screenAssignments() const
    {
        return m_windowScreenAssignments;
    }
    const QHash<QString, int>& desktopAssignments() const
    {
        return m_windowDesktopAssignments;
    }
    void setScreenAssignments(const QHash<QString, QString>& s)
    {
        if (m_windowScreenAssignments == s) {
            return;
        }
        m_windowScreenAssignments = s;
        Q_EMIT stateChanged();
    }

    /// Dynamic-workspaces arm: drop the per-window desktop tags naming a
    /// destroyed desktop and reset the last-used memo if it named it. The
    /// snap assignments are desktop-tagged per window inside EVERY state
    /// (including the global holder), so a state whose own key survives a
    /// desktop's death can still hold stale values — the key-level prune
    /// cannot reach these. 0 (sticky / on-all-desktops) is a sentinel and is
    /// never touched by either arm.
    void reapDesktopValues(int desktop)
    {
        bool changed = false;
        for (auto it = m_windowDesktopAssignments.begin(); it != m_windowDesktopAssignments.end();) {
            if (it.value() == desktop) {
                it = m_windowDesktopAssignments.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (m_lastUsedDesktop == desktop) {
            m_lastUsedDesktop = 0;
            changed = true;
        }
        if (changed) {
            Q_EMIT stateChanged();
        }
    }

    /// Dynamic-workspaces arm: rewrite the per-window desktop tags and the
    /// last-used memo per `oldToNew` (absent = unchanged; 0 sentinel kept).
    void renumberDesktopValues(const QHash<int, int>& oldToNew)
    {
        bool changed = false;
        for (auto it = m_windowDesktopAssignments.begin(); it != m_windowDesktopAssignments.end(); ++it) {
            if (it.value() != 0) {
                const int mapped = oldToNew.value(it.value(), it.value());
                if (mapped != it.value()) {
                    it.value() = mapped;
                    changed = true;
                }
            }
        }
        if (m_lastUsedDesktop != 0) {
            const int mapped = oldToNew.value(m_lastUsedDesktop, m_lastUsedDesktop);
            if (mapped != m_lastUsedDesktop) {
                m_lastUsedDesktop = mapped;
                changed = true;
            }
        }
        if (changed) {
            Q_EMIT stateChanged();
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Floating State
    // ═══════════════════════════════════════════════════════════════════════════

    void setFloating(const QString& windowId, bool floating);

    /// Mark a window floating AND record its screen+desktop without a zone.
    /// Used by cross-engine handoff when the snap engine adopts a floating
    /// window from another engine — the screen entry is what makes the snap
    /// engine answerable for "where does this window live" lookups
    /// (screenAssignments / screenForTrackedWindow) so future shortcut
    /// routing reaches this engine.
    void setFloatingOnScreen(const QString& windowId, const QString& screenId, int virtualDesktop);

    /// Save zone assignment before floating for later restore.
    UnassignResult unsnapForFloat(const QString& windowId);
    QString preFloatZone(const QString& windowId) const;
    QStringList preFloatZones(const QString& windowId) const;
    QString preFloatScreen(const QString& windowId) const;
    /// Returns true when an entry was actually removed, so callers can gate
    /// their dirty-marking on a real mutation.
    bool clearPreFloatZone(const QString& windowId);
    void addPreFloatZone(const QString& windowId, const QStringList& zoneIds);
    void addPreFloatScreen(const QString& windowId, const QString& screenId);

    const QHash<QString, QStringList>& preFloatZoneAssignments() const
    {
        return m_preFloatZoneAssignments;
    }
    const QHash<QString, QString>& preFloatScreenAssignments() const
    {
        return m_preFloatScreenAssignments;
    }
    void setPreFloatScreenAssignments(const QHash<QString, QString>& a)
    {
        if (m_preFloatScreenAssignments == a) {
            return;
        }
        m_preFloatScreenAssignments = a;
        Q_EMIT stateChanged();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Window Lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    void windowClosed(const QString& windowId);
    bool isEmpty() const;
    void clear();

    /// Move @p windowId's per-window placement entries (zone assignment, live
    /// screen, desktop, floating bit, pre-float zone/screen, auto-snap flag) OUT
    /// of this store and INTO @p target, rewriting the LIVE screen assignment to
    /// @p newScreenId so target->screenForWindow(windowId) reports the destination
    /// monitor (the #724 cross-monitor determinism requirement). The pre-float
    /// zone/screen ride along UNCHANGED: they name the SOURCE monitor's home zone,
    /// preserved so an unfloat on any monitor restores the home zone (cross-monitor
    /// restore is allowed; there is no refusal guard). The global-scalar fields
    /// (last-used-zone, user-snapped classes) are NOT moved — they stay global.
    /// No-op when @p target is null/this or the window has no entry in this store.
    void migrateWindowTo(SnapState* target, const QString& windowId, const QString& newScreenId);

    /// Remove ALL of @p windowId's per-window data from this store (zone/screen/
    /// desktop assignment, floating bit, pre-float zone/screen, auto-snap flag),
    /// returning true if anything was removed. Emits NO signal: this is the snap
    /// engine's single-owner enforcement primitive — it evicts a window from every
    /// store EXCEPT the one the reverse map owns it in, so a re-keyed window never
    /// leaves a phantom copy behind (see SnapEngine::stateForWindowOnScreen). An
    /// evicted phantom was never a legitimate resident here, so firing
    /// windowUnassigned/stateChanged for it would be wrong. windowClosed() is the
    /// signalling wrapper for the genuine "this window went away" case.
    bool removeWindowData(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Last-Used Zone Tracking
    //
    // Each per-(screen,desktop,activity) store tracks its OWN last-used zone, so a
    // window opening on monitor A only ever restores to a zone A was last snapped to
    // (never monitor B's). The on-disk `LastUsedZoneId` persists a single id; the
    // facade picks the representative store by lastUsedSeq() (most-recently updated).
    // ═══════════════════════════════════════════════════════════════════════════

    /// Update last-used zone and emit stateChanged.
    void updateLastUsedZone(const QString& zoneId, const QString& screenId, const QString& windowClass,
                            int virtualDesktop);

    /// Restore last-used zone fields from persistence without emitting stateChanged.
    void restoreLastUsedZone(const QString& zoneId, const QString& screenId, const QString& zoneClass, int desktop);

    QString lastUsedZoneId() const
    {
        return m_lastUsedZoneId;
    }
    QString lastUsedScreenId() const
    {
        return m_lastUsedScreenId;
    }
    QString lastUsedZoneClass() const
    {
        return m_lastUsedZoneClass;
    }
    int lastUsedDesktop() const
    {
        return m_lastUsedDesktop;
    }
    /// Monotonic stamp bumped every time this store's last-used zone is set to a
    /// non-empty value (via updateLastUsedZone / restoreLastUsedZone). 0 means
    /// "never set". The facade compares stamps across stores to pick the single
    /// representative last-used zone it persists to disk.
    quint64 lastUsedSeq() const
    {
        return m_lastUsedSeq;
    }
    void retagLastUsedZoneClass(const QString& newClass)
    {
        if (m_lastUsedZoneClass == newClass) {
            return;
        }
        m_lastUsedZoneClass = newClass;
        Q_EMIT stateChanged();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-Snap Bookkeeping
    // ═══════════════════════════════════════════════════════════════════════════

    void recordSnapIntent(const QString& windowClass, bool wasUserInitiated);
    const QSet<QString>& userSnappedClasses() const
    {
        return m_userSnappedClasses;
    }
    void setUserSnappedClasses(const QSet<QString>& classes)
    {
        if (m_userSnappedClasses == classes) {
            return;
        }
        m_userSnappedClasses = classes;
        Q_EMIT stateChanged();
    }

    void markAsAutoSnapped(const QString& windowId);
    bool isAutoSnapped(const QString& windowId) const;
    bool clearAutoSnapped(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Occupied Zone Queries
    // ═══════════════════════════════════════════════════════════════════════════

    /// Build the set of zone IDs currently occupied by snapped windows.
    /// Desktop 0 means "on all desktops" per KWin convention — windows with
    /// desktop 0 pass the filter and appear occupied on every desktop.
    QSet<QString> buildOccupiedZoneSet(const QString& screenFilter = {}, int desktopFilter = 0) const;

    /// Remove zone/screen/desktop assignments for windows not in the alive set.
    int pruneStaleAssignments(const QSet<QString>& aliveWindowIds);

    // ═══════════════════════════════════════════════════════════════════════════
    // State Access (for persistence layer)
    // ═══════════════════════════════════════════════════════════════════════════

    const QHash<QString, QStringList>& zoneAssignments() const
    {
        return m_windowZoneAssignments;
    }

Q_SIGNALS:
    void windowAssigned(const QString& windowId, const QString& zoneId);
    void windowUnassigned(const QString& windowId);
    void floatingChanged(const QString& windowId, bool floating);
    void stateChanged();

private:
    /// Resolve a raw windowId to its canonical (first-seen) composite for the
    /// stable instance id, WITHOUT seeding — the daemon seeds once per window in
    /// WindowTrackingAdaptor::setWindowMetadata, so every snap accessor here only
    /// looks up. Returns the input verbatim when the instance has no canonical
    /// entry (or no registry is attached, e.g. unit tests), which also makes the
    /// bare-appId alias writes (addPreFloat*/clearPreFloatZone) safe. See the
    /// .cpp header comment.
    QString canonicalizeForLookup(const QString& rawWindowId) const;

    /// Shared body of unassignWindow / unsnapForFloat. Removes the window's
    /// zone assignment and (optionally) its screen+desktop assignment, clears
    /// the last-used-zone tracker if it referenced one of the removed zones,
    /// and emits the windowUnassigned/stateChanged signals.
    /// `preserveScreenAndDesktop` keeps the screen + desktop assignment maps
    /// intact — used by unsnapForFloat so the snap engine still knows which
    /// screen the now-floating window lives on.
    UnassignResult clearZoneAssignment(const QString& windowId, bool preserveScreenAndDesktop);

    QSet<QString> allManagedWindowIds() const
    {
        QSet<QString> all;
        all.reserve(m_windowZoneAssignments.size() + m_floatingWindows.size() + m_autoSnappedWindows.size());
        for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
            all.insert(it.key());
        }
        all.unite(m_floatingWindows);
        all.unite(m_autoSnappedWindows);
        return all;
    }

    QString m_screenId;

    /// Borrowed; not owned. See setWindowRegistry. Null in unit tests.
    PhosphorEngine::IWindowRegistry* m_windowRegistry = nullptr;

    QHash<QString, QStringList> m_windowZoneAssignments;
    QHash<QString, QString> m_windowScreenAssignments;
    QHash<QString, int> m_windowDesktopAssignments;
    QSet<QString> m_floatingWindows;
    QHash<QString, QStringList> m_preFloatZoneAssignments;
    QHash<QString, QString> m_preFloatScreenAssignments;

    QString m_lastSnappedFocus;
    QString m_lastFloatingFocus;

    QString m_lastUsedZoneId;
    QString m_lastUsedScreenId;
    QString m_lastUsedZoneClass;
    int m_lastUsedDesktop = 0;
    /// Per-store recency stamp for the last-used zone (see lastUsedSeq()).
    quint64 m_lastUsedSeq = 0;

    QSet<QString> m_userSnappedClasses;
    QSet<QString> m_autoSnappedWindows;
};

} // namespace PhosphorSnapEngine
