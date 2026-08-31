// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QRect>
#include <QString>
#include <QStringList>

#include <utility>

namespace PhosphorEngine {

/// One engine's view of a window: which managed slot it occupies (or that it is
/// floating / unmanaged) in THAT engine's mode. State is PER ENGINE — a window
/// can be `snapped` in the snap engine AND `floating` in the autotile engine at
/// the same time, each engine remembering the window's state in its OWN mode.
///
/// The slot is an IDENTIFIER, never a rectangle: `zoneIds` for the snap engine,
/// `order` for the autotile engine. A managed rect (the zone or tile geometry) is
/// NEVER stored here — that is what keeps the shared free/float geometry from
/// being poisoned by a zone/tile rect (the per-mode geometry leak this model
/// fixes). The window's actual free/float position lives once, shared, in
/// WindowPlacement::freeGeometryByScreen.
struct EngineSlot
{
    QString state; ///< engine-defined token: snap "snapped"/"floating"; autotile "tiled"/"floating" ("free" retired)
    QStringList zoneIds; ///< snap slot — zone UUIDs (first is primary); empty for autotile
    int order = -1; ///< autotile slot — tile index within the screen; -1 for snap

    bool operator==(const EngineSlot& o) const
    {
        return state == o.state && zoneIds == o.zoneIds && order == o.order;
    }
    bool operator!=(const EngineSlot& o) const
    {
        return !(*this == o);
    }
    bool isEmpty() const
    {
        return state.isEmpty();
    }
};

/// One window's single, authoritative placement record — the unit of the unified,
/// engine-agnostic restore model. The WindowPlacementStore holds at most ONE
/// record per window (NOT one per engine). The record carries:
///
///  - per-engine STATE in `engines` (keyed by IPlacementEngine::engineId()): each
///    engine's own slot + state, independent of the others (per-mode float
///    independence — floating in snap does not float in autotile);
///  - ONE SHARED free/float GEOMETRY in `freeGeometryByScreen`: the position the
///    window occupies when it is NOT engine-managed, shared across snap/autotile
///    but keyed PER SCREEN so a window remembers a distinct free spot per monitor.
///    Written ONLY from a genuine free/floating frame; FROZEN while the window is
///    snapped or tiled. A zone/tile rect never enters this map.
///
/// Extensibility: a new engine needs no change here — it keys its own slot under
/// its engineId() and reads/writes the shared geometry. Core code never
/// interprets an EngineSlot::state beyond equality.
struct WindowPlacement
{
    // ── Identity (the two store keys) ──
    QString windowId; ///< full `appId|uuid`; exact key for daemon-restart (uuid stable)
    QString appId; ///< window class; FIFO key for close/reopen (uuid changes)

    // ── Context (the universal restore + disabled-context gate) ──
    QString screenId; ///< last managed-context screen
    /// 0 = all-desktops / sticky / unknown. The three meanings are
    /// deliberately conflated in one sentinel: LayoutRegistry::modeForScreen
    /// treats 0 as its supported desktop-agnostic input, which is the right
    /// answer for a genuinely sticky window and an approximation for a
    /// window whose desktop was simply never captured (a plain setFloating
    /// records none). Under per-desktop mode splits the approximation can
    /// resolve a mode the window's real desktop does not have; splitting the
    /// sentinel into sticky-vs-unknown is a persisted-data-model change and
    /// has not been taken.
    int virtualDesktop = 0;
    QString activity; ///< empty = all-activities / unknown
    WindowKind kind = WindowKind::Unknown;

    // ── Per-engine managed state, keyed by engineId() ──
    QHash<QString, EngineSlot> engines;

    // ── Shared free/float geometry, keyed by screenId ──
    QHash<QString, QRect> freeGeometryByScreen;

    // ── Recency (most-recent-wins ordering; stamped by the store) ──
    quint64 sequence = 0;

    /// TRANSIENT, never serialized: true only for a record this daemon read back
    /// from disk at startup and has not yet re-captured live. It is what makes
    /// the cross-desktop restore (WindowTrackingAdaptor::applyPersistedDesktopRestore)
    /// a genuine login-time behaviour without a timer — the flag can only ever be
    /// set on a record that predates this daemon's start, and the first live
    /// engine capture clears it, so the restore fires at most once per record and
    /// never for a placement made in this session.
    ///
    /// Deliberately absent from sameContentAs: it is not part of the persisted
    /// content, and letting it count as a change would make the clearing merge
    /// re-mark the store dirty and reschedule the save timer on every capture.
    bool fromPersistedSession = false;

    bool isValid() const
    {
        return !windowId.isEmpty() && !appId.isEmpty();
    }

    /// The slot for @p engineId, or a default (empty) slot if this engine has no
    /// recorded state for the window.
    EngineSlot slotFor(const QString& engineId) const
    {
        return engines.value(engineId);
    }

    /// The shared free/float geometry for @p screenId, or an invalid rect if none
    /// has been captured on that screen yet.
    QRect freeGeometryFor(const QString& screenId) const
    {
        return freeGeometryByScreen.value(screenId);
    }

    /// The screenId of the deterministic cross-screen fallback pick: the
    /// lexicographically-smallest screenId holding a valid rect. There is no
    /// per-screen recency to pick a "newest", and QHash iteration order is
    /// unspecified — without a total order the fallback float-back screen
    /// would differ run to run. Empty when no screen has a valid rect.
    QString anyFreeGeometryScreenId() const
    {
        QString best;
        for (auto it = freeGeometryByScreen.constBegin(); it != freeGeometryByScreen.constEnd(); ++it) {
            if (it.value().isValid() && (best.isEmpty() || it.key() < best)) {
                best = it.key();
            }
        }
        return best;
    }

    /// Any captured free/float geometry (the deterministic pick of
    /// anyFreeGeometryScreenId()), used as a cross-screen fallback when the
    /// exact screen has none. Returns an invalid rect when the window has no
    /// free geometry on record at all.
    QRect anyFreeGeometry() const
    {
        return freeGeometryByScreen.value(anyFreeGeometryScreenId());
    }

    /// Whether this record carries anything worth restoring. True when the window
    /// has a captured free/float geometry, OR a MANAGED slot (`snapped`/`tiled`,
    /// which restore by zone/tile reference), OR a slot that still references a zone
    /// (`zoneIds` — a floated-from-snap window's pre-float zones) or a tile order.
    ///
    /// A bare slot with no geometry, no zone reference, and no tile order — whether
    /// `floating` (a never-snapped floated window captured frame-less) or the retired
    /// `free` token — has NOTHING to restore. Snapping now defaults every unmanaged
    /// window to `floating`, so this geometry-less floated residue is exactly the
    /// case that must be rejected: it must NOT be persisted, and the snap restore's
    /// accept predicate must never CONSUME it from the per-app FIFO. At MaxPerApp
    /// entries per app, such residue would otherwise starve and even evict the
    /// window's real placement (evictForCapacity prefers a contentless victim
    /// but falls back to the FIFO head), silently breaking geometry restore.
    bool hasRestorableContent() const
    {
        if (anyFreeGeometry().isValid()) {
            return true;
        }
        for (auto it = engines.constBegin(); it != engines.constEnd(); ++it) {
            const EngineSlot& s = it.value();
            if (s.state == stateSnapped() || s.state == stateTiled()) {
                return true;
            }
            if (!s.zoneIds.isEmpty() || s.order >= 0) {
                return true;
            }
        }
        return false;
    }

    /// Content equality IGNORING `sequence` (the store stamps a fresh sequence on
    /// every record(), so it must not count as a change). Lets the merge in
    /// record() short-circuit a re-capture that produced an identical placement —
    /// the save-time refreshOpenWindowPlacements() re-captures every open window
    /// each tick, and without this an unchanged window would re-mark dirty and
    /// reschedule the save timer forever (a self-perpetuating save/capture loop).
    bool sameContentAs(const WindowPlacement& o) const
    {
        return windowId == o.windowId && appId == o.appId && screenId == o.screenId
            && virtualDesktop == o.virtualDesktop && activity == o.activity && kind == o.kind && engines == o.engines
            && freeGeometryByScreen == o.freeGeometryByScreen;
    }

    /// Built-in engine-slot ids. Cross-engine readers (resnap, teardown
    /// capture, autotile's snap-defer gate) key slotFor() on these instead
    /// of spelling the id inline — same drift-prevention rationale as the
    /// state-token accessors below. Each engine's own engineId() override
    /// returns the same spelling.
    static QLatin1String snapEngineId()
    {
        return QLatin1String("snap");
    }
    static QLatin1String autotileEngineId()
    {
        return QLatin1String("autotile");
    }
    static QLatin1String scrollingEngineId()
    {
        return QLatin1String("scrolling");
    }

    /// Common state-token vocabulary. Engines may define more; these cover the
    /// built-in snap/autotile states.
    /// Retired snap state — snapping is now two-state (snapped/floating). No engine
    /// produces it any more; a legacy `free` record persisted by an older build
    /// deserializes to this token and is restored as floating (see
    /// SnapEngine::resolveWindowRestore). Retained only for that legacy-record
    /// mapping and the tests that exercise it.
    static QLatin1String stateFree()
    {
        return QLatin1String("free");
    }
    static QLatin1String stateFloating()
    {
        return QLatin1String("floating");
    }
    static QLatin1String stateSnapped()
    {
        return QLatin1String("snapped");
    }
    static QLatin1String stateTiled()
    {
        return QLatin1String("tiled");
    }
    /// An engine that KNOWINGLY GAVE THE WINDOW UP (a cross-mode handoff)
    /// leaves this instead of its managed slot. It is deliberately not an
    /// absent slot: `takeForReopen`'s exact-final gate treats "a record for
    /// this instance carrying a slot for the ASKING engine that the reopen
    /// predicate rejects" as a FINAL verdict with no FIFO fallback, and a
    /// released window must keep that verdict — dropping the slot outright
    /// made it indistinguishable from a fresh open, which then consumed a
    /// SIBLING instance's floating record and restored at the sibling's
    /// position. It is equally not a managed state: the cross-screen reclaim
    /// keys on snapped/tiled, so a released slot can no longer advertise a
    /// home the engine has given up.
    static QLatin1String stateReleased()
    {
        return QLatin1String("released");
    }

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj[QLatin1String("windowId")] = windowId;
        obj[QLatin1String("screen")] = screenId;
        obj[QLatin1String("desktop")] = virtualDesktop;
        if (!activity.isEmpty()) {
            obj[QLatin1String("activity")] = activity;
        }
        obj[QLatin1String("kind")] = static_cast<int>(kind);

        QJsonObject eng;
        for (auto it = engines.constBegin(); it != engines.constEnd(); ++it) {
            const EngineSlot& s = it.value();
            if (s.isEmpty()) {
                continue;
            }
            QJsonObject so;
            so[QLatin1String("state")] = s.state;
            if (!s.zoneIds.isEmpty()) {
                QJsonArray z;
                for (const QString& id : s.zoneIds) {
                    z.append(id);
                }
                so[QLatin1String("zoneIds")] = z;
            }
            if (s.order >= 0) {
                so[QLatin1String("order")] = s.order;
            }
            eng[it.key()] = so;
        }
        if (!eng.isEmpty()) {
            obj[QLatin1String("engines")] = eng;
        }

        QJsonObject fg;
        for (auto it = freeGeometryByScreen.constBegin(); it != freeGeometryByScreen.constEnd(); ++it) {
            const QRect& g = it.value();
            if (!g.isValid()) {
                continue;
            }
            QJsonObject go;
            go[QLatin1String("x")] = g.x();
            go[QLatin1String("y")] = g.y();
            go[QLatin1String("w")] = g.width();
            go[QLatin1String("h")] = g.height();
            fg[it.key()] = go;
        }
        if (!fg.isEmpty()) {
            obj[QLatin1String("freeGeo")] = fg;
        }

        obj[QLatin1String("seq")] = static_cast<double>(sequence);
        return obj;
    }

    static WindowPlacement fromJson(const QString& appId, const QJsonObject& obj)
    {
        WindowPlacement p;
        p.appId = appId;
        p.windowId = obj.value(QLatin1String("windowId")).toString();
        p.screenId = obj.value(QLatin1String("screen")).toString();
        p.virtualDesktop = obj.value(QLatin1String("desktop")).toInt();
        p.activity = obj.value(QLatin1String("activity")).toString();
        p.kind = clampWindowKindFromWire(obj.value(QLatin1String("kind")).toInt());

        const QJsonObject eng = obj.value(QLatin1String("engines")).toObject();
        for (auto it = eng.constBegin(); it != eng.constEnd(); ++it) {
            const QJsonObject so = it.value().toObject();
            EngineSlot s;
            s.state = so.value(QLatin1String("state")).toString();
            for (const QJsonValue& v : so.value(QLatin1String("zoneIds")).toArray()) {
                const QString z = v.toString();
                if (!z.isEmpty()) {
                    s.zoneIds.append(z);
                }
            }
            s.order = so.value(QLatin1String("order")).toInt(-1);
            if (!s.isEmpty()) {
                p.engines.insert(it.key(), s);
            }
        }

        const QJsonObject fg = obj.value(QLatin1String("freeGeo")).toObject();
        for (auto it = fg.constBegin(); it != fg.constEnd(); ++it) {
            const QJsonObject go = it.value().toObject();
            const QRect g(go.value(QLatin1String("x")).toInt(), go.value(QLatin1String("y")).toInt(),
                          go.value(QLatin1String("w")).toInt(), go.value(QLatin1String("h")).toInt());
            if (g.isValid()) {
                p.freeGeometryByScreen.insert(it.key(), g);
            }
        }

        p.sequence = static_cast<quint64>(obj.value(QLatin1String("seq")).toDouble());
        // fromJson is the primary producer of this flag, but NOT the only one:
        // WindowPlacementStore::record()'s append branch copies an incoming
        // record wholesale, so a persisted record replayed through record()
        // (deserialize) or re-recorded by takeForReopen carries the flag with
        // it. Both of those are startup-time or already-spent paths.
        //
        // Deserialize itself is not unique either. WTA::loadState runs from the
        // WindowTrackingAdaptor constructor, and again through the engines' load
        // delegate (wired for the snap, autotile and scroll engines) driven from
        // Daemon::finalizeStartup, so the store is loaded at least twice per
        // startup and every load re-arms every record. The flag's correctness
        // therefore rests on all of those calls being startup-time, which is why
        // it is ALSO cleared by the first live engine capture rather than
        // relying on the load being unique. There is no matching key in toJson:
        // the flag must not survive a save/load round trip.
        p.fromPersistedSession = true;
        return p;
    }
};

/// Shared cross-engine ownership predicate over a placement record: does this
/// record carry @p engineId's slot in its MANAGED state (@p managedState —
/// snap "snapped", autotile/scrolling "tiled") with a RECORDED screen that is
/// a DIFFERENT screen than the opening one, itself currently in that engine's
/// mode, resolved in the RECORD'S OWN (desktop, activity) context? A
/// same-screen record (or one with no screen of its own) is never
/// "cross-screen": the window is already where its managed slot lives, so the
/// engine owning the OPENING context claims it and the slot merely lies
/// dormant. Without the same-screen bail, a record whose (desktop, activity)
/// context differs from the opening one — a sticky window, or a per-desktop
/// mode split on one monitor — would defer here while the owner's reciprocal
/// gate also stands down, stranding the window unmanaged.
///
/// Every engine is BOTH a claimer and a deferrer through this one predicate:
/// the engine whose (engineId, managedState) matches the record — with the
/// recorded screen still in that engine's mode — claims the window
/// cross-screen (snap via resolveWindowRestore's recorded-screen restore, the
/// tiling engines via claimCrossScreenReopen), and every OTHER engine's open
/// path stands down. KWin's session restore opens windows on a
/// nondeterministic output, so a window whose record homes it on monitor A
/// routinely arrives on monitor B; without the reclaim it strands there,
/// unmanaged, on whatever engine owns B (the login-restore
/// windows-on-the-wrong-monitor bug). Every engine must reach its verdict
/// from the same record or the window ends up both-claimed or both-skipped.
/// Keying on the record's context (not each engine's live current desktop,
/// which can differ under per-screen virtual-desktop overrides) plus running
/// this ONE predicate on every side makes the N-way agreement hold by
/// construction: mode is exclusive per (screen, desktop, activity), so at
/// most one engine's mode check passes for the recorded home. A new tiling
/// engine must add both the defer gate and the claim.
///
/// Callers must pass a MANAGED token as @p managedState (stateSnapped /
/// stateTiled) — the function itself matches whatever token it is handed.
/// Float restore is screen-local by doctrine (it restores a position WITHIN
/// the monitor KWin chose, never moves the window across monitors), so a
/// FLOATING state must never be used to earn a cross-screen pull.
///
/// A matched slot is NOT proof of current ownership. Engine slots are only
/// ever merged, never cleared on ordinary close (a window that closed tiled
/// keeps its slot — that persistence IS what login restore reads), and an
/// engine that knowingly gives a window up mid-session must clear its slot
/// via WindowPlacementStore::clearEngineSlot, or the stale slot plus the
/// record-level screenId will read as a home to pull the window back to.
/// The record-level screenId is likewise only as fresh as the last owning
/// engine's successful capture; engine-miss captures leave it stale. Both
/// halves are why claimants must validate the verdict against LIVE state
/// (live screen set, membership after adoption) rather than trusting the
/// record alone.
///
/// @p isEngineMode is invoked as (screenId, virtualDesktop, activity) → bool
/// and must answer whether that context resolves to @p engineId's mode;
/// callers wrap their layout-manager mode lookup (and any null-manager
/// permissiveness) in it, keeping this library free of the zones-layer mode
/// type.
/// Whether @p windowId carries a stable, FIFO-matchable appId — the guard
/// every cross-engine gate and claim runs before consulting the placement
/// store. A bare id (no `appId|uuid` composite, so extractAppId returns the
/// id itself) or an empty appId has no bucket: no record can match it, and
/// treating it as matchable would let a degenerate id slip past the
/// same-screen bail (empty compares unequal to every screen). Spelled once
/// here because five call sites across three engines each carried the same
/// two-term test inline, and a sixth engine would get it wrong by omission.
inline bool hasStableAppIdFor(const QString& appId, const QString& windowId)
{
    return !appId.isEmpty() && appId != windowId;
}

/// Whether a record's own (desktop, activity) context is compatible with the
/// live context an engine would INSERT it into — the guard a cross-screen
/// claim needs because the reclaim VERDICT is granted on the record's context
/// while the adoption keys the window by the home screen's CURRENT context.
/// Without it, a window recorded on desktop 3 is reclaimed onto the strip or
/// layout of whatever desktop that screen happens to show, displacing that
/// desktop's windows while KWin still has the window on its own.
///
/// The sentinels are honoured rather than compared: `virtualDesktop == 0`
/// means all-desktops / sticky / unknown and an empty activity means
/// all-activities / unknown (see WindowPlacement::virtualDesktop), so those
/// records are compatible with any live context — the same
/// desktop-agnostic reading LayoutRegistry::modeForScreen gives them, which
/// is what granted the verdict in the first place. Only a CONCRETE recorded
/// context that disagrees with a concrete live one refuses.
inline bool recordContextMatchesLive(const WindowPlacement& p, int liveDesktop, const QString& liveActivity)
{
    if (p.virtualDesktop != 0 && liveDesktop != 0 && p.virtualDesktop != liveDesktop) {
        return false;
    }
    return p.activity.isEmpty() || liveActivity.isEmpty() || p.activity == liveActivity;
}

template<typename IsEngineMode>
bool pendingCrossScreenManagedRestore(const WindowPlacement& p, QLatin1String engineId, QLatin1String managedState,
                                      const QString& openingScreenId, IsEngineMode&& isEngineMode)
{
    if (p.slotFor(engineId).state != managedState) {
        return false;
    }
    if (p.screenId.isEmpty() || p.screenId == openingScreenId) {
        return false; // same screen (or unscreened): the opening context's engine owns it
    }
    return isEngineMode(p.screenId, p.virtualDesktop, p.activity);
}

/// The snap-engine specialization of pendingCrossScreenManagedRestore — the
/// original three-way gate (SnapEngine claims, AutotileEngine and ScrollEngine
/// defer). Kept as a named form because "snapped snap-slot on a
/// snapping-mode home" is the verdict three call sites spell.
template<typename IsSnappingMode>
bool pendingCrossScreenSnapRestore(const WindowPlacement& p, const QString& openingScreenId,
                                   IsSnappingMode&& isSnappingMode)
{
    return pendingCrossScreenManagedRestore(p, WindowPlacement::snapEngineId(), WindowPlacement::stateSnapped(),
                                            openingScreenId, std::forward<IsSnappingMode>(isSnappingMode));
}

} // namespace PhosphorEngine
