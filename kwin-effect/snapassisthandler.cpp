// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassisthandler.h"
#include "plasmazoneseffect.h"
#include "kwin_compositor_bridge.h"
#include "snapassistthumbnailcapture.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ZoneMarshalling.h>
#include <PhosphorCompositor/SnapAssistFilter.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUuid>
#include <QVector>

Q_LOGGING_CATEGORY(lcSnapAssist, "kwin.effect.plasmazones.snapassist", QtWarningMsg)

namespace PlasmaZones {

using namespace PhosphorCompositor;

SnapAssistHandler::SnapAssistHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void SnapAssistHandler::showContinuationIfNeeded(const QString& screenId, const QString& requireSnappedWindowId)
{
    if (screenId.isEmpty()) {
        qCInfo(lcSnapAssist) << "Snap assist continuation skipped: empty screen name";
        return;
    }
    if (!m_snapAssistEnabled) {
        qCInfo(lcSnapAssist) << "Snap assist continuation skipped: snapAssistEnabled is false";
        return;
    }
    if (!m_effect->isDaemonReady("snap assist continuation")) {
        return;
    }
    qCInfo(lcSnapAssist) << "Snap assist continuation: querying empty zones for screen" << screenId;
    QDBusPendingCall emptyCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getEmptyZones"), {screenId});
    auto* watcher = new QDBusPendingCallWatcher(emptyCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, screenId, requireSnappedWindowId](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<PhosphorProtocol::EmptyZoneList> reply = *w;
                if (!reply.isValid() || reply.value().isEmpty()) {
                    qCInfo(lcSnapAssist) << "Snap assist continuation: no empty zones"
                                         << (reply.isValid() ? QStringLiteral("(empty list)")
                                                             : QStringLiteral("(invalid reply)"));
                    return;
                }
                // When an anchor window is required, it is also the window to
                // exclude from candidates — it is already placed in a zone.
                asyncShow(requireSnappedWindowId, screenId, reply.value(), requireSnappedWindowId);
            });
}

void SnapAssistHandler::asyncShow(const QString& excludeWindowId, const QString& screenId,
                                  const PhosphorProtocol::EmptyZoneList& emptyZones,
                                  const QString& requireSnappedWindowId)
{
    if (!m_effect->isDaemonReady("snap assist snapped windows")) {
        return;
    }
    QDBusPendingCall snapCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);
    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, excludeWindowId, screenId, emptyZones, requireSnappedWindowId](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<QStringList> reply = *w;
                QSet<QString> snappedWindowIds;
                if (reply.isValid()) {
                    for (const QString& id : reply.value()) {
                        snappedWindowIds.insert(id);
                    }
                }
                // Resnap anchor gate: the resnap-completion path keys the
                // continuation to the active window. If that window is not
                // among the daemon's snapped windows, the resnap placed
                // nothing meaningful in a zone (e.g. an autotile→snap toggle
                // with no prior snap assignments) — every zone is empty and a
                // "continuation" would just be snap assist for the whole
                // layout. Bail. Empty requireSnappedWindowId (drag-snap and
                // overlay-driven continuations) skips the gate entirely.
                if (!requireSnappedWindowId.isEmpty() && !snappedWindowIds.contains(requireSnappedWindowId)) {
                    qCInfo(lcSnapAssist) << "Snap assist skipped: anchor window" << requireSnappedWindowId
                                         << "is not snapped — resnap placed nothing in a zone on" << screenId;
                    return;
                }
                PhosphorProtocol::SnapAssistCandidateList candidates =
                    buildCandidates(excludeWindowId, screenId, snappedWindowIds);
                if (candidates.isEmpty()) {
                    qCInfo(lcSnapAssist) << "Snap assist skipped: no unsnapped candidate windows on" << screenId;
                    return;
                }
                if (!m_effect->isDaemonReady("snap assist show")) {
                    return;
                }

                // Kick off in-process thumbnail captures alongside the
                // showSnapAssist D-Bus call. The two are independent — the
                // overlay opens on icons, and per-window setSnapAssistThumbnail
                // calls land asynchronously as each window is rendered and
                // posted. Either ordering is safe on the daemon side: a
                // late thumbnail updates the live candidate list, an early one
                // is held in the bounded LRU and applied when the overlay
                // shows.
                if (!m_capture) {
                    m_capture = new SnapAssistThumbnailCapture(this);
                }
                QVector<SnapAssistThumbnailCapture::Candidate> captureList;
                captureList.reserve(candidates.size());
                for (const auto& c : candidates) {
                    if (c.compositorHandle.isEmpty()) {
                        continue;
                    }
                    const QUuid id(c.compositorHandle);
                    if (id.isNull()) {
                        continue;
                    }
                    captureList.append({id});
                }
                m_capture->captureCandidates(captureList);

                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::Overlay, QStringLiteral("showSnapAssist"),
                    {screenId, QVariant::fromValue(emptyZones), QVariant::fromValue(candidates)},
                    QStringLiteral("showSnapAssist"));
                qCInfo(lcSnapAssist) << "Snap Assist shown with" << candidates.size() << "candidates";
            });
}

void SnapAssistHandler::resetRecentlyPostedThumbnails()
{
    if (m_capture) {
        m_capture->resetRecentlyPosted();
    }
}

PhosphorProtocol::SnapAssistCandidateList
SnapAssistHandler::buildCandidates(const QString& excludeWindowId, const QString& screenId,
                                   const QSet<QString>& snappedWindowIds) const
{
    PhosphorProtocol::SnapAssistCandidateList candidates =
        SnapAssistFilter::buildCandidates(m_effect->compositorBridge(), excludeWindowId, screenId, snappedWindowIds);

    // KWin-specific: fill compositorHandle (internal UUID) for overlay window identification.
    //
    // Earlier revisions also dropped autotile-tracked candidates here via
    // @c AutotileHandler::isTrackedWindow, but that flag tracks "we have notified
    // autotile about this window at some point" — it is NOT a live "this window
    // currently lives on an autotile screen" check. After a window moves from an
    // autotile monitor to a manual-mode screen, the flag stays set until autotile's
    // own bookkeeping catches up; using it here erased perfectly valid candidates
    // (e.g. a window the user dragged off the autotile monitor onto vs:0) and
    // stranded snap-assist with zero candidates on the manual screen.
    //
    // The screen-membership question is now answered authoritatively in
    // SnapAssistFilter via @c VirtualScreenId::samePhysical(info.screenId, screenId):
    // candidates are restricted to the target's physical monitor, and trigger
    // sites gate on @c !isAutotileScreen(screenId), so by transitivity no
    // surviving candidate is on an autotile monitor in normal flow.
    //
    // Sibling-VS inclusion (windows on the other VS of the same physical monitor
    // still count as candidates) is also handled inside SnapAssistFilter, so it
    // does NOT need to be re-applied here.
    for (auto& c : candidates) {
        auto* ew = KWinCompositorBridge::toEffectWindow(m_effect->compositorBridge()->findWindowById(c.windowId));
        if (ew) {
            c.compositorHandle = ew->internalId().toString();
        }
    }
    return candidates;
}

} // namespace PlasmaZones
