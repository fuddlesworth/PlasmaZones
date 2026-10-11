// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorIdentity/ScreenId.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <core/output.h>
#include <effect/effecthandler.h>
#include <virtualdesktops.h>
#include <window.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QStringList>

#include <climits>

#include "tilinghandler/tilinghandler.h"
#include "handlers/screenchangehandler.h"
#include "compositor/compositorclock.h"
#include "compositor/stripviewanimator.h"
#include "compositor/windowanimator.h"

namespace PlasmaZones {

namespace {

// Drop every entry whose key is not a live physical screen id. Both screen handlers rebuild
// connectedPhysicalIds eagerly and then call this, because a screen id is not stable across a
// hotplug: plugging an identical twin gives a monitor's id a connector suffix and unplugging the
// twin takes it away again, so a cache keyed by the id accumulates one orphan per spelling change
// that a single remove-by-current-id can never reach. Pruning a still-live screen whose spelling
// just changed is correct too, not collateral: its old key IS stale.
//
// TWO READERS, and the second is why this is a prune rather than a clear. reportScreenDesktop's
// dedup pays one non-deduplicated report for a dropped key, which is nothing. But
// lastReportedScreenDesktops() also feeds PreTileDecisions::announceMatchesReportedDesktops,
// whose contract is that a MISSING key is not a mismatch — so a dropped key widens that vacuous
// accept for as long as it is absent. What closes it is not a daemon re-announce, which is the
// thing being GATED rather than a writer of this map: the gate arms only for a desktop switch,
// and the effect's own desktopChanged handler calls reportScreenDesktop, re-inserting the key
// before the daemon's announce for that switch can come back. The alternative, keeping a spelling
// no consumer can match, is worse. Do not turn this into a clear.
void pruneToLiveScreens(QHash<QString, int>& cache, const QSet<QString>& live)
{
    for (auto it = cache.begin(); it != cache.end();) {
        if (live.contains(it.key())) {
            ++it;
        } else {
            it = cache.erase(it);
        }
    }
}

} // namespace

// @p excluded is left out of the duplicate scan below, and exists for ONE caller:
// onScreenRemoved's eager rebuild. The failure it was written for is the #724 family: resolving
// the SURVIVOR of two identical monitors in its disambiguated baseId/connector form while the
// twin is still there to collide with, caching that, and seeding connectedPhysicalIds with it,
// while the daemon resolves after the unplug with one monitor present and produces the
// unsuffixed base id. The two sides then disagree about the screen's name, and the concrete
// break is scrollTrackedScreenFor, which gates on connectedPhysicalIds().contains(...) and fails
// OPEN for the paint clip and the input filter when the id misses.
//
// THE EXCLUSION CANNOT FIRE ON EITHER EMIT SITE, and two earlier versions of this note each got
// that wrong in a different direction. It does NOT run "while KWin still lists the dying output":
// Workspace::updateOutputs clears and rebuilds m_outputs from the surviving backend outputs
// BEFORE emitting outputRemoved, so screens() is already pruned and `other == excluded` never
// matches. Nor is it saved by the ~Workspace emit, which is the only other site and does iterate
// m_outputs intact: that emit reaches nobody, because ~ApplicationWayland unloads every effect
// and then deletes the EffectsHandler (taking the outputRemoved forward with it) before it
// destroys the Workspace. Upstream states the discipline on that delete: effect windows outlive
// effects. So the parameter is belt-and-braces on every reachable path. It stays because it is
// one pointer compare and it pins the #724 spelling invariant locally against an upstream
// reordering of updateOutputs. Removing it would also take the `other == excluded` branch below
// and the caching note further down, so remove all three together or none.
//
// HONOURED ON A CACHE MISS ONLY. The screenIdCache lookup below runs before @p excluded is
// looked at, so a warm entry is returned whatever the caller passed. That is safe for the one
// caller because onScreenRemoved calls clearScreenIdCache() immediately before its rebuild loop
// and nothing repopulates the cache in between — it is NOT a property of this function. A
// second caller that wants the exclusion has to clear the cache first, or it gets the
// unexcluded answer with no signal that it did.
QString PlasmaZonesEffect::outputScreenId(const KWin::LogicalOutput* output, const KWin::LogicalOutput* excluded) const
{
    if (!output) {
        return QString();
    }
    const QString connectorName = output->name();

    // Cache: screen IDs are stable for the lifetime of an output. Caching avoids
    // repeated QGuiApplication::screens() iteration and sysfs reads (~30Hz during drag).
    // Invalidated on screen add/remove (m_idCaches.screenIdCache cleared by screen change handler).
    auto it = m_idCaches.screenIdCache.constFind(connectorName);
    if (it != m_idCaches.screenIdCache.constEnd()) {
        return *it;
    }

    // Build a screen ID that exactly matches the daemon's PhosphorScreens::ScreenIdentity::identifierFor().
    // Uses PhosphorIdentity::ScreenId for hex normalization and sysfs EDID
    // fallback, ensuring byte-identical output across daemon and compositor processes.
    //
    // Try QScreen::serialNumber() first (same source as daemon), then sysfs fallback.
    QString serialNumber;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == connectorName) {
            serialNumber = screen->serialNumber();
            break;
        }
    }

    const QString baseId = PhosphorIdentity::ScreenId::buildScreenBaseId(output->manufacturer(), output->model(),
                                                                         serialNumber, connectorName);

    // Disambiguate identical monitors: if another screen produces the same base ID,
    // append "/ConnectorName" to make each unique. Mirrors daemon's screenIdentifier().
    //
    // The comparison ids are built from the OTHER KWin outputs, from exactly the
    // sources the primary id above uses (the output's own manufacturer / model /
    // connector, with the serial taken from the matching QScreen). A QScreen-derived
    // comparison could not match: inside the compositor QScreen::manufacturer() and
    // model() are empty, so every id it produced degraded to a serial-only or
    // connector-name form and no genuine duplicate pair was ever detected.
    bool hasDuplicate = false;
    for (const auto* other : KWin::effects->screens()) {
        if (!other || other == excluded || other->name() == connectorName) {
            continue;
        }
        const QString otherConnector = other->name();
        QString otherSerial;
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screen->name() == otherConnector) {
                otherSerial = screen->serialNumber();
                break;
            }
        }
        if (PhosphorIdentity::ScreenId::buildScreenBaseId(other->manufacturer(), other->model(), otherSerial,
                                                          otherConnector)
            == baseId) {
            hasDuplicate = true;
            break;
        }
    }

    QString result = hasDuplicate ? baseId + QLatin1Char('/') + connectorName : baseId;
    // An EXCLUDED resolve is cached too, and the honest reason is narrow: this insert runs on
    // every resolve whatever @p excluded was, so the entry is a by-product rather than a choice.
    // It is harmless because the excluded and unexcluded answers are identical here, the dying
    // output being already pruned from the list the duplicate scan walks. An earlier version said
    // the caching stopped a later unexcluded caller "recomputing the suffixed form from a
    // screens() list that still holds the dying twin"; that list does not hold it.
    m_idCaches.screenIdCache.insert(connectorName, result);
    // Recorded per OUTPUT as well, on the miss path only. onScreenRemoved reads this instead of
    // recomputing, because a recompute is only reproducible while the peer set is intact. The
    // cache-hit early return above deliberately does not write: clearScreenIdCache runs on every
    // add, so a connector cannot be warm under a different output than the one that warmed it, and
    // writing on the hit path would add a hash insert to the ~30 Hz drag path this cache protects.
    m_idCaches.screenIdByOutput.insert(output, result);
    return result;
}

void PlasmaZonesEffect::reportScreenDesktop(const QString& screenId, int desktop)
{
    if (screenId.isEmpty() || desktop < 1) {
        return;
    }
    // Dedup KWin's per-output desktopChanged — only forward a genuine change.
    // m_lastScreenDesktop is updated even when the daemon service isn't
    // registered yet; the bringup re-sync (daemon_bringup.cpp) re-pushes every
    // screen's authoritative desktop after (re)registration, so a missed live
    // report here is recovered there.
    if (m_lastScreenDesktop.value(screenId, -1) == desktop) {
        return;
    }
    m_lastScreenDesktop.insert(screenId, desktop);
    if (m_daemonGate.serviceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("screenDesktopChanged"), {screenId, desktop});
    }
}

// Resolve the monitor by the window's POSITION, not by w->screen(). KWin can
// assign a window the wrong one of two identical-model outputs, so trusting
// w->screen() made the effect disagree with the daemon about which monitor a
// window sits on, which then bounced a snapped window off to the other monitor
// (Discussion #724). Mirrors the snap-assist path in snaphandler.cpp.
//
// screenAt() is NEAREST-output, not containment: upstream Workspace::outputAt
// clamps the point into each output's rect and keeps the smallest squared
// distance, so it answers for a point outside every output too and returns null
// only when the output list is empty. An earlier version of this comment said
// "the output whose geometry contains the window centre" and that the
// w->screen() fallback fires "when no output contains the centre (window fully
// off-screen mid-reconfigure)". Both were wrong: a fully off-screen window
// still resolves to the nearest output, and the fallback is therefore
// unreachable in a live session, and upstream ENFORCES that rather than it being
// luck: updateOutputs appends a PlaceholderOutput whenever the backend list comes
// back empty, under the comment "The workspace requires at least one output
// connected". The fallback stays because it costs one comparison and is cheaper to
// carry than the risk of tracking an upstream change to outputAt's empty-list
// behaviour, NOT because a zero-output case is reachable.
KWin::LogicalOutput* PlasmaZonesEffect::windowOutput(KWin::EffectWindow* w) const
{
    // KWIN::EFFECTS CANNOT BE NULL HERE, and this is the one place that says so, because
    // an earlier version of this comment claimed the opposite ("the gates in
    // surface_gating.cpp reach this on teardown paths where KWin::effects can already be
    // null") and three separate review passes took it at face value, each time reporting
    // the unguarded siblings below as null-deref bugs. The ordering is upstream's, in
    // kwin/src/effect/effecthandler.cpp, which the package does not install — only the
    // header is in /usr/include/kwin, which is why reading from inside this tree could
    // never settle it. EffectsHandler's constructor assigns KWin::effects = this before
    // queryAndLoadAll() loads any effect (the loader OBJECT is constructed earlier, in the
    // member-init list, so "before the loader runs" was loose), and its destructor is
    // `unloadAllEffects(); KWin::effects = nullptr;` in that order. unloadAllEffects goes
    // through destroyEffect(), which does a SYNCHRONOUS `delete effect;` rather than
    // deleteLater, so an effect object cannot outlive the handler and a queued delivery
    // cannot arrive after the global is cleared either. So every effect plugin is destroyed while the global is
    // still valid, and the global is null only when NoCompositing made the constructor
    // return early, in which case no effect was loaded and none of this code runs.
    // The `!KWin::effects` test stays because it costs nothing and a comparison is a
    // cheaper thing to carry than the risk of tracking an upstream teardown change. It is
    // belt-and-braces, NOT evidence that null is reachable: outputForScreenId,
    // connectedPhysicalIds and fetchAllVirtualScreenConfigs below deref the global
    // unguarded and are CORRECT to, and the guards downstream in tilinghandler/state.cpp
    // are equally dead. Do not raise those as findings, and do not add more of them.
    // An EffectWindow*, a LogicalOutput* or a findWindow() result is a different matter
    // and still needs its own check — this invariant is about the global only.
    if (!w || !KWin::effects) {
        return nullptr;
    }
    const QPointF cf = w->frameGeometry().center();
    const QPoint c(qRound(cf.x()), qRound(cf.y()));
    KWin::LogicalOutput* output = KWin::effects->screenAt(c);
    return output ? output : w->screen();
}

KWin::LogicalOutput* PlasmaZonesEffect::outputForScreenId(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return nullptr;
    }
    // Virtual screens subdivide one output, so match on the physical part.
    // No KWin::effects guard, deliberately: see the invariant at windowOutput above.
    const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
    for (const auto& output : KWin::effects->screens()) {
        if (outputScreenId(output) == physId) {
            return output;
        }
    }
    return nullptr;
}

const QSet<QString>& PlasmaZonesEffect::connectedPhysicalIds() const
{
    if (!m_idCaches.connectedPhysicalIdsValid) {
        m_idCaches.connectedPhysicalIds.clear();
        for (const auto* output : KWin::effects->screens()) {
            const QString physId = outputScreenId(output);
            if (!physId.isEmpty()) {
                m_idCaches.connectedPhysicalIds.insert(physId);
            }
        }
        m_idCaches.connectedPhysicalIdsValid = true;
    }
    return m_idCaches.connectedPhysicalIds;
}

QString PlasmaZonesEffect::getWindowScreenId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    // The id is built ONLY when the override below can actually consult it,
    // preserving the no-scrolling session's pure positional path (no id-cache
    // probe per call). An empty id reaching the overload means "no id
    // available" and skips the override exactly as this short-circuit does.
    return getWindowScreenId(w, m_tilingHandler->hasScrollingScreens() ? getWindowId(w) : QString());
}

QString PlasmaZonesEffect::getWindowScreenId(KWin::EffectWindow* w, const QString& windowId) const
{
    if (!w) {
        return QString();
    }
    // Engine-authoritative override for scroll-managed windows: the strip
    // parks off-viewport columns and hidden tabs below every output, inside
    // none, so a parked frame resolves to the nearest output, which can be a
    // NEIGHBOUR, and the position-derived resolution below would
    // misattribute the window (wrong minimize routing, wrong close/float
    // record, wrong Mode stamp). Scroll windows change screens only through
    // engine-driven handoffs, which update the tracked screen first.
    // hasScrollingScreens short-circuit keeps the common no-scrolling
    // session on the pure positional path (no id-cache lookups per call).
    // Both invariant gates (tiled membership AND connected output) live in
    // scrollTrackedScreenFor itself. m_tilingHandler is constructed first
    // and lives for the effect's lifetime (the VS re-resolve loop below
    // derefs it unguarded for the same reason).
    if (!windowId.isEmpty() && m_tilingHandler->hasScrollingScreens()) {
        const QString tracked = m_tilingHandler->scrollTrackedScreenFor(windowId);
        if (!tracked.isEmpty()) {
            return tracked;
        }
    }
    const QPointF cf = w->frameGeometry().center();
    const QPoint c(qRound(cf.x()), qRound(cf.y()));

    // Position-resolved output (see windowOutput). outputScreenId derives the
    // id from the KWin output's OWN EDID (manufacturer / model / connector),
    // which agrees with the daemon per-output — the #724 bug was only the
    // window→output trust. (QScreen can't be used here: inside the compositor
    // QScreen::manufacturer() / model() are empty, so a QScreen-derived id
    // degrades to "::serial".) A window filling a split output answers the
    // virtual screen it belongs to, not the one under the output's centre.
    KWin::LogicalOutput* const output = windowOutput(w);
    if (const QString filling = fillingWindowScreenId(w, windowId, output); !filling.isEmpty()) {
        return filling;
    }
    return resolveEffectiveScreenId(c, output);
}

QString PlasmaZonesEffect::pendingWindowScreenId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    const QString windowId = m_tilingHandler->hasScrollingScreens() ? getWindowId(w) : QString();
    // A strip column's screen is the engine's, never its position (see the
    // override in getWindowScreenId), in flight or not.
    if (!windowId.isEmpty() && !m_tilingHandler->scrollTrackedScreenFor(windowId).isEmpty()) {
        return getWindowScreenId(w, windowId);
    }
    // A window KWin has been asked to move that has not committed the move
    // yet sits where its LAST acked configure put it. Reading that position
    // after a newer request named another output answered the output the
    // window is leaving: a held "move to output" key (or a move reversed
    // inside one round trip) acks the intermediate output after the daemon
    // already stored the final one, and the report repointed or unsnapped the
    // window there. moveResizeGeometry is the geometry KWin last requested, so
    // while its POSITION differs from the committed frame the request is
    // where the window is going. A size-only difference is not a move (a
    // client that commits a smaller size than it was asked for keeps one for
    // good), and a user's own interactive move is the frame itself.
    if (KWin::Window* const kw = w->window(); kw && KWin::effects && !w->isUserMove() && !w->isUserResize()) {
        const QRectF pending = kw->moveResizeGeometry();
        if (pending.isValid() && pending.topLeft().toPoint() != w->frameGeometry().topLeft().toPoint()) {
            const QPointF cf = pending.center();
            const QPoint c(qRound(cf.x()), qRound(cf.y()));
            KWin::LogicalOutput* const output = KWin::effects->screenAt(c);
            // KWin's move of a maximized window carries its restore rect.
            if (const QString filling = fillingWindowScreenId(w, windowId, output ? output : windowOutput(w));
                !filling.isEmpty()) {
                return filling;
            }
            return resolveEffectiveScreenId(c, output ? output : windowOutput(w));
        }
    }
    return getWindowScreenId(w, windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual Screen Support
// ═══════════════════════════════════════════════════════════════════════════════

QString PlasmaZonesEffect::resolveEffectiveScreenId(const QPoint& pos, const KWin::LogicalOutput* output) const
{
    return resolveEffectiveScreenId(pos, outputScreenId(output));
}

QString PlasmaZonesEffect::resolveEffectiveScreenId(const QPoint& pos, const QString& physId) const
{
    if (physId.isEmpty()) {
        return physId;
    }

    // Check if this physical screen has virtual subdivisions
    auto it = m_virtualScreenDefs.constFind(physId);
    if (it == m_virtualScreenDefs.constEnd() || it->isEmpty()) {
        return physId; // No subdivisions, return physical ID
    }

    // Find which virtual screen contains the point.
    // Use exclusive-right/bottom semantics to match the daemon's containment check.
    // QRect::contains() uses inclusive-right, which causes boundary-pixel mismatches
    // between effect and daemon for abutting virtual screens.
    for (const auto& vs : *it) {
        const QRect& r = vs.geometry;
        if (pos.x() >= r.x() && pos.x() < r.x() + r.width() && pos.y() >= r.y() && pos.y() < r.y() + r.height()) {
            return vs.id;
        }
    }

    // Fallback: pick nearest virtual screen (covers rounding gaps)
    QString nearestVsId;
    int minDist = INT_MAX;
    for (const auto& vs : *it) {
        // Manhattan distance from point to nearest edge of the rect
        int dx = 0;
        int dy = 0;
        // Use exclusive-right/bottom (x + width, y + height) to match the
        // primary containment check above.  QRect::right()/bottom() return
        // inclusive values (x + width - 1), which would be off by 1px.
        const int exRight = vs.geometry.x() + vs.geometry.width();
        const int exBottom = vs.geometry.y() + vs.geometry.height();
        if (pos.x() < vs.geometry.left()) {
            dx = vs.geometry.left() - pos.x();
        } else if (pos.x() >= exRight) {
            dx = pos.x() - exRight;
        }
        if (pos.y() < vs.geometry.top()) {
            dy = vs.geometry.top() - pos.y();
        } else if (pos.y() >= exBottom) {
            dy = pos.y() - exBottom;
        }
        int dist = dx + dy;
        if (dist < minDist) {
            minDist = dist;
            nearestVsId = vs.id;
        }
    }
    if (!nearestVsId.isEmpty()) {
        return nearestVsId;
    }
    // Ultimate fallback (should never reach here)
    qCWarning(lcEffect) << "resolveEffectiveScreenId: no virtual screens found for" << physId;
    return physId;
}

void PlasmaZonesEffect::fetchVirtualScreenConfig(const QString& physicalScreenId, uint64_t generation)
{
    // Bump this physId's fetch sequence. The async reply below applies to
    // m_virtualScreenDefs only if this is still the latest fetch for the
    // screen — otherwise a slower reply for an older config could land last
    // and clobber a newer one (remove-then-readd raced through D-Bus).
    const uint64_t seq = ++m_daemonGate.vsFetchSeqPerPhysId[physicalScreenId];

    // A LIVE fetch is outstanding work on the shared virtualScreensReady gate, so it has to be
    // counted like the startup batch counts its own. onVirtualScreensChanged is a PER-SCREEN
    // signal closing ONE flag, so a reconfigure across several monitors issues several of these;
    // uncounted, the first reply reopened the gate while the rest were still in flight.
    if (generation == 0) {
        ++m_daemonGate.pendingLiveVsConfigReplies;
    }

    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Screen,
                                                   QStringLiteral("getVirtualScreenConfig"), {physicalScreenId}),
        this);
    QPointer<PlasmaZonesEffect> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [self, physicalScreenId, generation, seq](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (!self)
                    return;
                // Helper lambda: decrement pending counter and fire deferred processing when all done.
                // Only participates in the startup gate if generation != 0 (issued by fetchAllVirtualScreenConfigs)
                // and the generation matches the current one (not stale from a prior fetch cycle).
                // Captures self by value (QPointer copy) to avoid dangling reference.
                auto countdownVsGate = [self, generation]() {
                    if (generation == 0 || !self || self->m_daemonGate.vsConfigGeneration != generation) {
                        return;
                    }
                    if (self->m_daemonGate.pendingVsConfigReplies > 0
                        && --self->m_daemonGate.pendingVsConfigReplies == 0) {
                        // The gate opens only when NO vs-config work is outstanding, and a live
                        // fetch is work too. Kept as its own test rather than folded into the
                        // condition above, because the batch's keyspace reconcile below belongs to
                        // the batch finishing and must run whether or not a live fetch is still in
                        // flight — coupling them would have made a concurrent live reconfigure
                        // silently skip it.
                        if (self->m_daemonGate.pendingLiveVsConfigReplies == 0) {
                            self->m_daemonGate.virtualScreensReady = true;
                        }
                        // The screen-id keyspace just changed shape. Until these
                        // definitions landed, resolveEffectiveScreenId returned the
                        // PHYSICAL id for a subdivided monitor, while the daemon keys
                        // its published active layouts by EFFECTIVE (virtual) id — so
                        // any verdict resolved in that window matched a screen id that
                        // no longer describes the window, and cached it. Drop those
                        // verdicts and re-fold the decorations they baked into. Both
                        // are coalesced, so the multi-screen batch pays for one.
                        self->invalidateAllRuleCaches();
                        self->scheduleBorderSweep();
                        if (self->m_daemonGate.serviceRegistered) {
                            self->processDaemonReadyWindowState();
                        }
                    }
                };

                QDBusPendingReply<QString> reply = *w;

                // A newer fetch for this physId issued after this one makes
                // this reply stale: its payload describes a superseded
                // config. Drop it without touching m_virtualScreenDefs or
                // m_daemonGate.virtualScreensReady — the latest fetch's reply owns those
                // — but still run countdownVsGate so a startup batch's reply
                // tally isn't left hanging on the superseded call.
                const bool isLatest = self->m_daemonGate.vsFetchSeqPerPhysId.value(physicalScreenId) == seq;

                // Live VS-config changes (generation == 0) flip
                // m_daemonGate.virtualScreensReady = false in onVirtualScreensChanged so
                // window-screen-crossing detection pauses until the reply
                // lands. EVERY early-return below must restore the flag for
                // generation == 0 — otherwise an errored / stale / malformed
                // reply leaves the gate closed forever and VS crossings
                // silently stop being detected for that physical screen.
                // @p defsMutated says whether this path actually changed
                // m_virtualScreenDefs. The flag restore is unconditional (see the
                // contract above), but the invalidate+sweep is not: a superseded
                // reply leaves the keyspace exactly as it found it, so sweeping for
                // it would re-fold every decoration for nothing.
                // A live reply arriving while a startup batch is still outstanding
                // must NOT open the gate: the batch owns it and countdownVsGate is
                // the only thing entitled to flip it once the last reply lands.
                // Opening it here would let crossing detection run against a
                // half-populated m_virtualScreenDefs.
                const auto restoreReadyIfLive = [self, generation](bool defsMutated) {
                    if (generation != 0) {
                        return;
                    }
                    // DISCHARGE THIS REPLY FIRST, whatever it went on to decide. The decrement
                    // lives here rather than at each of the five return paths precisely because
                    // the contract above already obliges every one of them to reach this lambda,
                    // exactly once — so this is the one place that cannot be forgotten by a new
                    // early return, and cannot be run twice by an existing one.
                    if (self->m_daemonGate.pendingLiveVsConfigReplies > 0) {
                        --self->m_daemonGate.pendingLiveVsConfigReplies;
                    }
                    // BOTH counters, because the gate is one flag over two kinds of outstanding
                    // work. Testing only the batch's counter meant that with several monitors
                    // reconfigured at once — one per-screen signal each, one closed flag between
                    // them — the first reply to land reopened the gate for all the others, and a
                    // superseded reply for a single screen did the same. The crossing detector
                    // then read half-updated definitions, which is the phantom crossing this gate
                    // was added to stop.
                    if (self->m_daemonGate.pendingVsConfigReplies == 0
                        && self->m_daemonGate.pendingLiveVsConfigReplies == 0) {
                        self->m_daemonGate.virtualScreensReady = true;
                    }
                    if (!defsMutated) {
                        return;
                    }
                    // A LIVE reconfigure reaches here and never reaches
                    // countdownVsGate, which returns immediately for
                    // generation 0 — so the invalidate+sweep that batch path
                    // performs would otherwise not happen for the case it was
                    // written for. The screen-id keyspace has just changed
                    // shape, so cached ScreenId / ScreenOrientation /
                    // ActiveLayout verdicts resolved against the old shape are
                    // stale. Note only scheduleBorderSweep coalesces (via
                    // m_borderSweepPending); invalidateAllRuleCaches runs its
                    // layer reconcile per call, so a reconfigure touching N
                    // physical screens pays that N times. Wasteful, not wrong,
                    // and it early-returns entirely for a session with no
                    // animation, layer or exclusion rules.
                    self->invalidateAllRuleCaches();
                    self->scheduleBorderSweep();
                };

                if (reply.isError()) {
                    qCDebug(lcEffect) << "fetchVirtualScreenConfig: no virtual screens for" << physicalScreenId
                                      << reply.error().message();
                    const bool removed = isLatest && self->m_virtualScreenDefs.remove(physicalScreenId) > 0;
                    if (removed) {
                        // The monitor's children just stopped existing, so every
                        // window tracked against one of them holds an id that no
                        // longer resolves — same re-resolve the success path runs.
                        self->reresolveTrackedScreens();
                    }
                    countdownVsGate();
                    restoreReadyIfLive(removed);
                    return;
                }

                if (!isLatest) {
                    countdownVsGate();
                    restoreReadyIfLive(false);
                    return;
                }

                const QString json = reply.value();
                QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
                if (!doc.isObject()) {
                    const bool removed = self->m_virtualScreenDefs.remove(physicalScreenId) > 0;
                    if (removed) {
                        self->reresolveTrackedScreens();
                    }
                    countdownVsGate();
                    restoreReadyIfLive(removed);
                    return;
                }

                QJsonArray screens = doc.object().value(QLatin1String("screens")).toArray();

                // Look up the physical output geometry ONCE rather than per VS definition (O(N) vs O(N*M))
                //
                // KWin::effects tested only as belt-and-braces: it CANNOT be null while this
                // effect object lives — see the invariant at windowOutput above, which is the
                // one place in the tree that states it. The earlier version of this comment
                // argued the opposite, that `self` being QPointer-alive says nothing about the
                // global, and that argument is what three review passes cited to report the
                // unguarded siblings as bugs. A null global would skip the loop
                // entirely, leaving physGeom default-constructed, and the arm below returns.
                QRect physGeom;
                if (KWin::effects) {
                    for (const auto* out : KWin::effects->screens()) {
                        if (self->outputScreenId(out) == physicalScreenId) {
                            physGeom = out->geometry();
                            break;
                        }
                    }
                }

                // RETURN, which is what the warning has always claimed. Falling through
                // left every def.geometry default-constructed, so none was admitted below,
                // `defs` came out empty, and the empty-defs arm REMOVED this monitor's
                // virtual-screen definitions — a wipe, reported as a skip, followed by a
                // re-resolve and a full rule-cache invalidate. Nothing re-fetched on
                // reconnect either, because the stored defs the reconnect would compare
                // against were gone. The genuine hot-unplug case has its own pruner in
                // fetchAllVirtualScreenConfigs, which erases defs for physical ids no
                // longer in screens(), so dropping them here was a second and silent one.
                //
                // Shaped like the !isLatest arm above: both side-effect obligations are
                // discharged, since the gate countdown has to happen on every reply and the
                // generation-0 ready restore is unconditional.
                if (!physGeom.isValid()) {
                    qCWarning(lcEffect) << "Physical output" << physicalScreenId
                                        << "not found (hot-unplug?) — skipping VS config update;"
                                        << "will re-fetch on reconnect";
                    countdownVsGate();
                    restoreReadyIfLive(false);
                    return;
                }

                QVector<EffectVirtualScreenDef> defs;
                for (const QJsonValue& val : screens) {
                    QJsonObject obj = val.toObject();
                    QJsonObject region = obj.value(QLatin1String("region")).toObject();

                    EffectVirtualScreenDef def;
                    def.id = obj.value(QLatin1String("id")).toString();

                    // Compute absolute geometry from fractional region within physical
                    // screen. No isValid() test: the arm above returns on an invalid
                    // physGeom now, so a guard here would be dead and would re-imply that
                    // an invalid one reaches this loop.
                    qreal rx = region.value(QLatin1String("x")).toDouble();
                    qreal ry = region.value(QLatin1String("y")).toDouble();
                    qreal rw = region.value(QLatin1String("width")).toDouble();
                    qreal rh = region.value(QLatin1String("height")).toDouble();
                    // Edge-consistent rounding: compute edges then derive width/height
                    // to avoid 1px gaps between abutting virtual screens
                    int left = physGeom.x() + qRound(rx * physGeom.width());
                    int top = physGeom.y() + qRound(ry * physGeom.height());
                    int right = physGeom.x() + qRound((rx + rw) * physGeom.width());
                    int bottom = physGeom.y() + qRound((ry + rh) * physGeom.height());
                    def.geometry = QRect(left, top, right - left, bottom - top);

                    if (def.geometry.isValid() && !def.id.isEmpty()) {
                        defs.append(def);
                    }
                }

                bool defsMutated = true;
                if (defs.isEmpty()) {
                    defsMutated = self->m_virtualScreenDefs.remove(physicalScreenId) > 0;
                } else {
                    qCInfo(lcEffect) << "Loaded" << defs.size() << "virtual screens for" << physicalScreenId;
                    // Compare before storing. A re-fetch that reports the same
                    // subdivisions (a screen-change burst re-triggering the fetch, a
                    // reconfigure that only touched another monitor) leaves the
                    // screen-id keyspace exactly as it was, so no cached ScreenId /
                    // ScreenOrientation / ActiveLayout verdict can resolve
                    // differently and the invalidate + full decoration re-fold
                    // restoreReadyIfLive would run is pure waste.
                    const auto existing = self->m_virtualScreenDefs.constFind(physicalScreenId);
                    defsMutated = existing == self->m_virtualScreenDefs.constEnd() || existing.value() != defs;
                    self->m_virtualScreenDefs.insert(physicalScreenId, defs);
                }

                self->reresolveTrackedScreens();

                countdownVsGate();
                restoreReadyIfLive(defsMutated);
            });
}

void PlasmaZonesEffect::reresolveTrackedScreens()
{
    // Re-resolve tracked screen IDs so stale virtual screen IDs
    // are replaced with IDs from the updated boundaries.
    for (auto it = m_trackedScreenPerWindow.begin(); it != m_trackedScreenPerWindow.end(); ++it) {
        auto* window = it.key();
        if (!window || window->isDeleted()) {
            continue;
        }
        // Position-based resolution (getWindowScreenId), consistent
        // with the daemon — do not trust window->screen() for
        // identical-model monitors. For SCROLL-managed windows this
        // re-resolve is deliberately inert: getWindowScreenId answers from
        // the engine's tracked screen (a parked frame's position is
        // meaningless), so writing it back re-keys nothing. That is
        // acceptable, not a gap: the daemon retiles every scrolling screen
        // on a VS reconfigure, and the tile-apply path rewrites both maps
        // with the new effective ids — the daemon, not this loop, is the
        // re-keying authority for strip windows.
        const QString windowId = getWindowId(window);
        if (!m_tilingHandler->scrollTrackedScreenFor(windowId).isEmpty()) {
            continue;
        }
        const QString newScreenId = getWindowScreenId(window);
        if (!newScreenId.isEmpty()) {
            const bool changed = it.value() != newScreenId;
            it.value() = newScreenId;
            // Also update the autotile handler's notified screen map
            // so slotWindowFrameGeometryChanged does not compare against
            // the stale pre-config-change screen ID.
            m_tilingHandler->updateNotifiedScreen(windowId, newScreenId);
            // A split added or removed under the focused window changes the
            // screen the daemon's shortcuts act on without any move: a
            // removed split left it naming the dead virtual screen until
            // the next activation.
            if (changed) {
                reportActiveWindowScreen(window, newScreenId);
            }
        }
    }
}

void PlasmaZonesEffect::fetchAllVirtualScreenConfigs()
{
    const auto outputs = KWin::effects->screens();

    // Collect physical screen IDs in a single pass to avoid count/iterate race
    // (a screen removed between two loops would cause count and calls to diverge)
    QStringList physIds;
    for (const auto* output : outputs) {
        const QString physId = outputScreenId(output);
        if (!physId.isEmpty()) {
            physIds.append(physId);
        }
    }

    physIds.removeDuplicates();

    // Prune stale m_virtualScreenDefs entries for physical screens that are no
    // longer connected. Without this, resolveEffectiveScreenId could match against
    // geometry from a disconnected monitor.
    const QSet<QString> currentPhysIds(physIds.begin(), physIds.end());
    for (auto it = m_virtualScreenDefs.begin(); it != m_virtualScreenDefs.end();) {
        if (!currentPhysIds.contains(it.key()))
            it = m_virtualScreenDefs.erase(it);
        else
            ++it;
    }

    if (physIds.isEmpty()) {
        // No physical screens to query — gate opens immediately
        m_daemonGate.virtualScreensReady = true;
        m_daemonGate.pendingVsConfigReplies = 0;
        if (m_daemonGate.serviceRegistered) {
            processDaemonReadyWindowState();
        }
        return;
    }

    // Bump generation so stale callbacks from prior fetches are ignored
    const uint64_t generation = ++m_daemonGate.vsConfigGeneration;
    m_daemonGate.pendingVsConfigReplies = physIds.size();
    m_daemonGate.virtualScreensReady = false;

    for (const QString& physId : physIds) {
        fetchVirtualScreenConfig(physId, generation);
    }
}

void PlasmaZonesEffect::onVirtualScreensChanged(const QString& physicalScreenId)
{
    qCInfo(lcEffect) << "Virtual screens changed for" << physicalScreenId;
    clearScreenIdCache();
    m_lastEffectiveScreenId.clear();
    // Temporarily disable VS-aware crossing detection while the async fetch is in-flight.
    // Without this, slotWindowFrameGeometryChanged uses stale boundary definitions from the
    // old config, potentially causing spurious VS crossing events during the D-Bus round-trip.
    m_daemonGate.virtualScreensReady = false;
    fetchVirtualScreenConfig(physicalScreenId); // generation=0, won't participate in startup gate
}

PhosphorAnimation::IMotionClock* PlasmaZonesEffect::clockForOutput(KWin::LogicalOutput* output) const
{
    if (output) {
        auto it = m_motionClocksByOutput.find(output);
        if (it != m_motionClocksByOutput.end()) {
            return it->second.get();
        }
    }
    return m_motionClockFallback.get();
}

void PlasmaZonesEffect::onScreenAdded(KWin::LogicalOutput* output)
{
    if (!output) {
        return;
    }
    // Two caches sit under a screen id and BOTH have to go, which is why this is two
    // calls. clearScreenIdCache() drops the effect's own screenIdCache and invalidates its connected-id set; the
    // serial those ids are built from comes from ScreenId::readEdidHeaderSerial, a
    // process-local static hash keyed by CONNECTOR NAME, and buildScreenBaseId prefers it
    // over QScreen::serialNumber(). The effect is loaded into kwin_wayland, so it holds its
    // own copy of that static, and nothing here was invalidating it — while the daemon does,
    // on both add and remove. A connector reused by a DIFFERENT monitor (dock swap, KVM,
    // different cable) therefore had the effect building newManuf:newModel:OLDSERIAL while
    // the daemon built the new serial: the same cross-process spelling disagreement the
    // #724 family is about, and reachable without two identical monitors.
    // ADD SIDE ONLY. onScreenRemoved must NOT mirror this: it reads removedScreenId from the
    // per-output record, and falls back to a resolve BEFORE its own cache clear, both so the
    // id matches the spelling the pre-unplug state was stored under. Re-reading a disconnected
    // connector's now-empty edid would change that spelling on the fallback path and break the
    // one consumer that compares against it.
    // Add-side alone is sufficient for the reused-connector case, because the new monitor's
    // serial is read fresh on its first resolve.
    PhosphorIdentity::ScreenId::invalidateEdidCache(output->name());
    // Hotplug is the earliest signal in the cascade (before any per-window
    // outputChanged): the connected-physical-id set must invalidate HERE or
    // scrollTrackedScreenFor's liveness gate answers from the pre-plug set
    // for the whole cascade.
    clearScreenIdCache();
    // Belt-and-braces against address reuse: KWin allocates a fresh LogicalOutput when the backend
    // output does not match an existing one, and that allocation can land where a freed one was. A
    // stale recorded spelling under the same pointer would then be read on this output's removal.
    // clearScreenIdCache deliberately does NOT touch this map, so the erase has to be here.
    m_idCaches.screenIdByOutput.remove(output);

    // Refresh every surviving peer's recorded spelling, the add-side twin of onScreenRemoved's
    // rebuild loop. An add can flip a PEER's duplicate verdict — the monitor that was alone now
    // has an identical twin, so its id gains the connector suffix and its state is re-published
    // under the new spelling — while the erase above only refreshes the output that arrived. The
    // resolve writes screenIdByOutput on its miss path and the cache was just cleared, so that
    // call re-records every output and eagerly rebuilds the connected set in the same pass.
    //
    // WHY A RESOLVE AND NOT A MAP CLEAR: upstream emits outputAdded BEFORE outputRemoved within
    // one updateOutputs, so on a bundled add+remove (a total unplug appends a placeholder) this
    // runs while the departing outputs still need their records. Clearing the map would wipe them
    // and leave onScreenRemoved resolving cold, which is the defect the record exists to prevent.
    // A resolve cannot: the dying outputs are already pruned from m_outputs before any emit, so
    // screens() does not list them and nothing here touches their entries.
    //
    // Spelled as the ACCESSOR rather than a loop of its own: it rebuilds the set eagerly and
    // re-records every output on the way through, which is exactly what a hand-written copy did,
    // and a hand-written copy of a body that already exists is how the add and remove sides drift
    // apart.
    //
    // THE INVALIDATE IS NOT REDUNDANT with clearScreenIdCache above, even though that already
    // cleared the flag. It restores the property the hand-written loop had STRUCTURALLY: this
    // rebuild is unconditional. Without it, inserting any read of connectedPhysicalIds() between
    // the two — a log line, a diagnostic — flips the flag true and silently turns the call below
    // into a no-op, leaving the pre-clear set alive, every per-output record unrefreshed, and the
    // prune on the next line running against a stale set. That failure has no symptom at the site.
    m_idCaches.connectedPhysicalIdsValid = false;
    (void)connectedPhysicalIds();
    pruneToLiveScreens(m_lastScreenDesktop, m_idCaches.connectedPhysicalIds);
    // The windows KWin returns to it wait for the settle's verdict.
    m_screenChangeHandler->noteOutputAdded(output);
    // The desktop it shows. KWin picks it with no desktop change, so nothing
    // else reports it and the engines would lay the output out under the
    // desktop they started on (F700). Sent as a seed, which the daemon takes
    // without a desktop switch, on the connection ahead of the settle report.
    if (!output->isPlaceholder()) {
        const QString screenId = outputScreenId(output);
        const KWin::VirtualDesktop* const vd = KWin::effects->currentDesktop(output);
        if (vd && !screenId.isEmpty()) {
            const int desktop = static_cast<int>(vd->x11DesktopNumber());
            m_lastScreenDesktop.insert(screenId, desktop);
            if (m_daemonGate.serviceRegistered) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("seedScreenDesktop"),
                    {screenId, desktop});
            }
        }
    }

    // Construct a bound clock for this output. Idempotent: if the same output
    // arrives twice (rare, but possible on some compositors' hotplug
    // sequences) the early return keeps the existing clock and skips the
    // re-raster below. The map is already safe from a double-add on its own,
    // because unordered_map::emplace does not overwrite an existing key; what
    // this guard must never be "simplified" into is an assigning insert. Every
    // in-flight AnimatedValue holds its clock as a RAW `IMotionClock*` in its
    // spec and dereferences it on every advance and requestFrame
    // (AnimatedValue.h:98, 194, 252, 262, 277, 378), so replacing the
    // unique_ptr would free the clock out from under them — a use-after-free
    // in the compositor, not a recoverable timing glitch.
    if (m_motionClocksByOutput.find(output) != m_motionClocksByOutput.end()) {
        return;
    }
    m_motionClocksByOutput.emplace(output, std::make_unique<CompositorClock>(output));
    // The painter's per-output state was dropped with the old output, and
    // noteScrollTabOutputRemoved dropped that screen's payload with it, so
    // for a plain re-plug this re-seeds nothing until the daemon's next
    // strips broadcast names the screen again. It covers the narrower case
    // of a payload that arrived between the remove and this add (the
    // handler's fetch replies are not tied to the output object), and it
    // re-rasters every other screen's model, which is harmless.
    m_tilingHandler->rebuildAllScrollTabIndicators();
}

void PlasmaZonesEffect::onScreenRemoved(KWin::LogicalOutput* output)
{
    if (!output) {
        return;
    }
    // READ the id this output was published under, rather than resolving one. A resolve is only
    // reproducible while the peer set is intact, and on a MULTI-output removal of identical
    // monitors it is not: KWin prunes every removed output before emitting the first screenRemoved,
    // so the second twin's resolve runs against a list holding NEITHER of them, finds no duplicate,
    // and returns the bare baseId where the suffixed form was used to key its state. The consumer
    // below matches on that key, so it silently drops nothing. Worse with a bundled add (a total
    // unplug appends a placeholder output, whose add clears the cache), where even the FIRST
    // removal resolves cold.
    //
    // The recompute stays as the fallback. It is BELT-AND-BRACES, not a live path: onScreenAdded
    // resolves every output, and every output gets one, so no live output reaches here unrecorded.
    // It stays because it is the guard against a future change to that add-side resolve, in the
    // same spirit as the other belt-and-braces notes in this file.
    //
    // THE TWO-STEP FORM IS LOAD-BEARING. `value(output, outputScreenId(output))` reads as a cache
    // lookup with a lazy fallback and is neither. `defaultValue` is an ordinary function ARGUMENT
    // of a two-parameter overload, not a C++ default argument, so it is evaluated before the call
    // whether or not the key is present — and outputScreenId's miss path inserts into THIS map, so
    // the recompute ran first, overwrote the recorded spelling with the cold one, and value()
    // handed that straight back. Written that way the fix was inert in both scenarios above. Read
    // first, resolve only on a genuine miss. (An earlier version of this note, and the commit that
    // landed it, said "default argument", which teaches the opposite: a real default argument IS
    // evaluated only when the caller omits it.)
    const auto recordedId = m_idCaches.screenIdByOutput.constFind(output);
    const QString removedScreenId =
        recordedId != m_idCaches.screenIdByOutput.constEnd() ? *recordedId : outputScreenId(output);
    // Record the windows on it as they are, under that id: KWin moves them
    // and restores their older state right after this signal (F709).
    m_screenChangeHandler->captureEvacuees(output, removedScreenId);
    // Unplug twin of the onScreenAdded invalidation: KWin fires
    // screenRemoved BEFORE the per-window outputChanged cascade, and the
    // connected-output gate in scrollTrackedScreenFor exists for exactly
    // that cascade — a stale cached set would keep answering the dead
    // screen for every scroll-tiled window's close/minimize/drag routing.
    clearScreenIdCache();

    // Rebuild the connected set eagerly, MINUS the dying output, so the cascade's cost is paid
    // once here rather than at whichever consumer trips the lazy rebuild. The ANSWER is the same
    // either way — see below — so this buys determinism, not correctness. The next
    // add/remove/reconfigure invalidates it again.
    //
    // WHAT THIS DOES NOT DEFEND AGAINST, corrected from upstream: an earlier version said
    // screens() "still lists this output while screenRemoved is being delivered". It does not.
    // Workspace::updateOutputs prunes m_outputs before it emits, so the loop below, the lazy
    // rebuild in connectedPhysicalIds() and outputScreenId's duplicate scan all see the same
    // post-unplug list. A later version tried to rescue the old rationale by pointing at the
    // ~Workspace emit; that emit reaches no effect at all (see outputScreenId's note), so there
    // is no teardown cascade to defend either.
    //
    // The dying output is still excluded from each RESOLVE as well as from the loop, so the two
    // agree by construction and neither can reintroduce the #724 spelling if upstream ever moves
    // the emit ahead of the prune. The failure that shape guards against is the survivor of two
    // identical monitors coming out in its disambiguated baseId/connector form while the daemon
    // has moved to the unsuffixed one.
    m_idCaches.connectedPhysicalIds.clear();
    for (const auto* other : KWin::effects->screens()) {
        if (other == output) {
            continue;
        }
        const QString physId = outputScreenId(other, output);
        if (!physId.isEmpty()) {
            m_idCaches.connectedPhysicalIds.insert(physId);
        }
    }
    m_idCaches.connectedPhysicalIdsValid = true;

    // Drop this output's per-screen desktop dedup entry, symmetric with the
    // daemon's VirtualDesktopManager::removeScreenDesktop (#648): otherwise
    // reportScreenDesktop's m_lastScreenDesktop cache retains a stale value for
    // a disconnected connector. Runs before the motion-clock early-return below
    // so it fires even for an output that never had an animation clock.
    //
    // Pruned against the set rebuilt above rather than removed by removedScreenId, because a
    // remove-by-id reaches ONE spelling and this map can hold two for the same monitor. It drops
    // the departing screen either way, since the rebuild loop excludes it.
    pruneToLiveScreens(m_lastScreenDesktop, m_idCaches.connectedPhysicalIds);

    // Drop any live desktop-switch transition on this output. A disconnected
    // LogicalOutput* left in the transition manager's active map would dangle:
    // scheduleRepaints()/paintOutput() deref the key, and the fullscreen-effect
    // claim would never release once its output vanished mid-transition. Runs
    // before the motion-clock early-return so it fires even for an output that
    // never had an animation clock.
    m_desktopTransition.outputRemoved(output);

    // Drop any strip-pass entry for this output for the same dangling-key
    // reason; its sibling spring state goes with the forgetOutput below.
    m_stripTransition.outputRemoved(output);

    // The pointer pass keeps its history in ONE output's device-px canvas and
    // stores that output as a raw pointer, so a disconnected LogicalOutput*
    // here would be dereferenced by its damage math and its repaint pump.
    m_pointerPass.outputRemoved(output);

    // Drop this output's strip view accumulator. The map is keyed by
    // LogicalOutput*, so a disconnected one would leave an entry whose key can
    // be reused by a later hotplug landing at the same address — the next
    // scroll on the new output would then spring from the dead one's baseline.
    // Runs before the motion-clock early-return so it fires even for an output
    // that never had an animation clock, same as the two clears above.
    m_stripViewAnimator->forgetOutput(output);
    // A pill on the dying output may hold the hover and the override cursor;
    // the handler's clear releases both with the painter's state and drops
    // the screen's model and overrides (a re-plug is re-seeded by the
    // daemon's replay or the next relayout). Takes the id resolved above
    // rather than re-resolving, for the same cache reason.
    m_tilingHandler->noteScrollTabOutputRemoved(output, removedScreenId);

    // The one consumer of removedScreenId has run, so drop this output's recorded spelling. HERE,
    // not after the motion-clock early-return below: the key is a raw LogicalOutput* and an entry
    // that outlives its output is the same address-reuse hazard the suppression set and the strip
    // animator each describe in this handler — a later hotplug landing at the same address would
    // read the dead output's spelling.
    m_idCaches.screenIdByOutput.remove(output);

    // m_fullscreenSuppressedOutputs holds raw LogicalOutput* too. It is only ever
    // COMPARED, never dereferenced, so a stale entry cannot crash — but a later
    // hotplug landing at the same address inherits the dead output's suppression
    // and every window on the NEW output is silently left undecorated (the gate
    // routes through the normal undecorate path, so it is a teardown, not a paint
    // glitch) until some unrelated trigger happens to refresh.
    //
    // An explicit ERASE, deliberately, not refreshFullscreenSuppression(). THREE earlier claims
    // for this choice were wrong, recorded so they are not re-argued. It does NOT rest on whether
    // KWin has already dropped the dying output from screenAt()'s answer: it has, because
    // updateOutputs prunes m_outputs before emitting, so a rebuild cannot put the dead pointer
    // back. It does NOT avoid the rebuild's decoration sweep: refreshFullscreenSuppression is
    // connected to this same screenRemoved signal in connectWindowAndScreenSignals, after this
    // handler's own connection, so Qt delivers it later in the same emit. One slot runs between
    // them, ScreenChangeHandler::slotScreenLayoutChanged, and it cannot touch the suppression set
    // (it sets a pending flag, starts the debounce and schedules a client-area report), so it does
    // not disturb the argument. The sweep then fires whenever the rebuilt set differs. And it is NOT saved by the
    // ~Workspace emit, which reaches no effect at all — see outputScreenId's note.
    //
    // WHAT THE ERASE ACTUALLY BUYS, which is small and real: it SUPPRESSES one redundant
    // updateAllDecorations() sweep in the case where fullscreen windows sit on both the dying
    // output and a survivor. Stored {A,B} erases to {B}; the rebuild resolves both windows to B,
    // so covered {B} equals {B} and the early return fires. Without the erase the comparison
    // would be {B} against {A,B}, which differs, and the full sweep would run for no change in
    // outcome. In the commoner single-fullscreen case the set does move (the window is still
    // positioned over the dead output's geometry and screenAt is nearest-output, so it resolves
    // to a survivor) and the sweep runs either way. Runs BEFORE the motion-clock early-return
    // below, with the other output-forgetting calls above, so it fires for an output that never
    // had an animation clock.
    if (m_fullscreenSuppressedOutputs.remove(output)) {
        // Keep the pointer pass's copy in step, the way refreshFullscreenSuppression
        // does on a real change. Other outputs' entries are untouched, so no
        // decoration re-sweep is owed.
        m_pointerPass.setSuppressedOutputs(m_fullscreenSuppressedOutputs);
    }

    // Any in-flight AnimatedValue whose MotionSpec captured this clock's
    // pointer would UAF on its next advance() if we just dropped the
    // unique_ptr. Reap only the animations bound to THIS output's clock
    // — other outputs' animations keep ticking uninterrupted. Uses the
    // controller's reapAnimationsForClock() helper which iterates
    // m_animations and filters on spec().clock pointer equality.
    auto it = m_motionClocksByOutput.find(output);
    if (it == m_motionClocksByOutput.end()) {
        return;
    }
    // Both animators are unique_ptrs initialized in the ctor and never reset
    // except during ~PlasmaZonesEffect; any screenRemoved signal posted after
    // our destruction is auto-disconnected by QObject's teardown, so this
    // should be unreachable. Asserted so a debug build says so loudly, and
    // guarded so a release build cannot dereference null on the reap below if
    // the invariant is ever broken — the clock is already out of the map here,
    // so returning early leaks nothing.
    Q_ASSERT(m_windowAnimator);
    Q_ASSERT(m_stripViewAnimator);
    if (!m_windowAnimator || !m_stripViewAnimator) {
        qCWarning(lcEffect) << "onScreenRemoved: animator missing during output teardown; skipping reap";
        return;
    }

    // Ordering matters: extract the unique_ptr and erase the map
    // entry BEFORE calling reap. A re-entrant `onAnimationReaped` hook
    // that starts a new animation on a handle whose `screen()` still
    // returns the dying output would otherwise route through
    // `clockForOutput(output)` → find this clock in the map → bind
    // the new animation to it. The subsequent destructor run would
    // then UAF on the next advanceAnimations. By erasing first, the
    // lookup falls through to the fallback clock — new animations
    // started during reap are born bound to the fallback, never the
    // dying clock. The `dyingClock` unique_ptr keeps the clock alive
    // for the reap iteration itself (the captured raw pointer remains
    // valid through the function's scope).
    std::unique_ptr<CompositorClock> dyingClock = std::move(it->second);
    m_motionClocksByOutput.erase(it);
    m_windowAnimator->reapAnimationsForClock(dyingClock.get());
    // The strip view spring binds to the same per-output clocks, so it owes the
    // same reap. forgetOutput() above already dropped this output's entry and
    // the strip resolver has no fallback, so nothing else can be holding this
    // clock — the reap is the belt to that braces, and it also covers a leg
    // started re-entrantly during the erase above.
    m_stripViewAnimator->reapAnimationsForClock(dyingClock.get());
    // dyingClock destroyed at scope exit — at this point reap has
    // cleared every animation that captured the pointer, so the
    // destruction cannot strand a dangling MotionSpec::clock.
}

} // namespace PlasmaZones
