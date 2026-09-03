// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Engine relay entry points of TilingAdaptor: the composition root connects
// every pipeline engine's outbound signals here and the adaptor re-emits
// them on the D-Bus surface. Split from tilingadaptor.cpp by concern; the
// lifecycle (open / close / focus / release) dispatch stays there.

#include "tilingadaptor.h"

#include "core/platform/logging.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorScreens/Manager.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <limits>
#include <utility>

namespace PlasmaZones {

void TilingAdaptor::relayTileRequestsJson(const QString& tileRequestsJson)
{
    if (tileRequestsJson.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(tileRequestsJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcDbusTiling) << "relayTileRequestsJson: invalid JSON:" << parseError.errorString();
        return;
    }

    PhosphorProtocol::TileRequestList requests;
    QSet<QString> seenWindowIds;
    bool sawValidatorDrop = false;
    const QJsonArray batchEntries = doc.array();
    for (const QJsonValue& val : batchEntries) {
        QJsonObject obj = val.toObject();
        PhosphorProtocol::TileRequestEntry entry;
        entry.windowId = obj.value(QLatin1String("windowId")).toString();
        // One entry per window per batch: the effect applies entries in
        // order, so a duplicate would apply twice (last wins) and, with
        // scrollEdge now driving the animation anchor, two entries naming
        // different edges would animate the window in from one side and
        // re-anchor it to the other — and with windowedFullscreen in the
        // struct, WHICH duplicate wins now decides KWin fullscreen state on
        // the client, not just an animation anchor. No producer emits
        // duplicates today; this is boundary hardening. First VALID entry
        // wins: the id is recorded after the validation bail below, so a
        // malformed first entry does not consume the window's slot and shut
        // out a good second one.
        if (seenWindowIds.contains(entry.windowId)) {
            qCDebug(lcDbusTiling) << "relayTileRequestsJson: dropping duplicate entry for" << entry.windowId;
            continue;
        }
        entry.floating = obj.value(QLatin1String("floating")).toBool(false);
        if (!entry.floating) {
            entry.x = obj.value(QLatin1String("x")).toInt();
            entry.y = obj.value(QLatin1String("y")).toInt();
            entry.width = obj.value(QLatin1String("width")).toInt();
            entry.height = obj.value(QLatin1String("height")).toInt();
        }
        // No geometry pre-check here: validationError() below owns the
        // degenerate-rect rejection for non-floating entries (its coverage
        // is a strict superset of the old `width <= 0 || height <= 0`
        // check), and routing the drop through it logs at qCWarning — a
        // producer emitting a zero rect is producer garbling and a bug
        // report by this boundary's own policy, not debug noise.
        // zoneId is NOT parsed. The protocol declares it reserved and always
        // empty on this wire — no producer writes it and no consumer reads
        // it — so parsing it only gave a foreign producer a field to put
        // arbitrary string data in, riding all the way to the compositor.
        entry.screenId = obj.value(QLatin1String("screenId")).toString();
        entry.monocle = obj.value(QLatin1String("monocle")).toBool(false);
        // Scrolling windowed fullscreen. Only meaningful on a tiled entry;
        // the floating pair is rejected by validationError() below like any
        // other garbling.
        entry.windowedFullscreen = obj.value(QLatin1String("windowedFullscreen")).toBool(false);
        // Scrolling column maximize. Same shape as the flag above: only
        // meaningful on a tiled entry, and validationError() below rejects
        // the floating and monocle pairs.
        entry.maximizedToEdges = obj.value(QLatin1String("maximizedToEdges")).toBool(false);
        entry.stacking = obj.value(QLatin1String("stacking")).toString();
        entry.scrollEdge = obj.value(QLatin1String("scrollEdge")).toString();
        // Absent for every non-scrolling producer, and absent within scrolling
        // for a window the view does not carry — both mean zero, which is what
        // the default gives. Dropped on a floating entry like the other paint
        // hints: the engine only ever writes it inside the tiled emit loop,
        // and a garbled floating delta could seed a view spring for a screen
        // whose strip never moved.
        if (!entry.floating) {
            entry.viewDelta = obj.value(QLatin1String("viewDelta")).toInt(0);
        } else if (obj.contains(QLatin1String("viewDelta"))) {
            qCDebug(lcDbusTiling) << "relayTileRequestsJson: dropping viewDelta on floating entry" << entry.windowId;
        }
        // Set only by the scroll engine's drag edge auto-scroll batches;
        // absent everywhere else, which is what the default gives. Dropped
        // on a floating entry like its sibling paint hints below (visual
        // position, tabFrom): a floating entry carries no view membership,
        // and the effect additionally gates the flag on viewDelta != 0, so
        // a garbled floating viewImmediate must not survive the boundary.
        if (!entry.floating) {
            entry.viewImmediate = obj.value(QLatin1String("viewImmediate")).toBool(false);
        } else if (obj.contains(QLatin1String("viewImmediate"))) {
            qCDebug(lcDbusTiling) << "relayTileRequestsJson: dropping viewImmediate on floating entry"
                                  << entry.windowId;
        }
        // Present only for a parked scrolling column; absent means the
        // committed rect IS the paint position.
        //
        // BOTH keys, both numeric, and never on a floating entry. This is an
        // unmarshal boundary, so it validates rather than coerces: presence of
        // visualX alone would let visualY default to 0 and paint the column at
        // the top of the screen, a non-numeric value would do the same while
        // still latching the flag, and a floating entry skips the geometry
        // parse above so its committed rect is (0,0,0,0) — the effect computes
        // the paint translation against that rect, so a visual position paired
        // with it is meaningless. The engine emits none of these; the point is
        // that a garbled payload fails closed instead of mispainting.
        const QJsonValue visualXVal = obj.value(QLatin1String("visualX"));
        const QJsonValue visualYVal = obj.value(QLatin1String("visualY"));
        bool visualPosValid = false;
        if (!entry.floating && visualXVal.isDouble() && visualYVal.isDouble()) {
            // Integral check by value, not by type: QJsonValue has no
            // integer predicate (isDouble() answers for every number) and
            // toInt() returns its DEFAULT for a fractional double — so
            // without the floor test a 4000.5 would decode to 0 while still
            // latching the flag, exactly the mispaint the validation above
            // promises to fail closed on.
            const double vx = visualXVal.toDouble();
            const double vy = visualYVal.toDouble();
            if (vx == std::floor(vx) && vy == std::floor(vy) && std::abs(vx) <= double(std::numeric_limits<int>::max())
                && std::abs(vy) <= double(std::numeric_limits<int>::max())) {
                entry.visualX = static_cast<int>(vx);
                entry.visualY = static_cast<int>(vy);
                entry.hasVisualPos = true;
                visualPosValid = true;
            }
        }
        if (!visualPosValid && (!visualXVal.isUndefined() || !visualYVal.isUndefined())) {
            qCDebug(lcDbusTiling) << "relayTileRequestsJson: ignoring malformed visual position for" << entry.windowId;
        }
        // The tab this entry is replacing, present only on a tab being
        // activated in a tabbed column. Never on a floating entry: a floating
        // window is not a tab of anything, and its committed rect is
        // (0,0,0,0), so the cross-fade the field asks for would have no rect
        // to run in. Dropped rather than failing the entry — this is a paint
        // hint, and losing it costs only the cross-fade, exactly like the
        // visual position above.
        if (!entry.floating) {
            entry.tabFrom = obj.value(QLatin1String("tabFrom")).toString();
        } else if (obj.contains(QLatin1String("tabFrom"))) {
            qCDebug(lcDbusTiling) << "relayTileRequestsJson: dropping tabFrom on floating entry" << entry.windowId;
        }
        // The protocol type ships its own validator (empty windowId /
        // screenId, degenerate rect, a tabFrom naming its own window) — run
        // it rather than re-deriving a subset of its checks here.
        if (const QString validationError = entry.validationError(); !validationError.isEmpty()) {
            sawValidatorDrop = true;
            // Warning, not debug: every documented cause of a validator
            // failure at this boundary is producer garbling, and the drop
            // silently discards a whole placement — the event is by
            // construction a bug report, not noise. (The duplicate and
            // malformed-visual-position drops above stay at debug; both
            // have documented benign shapes.)
            qCWarning(lcDbusTiling) << "relayTileRequestsJson: dropping entry:" << validationError;
            continue;
        }
        seenWindowIds.insert(entry.windowId);
        requests.append(entry);
    }

    // A fully-rejected batch must not be indistinguishable from a batch that
    // was never produced. No partial-count aggregate on purpose: every
    // non-benign drop already warns individually at its entry, and a count
    // computed from the array size would fold the BENIGN drops (duplicates
    // and the malformed-visual-position bail, both deliberately debug-level;
    // degenerate rects route through validationError and warn like any
    // other validator drop) into a warning that reads as suppressed errors.
    // The same reasoning gates the aggregate itself on a validator drop
    // having occurred: a batch emptied entirely by benign drops is not
    // suppressed errors either.
    if (requests.isEmpty() && !batchEntries.isEmpty()) {
        if (sawValidatorDrop) {
            qCWarning(lcDbusTiling) << "relayTileRequestsJson: every entry of a" << batchEntries.size()
                                    << "entry batch was dropped — nothing emitted";
        } else {
            qCDebug(lcDbusTiling) << "relayTileRequestsJson: every entry of a" << batchEntries.size()
                                  << "entry batch was benignly dropped — nothing emitted";
        }
    }
    if (requests.isEmpty()) {
        return;
    }
    // ORDERING CONTRACT with the coalesced screens announce: a batch must
    // never reach the wire ahead of an announce that was queued before it.
    // The effect answers a desktop-switch announce by bumping its global
    // stagger epoch, which voids every in-flight staggered apply — the
    // guard exists so geometry resolved for the OLD desktop never lands on
    // the new one. A batch the engines resolved AFTER the context switch
    // (a focus report that arrives in the same dispatch as the desktop
    // report reanchors the strip synchronously, and its relayout emits
    // right here, before the 0ms announce has fired) is not that batch, but
    // the effect cannot tell the two apart: it applied the first entry,
    // then the announce landed and the guard dropped the rest. Three
    // columns, focus the middle one from another desktop, and the focused
    // column sat at its old rect while its neighbour had already moved —
    // and the engine's emit-on-change baseline believed the batch landed,
    // so nothing ever re-sent it. Holding the batch until the announce has
    // gone out puts the two on the wire in the order the effect's guard
    // assumes. The flush runs inside the announce lambda, right after the
    // emit, so a held batch trails its announce by nothing observable.
    if (m_screensAnnouncePending) {
        qCDebug(lcDbusTiling) << "relayTileRequestsJson: holding a" << requests.size()
                              << "window batch behind the pending screens announce";
        m_tileBatchesHeldForAnnounce.append(std::move(requests));
        return;
    }
    qCDebug(lcDbusTiling) << "Emitting windowsTileRequested:" << requests.size() << "windows";
    Q_EMIT windowsTileRequested(requests);
}

void TilingAdaptor::flushTileBatchesHeldForAnnounce()
{
    // Emission order is arrival order: the effect applies batches in
    // sequence and a later batch for the same screen supersedes an earlier
    // one through its per-screen generation, so reordering two held batches
    // would let the older rects win.
    const auto held = std::exchange(m_tileBatchesHeldForAnnounce, {});
    for (const PhosphorProtocol::TileRequestList& requests : held) {
        qCDebug(lcDbusTiling) << "Emitting windowsTileRequested:" << requests.size()
                              << "windows (held behind the screens announce)";
        Q_EMIT windowsTileRequested(requests);
    }
}

void TilingAdaptor::relayWindowsReleased(const QStringList& windowIds)
{
    // The one relay with no change gate — every in-tree producer already
    // gates on a non-empty list, so this belt only keeps a future producer
    // from putting pure noise on the bus (the XML documents that no
    // effect-side subscriber exists today).
    if (windowIds.isEmpty()) {
        return;
    }
    Q_EMIT windowsReleasedFromTiling(windowIds);
}

void TilingAdaptor::notifyEngineScreensChanged(bool isDesktopSwitch)
{
    // Coalesce (see header doc): a mode flip fires this once per engine in
    // one synchronous pass; emitting eagerly would broadcast an intermediate
    // union that momentarily drops the flipping screen and triggers the
    // effect's full restore path. Defer to the event loop so one emission
    // carries the pass's final state.
    m_pendingIsDesktopSwitch = m_pendingIsDesktopSwitch || isDesktopSwitch;
    if (m_screensAnnouncePending) {
        return;
    }
    m_screensAnnouncePending = true;
    QMetaObject::invokeMethod(
        this,
        [this, generation = m_announceGeneration]() {
            // Deliberately redundant with the empty-pipeline bail below:
            // either alone produces the same silence, so no test can tell them
            // apart — and that is the point, since they guard different things
            // (a superseded announce versus a torn-down pipeline) and the pair
            // is one deletion away from unguarded.
            if (generation != m_announceGeneration) {
                return; // clearEngine voided this session's announce
            }
            m_screensAnnouncePending = false;
            const bool desktopSwitch = m_pendingIsDesktopSwitch;
            m_pendingIsDesktopSwitch = false;
            // A queued announce that fires after clearEngine() (shutdown)
            // must NOT broadcast an empty union — the effect would treat it
            // as a genuine disable and run its destructive per-window
            // teardown against a daemon that is merely restarting.
            if (m_lifecycleEngines.isEmpty()) {
                // The batches held behind this announce belong to the same
                // dead session; clearEngine already swept them, and this
                // sweep is redundant with it the way the bail itself is.
                m_tileBatchesHeldForAnnounce.clear();
                return;
            }
            // Stamp the announce with the desktop each screen was resolved
            // against. The receiver needs it to reject a late announce (see
            // the signal's doc); read here, at emit time, so it describes the
            // set actually being sent rather than whatever was current when
            // the announce was first queued.
            const QStringList announced = combinedManagedScreens();
            QVariantMap screenDesktops;
            if (m_windowTrackingAdaptor) {
                // Stamp every screen the daemon can resolve a desktop for, NOT
                // just the announced (still-managed) ones. The receiver's
                // destructive half — the demote/pre-tile-restore pass — is
                // driven by the REMOVED set, i.e. precisely the screens absent
                // from `announced`. Stamping only the survivors left those
                // screens unstamped, and the receiver compares just the keys it
                // was given, so the announce whose whole effect is the restore
                // was the one announce that could never be rejected as stale.
                // In the limiting case (switching to a desktop where nothing is
                // managed) `announced` is empty, the map was empty, and the
                // receiver skipped its staleness gate outright.
                //
                // The cost is a wider gate: the receiver rejects on ANY key
                // that disagrees, so with per-output desktops (#648) a screen
                // uninvolved in this switch can now veto an announce whose
                // managed screens were all in agreement. That is deliberate.
                // Rejection converges (the daemon re-announces for the desktop
                // the effect has since reported) while a missed rejection does
                // not, and the announce this stamp exists to make rejectable
                // is the one that runs the destructive restore pass.
                QStringList stampable = announced;
                if (m_screenManager) {
                    for (const QString& screenId : m_screenManager->effectiveScreenIds()) {
                        if (!stampable.contains(screenId)) {
                            stampable.append(screenId);
                        }
                    }
                }
                for (const QString& screenId : std::as_const(stampable)) {
                    screenDesktops.insert(screenId, m_windowTrackingAdaptor->currentDesktopForScreen(screenId));
                }
            }
            Q_EMIT managedScreensChanged(announced, desktopSwitch, screenDesktops);
            relayEnabledChanged();
            // Tile batches that arrived while this announce was queued go
            // out NOW, behind it (see relayTileRequestsJson's ordering
            // contract). Before the parked-open retry below: a retry can
            // itself produce a batch, and that one must trail the held
            // ones, which the engines resolved first.
            flushTileBatchesHeldForAnnounce();
            // Retry opens parked during the flip (see m_unclaimedOpens):
            // engines have their post-flip screen sets by now. Insertion
            // order is preserved (column order / master assignment depend
            // on it), routing is NOT re-run (already baked into the parked
            // entry), and a still-unclaimed entry is dropped rather than
            // re-parked.
            if (!m_unclaimedOpens.isEmpty()) {
                const auto parked = std::exchange(m_unclaimedOpens, {});
                // Burst bracket, same as windowsOpenedBatch: a flip can park a
                // whole screen's opens, and replaying them per-arrival would
                // march the strip through the partial intermediates the
                // bracket exists to suppress.
                for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
                    engine->beginArrivalBurst();
                }
                for (const auto& parkedOpen : parked) {
                    dispatchOpenToClaimingEngine(parkedOpen.entry, /*allowPark=*/false,
                                                 parkedOpen.allowCrossScreenClaim);
                }
                for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
                    engine->endArrivalBurst();
                }
            }
        },
        Qt::QueuedConnection);
}

void TilingAdaptor::relayEnabledChanged()
{
    // Dedup: two engines feed one signal, and every screen-set change on
    // either would otherwise re-broadcast an unchanged bool.
    const bool now = enabled();
    if (m_lastEnabledBroadcast.has_value() && *m_lastEnabledBroadcast == now) {
        return;
    }
    m_lastEnabledBroadcast = now;
    Q_EMIT enabledChanged(now);
}

} // namespace PlasmaZones
