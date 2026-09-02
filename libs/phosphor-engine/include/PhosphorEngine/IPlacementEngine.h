// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// FILE-SIZE EXCEPTION (sanctioned): one interface, one header. The whole
// file is a single abstract class — the placement-engine contract every
// engine implements and the daemon dispatches against — and C++ cannot
// split one class's members across headers. Extracting groups of virtuals
// into secondary bases would change the contract's shape (and every
// implementer and mock) purely to satisfy a line count.

#pragma once

#include <phosphorengine_export.h>

#include <PhosphorEngine/IPlacementState.h>
#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorEngine/WindowPlacement.h>

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <optional>

class QObject;

namespace PhosphorEngine {

class ICrossSurfaceResolver;

/// Unified placement engine interface.
///
/// ## Required vs Optional Methods
///
/// Methods are divided into two categories:
///
/// **REQUIRED (pure virtual, = 0):** Every engine MUST implement these.
/// They represent the core contract: screen ownership, window lifecycle,
/// float management, and navigation intents.
///
/// **OPTIONAL (have default no-op implementations):** Engines override
/// only the capabilities they support. A snap engine ignores master
/// operations; an autotile engine ignores per-screen config. The defaults
/// are safe no-ops so the daemon can call any method without branching
/// on engine type.
///
/// ## Design Rationale
///
/// All three engines — snap (manual zone layouts), autotile (automatic
/// tiling algorithms), and scrolling (niri-style column strip) — implement
/// this so the daemon can dispatch all window lifecycle events and user
/// navigation intents through a single polymorphic call — zero mode
/// branches.
///
/// Each method represents a USER INTENT, not a mode-specific
/// implementation step. "Move focused window left" has different internal
/// meaning in tile-swap mode vs. zone-snap mode, but the user's request
/// is the same — the interface names the request and each engine fulfills
/// it in its own terms.
///
/// The REQUIRED navigation intents are idempotent with respect to "no
/// focused window" — each engine's implementation emits navigation
/// feedback with a sensible reason code when there's nothing to act on,
/// rather than erroring out. The OPTIONAL surface below does not share
/// that promise: its defaults are deliberately silent no-ops.
class PHOSPHORENGINE_EXPORT IPlacementEngine
{
protected:
    IPlacementEngine() = default;

public:
    virtual ~IPlacementEngine() = default;
    // Polymorphic base: never copied or moved (every concrete engine is a
    // QObject anyway; this makes slicing a compile error at the interface).
    IPlacementEngine(const IPlacementEngine&) = delete;
    IPlacementEngine& operator=(const IPlacementEngine&) = delete;

    // ═══════════════════════════════════════════════════════════════════════════
    // Screen ownership
    // ═══════════════════════════════════════════════════════════════════════════

    /// Whether this engine is active on the given screen.
    virtual bool isActiveOnScreen(const QString& screenId) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    /// A new window appeared on this engine's screen.
    virtual void windowOpened(const QString& windowId, const QString& screenId, int minWidth = 0,
                              int minHeight = 0) = 0;

    /// Convenience overload — equivalent to windowOpened(id, screen, 0, 0).
    ///
    /// UNCALLABLE through this interface as written: every implementation
    /// overrides the four-argument virtual, and an override HIDES every
    /// same-named base overload, so `engine->windowOpened(id, screen)` fails
    /// to compile against a concrete engine. Each engine restores it with a
    /// `using IPlacementEngine::windowOpened;`, which is what actually makes
    /// the two-argument form work — so this overload is real but only for
    /// implementations that opt back into it, not for interface-typed callers
    /// that never see a concrete type.
    void windowOpened(const QString& windowId, const QString& screenId)
    {
        windowOpened(windowId, screenId, 0, 0);
    }

    /// OPTIONAL: cross-screen session reclaim, the tiling-engine counterpart
    /// of the snap engine's recorded-screen restore. Offered a window that
    /// opened on @p openingScreenId — a screen this engine may not own — the
    /// engine checks the unified placement store for ITS OWN managed slot
    /// recorded on a DIFFERENT screen that is still in this engine's mode
    /// (PhosphorEngine::pendingCrossScreenManagedRestore), and on a match
    /// adopts the window into that recorded home screen (its retile then
    /// physically moves the window there). KWin's session restore opens
    /// windows on a nondeterministic output, so without this a whole strip's
    /// windows strand floated on whatever monitor KWin picked at login.
    /// Default false: an engine without a cross-screen restore story (snap
    /// claims through resolveWindowRestore instead) never claims here.
    ///
    /// Contract for implementations:
    ///  - Self-gate on first observation by MEMBERSHIP (a window the engine
    ///    already holds in a state is an in-session move, never a session
    ///    restore — and the raw reverse-map key is not membership).
    ///  - Decide via WindowPlacementStore::peekForReclaim, never plain
    ///    peek(): the live-instance exclusion is what stops a fresh second
    ///    instance being yanked onto its open sibling's monitor.
    ///  - Return the REAL adoption outcome, verified by membership after the
    ///    open-path re-entry. Answering true optimistically converts every
    ///    downstream refusal into a window no engine manages: the caller
    ///    hands a claimed window to no other engine.
    virtual bool claimCrossScreenReopen(const QString& windowId, const QString& openingScreenId, int minWidth = 0,
                                        int minHeight = 0)
    {
        Q_UNUSED(windowId)
        Q_UNUSED(openingScreenId)
        Q_UNUSED(minWidth)
        Q_UNUSED(minHeight)
        return false;
    }

    /// OPTIONAL: the screen this engine genuinely HOLDS the window on IN THE
    /// SCREEN'S CURRENT CONTEXT — a MEMBERSHIP answer (tiled or
    /// engine-floating both count; a phantom reverse-map key does not),
    /// empty when the engine does not hold it or holds it only in a
    /// background context.
    ///
    /// This exists for the adaptor's post-reclaim ownership check: after a
    /// cross-screen reclaim, the effect's already-queued arrival announce
    /// still carries the ARRIVAL screen, and dispatching it would migrate
    /// the window straight back. isWindowTracked cannot serve — its contract
    /// is PER-ENGINE: SnapEngine and ScrollEngine answer from the raw
    /// reverse-map key, which a refused adoption can leave dangling, while
    /// AutotileEngine verifies membership as well (a phantom key answers
    /// false there — see its override doc for why that engine needed the
    /// stricter form). Callers wanting one uniform answer across engines
    /// cannot get it from that predicate. isWindowManaged/isWindowTiled
    /// cannot serve either — both exclude engine-floating windows, which a
    /// reclaim can legitimately produce.
    ///
    /// CURRENT-context only, and that restriction is what keeps the check
    /// from suppressing repair. A reclaim's adoption always keys by the home
    /// screen's current context, so a fresh reclaim is always visible here;
    /// a hold in a BACKGROUND context (a mode flip preserves other-desktop
    /// states and their keys) is never a fresh reclaim, and the announce
    /// that would heal such a stale hold — the engines' own cross-screen
    /// migration in windowOpened — must not be refused. A stale hold in the
    /// CURRENT context is still indistinguishable from a fresh one here and
    /// remains healed by windowFocused instead.
    virtual QString heldScreenForWindow(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return {};
    }

    /// Bracket a BURST of windowOpened calls delivered together (the
    /// adaptor's three dispatch loops: windowsOpenedBatch, the deferred-open
    /// flush, and the parked-open replay — daemon bring-up re-announce and
    /// mode flips). The cross-screen reclaim's windowOpened re-entry is a
    /// fourth caller: inside the tiling dispatch it inherits that loop's
    /// bracket; off the snap facade it is deliberately UNBRACKETED — each
    /// resolveWindowRestore is its own D-Bus message, so there is no batch
    /// to bracket, matching the per-window cadence snap restores have always
    /// had on that channel. An engine that applies geometry
    /// per arrival may defer those applies until endArrivalBurst so a
    /// restore of an unchanged session resolves one final layout instead of
    /// N visible intermediates marching across the screen. Defaults are
    /// no-ops: an engine whose arrivals already coalesce (autotile's queued
    /// retile) needs nothing. Brackets may nest; only the outermost end
    /// flushes. Model state is fully updated during the burst either way —
    /// only the compositor-facing geometry apply is deferred.
    virtual void beginArrivalBurst()
    {
    }
    virtual void endArrivalBurst()
    {
    }

    /// A window was closed.
    virtual void windowClosed(const QString& windowId) = 0;

    /// A window gained focus (called when the compositor reports activation).
    /// Named "focused" here because it's the engine's perspective; the D-Bus
    /// protocol and DaemonClient use "windowActivated" — same event.
    virtual void windowFocused(const QString& windowId, const QString& screenId) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Float management (explicit window ID)
    //
    // These take a concrete window ID — used by the D-Bus adaptor and
    // engine-internal paths that already know which window to act on.
    // toggleFocusedFloat() in the Navigation section is the user-intent
    // entry point that resolves the focused window from NavigationContext.
    // ═══════════════════════════════════════════════════════════════════════════

    /// Toggle between managed and floating.
    virtual void toggleWindowFloat(const QString& windowId, const QString& screenId) = 0;

    /// Set floating state explicitly (directional, not toggle).
    ///
    /// @param screenId The window's authoritative current screen, when the
    /// caller knows it (the D-Bus setWindowFloatingForScreen threads the
    /// effect's live output here). An engine WITHOUT live per-window screen
    /// tracking MUST prefer this over its own tracked association, which
    /// can be stale after a floating window drifts across monitors — using
    /// the stale screen makes the unfloat's cross-monitor guard
    /// non-deterministic (snap and scroll honour it for exactly that
    /// reason). AutotileEngine deliberately resolves from its own tracking
    /// instead: its focus-driven migration keeps the association current,
    /// and the parameter can lag it mid-handoff. Empty (the default) means
    /// "resolve it yourself" for internal callers.
    virtual void setWindowFloat(const QString& windowId, bool shouldFloat, const QString& screenId = QString()) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation (user intents)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Move keyboard focus to the adjacent window.
    virtual void focusInDirection(const QString& direction, const NavigationContext& ctx) = 0;

    /// Move the focused window to the adjacent slot.
    virtual void moveFocusedInDirection(const QString& direction, const NavigationContext& ctx) = 0;

    /// Grow or shrink the focused window's zone span toward the direction:
    /// extend into the adjacent zone(s) when some exist beyond that edge,
    /// otherwise retract the opposite edge. Zone spanning is a snap-mode
    /// concept, so unlike the required intents above this has a default
    /// no-op, keeping the daemon free of engine-type branching. Engines
    /// that want the shortcut to give feedback instead of silence override
    /// it — AutotileEngine reports a "not_supported" failure OSD.
    virtual void spanFocusedInDirection(const QString& direction, const NavigationContext& ctx)
    {
        Q_UNUSED(direction)
        Q_UNUSED(ctx)
    }

    /// Jump focus between the float layer and the engine's placement layer
    /// (niri's switch-focus-between-floating-and-tiling): activate the last
    /// focused window on the OTHER layer, falling back to a scan when that
    /// memory is stale. Minimized-window filtering is each engine's own
    /// LayerSwitchSide::isEligible — the resolver applies no policy of its
    /// own, so a new engine must install the filter itself (a compositor
    /// state the registry has not reported is treated as visible: a focus
    /// verb must not refuse a window merely because its state is unknown).
    /// All three engines implement it on the shared resolver
    /// (resolveLayerFocusSwitch), so the default is a no-op only for
    /// hypothetical future engines — defaulted rather than pure so the
    /// daemon routes it mode-agnostically like spanFocusedInDirection.
    virtual void switchFocusBetweenFloatingAndTiling(const QString& screenId)
    {
        Q_UNUSED(screenId)
    }

    /// Swap the focused window with the adjacent window.
    virtual void swapFocusedInDirection(const QString& direction, const NavigationContext& ctx) = 0;

    /// Move the focused window to the Nth position.
    virtual void moveFocusedToPosition(int position, const NavigationContext& ctx) = 0;

    /// Rotate all managed windows on the screen.
    virtual void rotateWindows(bool clockwise, const NavigationContext& ctx) = 0;

    /// Re-apply the current layout to all managed windows.
    virtual void reapplyLayout(const NavigationContext& ctx) = 0;

    /// Bring every unmanaged window on the screen back under this engine's
    /// placement (zones for snap, the strip for scrolling). Stated
    /// layout-neutrally on purpose: an engine without a layout concept
    /// (layoutSupport() == LayoutSupport::None) still implements this intent.
    virtual void snapAllWindows(const NavigationContext& ctx) = 0;

    /// Cycle keyboard focus through managed windows.
    virtual void cycleFocus(bool forward, const NavigationContext& ctx) = 0;

    /// Move the focused window to the first empty slot. Engines whose
    /// placement has no empty-slot concept answer with a
    /// "push"/"not_supported" feedback emit rather than silence — the
    /// shortcut must not read as broken (same policy the span default
    /// documents above).
    virtual void pushToEmptyZone(const NavigationContext& ctx) = 0;

    /// Restore the focused window out of its managed state.
    virtual void restoreFocusedWindow(const NavigationContext& ctx) = 0;

    /// Toggle the focused window between managed and floating.
    virtual void toggleFocusedFloat(const NavigationContext& ctx) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Screen management (override if engine tracks multi-screen state)
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QSet<QString> activeScreens() const
    {
        return {};
    }
    virtual void setActiveScreens(const QSet<QString>& screens)
    {
        Q_UNUSED(screens)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Window-appearance re-application (compositor reconnect)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Re-drive the compositor's per-window appearance (border, hidden title
    /// bar) for every window this engine currently manages, WITHOUT recomputing
    /// the layout — windows keep their current zones/positions. The compositor
    /// derives each window's chrome from the geometry/state this re-emits.
    ///
    /// Called by the daemon when the compositor bridge (re)registers: on a
    /// daemon or effect restart the compositor drops its per-window appearance
    /// state, so it must be re-driven from the daemon's authoritative placement
    /// state. Distinct from reapplyLayout(), which is a user navigation action
    /// that recomputes the layout and may move windows. Default is a no-op for
    /// engines that don't manage compositor-side window chrome.
    virtual void reapplyManagedWindowAppearance()
    {
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Unified placement capture/restore (engine-agnostic restore model)
    //
    // The single seam for the unified WindowPlacement restore model. An engine
    // implements exactly these two methods to participate in save+restore; a new
    // engine (e.g. the scrolling engine) needs no core/schema change — it keys
    // its own EngineSlot (state token + slot reference) under its engineId() in the
    // single per-window record and reads/writes the shared freeGeometryByScreen.
    // ═══════════════════════════════════════════════════════════════════════════

    /// Report @p windowId's CURRENT placement for persistence, or nullopt if this
    /// engine does not manage it. Fill the engine's EngineSlot — `state` from its
    /// token vocabulary (free/floating/snapped/tiled/...) plus its slot reference
    /// (zone IDs / tile order) — under engines[engineId()], and the screen/desktop/
    /// activity context. Do NOT set freeGeometryByScreen: the capture orchestrator
    /// fills the shared free/float geometry from the live frame, and only when the
    /// state is free/floating, so a managed rect never becomes the float-back.
    ///
    /// This is the polymorphic capture seam the common layer drives — the WTA close
    /// hook and the save-time snapshot call it for every window.
    virtual std::optional<WindowPlacement> capturePlacement(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return std::nullopt;
    }

    /// Apply @p placement to a (re)opening window on @p screenId. Return true if
    /// this engine claimed and applied it. Dispatch on placement.slotFor(engineId())
    /// .state, reading the engine's slot reference and the shared freeGeometryByScreen.
    ///
    /// Contract pair of capturePlacement() and the engine-agnostic entry point for a
    /// new engine. NOTE: NO built-in engine overrides this today — snap, autotile
    /// and scrolling all apply restore inline in their own open paths
    /// (SnapEngine::resolveWindowRestore consults the store and returns a SnapResult
    /// to the effect; AutotileEngine::insertWindow and the scroll engine's open path
    /// take()/claim the record themselves) because those paths carry engine-specific
    /// policy (snap's auto-snap fallback chain; autotile's burst-insert coalescing;
    /// scrolling's strip-stash claim) that a single apply-this-record call cannot
    /// express. NOTHING in-tree calls this virtual today — a minimal future
    /// engine that implements it MUST have its own open path invoke it
    /// directly; no orchestrator will.
    virtual bool restorePlacement(const WindowPlacement& placement, const QString& screenId)
    {
        Q_UNUSED(placement)
        Q_UNUSED(screenId)
        return false;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Window ordering and focus (mode-transition capture and seed)
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QStringList managedWindowOrder(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return {};
    }
    virtual void setInitialWindowOrder(const QString& screenId, const QStringList& windowIds)
    {
        Q_UNUSED(screenId)
        Q_UNUSED(windowIds)
    }

    /// The window this engine considers focused on @p screenId, or empty.
    ///
    /// The focus half of the mode-transition capture that managedWindowOrder
    /// supplies the position half of. Order alone is not enough to hand a
    /// flip back to the user unchanged: an engine with a VIEW (the strip) has
    /// to know which window to anchor on, and re-deriving that from position
    /// picks whichever column the seed happened to adopt first.
    virtual QString managedFocusedWindow(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return {};
    }
    /// The desktop this engine has PINNED @p screenId to, or 0 when it has not
    /// pinned it (which is the normal case, and the default here).
    ///
    /// A pin is an engine-private override for a screen whose windows are all
    /// sticky: it outranks the compositor's per-output desktop inside the
    /// engine's own key resolution, and is invisible from outside. That makes it
    /// the one way a capture can read one desktop's state while filing the
    /// result under another's key, recording a pairing that never existed.
    ///
    /// Reports the PIN specifically, not the engine's resolved desktop, so a
    /// caller comparing against its own desktop cannot be tripped by the two
    /// merely LABELLING a screen differently — an engine and a caller can hold
    /// consistent but differently-numbered views of the same screen (a virtual
    /// sub-screen resolves through its parent for one and not the other), and
    /// that costs nothing as long as each is self-consistent.
    ///
    /// Meant for a comparison gate, not for re-keying: the pin is dropped when
    /// the engine releases the screen, so filing by the pinned desktop would
    /// store under a key the re-entry lookup never consults.
    virtual int stickyPinnedDesktopForScreen(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return 0;
    }
    /// Hand the incoming engine the focus captured from the outgoing one.
    ///
    /// Advisory. WHEN to apply it is the implementor's choice, not part of this
    /// contract: the seeded window is usually not the LAST to re-announce, so
    /// an implementor that re-derives focus from arrivals has to defer the seed
    /// past them or have it overwritten. The scroll engine consumes it at the
    /// end of its arrival burst for exactly that reason; an implementor with no
    /// burst concept is free to apply it however it likes.
    ///
    /// An empty @p windowId is not a no-op: it means the capturing transition
    /// found no focus to report, and it must CLEAR any seed an earlier one
    /// left, or a stale seed outlives the transition that owned it.
    ///
    /// An engine with no view of its own has nothing to do with this and the
    /// default no-op is the right implementation for it — the focused window
    /// is wherever the compositor already has it.
    virtual void setInitialFocusedWindow(const QString& screenId, const QString& windowId)
    {
        Q_UNUSED(screenId)
        Q_UNUSED(windowId)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Per-screen config (override if engine supports per-screen overrides)
    //
    // SCOPE IS THE IMPLEMENTATION'S CHOICE, and the two in-tree engines differ.
    // AutotileEngine stores one map per SCREEN. ScrollEngine stores one per
    // (screen, desktop, activity) and answers for the screen's CURRENT context,
    // because its templates are resolved per context and keying by screen let
    // one desktop's template overwrite another's. A caller holding this
    // interface must therefore not assume a map it pushed survives a desktop or
    // activity switch, nor that the accessor replays what it last wrote.
    // clearPerScreenConfig is the whole-SCREEN door in both: it drops every
    // context's entry, so an engine keyed per context needs an empty
    // applyPerScreenConfig push, not a clear, to say "this context has none".
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides)
    {
        Q_UNUSED(screenId)
        Q_UNUSED(overrides)
    }
    virtual void clearPerScreenConfig(const QString& screenId)
    {
        Q_UNUSED(screenId)
    }
    virtual QVariantMap perScreenOverrides(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return {};
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Mode-specific float MARKER (runtime discriminator, NOT persistence)
    //
    // Distinguishes a USER float in this engine's mode from an incidental float
    // (e.g. autotile overflow). It is live runtime state the capture funnel reads
    // to decide whether a float should persist into the record — there is no
    // parallel "saved floats" store; the WindowPlacement record is the single
    // source of truth for cross-mode float state. All three verbs live here,
    // in one section (mark / query / clear).
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void markModeSpecificFloated(const QString& windowId)
    {
        Q_UNUSED(windowId)
    }
    virtual bool isModeSpecificFloated(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return false;
    }
    virtual void clearModeSpecificFloatMarker(const QString& windowId)
    {
        Q_UNUSED(windowId)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Drag insert preview (override if engine supports drag-to-insert)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Where a drag-insert preview should place the dragged window, in the
    /// TARGET ENGINE's slot vocabulary. A flat int cannot address the strip's
    /// two-axis drop space ("column 3, tile 1" vs "a new column between 2 and
    /// 3"), so the drag-insert verbs speak this struct; each engine documents
    /// its own field meaning.
    ///
    /// - Autotile: `primary` is the TILED-ONLY insert index (the unit
    ///   updateDragInsertPreview clamps against tiledWindowCount — NOT the
    ///   raw window-order index HandoffContext::insertIndex carries).
    ///   `secondary` and `newSlot` are unused.
    /// - Scrolling: `primary` is the COLUMN index. With `newSlot` true the
    ///   drop opens a NEW column at `primary` (existing columns from
    ///   `primary` shift right); otherwise the window joins the column at
    ///   `primary` as a tile at `secondary` (clamped into the stack;
    ///   -1 appends at the bottom). `leadingEdge` marks a new-column target
    ///   aimed from BEYOND the view's leading edge (left of everything
    ///   visible, or the first visible column's outer band): the drop is
    ///   identical, but the indicator renders it as a past-the-edge hint
    ///   instead of a full rect over the first visible column. Purely a
    ///   presentation tag — commit ignores it, autotile never sets it.
    struct DragInsertTarget
    {
        int primary = -1;
        int secondary = -1;
        bool newSlot = false;
        bool leadingEdge = false;

        bool isValid() const
        {
            return primary >= 0;
        }
        bool operator==(const DragInsertTarget& other) const = default;
    };

    // Contract shared by the preview verbs, which the two implementations
    // honour with OPPOSITE internal shapes:
    //  - begin MAY mutate managed state, and MAY adopt the window from
    //    another screen, from a floating set, or from untracked entirely
    //    (both engines do all three).
    //  - an engine may or may not keep the window MANAGED while its preview
    //    is live — autotile keeps it a tile and restructures per update;
    //    scrolling DETACHES it until commit. Cross-engine callers must not
    //    use isWindowTiled/isWindowManaged as a drag-state probe.
    //  - begin on an engine already holding a preview cancels the old one
    //    first; commit/cancel are no-ops with no preview live; and
    //    dragInsertPreviewScreenId is empty with no preview live.

    virtual bool hasDragInsertPreview() const
    {
        return false;
    }
    virtual bool beginDragInsertPreview(const QString& windowId, const QString& screenId)
    {
        Q_UNUSED(windowId)
        Q_UNUSED(screenId)
        return false;
    }
    virtual void commitDragInsertPreview()
    {
    }
    virtual void cancelDragInsertPreview()
    {
    }
    virtual QString dragInsertPreviewScreenId() const
    {
        return {};
    }

    /// The screen the previewed window was on BEFORE begin adopted it, or
    /// empty when it had no prior state (begin took it from untracked) or no
    /// preview is live. Distinct from dragInsertPreviewScreenId whenever the
    /// drag crossed outputs, and the two together are what a caller needs to
    /// decide whether an output going away concerns this preview: cancel
    /// restores the window to the PRIOR screen, so a preview whose prior
    /// screen is disappearing can no longer be cancelled meaningfully even
    /// though its target survives.
    virtual QString dragInsertPreviewPriorScreenId() const
    {
        return {};
    }

    /// Compute the drop target for a cursor position on a managed screen.
    /// Returns an invalid target when the screen has no active state.
    virtual DragInsertTarget computeDragInsertTargetAtPoint(const QString& screenId, const QPoint& cursorPos) const
    {
        Q_UNUSED(screenId)
        Q_UNUSED(cursorPos)
        return {};
    }

    /// Update the drop target for an active drag-insert preview. An invalid
    /// target is IGNORED, never clamped — implementations keep the previous
    /// stored target (autotile's engine-local int form clamps instead; that
    /// contract does not cross this seam).
    virtual void updateDragInsertPreview(const DragInsertTarget& target)
    {
        Q_UNUSED(target)
    }

    /// Advance edge auto-scroll for a live drag-insert preview (niri's
    /// dnd-edge-view-scroll): an engine whose layout is a scrollable
    /// viewport moves its VIEW while the cursor sits inside a band at the
    /// work area's edge, so a drop can reach a column that is off screen.
    /// Structure is untouched — this is the one thing a DETACH-ONCE engine
    /// may move mid-drag.
    ///
    /// Driven by the daemon's repeating drag-scroll timer, NOT by cursor
    /// motion: the whole point is that a PARKED cursor keeps scrolling, and
    /// motion events stop arriving the moment the hand stops. @p dtSeconds
    /// is the real elapsed time since the previous tick, so the speed ramp
    /// is frame-rate independent and a stalled timer cannot lurch.
    ///
    /// Returns true when the caller should REPAINT the drop indicator, which
    /// is not the same as "the view moved". A tick that is pinned at a strip
    /// end moves nothing yet still rewrites the owned target, and a tick that
    /// carries the cursor out of the band hands the target back and repairs
    /// it — both need the indicator redrawn. An implementation that returned
    /// true only for actual view motion would drop those repaints.
    ///
    /// While this is scrolling, the implementation OWNS the drop target: it
    /// writes the edge slot itself and the caller must not re-hit-test (see
    /// dragAutoScrollActive). Columns sliding under a stationary cursor
    /// otherwise re-resolve the target on every boundary that passes, which
    /// flips the indicator between a new column and a join.
    virtual bool dragAutoScrollTick(const QString& screenId, const QPoint& cursorPos, qreal dtSeconds)
    {
        Q_UNUSED(screenId)
        Q_UNUSED(cursorPos)
        Q_UNUSED(dtSeconds)
        return false;
    }

    /// Whether edge auto-scroll currently owns the drop target. True from the
    /// moment the band's delay elapses until the cursor leaves the band, the
    /// preview ends, the strip shrinks to fit the viewport, the feature is
    /// switched off, or any other condition that makes the scroll incoherent
    /// (the implementation's disarm paths are the full list — a vanished
    /// state, a dead work area, no visible column, a foreign screen). Not an
    /// exhaustive contract, so do not read it as one.
    /// Reaching a strip end does NOT end ownership: the view is
    /// pinned but the cursor is still asking to insert past that edge, and
    /// that edge slot stays the promise. Handing the target back there would
    /// resume per-column hit-testing the instant the strip pins, which is the
    /// churn this whole mechanism exists to prevent.
    ///
    /// While true the caller keeps pushing the indicator but must leave
    /// computeDragInsertTargetAtPoint alone.
    virtual bool dragAutoScrollActive() const
    {
        return false;
    }

    /// Give the drop target back and forget any armed band, WITHOUT moving
    /// the view or re-aiming. For a caller that is taking over aiming by
    /// another route (the strip selector popup) and needs the engine to stop
    /// owning the target and to serve a fresh start delay next time.
    ///
    /// Distinct from letting a tick disarm on its own: a tick reads the
    /// cursor, so it can only release ownership when the cursor has actually
    /// left the band, and it can just as easily TAKE ownership. This never
    /// takes it.
    virtual void cancelDragAutoScroll()
    {
    }

    /// The rect the dragged window would occupy if the live preview were
    /// dropped now, in absolute px on @p screenId, for a caller that wants to
    /// PAINT the drop target. Empty when no preview is live, no target has
    /// been hit-tested yet, or the preview belongs to another screen.
    ///
    /// Measured in the layout's CURRENT view. A drop may additionally scroll
    /// the view — the scroll engine focuses the dropped window, which can
    /// re-anchor the strip — so this marks the place under the cursor that the
    /// user is aiming at, not the screen position the window settles at once
    /// any post-drop scroll finishes. Painting the post-scroll position would
    /// move the indicator away from the cursor while the user is still
    /// choosing, which is the worse of the two.
    ///
    /// Default empty, and that is the right answer for an engine that
    /// restructures live: autotile's feedback IS its restructure, so painting
    /// a second indicator over it would double-report the same thing. Only an
    /// engine that defers structure to the drop (the scroll strip, per the
    /// DETACH-ONCE contract above) has a target that is otherwise invisible.
    ///
    /// Mostly not clamped to the viewport — a join target's rect is where the
    /// slot genuinely is — with two deliberate NEW-COLUMN exceptions, both
    /// niri's insert-hint rules: a before-the-first slot is placed just
    /// OUTSIDE the first column (its raw post-insert position coincides with
    /// that column and would read as "replace this"), and any new-column
    /// slot past a visible edge is clamped so at least half the rect stays
    /// on screen. Without the clamp, the end slots of a FULL viewport
    /// resolve entirely off screen and the overlay clips the indicator
    /// away, leaving the drop that most needs feedback with none; the
    /// half-in band at the edge marks "insert past this edge" without
    /// pretending to be the slot's true position.
    virtual QRect dragInsertIndicatorRect(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return {};
    }

    /// The window currently under a compositor interactive move (the whole
    /// drag, preview or not). Empty clears. While set, an engine that still
    /// models the window as tiled must neither emit geometry for it nor
    /// reconcile its geometry acks — KWin's interactive move owns the frame
    /// until drop, and fighting it yanks the window from the cursor (and a
    /// per-ack reconcile pins size intents to transient drag frames). The
    /// daemon sets it at beginDrag and clears it before the drop is
    /// finalized, so commit/float paths apply normally. Today the daemon
    /// calls this on the SCROLL engine only, and only ScrollEngine
    /// overrides it: autotile also retiles mid-drag but exempts the dragged
    /// window inside applyTiling's emit filter instead. Override this when
    /// an engine has no such filter of its own.
    virtual void setInteractiveDragWindow(const QString& windowId)
    {
        Q_UNUSED(windowId)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Per-window tracking state
    //
    // What the daemon asks an engine about ONE window: does it track it, does
    // it manage/tile it, which screen does it think the window is on, what min
    // size does it model — plus the two per-window updates that follow from
    // those answers (a late min-size discovery, an interactive resize). The
    // drag-preview verbs above are a separate group; these have nothing to do
    // with a drag.
    // ═══════════════════════════════════════════════════════════════════════════

    virtual bool isWindowTracked(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return false;
    }
    /// Whether the engine considers the window "managed" (eligible for
    /// layout operations). Semantics are engine-specific:
    /// - Autotile: equivalent to isWindowTiled (floating windows excluded).
    /// - Scrolling: the window occupies a strip column (floating windows
    ///   excluded), same shape as autotile.
    /// - Snap: NOT implemented — SnapEngine keeps the inherited false.
    /// Callers that need a consistent cross-engine check for "engine owns
    /// this window at all" should use isWindowTracked instead; that is also
    /// the only correct check on a snap screen.
    virtual bool isWindowManaged(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return false;
    }

    /// Whether the window is actively tiled (engine-owned, non-floating).
    /// Distinct from isWindowTracked (which includes floating windows).
    virtual bool isWindowTiled(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return false;
    }

    /// Return the screen this engine considers the window to be on, or empty
    /// if the window isn't tracked by this engine.
    ///
    /// The daemon-side shortcut router consults this across engines to resolve
    /// the active window's current screen for routing decisions (float, focus,
    /// move). Without it, a cross-engine handoff (e.g. drag-insert from snap
    /// into autotile) leaves the daemon's screenAssignments lookup empty
    /// because the source engine has released its tracking, and the next
    /// shortcut routes to whichever engine the cached focus screen pointed at
    /// rather than the engine that now owns the window.
    virtual QString screenForTrackedWindow(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return {};
    }

    /// The window's client-reported minimum size as last known by this
    /// engine, or an UNKNOWN answer when it has none. Read by the cross-engine
    /// handoff dispatcher to seed HandoffContext::minSize; must be queried
    /// before handoffRelease. Default suits engines without a min-size model.
    ///
    /// "Unknown" is spelled two ways in the tree and a caller must accept both:
    /// a default-constructed 0x0, and an INVALID QSize (-1x-1), which the scroll
    /// engine returns deliberately to distinguish "no entry" from "a real zero".
    /// Every in-tree consumer clamps with qMax against 0, which treats the two
    /// identically; a new consumer must do the same rather than assuming either.
    virtual QSize windowMinimumSize(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return {};
    }

    /// Update a window's minimum size after the initial windowOpened.
    /// The compositor discovers a min size late for some clients (or the
    /// client raises it at runtime); engines that fit windows to slots
    /// re-validate their layout on a change. Default is a no-op for
    /// engines without a min-size model.
    virtual void windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight)
    {
        Q_UNUSED(windowId)
        Q_UNUSED(minWidth)
        Q_UNUSED(minHeight)
    }

    /// Notify the engine that a tracked window finished an interactive resize.
    ///
    /// The daemon's WindowTracking adaptor forwards the compositor's
    /// interactive-resize-finished event here so an engine can reflow the rest
    /// of its layout to absorb the change (autotile fills the freed gap;
    /// GitHub #652). @p oldFrame / @p newFrame are the window's frame geometry
    /// before and after the resize; @p screenId is the screen the daemon
    /// resolved the window to. Default is a no-op for engines (e.g. snap) that
    /// have no neighbour-reflow model.
    virtual void onWindowResized(const QString& windowId, const QRect& oldFrame, const QRect& newFrame,
                                 const QString& screenId)
    {
        Q_UNUSED(windowId)
        Q_UNUSED(oldFrame)
        Q_UNUSED(newFrame)
        Q_UNUSED(screenId)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Cross-engine handoff
    //
    // When a window crosses screens whose owning engines differ (e.g. a snap
    // screen → an autotile screen, via drag drop), ownership has to transfer
    // between two engines. Without an explicit contract, each engine makes
    // local guesses (autotile's "is this a floating window I should adopt?"
    // branch, snap's "do I know this window?" early-return) and the daemon
    // misroutes user intents during the transition window.
    //
    // The handoff is a two-step transaction the daemon orchestrates:
    //   1. fromEngine->handoffRelease(windowId)
    //   2. toEngine->handoffReceive(ctx)
    //
    // Each engine's release is a tracking-only clear (no geometry mutation —
    // the receiving engine places the window). Each engine's receive applies
    // its own placement policy (autotile picks an insert slot or floats;
    // snap picks the nearest zone or floats) using the context fields.
    //
    // Engines that don't currently distinguish ownership across screens can
    // leave both as no-ops; the defaults are safe.
    //
    // The verbs are prefixed `handoff*` to keep them distinct from an engine
    // dropping a window entirely (the window is no longer engine-managed at all
    // — a different concept from "transferring ownership to another engine").
    // ═══════════════════════════════════════════════════════════════════════════

    /// Context for a cross-engine window handoff. Populated by the daemon
    /// from the source engine's state and the drop event before invoking
    /// handoffReceive on the destination engine.
    struct HandoffContext
    {
        QString windowId;
        QString fromEngineId; ///< source engine identity ("snap" / "autotile" / "scrolling" / "")
        QString toScreenId; ///< destination screen (must be owned by `to` engine)
        int toDesktop = 0; ///< destination virtual desktop (1-based); 0 = current
                           ///< desktop (drag-drop / same-desktop monitor crossing).
                           ///< Set for a cross-VIRTUAL-DESKTOP handoff so the
                           ///< receiver places the window in the target desktop's
                           ///< state/layout, not the currently-visible one.
        QPoint dropPos; ///< cursor position at drop, or invalid for non-drag handoffs
        QRect sourceGeometry; ///< window's frame at handoff time (for size preservation)
        QSize minSize; ///< minimum size as the SOURCE engine models it (0x0
                       ///< when unknown) — autotile stores it screen-capped,
                       ///< scrolling raw, so treat it as a hint, not the
                       ///< client's exact value. The compositor only
                       ///< re-reports a min size when it changes or a retile
                       ///< discovers a refusal, so the receiver seeds its own
                       ///< min-size model from this instead of waiting a
                       ///< refuse/re-discover round-trip. Callers must query
                       ///< the source BEFORE handoffRelease drops its tracking.
        QStringList sourceZoneIds; ///< zones the window held at source (empty if not snapped)
        bool wasFloating = false; ///< window was floating in source engine
        bool heldFocus = false; ///< the window held compositor focus at
                                ///< handoff time, per the daemon's
                                ///< windowActivated tracking. Receivers seed
                                ///< their focus-side memory from it: a
                                ///< focused window KEEPS focus across the
                                ///< handoff, so no focusChanged report ever
                                ///< arrives to record the side change.
        int insertIndex = -1; ///< PER-TARGET unit. Autotile target: raw
                              ///< window-order index (position in windowOrder(),
                              ///< counting floats — NOT the tiled-only index).
                              ///< Scrolling target: COLUMN index (0 = first
                              ///< column; -1 appends at the strip's right end).
                              ///< Used by cross-mode SWAP (partner's exact slot)
                              ///< and by edge-aware cross-mode MOVE entry.
                              ///< Ignored by snap targets.
    };

    /// Receive ownership of a window from another engine.
    ///
    /// Implementations should:
    /// - Add the window to their own tracking (per-screen/per-state).
    /// - Decide placement (snap to zone / tile / float) using the context
    ///   and engine-local policy. Drag drops typically place at dropPos;
    ///   non-drag handoffs (cross-engine focus changes, programmatic moves)
    ///   typically respect wasFloating (the window keeps its live frame, so
    ///   no geometry is carried in the context).
    /// - Emit any `windowFloatingChanged` / placement signals their normal
    ///   placement paths emit, so downstream state stays consistent.
    ///
    /// Default is a no-op so engines that don't yet implement the handoff
    /// don't reject the call — the orchestrator falls back to its legacy path.
    virtual void handoffReceive(const HandoffContext& ctx)
    {
        Q_UNUSED(ctx)
    }

    /// Release ownership of a window WITHOUT modifying its geometry.
    ///
    /// Implementations should:
    /// - Remove the window from per-screen/per-state tracking.
    /// - Clear zone assignments (if any) WITHOUT triggering a resnap of
    ///   neighbours — that's the receiving engine's job once it places the
    ///   window in its layout.
    /// - Preserve any pre-tile / pre-float captured geometry that should
    ///   survive the cross-engine move (the receiving engine may consult
    ///   it via the HandoffContext for size preservation).
    ///
    /// Default is a no-op for the same reason as handoffReceive.
    virtual void handoffRelease(const QString& windowId)
    {
        Q_UNUSED(windowId)
    }

    /// Stable engine identity for HandoffContext.fromEngineId. Conventional
    /// values: "snap" / "autotile" / "scrolling". Empty string means "unidentified" and
    /// disables receive-side reasoning that depends on the source mode.
    virtual QString engineId() const
    {
        return {};
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Layout capability (UI-facing; distinct from algorithm identity)
    // ═══════════════════════════════════════════════════════════════════════════

    /// How this engine relates to user-selectable layouts — the entries the
    /// layout picker, drag layout popup, quick-layout slots and layout cycle
    /// operate on. The daemon consults this per screen (via the router's
    /// engineFor) to decide what layout-selection UI and shortcuts mean
    /// there.
    enum class LayoutSupport {
        /// No layout concept at all: the daemon suppresses the picker/popup
        /// and answers the layout shortcuts with a "not available" OSD
        /// instead of falling back to snap-layout semantics.
        None,
        /// Layouts drive window placement (snap zone layouts, autotile
        /// algorithm cards): the classic picker semantics.
        Placement,
        /// The engine consumes a first-class TEMPLATE object rather than a
        /// placement layout. For the scrolling engine that object is a native
        /// ScrollingTemplate: a seed blueprint of column widths and displays,
        /// the default-width trio for columns beyond it, and the preset width
        /// and height vocabularies the size shortcuts cycle through. Picking an
        /// entry sets the screen's template, it does not place windows. The
        /// daemon routes such applies to the assignment's template slot and
        /// makes the native template cards the candidate set (no zone layouts,
        /// no autotile cards).
        Templates
    };

    /// Default None: an engine must opt in to being a layout consumer.
    virtual LayoutSupport layoutSupport() const
    {
        return LayoutSupport::None;
    }

    /// Whether the daemon's edge-triggered drag popup (the zone selector
    /// surface) should render this engine's DRAG-INSERT vocabulary — strip
    /// column cards whose gap / join / half targets translate into
    /// DragInsertTarget — instead of zone layouts, on screens this engine
    /// owns. Consumed by WindowDragAdaptor's trigger gate and OverlayService's
    /// model selection. Default false: the screen's drag popup speaks the
    /// zone-layout vocabulary instead (snap screens keep the classic zone
    /// selector; an ENGINE-owned screen without this capability suppresses
    /// the popup entirely — the pre-existing autotile behaviour — because
    /// the engine owns placement there and has no picker of its own).
    virtual bool providesDragInsertSelector() const
    {
        return false;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Algorithm / mode identity (override if engine has switchable algorithms)
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QString algorithmId() const
    {
        return {};
    }
    virtual void setAlgorithm(const QString& algorithmId)
    {
        Q_UNUSED(algorithmId)
    }
    virtual bool isEnabled() const noexcept
    {
        return false;
    }
    virtual QString activeScreen() const
    {
        return {};
    }
    virtual void setActiveScreenHint(const QString& screenId)
    {
        Q_UNUSED(screenId)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Desktop/activity context (override if engine is desktop-aware)
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void setCurrentDesktop(int desktop)
    {
        Q_UNUSED(desktop)
    }
    /// Set a single screen's current virtual desktop (Plasma 6.7 "switch desktops
    /// independently for each screen"). A PURE context swap — it selects which
    /// per-(screen, desktop) tiling state is current for this screen; it does NOT
    /// migrate windows between desktop states (the other desktop's state must stay
    /// put so it reappears when that screen returns). Default no-op for engines
    /// that are not per-screen-desktop-aware.
    virtual void setCurrentDesktopForScreen(const QString& screenId, int desktop)
    {
        Q_UNUSED(screenId)
        Q_UNUSED(desktop)
    }
    /// Drop a screen's per-output desktop, reverting it to the global current.
    virtual void clearCurrentDesktopForScreen(const QString& screenId)
    {
        Q_UNUSED(screenId)
    }
    virtual void setCurrentActivity(const QString& activity)
    {
        Q_UNUSED(activity)
    }
    /// Inject the cross-surface resolver used to find a neighbouring output /
    /// desktop when directional navigation reaches a surface boundary. The
    /// resolver is borrowed, not owned; the caller must keep it alive for the
    /// engine's lifetime (in the daemon it outlives both engines by member
    /// order). Engines that don't support cross-surface navigation ignore it.
    virtual void setCrossSurfaceResolver(ICrossSurfaceResolver* resolver)
    {
        Q_UNUSED(resolver)
    }
    virtual void updateStickyScreenPins(const std::function<bool(const QString&)>& isWindowSticky)
    {
        Q_UNUSED(isWindowSticky)
    }
    virtual QSet<int> desktopsWithActiveState() const
    {
        return {};
    }
    virtual void pruneStatesForDesktop(int removedDesktop)
    {
        Q_UNUSED(removedDesktop)
    }
    virtual void pruneStatesForActivities(const QStringList& validActivities)
    {
        Q_UNUSED(validActivities)
    }
    /// Prune per-(screen, desktop, activity) state for a PHYSICALLY REMOVED output
    /// (monitor hot-unplug), matching every virtual sub-screen of the removed
    /// physical id. All three engines override this and the daemon drives each
    /// from its screenRemoved handling: snap's stores are created lazily on
    /// placement with no screens set to reap them, and the two tiling engines'
    /// screens-set sweeps only reap CURRENT-context states, so sibling-context
    /// states (other desktops/activities) of the removed output would leak
    /// without the explicit whole-output prune. A tiling-family engine must emit
    /// windowsReleased for the windows it drops here, AFTER its reverse-map
    /// cleanup, so the daemon's restore consumers can re-home them; snap does not,
    /// because it is the engine those releases are restored INTO.
    virtual void pruneStatesForRemovedScreen(const QString& physicalScreenId)
    {
        Q_UNUSED(physicalScreenId)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Settings synchronization (override if engine caches config)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Re-read all tuning values from the engine's settings interface.
    /// Called by the daemon after any settings change. Engines that cache
    /// config values (e.g. AutotileEngine) override this to repopulate
    /// their config struct. Engines that read on demand (e.g. SnapEngine)
    /// leave this as a no-op.
    virtual void refreshConfigFromSettings()
    {
    }
    /// One home for the master/split ratio step so the default return below
    /// and the two default arguments cannot drift apart.
    static constexpr qreal kDefaultSplitRatioStep = 0.05;
    virtual qreal effectiveSplitRatioStep(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return kDefaultSplitRatioStep;
    }
    /// Runtime max-windows limit. Returns -1 (unlimited sentinel) by default;
    /// engines that enforce a cap override with the actual value.
    /// Callers must treat -1 as "no limit" — never use as a divisor.
    virtual int runtimeMaxWindows() const
    {
        return -1;
    }
    /// The user's saved per-algorithm max-windows tuning for @p algorithmId,
    /// or std::nullopt when the engine keeps no such slot. Engines with
    /// per-algorithm tuning (AutotileEngine) override this; both mixed-
    /// algorithm cap paths consult it — the daemon's per-screen MaxWindows
    /// injection and PerScreenConfigResolver::effectiveMaxWindows step 3 —
    /// so a screen pinned to a non-global algorithm gets the user's saved
    /// cap, not the algorithm's built-in default.
    virtual std::optional<int> savedMaxWindowsForAlgorithm(const QString& algorithmId) const
    {
        Q_UNUSED(algorithmId)
        return std::nullopt;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Retile / refresh (override if engine supports on-demand retile)
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void retile(const QString& screenId = QString())
    {
        Q_UNUSED(screenId)
    }
    virtual void scheduleRetileForScreen(const QString& screenId)
    {
        Q_UNUSED(screenId)
    }

    // Per-window restore persistence is unified: engines implement
    // capturePlacement()/restorePlacement() (above) and the common
    // WindowPlacementStore handles capture timing, serialization, and the single
    // WindowPlacements config key. No engine-specific serialize/deserialize hooks.

    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Init hooks (override to receive shared services)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Attach a window-class registry (QObject carrying WindowRegistry).
    /// Engines qobject_cast to their concrete type internally.
    /// @param registry Not owned; must outlive this engine.
    virtual void setWindowRegistry(QObject* registry)
    {
        Q_UNUSED(registry)
    }
    // ═══════════════════════════════════════════════════════════════════════════
    // OPTIONAL: Master operations (autotile-specific, no-op on snap engine)
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void increaseMasterRatio(qreal delta = kDefaultSplitRatioStep)
    {
        Q_UNUSED(delta)
    }
    virtual void decreaseMasterRatio(qreal delta = kDefaultSplitRatioStep)
    {
        Q_UNUSED(delta)
    }
    virtual void increaseMasterCount()
    {
    }
    virtual void decreaseMasterCount()
    {
    }
    virtual void focusMaster()
    {
    }
    virtual void swapFocusedWithMaster()
    {
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void saveState() = 0;
    virtual void loadState() = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // State access
    // ═══════════════════════════════════════════════════════════════════════════

    /// Per-screen state object for the given screen. May return nullptr
    /// if the engine does not manage the screen OR if per-screen state
    /// ownership has not yet been wired for that engine.
    /// Callers must not use a non-null return as a proxy for "engine
    /// manages this screen" — use isActiveOnScreen() for that check.
    virtual IPlacementState* stateForScreen(const QString& screenId) = 0;
    virtual const IPlacementState* stateForScreen(const QString& screenId) const = 0;
};

} // namespace PhosphorEngine
