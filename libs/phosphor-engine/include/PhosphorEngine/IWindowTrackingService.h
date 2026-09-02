// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <phosphorengine_export.h>

#include <QHash>
#include <QList>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

#include <optional>

class QObject;

namespace PhosphorZones {
class Layout;
}

namespace PhosphorScreens {
class ScreenManager;
}

namespace PhosphorEngine {

class PHOSPHORENGINE_EXPORT IWindowTrackingService
{
public:
    virtual ~IWindowTrackingService() = default;

    virtual QObject* asQObject() = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Screen manager access
    // Tech debt: returns concrete ScreenManager* — should be an IScreenManager
    // interface once one exists. Acceptable as a stepping stone; the
    // IScreenManager extraction is tracked separately.
    // ═══════════════════════════════════════════════════════════════════════════

    virtual PhosphorScreens::ScreenManager* screenManager() const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone assignment management
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                                    int virtualDesktop) = 0;
    virtual void assignWindowToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                     int virtualDesktop) = 0;
    virtual void unassignWindow(const QString& windowId) = 0;

    /// A window's recorded snap zone(s), preferring the LIVE assignment but falling
    /// back to the DURABLE placement-record snap slot when the live cache is cold.
    /// The live per-store zone maps are runtime-only — a daemon restart (and
    /// `handoffRelease` on autotile entry) clears them — so consumers that must
    /// survive a restart (the autotile→snap resnap) read this instead. Returns an
    /// empty list for a window that was never snapped in either source.
    virtual QStringList recordedSnapZones(const QString& windowId) const = 0;

    virtual QString zoneForWindow(const QString& windowId) const = 0;
    virtual QStringList zonesForWindow(const QString& windowId) const = 0;
    /// The screen a window is assigned to (empty when none), canonicalizing the
    /// id so a window resolves across the effect-restart re-identification skew
    /// (#628). There is deliberately no flat whole-map accessor on this interface:
    /// window placement is stored per (screen, desktop, activity) context, and a
    /// cross-store union erases the context that per-desktop consumers need.
    virtual QString screenForWindow(const QString& windowId) const = 0;
    /// Same, returning @p defaultScreen when the window has no assignment.
    virtual QString screenForWindow(const QString& windowId, const QString& defaultScreen) const = 0;
    virtual QStringList windowsInZone(const QString& zoneId) const = 0;
    virtual bool isWindowSnapped(const QString& windowId) const = 0;
    virtual QString findEmptyZone(const QString& screenId = QString()) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-snap
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void recordSnapIntent(const QString& windowId, bool wasUserInitiated) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Floating state
    // ═══════════════════════════════════════════════════════════════════════════

    virtual bool isWindowFloating(const QString& windowId) const = 0;
    /// Whether the window's float is a MINIMIZE SUSPENSION rather than
    /// placement intent. Outlives the live minimize bit through the
    /// unminimize animation grace so capture paths can keep preserving the
    /// pre-minimize record. Default false for implementations that do not
    /// track the classification.
    virtual bool isSuspensionFloat(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return false;
    }
    /// Drop the suspension classification. The adaptor owns it on the
    /// minimize/unminimize edges, but an engine-side USER unfloat (the float
    /// shortcut) also ends the suspension and never crosses that boundary —
    /// without this the bit strands and every later capture takes the
    /// minimize-preserve path. No-op for implementations that do not track
    /// the classification.
    virtual void clearSuspensionFloat(const QString& windowId)
    {
        Q_UNUSED(windowId)
    }
    virtual void setWindowFloating(const QString& windowId, bool floating) = 0;
    virtual void unsnapForFloat(const QString& windowId) = 0;
    virtual bool clearFloatingForSnap(const QString& windowId) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Sticky state
    // ═══════════════════════════════════════════════════════════════════════════

    virtual bool isWindowSticky(const QString& windowId) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Pre-float zone/screen restore
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QStringList preFloatZones(const QString& windowId) const = 0;
    virtual QString preFloatScreen(const QString& windowId) const = 0;
    /// Clear the saved pre-float zone/screen for @p windowId (both the windowId
    /// key and its appId alias). Used to drop stale pre-float state when a
    /// floating window crosses monitors, so a later unfloat does not restore it
    /// to a zone on the monitor it left.
    virtual void clearPreFloatZone(const QString& windowId) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-snap / pending restore
    // ═══════════════════════════════════════════════════════════════════════════

    virtual bool clearAutoSnapped(const QString& windowId) = 0;
    virtual bool consumePendingAssignment(const QString& windowId) = 0;
    virtual const QHash<QString, QList<PendingRestore>>& pendingRestoreQueues() const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Last-used zone tracking
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void updateLastUsedZone(const QString& zoneId, const QString& screenId, const QString& windowClass,
                                    int virtualDesktop) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // App identity
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QString currentAppIdFor(const QString& anyWindowId) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Geometry resolution
    // ═══════════════════════════════════════════════════════════════════════════

    /// The window's remembered UNMANAGED (free/float-back) geometry, resolved
    /// FOR @p screenId, or nullopt when no record can answer.
    ///
    /// "Validated" describes the POSITION, not the size: the answer is the spot
    /// remembered for @p screenId, sanity-checked against the live screen
    /// layout so a rect left off-canvas by a resolution change is pulled back.
    ///
    /// SCREEN-LOCAL. A spot remembered on another monitor is not a float-back
    /// for this one, so there is no cross-screen fallback — a non-null answer
    /// IS evidence the window was free on this screen. Callers that find
    /// nothing leave the window where it is.
    ///
    /// The lookup is restricted to the window's OWN record. It used to take an
    /// exactOnly flag defaulting to FALSE, which admitted a same-app SIBLING's
    /// free geometry as cross-instance float-back sharing. Every caller of this
    /// function asks a per-window question — "put THIS window back where IT
    /// was" — and for that the share is not a convenience but a teleport: an
    /// app's bucket fills with dead instances (MaxPerApp), a live window with no
    /// record of its own borrows a ghost's, and it lands wherever that ghost
    /// last was, on whatever monitor. Discussion #1028 is that bug. Two of the
    /// three engines had already opted out and written down why; the flag is
    /// gone so the remaining paths cannot drift back.
    ///
    /// The SIZE is whatever the compositor last reported and is never bounded
    /// here. A caller minting a SIZING INTENT out of it (a Fixed column width,
    /// a fixed tile height) owes it a bound of its own — the extents are
    /// untrusted input from a compositor and, through the injected service,
    /// from an embedder. Callers that only re-place a window at a remembered
    /// spot need no such bound.
    virtual std::optional<QRect> validatedUnmanagedGeometry(const QString& windowId, const QString& screenId) const = 0;

    /// Does @p geometry plausibly belong to the coordinate space @p screenId
    /// names?
    ///
    /// The same predicate validatedUnmanagedGeometry applies to its own answer,
    /// exposed because it is NOT the only reader of the shared free-geometry
    /// map. A caller that reaches a rect through the placement store directly
    /// — the engines' reopen and release paths do, via takeForReopen — gets no
    /// validation from that route and hands the rect straight to a geometry
    /// apply. A mis-keyed record then teleports the window to whatever monitor
    /// the coordinates describe, which is the failure the read guard exists to
    /// stop; those callers need the same check rather than their own copy of
    /// it.
    ///
    /// Not a substitute for an is-this-on-any-screen rescue test: this asks
    /// whether the rect belongs to THIS key, so a rect sitting squarely on a
    /// different monitor answers false. Fails OPEN when the screen cannot be
    /// resolved, so an embedder with no screen manager keeps its behaviour.
    virtual bool geometryBelongsToScreen(const QRect& geometry, const QString& screenId) const = 0;

    /// Record a window's SHARED free/float geometry (the single float-back store —
    /// the placement record's freeGeometryByScreen). This is the ONE writer all
    /// float-back captures route through (effect pre-tile/pre-snap capture, drag
    /// store, float-toggle capture), so snap and autotile read the same value and
    /// never drift. @p overwrite=false leaves an existing entry for @p screenId
    /// untouched (first-capture-wins). No-op on an invalid geometry.
    virtual void recordFreeGeometry(const QString& windowId, const QString& screenId, const QRect& geometry,
                                    bool overwrite) = 0;

    /// Clear a window's shared free/float geometry (all screens) from the record,
    /// leaving its engine slots intact. For the drag-out / layout-change consume
    /// paths that restore the float-back once and must not re-apply it.
    virtual void clearFreeGeometry(const QString& windowId) = 0;

    /// Downgrade @p engineId's slot on @p windowId's record to
    /// WindowPlacement::stateReleased() and mark the placements dirty. Called
    /// by an engine that KNOWINGLY gives a window up (cross-mode handoff), so
    /// its slot stops advertising a home the cross-screen reclaim would pull
    /// the window back to. Default no-op: an embedder without persistence has
    /// nothing to downgrade. NOT for ordinary close — a window that closed
    /// tiled keeps its slot, which is what login restore reads.
    virtual void releaseEngineSlot(const QString& windowId, const QString& engineId)
    {
        Q_UNUSED(windowId)
        Q_UNUSED(engineId)
    }
    /// Screen-scoped consume-once variant: clears only @p screenId's
    /// remembered float-back, preserving other monitors' entries. Default
    /// falls back to the all-screens form for implementations without
    /// per-screen granularity.
    virtual void clearFreeGeometry(const QString& windowId, const QString& screenId)
    {
        Q_UNUSED(screenId)
        clearFreeGeometry(windowId);
    }
    /// Frame for @p zoneId measured against @p screenId. The id is REQUIRED:
    /// an implementation backed by a screen manager answers an invalid rect for
    /// an empty or unresolvable id rather than standing the primary output in,
    /// because callers only check isValid() and a wrong monitor moves the window.
    virtual QRect zoneGeometry(const QString& zoneId, const QString& screenId) const = 0;
    virtual QRect resolveZoneGeometry(const QStringList& zoneIds, const QString& screenId) const = 0;
    virtual QString resolveEffectiveScreenId(const QString& screenId) const = 0;

    /// The unified, engine-agnostic placement store. Engines reach it through the
    /// tracking service to capture/restore the one WindowPlacement per window.
    virtual WindowPlacementStore& placementStore() = 0;
    // Tech debt: takes concrete Layout* — should take an opaque layout identifier
    // once the layout interface is extracted. Acceptable for this extraction phase.
    virtual QString findEmptyZoneInLayout(PhosphorZones::Layout* layout, const QString& screenId,
                                          int desktopFilter = 0) const = 0;
    virtual QSet<QUuid> buildOccupiedZoneSet(const QString& screenFilter = QString(), int desktopFilter = 0) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Resnap buffer
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QVector<ResnapEntry> takeResnapBuffer() = 0;
};

} // namespace PhosphorEngine
