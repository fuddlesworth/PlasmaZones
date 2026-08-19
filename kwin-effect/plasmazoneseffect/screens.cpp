// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <PhosphorIdentity/ScreenId.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <core/output.h>
#include <effect/effecthandler.h>

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
#include "compositor/compositorclock.h"
#include "compositor/stripviewanimator.h"
#include "compositor/windowanimator.h"

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

QString PlasmaZonesEffect::outputScreenId(const KWin::LogicalOutput* output) const
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
    // Uses shared ScreenIdUtils (compositor-common) for hex normalization and sysfs EDID
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
        if (!other || other->name() == connectorName) {
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
    m_idCaches.screenIdCache.insert(connectorName, result);
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

// Resolve the monitor by the window's POSITION — the KWin output whose geometry
// contains the window centre — NOT w->screen(). KWin can assign a window the
// wrong one of two identical-model outputs, so trusting w->screen() made the
// effect disagree with the daemon about which monitor a window sits on, which
// then bounced a snapped window off to the other monitor (Discussion #724).
// Mirrors the snap-assist path in snaphandler.cpp. The w->screen() fallback
// only fires when no output contains the centre (window fully off-screen
// mid-reconfigure).
KWin::LogicalOutput* PlasmaZonesEffect::windowOutput(KWin::EffectWindow* w) const
{
    if (!w) {
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
    // parks off-viewport columns and hidden tabs ENTIRELY outside their
    // screen rect, so a parked frame's centre lands inside a NEIGHBOUR
    // output's geometry and the position-derived resolution below would
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
    // degrades to "::serial".)
    return resolveEffectiveScreenId(c, windowOutput(w));
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
                        self->m_daemonGate.virtualScreensReady = true;
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
                    if (self->m_daemonGate.pendingVsConfigReplies == 0) {
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
                QRect physGeom;
                const auto outputs = KWin::effects->screens();
                for (const auto* out : outputs) {
                    if (self->outputScreenId(out) == physicalScreenId) {
                        physGeom = out->geometry();
                        break;
                    }
                }

                if (!physGeom.isValid()) {
                    qCWarning(lcEffect) << "Physical output" << physicalScreenId
                                        << "not found (hot-unplug?) — skipping VS config update;"
                                        << "will re-fetch on reconnect";
                }

                QVector<EffectVirtualScreenDef> defs;
                for (const QJsonValue& val : screens) {
                    QJsonObject obj = val.toObject();
                    QJsonObject region = obj.value(QLatin1String("region")).toObject();

                    EffectVirtualScreenDef def;
                    def.id = obj.value(QLatin1String("id")).toString();

                    // Compute absolute geometry from fractional region within physical screen
                    if (physGeom.isValid()) {
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
                    }

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
            it.value() = newScreenId;
            // Also update the autotile handler's notified screen map
            // so slotWindowFrameGeometryChanged does not compare against
            // the stale pre-config-change screen ID.
            m_tilingHandler->updateNotifiedScreen(windowId, newScreenId);
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
    m_idCaches.screenIdCache.clear();
    m_idCaches.connectedPhysicalIdsValid = false;
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
    // Hotplug is the earliest signal in the cascade (before any per-window
    // outputChanged): the connected-physical-id set must invalidate HERE or
    // scrollTrackedScreenFor's liveness gate answers from the pre-plug set
    // for the whole cascade.
    clearScreenIdCache();
    // Construct a bound clock for this output. Idempotent: if the same
    // output arrives twice (rare, but possible on some compositors'
    // hotplug sequences) we keep the existing clock rather than
    // replacing it — the old clock's latched presentTime would be
    // lost and any in-flight animations bound to it would see a dt
    // jump.
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
    // Resolve the dying connector's id BEFORE the cache is dropped: resolving
    // it afterwards would re-populate the fresh cache with the very entry this
    // handler exists to purge.
    const QString removedScreenId = outputScreenId(output);
    // Unplug twin of the onScreenAdded invalidation: KWin fires
    // screenRemoved BEFORE the per-window outputChanged cascade, and the
    // connected-output gate in scrollTrackedScreenFor exists for exactly
    // that cascade — a stale cached set would keep answering the dead
    // screen for every scroll-tiled window's close/minimize/drag routing.
    clearScreenIdCache();

    // Rebuild the connected set eagerly, MINUS the dying output. The lazy
    // rebuild in connectedPhysicalIds() reads KWin::effects->screens(), which
    // still lists this output while screenRemoved is being delivered, so the
    // first caller anywhere in the rest of the cascade would re-insert the
    // connector that is going away. The next add/remove/reconfigure
    // invalidates this again.
    m_idCaches.connectedPhysicalIds.clear();
    for (const auto* other : KWin::effects->screens()) {
        if (other == output) {
            continue;
        }
        const QString physId = outputScreenId(other);
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
    m_lastScreenDesktop.remove(removedScreenId);

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
