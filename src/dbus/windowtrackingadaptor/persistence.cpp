// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingadaptor.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/utils/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include <QScreen>
#include <QJsonDocument>
#include <QJsonObject>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

void WindowTrackingAdaptor::storePreTileGeometry(const QString& windowId, int x, int y, int width, int height,
                                                 const QString& screenId, bool overwrite)
{
    if (!validateWindowId(windowId, QStringLiteral("store pre-tile geometry"))) {
        return;
    }

    if (width <= 0 || height <= 0) {
        qCWarning(lcDbusWindow) << "Invalid geometry for pre-tile storage:"
                                << "width=" << width << "height=" << height;
        return;
    }

    // SINGLE float-back store: the placement record's SHARED per-screen free
    // geometry (NOT a per-engine store). The effect's guarded pre-tile/pre-snap
    // capture is the source; snap and autotile read the SAME value, so a window's
    // free position never differs between modes and a managed rect can't leak from
    // one engine's store into the other's float restore.
    if (m_service) {
        m_service->recordFreeGeometry(windowId, screenId, QRect(x, y, width, height), overwrite);
    }
    qCDebug(lcDbusWindow) << "Stored pre-tile geometry to record for" << windowId << "screen=" << screenId
                          << "overwrite=" << overwrite;
}

bool WindowTrackingAdaptor::hasPreTileGeometry(const QString& windowId)
{
    if (windowId.isEmpty() || !m_service) {
        return false;
    }
    const QString screenId = m_service->screenForWindow(windowId);
    return m_service->validatedUnmanagedGeometry(windowId, screenId).has_value();
}

void WindowTrackingAdaptor::clearPreTileGeometry(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("clear pre-tile geometry"))) {
        return;
    }
    // Single float-back store: clear the record's shared free geometry.
    if (m_service) {
        m_service->clearFreeGeometry(windowId);
    }
}

PhosphorProtocol::PreTileGeometryList WindowTrackingAdaptor::getPreTileGeometries()
{
    PhosphorProtocol::PreTileGeometryList result;
    if (!m_service) {
        return result;
    }
    // Source from the SINGLE float-back store — the unified record's shared
    // per-screen free geometry — so the effect's float-cache seed sees every
    // window's float-back regardless of which mode captured it.
    // Intentionally NOT gated by isPersistedContextDisabled (unlike
    // getPendingRestoreGeometries): float-back restores a window to a FREE
    // position, it does not snap it into a zone, so the snap-disable gate must
    // not apply here — mirroring the snap engine's floating-branch policy in
    // SnapEngine::resolveWindowRestore.
    for (const PhosphorEngine::WindowPlacement& p : m_service->placementStore().records()) {
        for (auto it = p.freeGeometryByScreen.constBegin(); it != p.freeGeometryByScreen.constEnd(); ++it) {
            const QRect& geo = it.value();
            if (!geo.isValid()) {
                continue;
            }
            PhosphorProtocol::PreTileGeometryEntry entry;
            entry.appId = p.appId;
            entry.x = geo.x();
            entry.y = geo.y();
            entry.width = geo.width();
            entry.height = geo.height();
            const QString& screen = it.key();
            if (!screen.isEmpty()) {
                entry.screenId = PhosphorIdentity::VirtualScreenId::isVirtual(screen)
                    ? screen
                    : PhosphorScreens::ScreenIdentity::idForName(screen);
            }
            result.append(entry);
        }
    }
    return result;
}

bool WindowTrackingAdaptor::getValidatedPreTileGeometry(const QString& windowId, int& x, int& y, int& width,
                                                        int& height)
{
    x = y = width = height = 0;

    if (windowId.isEmpty()) {
        return false;
    }

    // m_service is non-null by constructor invariant (the ctor qFatals on
    // null), so no guard is needed; the half-guard this replaced checked
    // one deref and missed the next.
    const QString screenId = m_service->screenForWindow(windowId);

    auto geo = m_service->validatedUnmanagedGeometry(windowId, screenId);
    if (!geo) {
        return false;
    }

    x = geo->x();
    y = geo->y();
    width = geo->width();
    height = geo->height();
    return true;
}

PhosphorProtocol::WindowGeometryList WindowTrackingAdaptor::getUpdatedWindowGeometries()
{
    QHash<QString, QRect> geometries = m_service->updatedWindowGeometries();
    PhosphorProtocol::WindowGeometryList result;
    result.reserve(geometries.size());
    for (auto it = geometries.constBegin(); it != geometries.constEnd(); ++it) {
        result.append(PhosphorProtocol::WindowGeometryEntry::fromRect(it.key(), it.value()));
    }
    qCDebug(lcDbusWindow) << "Returning updated geometries for" << result.size() << "windows";
    return result;
}

QString WindowTrackingAdaptor::getPendingRestoreGeometries()
{
    auto targets = m_service->pendingRestoreGeometries();
    if (targets.isEmpty()) {
        return QStringLiteral("{}");
    }

    // Disabled-context gate (discussion #461 item 7). The KWin effect's
    // instant-restore fast path teleports a window straight into target.geometry
    // from this cache, BEFORE the daemon's resolveWindowRestore predicate gate
    // runs — so an entry left here for a disabled monitor would be snapped onto
    // it regardless of the engine-side ShouldRestorePredicate, leaving a window
    // visually placed on a disabled context but absent from SnapState (the
    // "ghost" the instant-restore registration fix otherwise eliminates).
    // Funnel this read through the same isPersistedContextDisabled check that
    // guards saveState, loadState and the engine restore path so all four
    // paths can never drift.
    //
    // resolveWindowRestore carries no per-restore desktop, so the current
    // virtual desktop is used — consistent with the snap-side
    // setShouldRestorePredicate gate. Activity is left unset: snap-mode storage
    // carries no per-window activity tag (see isPersistedContextDisabled).
    QJsonObject result;
    for (auto it = targets.constBegin(); it != targets.constEnd(); ++it) {
        const auto& target = it.value();
        // Per-output virtual desktops (#648): gate each record on ITS screen's desktop.
        const int desktop = currentDesktopForScreen(target.screenId);
        if (isPersistedContextDisabled(target.screenId, desktop)) {
            qCDebug(lcDbusWindow) << "getPendingRestoreGeometries: skipping" << it.key()
                                  << "— disabled context on screen" << target.screenId;
            continue;
        }
        QJsonObject geoObj;
        geoObj[QLatin1String("x")] = target.geometry.x();
        geoObj[QLatin1String("y")] = target.geometry.y();
        geoObj[QLatin1String("width")] = target.geometry.width();
        geoObj[QLatin1String("height")] = target.geometry.height();
        geoObj[QLatin1String("screenId")] = target.screenId;
        result[it.key()] = geoObj;
    }

    qCDebug(lcDbusWindow) << "Returning pending restore geometries for" << result.size() << "apps";
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void WindowTrackingAdaptor::onLayoutChanged()
{
    // Delegate to service
    m_service->onLayoutChanged();

    // After layout becomes available, check if we have placement records to
    // restore. The unified WindowPlacementStore is the source of truth (the legacy
    // m_pendingRestoreQueues is in-session-only and empty at startup).
    if (m_service->placementStore().size() > 0) {
        m_hasPendingRestores = true;
        qCDebug(lcDbusWindow) << "Layout available with" << m_service->placementStore().size()
                              << "placement records, checking if panel geometry is ready";
        tryEmitPendingRestoresAvailable();
    }
}

void WindowTrackingAdaptor::onPanelGeometryReady()
{
    qCDebug(lcDbusWindow) << "Panel geometry: ready, checking if pending restores available";
    tryEmitPendingRestoresAvailable();
}

void WindowTrackingAdaptor::tryEmitPendingRestoresAvailable()
{
    // Don't emit more than once per session
    if (m_pendingRestoresEmitted) {
        return;
    }

    // Check both conditions: layout has pending restores AND panel geometry is known
    if (!m_hasPendingRestores) {
        qCDebug(lcDbusWindow) << "pendingRestoresAvailable: cannot emit, no pending restores";
        return;
    }

    // Check if panel geometry is ready, or if PhosphorScreens::ScreenManager doesn't exist (fallback)
    // If PhosphorScreens::ScreenManager instance is null, we proceed anyway with a warning - this is
    // better than blocking window restoration indefinitely
    if (m_service->screenManager() && !m_service->screenManager()->isPanelGeometryReady()) {
        qCDebug(lcDbusWindow) << "pendingRestoresAvailable: cannot emit, panel geometry not ready yet";
        return;
    }

    // Both conditions met (or PhosphorScreens::ScreenManager unavailable) - emit the signal
    m_pendingRestoresEmitted = true;
    if (!m_service->screenManager()) {
        qCWarning(lcDbusWindow)
            << "pendingRestoresAvailable: no PhosphorScreens::ScreenManager, geometry may be incorrect";
    } else {
        qCInfo(lcDbusWindow) << "Pending restores: panel geometry ready, notifying effect";
    }
    Q_EMIT pendingRestoresAvailable();
}

QString WindowTrackingAdaptor::detectScreenForZone(const QString& zoneId) const
{
    if (!m_layoutManager) {
        return QString();
    }
    auto zoneUuid = Utils::parseUuid(zoneId);
    if (!zoneUuid) {
        return QString();
    }

    // Search per-screen layouts to find which screen's layout contains this zone.
    // This correctly handles multi-monitor setups where each screen has a different layout.
    // Use effective screen IDs (virtual + physical) so virtual screen layouts are searched too.
    const QStringList effectiveIds =
        (m_service->screenManager() ? m_service->screenManager()->effectiveScreenIds() : QStringList());
    for (const QString& sid : effectiveIds) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(sid);
        PhosphorZones::Layout* layout =
            m_layoutManager->layoutForScreen(sid, desktop, m_layoutManager->currentActivity());
        if (layout && layout->zoneById(*zoneUuid)) {
            return sid;
        }
    }

    // Fallback: zone not in any screen-specific layout, try geometry projection
    // with the active layout (single-monitor or unconfigured multi-monitor)
    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        return QString();
    }
    PhosphorZones::Zone* zone = layout->zoneById(*zoneUuid);
    if (!zone) {
        return QString();
    }
    // Use effective screen IDs for virtual screen support
    auto* mgr = m_service->screenManager();
    if (mgr) {
        for (const QString& sid : mgr->effectiveScreenIds()) {
            if (!mgr->physicalScreenFor(sid).isValid()) {
                continue;
            }
            QRect effGeom = mgr->screenGeometry(sid);
            if (!effGeom.isValid()) {
                continue;
            }
            // Use effGeom consistently for both normalization and containment
            // so the zone center projection matches the containment bounds.
            // Using different geometries causes mismatches when available
            // geometry differs from full geometry.
            QRectF effGeomF(effGeom);
            QRectF normGeom = zone->normalizedGeometry(effGeomF);
            QPoint zoneCenter(effGeom.x() + qRound(normGeom.center().x() * effGeom.width()),
                              effGeom.y() + qRound(normGeom.center().y() * effGeom.height()));
            if (effGeom.contains(zoneCenter)) {
                return sid;
            }
        }
    } else {
        for (QScreen* screen : Utils::allScreens()) {
            QRectF refGeom = GeometryUtils::effectiveScreenGeometry(m_service->screenManager(), layout, screen);
            QRectF normGeom = zone->normalizedGeometry(refGeom);
            QPoint zoneCenter(refGeom.x() + qRound(normGeom.center().x() * refGeom.width()),
                              refGeom.y() + qRound(normGeom.center().y() * refGeom.height()));
            if (screen->geometry().contains(zoneCenter)) {
                return PhosphorScreens::ScreenIdentity::identifierFor(screen);
            }
        }
    }
    return QString();
}

} // namespace PlasmaZones
