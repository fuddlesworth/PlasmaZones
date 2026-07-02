// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <PhosphorIdentity/ScreenId.h>
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

#include "../autotilehandler.h"
#include "../compositorclock.h"
#include "../windowanimator.h"

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
    // Invalidated on screen add/remove (m_screenIdCache cleared by screen change handler).
    auto it = m_screenIdCache.constFind(connectorName);
    if (it != m_screenIdCache.constEnd()) {
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
    bool hasDuplicate = false;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() != connectorName
            && PhosphorIdentity::ScreenId::buildScreenBaseId(screen->manufacturer(), screen->model(),
                                                             screen->serialNumber(), screen->name())
                == baseId) {
            hasDuplicate = true;
            break;
        }
    }

    QString result = hasDuplicate ? baseId + QLatin1Char('/') + connectorName : baseId;
    m_screenIdCache.insert(connectorName, result);
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
    if (m_daemonServiceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("screenDesktopChanged"), {screenId, desktop});
    }
}

namespace {
// Physical screen id for a QScreen, byte-identical to the daemon's
// PhosphorScreens::ScreenIdentity::identifierFor(): derive the base id from the
// QScreen's OWN EDID fields (manufacturer/model/serial keyed by the QScreen
// connector name), then disambiguate identical monitors with a "/CONNECTOR"
// suffix. Deriving from the QScreen — not the window's KWin LogicalOutput — is
// what makes the effect agree with the daemon for identical-model monitors,
// whose per-window output the compositor and daemon can otherwise resolve to
// different serials (Discussion #724 follow-up).
QString screenIdForQScreen(const QScreen* screen)
{
    if (!screen) {
        return QString();
    }
    const QString baseId = PhosphorIdentity::ScreenId::buildScreenBaseId(screen->manufacturer(), screen->model(),
                                                                         screen->serialNumber(), screen->name());
    for (const QScreen* other : QGuiApplication::screens()) {
        if (other != screen
            && PhosphorIdentity::ScreenId::buildScreenBaseId(other->manufacturer(), other->model(),
                                                             other->serialNumber(), other->name())
                == baseId) {
            return baseId + QLatin1Char('/') + screen->name();
        }
    }
    return baseId;
}
} // namespace

QString PlasmaZonesEffect::getWindowScreenId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    const QPointF cf = w->frameGeometry().center();
    const QPoint c(qRound(cf.x()), qRound(cf.y()));

    // Resolve the PHYSICAL monitor by the window's POSITION and derive its id from
    // the QScreen exactly as the daemon does — NOT from the window's KWin output
    // (w->screen()), whose EDID-serial derivation names the wrong panel for
    // identical-model monitors, so the effect and daemon disagreed on which serial
    // a window sits on (Discussion #724 follow-up). Fall back to the output-based
    // id only when no QScreen contains the point.
    QString physId;
    if (const QScreen* screen = QGuiApplication::screenAt(c)) {
        physId = screenIdForQScreen(screen);
    }
    if (physId.isEmpty()) {
        physId = outputScreenId(w->screen());
    }
    return resolveEffectiveScreenId(c, physId);
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
    const uint64_t seq = ++m_vsFetchSeqPerPhysId[physicalScreenId];

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
                    if (generation == 0 || !self || self->m_vsConfigGeneration != generation) {
                        return;
                    }
                    if (self->m_pendingVsConfigReplies > 0 && --self->m_pendingVsConfigReplies == 0) {
                        self->m_virtualScreensReady = true;
                        if (self->m_daemonServiceRegistered) {
                            self->processDaemonReadyWindowState();
                        }
                    }
                };

                QDBusPendingReply<QString> reply = *w;

                // A newer fetch for this physId issued after this one makes
                // this reply stale: its payload describes a superseded
                // config. Drop it without touching m_virtualScreenDefs or
                // m_virtualScreensReady — the latest fetch's reply owns those
                // — but still run countdownVsGate so a startup batch's reply
                // tally isn't left hanging on the superseded call.
                const bool isLatest = self->m_vsFetchSeqPerPhysId.value(physicalScreenId) == seq;

                // Live VS-config changes (generation == 0) flip
                // m_virtualScreensReady = false in onVirtualScreensChanged so
                // window-screen-crossing detection pauses until the reply
                // lands. EVERY early-return below must restore the flag for
                // generation == 0 — otherwise an errored / stale / malformed
                // reply leaves the gate closed forever and VS crossings
                // silently stop being detected for that physical screen.
                const auto restoreReadyIfLive = [self, generation]() {
                    if (generation == 0) {
                        self->m_virtualScreensReady = true;
                    }
                };

                if (reply.isError()) {
                    qCDebug(lcEffect) << "fetchVirtualScreenConfig: no virtual screens for" << physicalScreenId
                                      << reply.error().message();
                    if (isLatest) {
                        self->m_virtualScreenDefs.remove(physicalScreenId);
                    }
                    countdownVsGate();
                    restoreReadyIfLive();
                    return;
                }

                if (!isLatest) {
                    countdownVsGate();
                    restoreReadyIfLive();
                    return;
                }

                const QString json = reply.value();
                QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
                if (!doc.isObject()) {
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                    countdownVsGate();
                    restoreReadyIfLive();
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

                if (defs.isEmpty()) {
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                } else {
                    qCInfo(lcEffect) << "Loaded" << defs.size() << "virtual screens for" << physicalScreenId;
                    self->m_virtualScreenDefs.insert(physicalScreenId, defs);
                }

                // Re-resolve tracked screen IDs so stale virtual screen IDs
                // are replaced with IDs from the updated boundaries.
                for (auto it = self->m_trackedScreenPerWindow.begin(); it != self->m_trackedScreenPerWindow.end();
                     ++it) {
                    auto* window = it.key();
                    if (!window || window->isDeleted()) {
                        continue;
                    }
                    {
                        // Position-based resolution (getWindowScreenId), consistent
                        // with the daemon — do not trust window->screen() for
                        // identical-model monitors.
                        const QString newScreenId = self->getWindowScreenId(window);
                        if (!newScreenId.isEmpty()) {
                            it.value() = newScreenId;
                            // Also update the autotile handler's notified screen map
                            // so slotWindowFrameGeometryChanged does not compare against
                            // the stale pre-config-change screen ID.
                            const QString windowId = self->getWindowId(window);
                            self->m_autotileHandler->updateNotifiedScreen(windowId, newScreenId);
                        }
                    }
                }

                countdownVsGate();
                restoreReadyIfLive();
            });
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
        m_virtualScreensReady = true;
        m_pendingVsConfigReplies = 0;
        if (m_daemonServiceRegistered) {
            processDaemonReadyWindowState();
        }
        return;
    }

    // Bump generation so stale callbacks from prior fetches are ignored
    const uint64_t generation = ++m_vsConfigGeneration;
    m_pendingVsConfigReplies = physIds.size();
    m_virtualScreensReady = false;

    for (const QString& physId : physIds) {
        fetchVirtualScreenConfig(physId, generation);
    }
}

void PlasmaZonesEffect::onVirtualScreensChanged(const QString& physicalScreenId)
{
    qCInfo(lcEffect) << "Virtual screens changed for" << physicalScreenId;
    m_screenIdCache.clear();
    m_lastEffectiveScreenId.clear();
    // Temporarily disable VS-aware crossing detection while the async fetch is in-flight.
    // Without this, slotWindowFrameGeometryChanged uses stale boundary definitions from the
    // old config, potentially causing spurious VS crossing events during the D-Bus round-trip.
    m_virtualScreensReady = false;
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
}

void PlasmaZonesEffect::onScreenRemoved(KWin::LogicalOutput* output)
{
    if (!output) {
        return;
    }

    // Drop this output's per-screen desktop dedup entry, symmetric with the
    // daemon's VirtualDesktopManager::removeScreenDesktop (#648): otherwise
    // reportScreenDesktop's m_lastScreenDesktop cache retains a stale value for
    // a disconnected connector. Runs before the motion-clock early-return below
    // so it fires even for an output that never had an animation clock.
    m_lastScreenDesktop.remove(outputScreenId(output));

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
    // m_windowAnimator is a unique_ptr initialized in the ctor and
    // never reset except during ~PlasmaZonesEffect; any screenRemoved
    // signal posted after our destruction is auto-disconnected by
    // QObject's teardown, so a nullptr guard here would be dead
    // code rather than defensive. Assert the invariant instead.
    Q_ASSERT(m_windowAnimator);

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
    // dyingClock destroyed at scope exit — at this point reap has
    // cleared every animation that captured the pointer, so the
    // destruction cannot strand a dangling MotionSpec::clock.
}

} // namespace PlasmaZones
