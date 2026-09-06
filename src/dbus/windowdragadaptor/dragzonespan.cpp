// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowdragadaptor.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QScreen>
#include <QUuid>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/interfaces/interfaces.h"
#include "core/platform/logging.h"
#include "core/utils/geometryutils.h"

/**
 * @file
 * @brief The two multi-zone modifier handlers: zone span (paint a rectangle of
 * zones) and multi-zone (union two zones under the cursor).
 *
 * Split out of drag.cpp to keep that file inside the file-size ceiling. Both
 * run off a dragMoved tick, both resolve their screen through
 * prepareHandlerContext, and both write the same m_currentMultiZoneGeometry /
 * m_paintedZoneIds state the drop reads, so they belong together and apart
 * from the tick that dispatches them.
 */

namespace PlasmaZones {

void WindowDragAdaptor::handleZoneSpanModifier(int x, int y)
{
    QScreen* screen = nullptr;
    QString screenId;
    auto* layout = prepareHandlerContext(x, y, screen, screenId);
    if (!layout) {
        return;
    }

    // Clear stale multi-zone state from proximity mode when transitioning to paint-to-span
    if (m_isMultiZoneMode && m_paintedZoneIds.isEmpty()) {
        m_currentAdjacentZoneIds.clear();
        m_isMultiZoneMode = false;
        m_currentMultiZoneGeometry = QRect();
    }

    // Find zone at cursor position using layout's smallest-area heuristic
    // (zone geometry already recalculated to absolute coords by prepareHandlerContext)
    PhosphorZones::Zone* foundZone = layout->zoneAtPoint(QPointF(x, y));

    // Accumulate painted zones (never remove during a paint drag)
    if (foundZone) {
        m_paintedZoneIds.insert(foundZone->id());
    }

    // Build zone list from painted zones, then expand using same raycast algorithm as editor
    if (!m_paintedZoneIds.isEmpty()) {
        QVector<PhosphorZones::Zone*> paintedZones;
        for (auto* zone : layout->zones()) {
            if (m_paintedZoneIds.contains(zone->id())) {
                paintedZones.append(zone);
            }
        }

        if (!paintedZones.isEmpty()) {
            // Use same raycasting/intersection algorithm as detectMultiZone and editor:
            // expand to include all zones that intersect the bounding rect of painted zones
            m_zoneDetector->setLayout(layout);
            QVector<PhosphorZones::Zone*> zonesToSnap = m_zoneDetector->expandPaintedZonesToRect(paintedZones);

            if (zonesToSnap.isEmpty()) {
                return;
            }

            QRectF combinedGeom = computeCombinedZoneGeometry(zonesToSnap, screen, layout, screenId);

            // Update multi-zone state from expanded zones (what we actually snap to)
            QVector<QUuid> zoneIds;
            zoneIds.reserve(zonesToSnap.size());
            for (auto* zone : zonesToSnap) {
                zoneIds.append(zone->id());
            }

            m_currentZoneId = zonesToSnap.first()->id().toString();
            // The screen the geometry below is absolute on. This is the
            // co-key of handleMultiZoneModifier's change gate: leaving it
            // stale lets a later same-zone-id tick on another screen skip
            // the refresh and keep this screen's absolute rect.
            m_currentZoneScreenId = screenId;
            m_currentAdjacentZoneIds = zoneIds;
            m_isMultiZoneMode = (zonesToSnap.size() > 1);
            m_currentMultiZoneGeometry = GeometryUtils::snapToRect(combinedGeom);
            if (zonesToSnap.size() == 1) {
                m_currentZoneGeometry = GeometryUtils::snapToRect(combinedGeom);
            }

            // Highlight expanded zones (raycasted) so user sees what they are actually snapping to
            m_zoneDetector->highlightZones(zonesToSnap);
            m_overlayService->highlightZones(zoneIdsToStringList(zoneIds));
        }
    }
}

void WindowDragAdaptor::handleMultiZoneModifier(int x, int y)
{
    QScreen* screen = nullptr;
    QString screenId;
    auto* layout = prepareHandlerContext(x, y, screen, screenId);
    if (!layout) {
        return;
    }

    m_zoneDetector->setLayout(layout);

    // Drop proxy first: a shell-registered miniature of this screen's zones
    // (registerDropProxy) stands in for the real zones while the cursor is
    // inside it. The cell under the cursor resolves to its real zone and
    // takes the single-zone arm below unchanged, so the overlay lights the
    // full-size zone and the drop commits there; inside the miniature but
    // over no cell, or over a cell whose zone the current layout does not
    // have (a stale registration), is no target. Multi-zone detection never
    // runs over the proxy: the miniature draws single cells only.
    const DropProxyRegistry::Resolution proxied = resolveDropProxyAt(screen, screenId, x, y);
    if (proxied.hit != DropProxyRegistry::Hit::Outside) {
        PhosphorZones::Zone* zone =
            proxied.hit == DropProxyRegistry::Hit::Cell ? layout->zoneById(QUuid(proxied.zoneId)) : nullptr;
        if (zone) {
            applySingleZoneTarget(zone, screen, screenId, layout);
        } else {
            clearZoneTarget();
        }
        return;
    }

    // Convert cursor position to screen-relative coordinates for detection
    QPointF cursorPos(static_cast<qreal>(x), static_cast<qreal>(y));

    // Call detectMultiZone instead of detectZone
    PhosphorZones::ZoneDetectionResult result = m_zoneDetector->detectMultiZone(cursorPos);

    if (result.isMultiZone && result.primaryZone) {
        // Multi-zone detected
        QString primaryZoneId = result.primaryZone->id().toString();
        QVector<QUuid> newAdjacentZoneIds;

        // Collect zone IDs from adjacent zones
        newAdjacentZoneIds.append(result.primaryZone->id());
        for (PhosphorZones::Zone* zone : result.adjacentZones) {
            if (zone && zone->id() != result.primaryZone->id()) {
                newAdjacentZoneIds.append(zone->id());
            }
        }

        // Only update if zone selection changed. screenId is part of the key:
        // the same zone UUID on a different monitor is a different rect, and
        // without it a crossing between two screens sharing a layout skips the
        // update and strands the previous screen's geometry.
        if (primaryZoneId != m_currentZoneId || screenId != m_currentZoneScreenId
            || newAdjacentZoneIds != m_currentAdjacentZoneIds) {
            m_currentZoneId = primaryZoneId;
            m_currentZoneScreenId = screenId;
            m_currentAdjacentZoneIds = newAdjacentZoneIds;
            m_isMultiZoneMode = true;

            // Build de-duplicated zone list for geometry and highlighting
            QVector<PhosphorZones::Zone*> zonesToHighlight;
            zonesToHighlight.append(result.primaryZone);
            for (PhosphorZones::Zone* zone : result.adjacentZones) {
                if (zone && zone != result.primaryZone) {
                    zonesToHighlight.append(zone);
                }
            }

            m_currentMultiZoneGeometry =
                GeometryUtils::snapToRect(computeCombinedZoneGeometry(zonesToHighlight, screen, layout, screenId));
            m_zoneDetector->highlightZones(zonesToHighlight);
            m_overlayService->highlightZones(zoneIdsToStringList(m_currentAdjacentZoneIds));
        }
    } else if (result.primaryZone) {
        // Single zone detected (fallback from multi-zone detection)
        applySingleZoneTarget(result.primaryZone, screen, screenId, layout);
    } else {
        clearZoneTarget();
    }
}

void WindowDragAdaptor::applySingleZoneTarget(PhosphorZones::Zone* zone, QScreen* screen, const QString& screenId,
                                              PhosphorZones::Layout* layout)
{
    const QString zoneId = zone->id().toString();
    if (zoneId != m_currentZoneId || screenId != m_currentZoneScreenId || m_isMultiZoneMode) {
        m_currentZoneId = zoneId;
        m_currentZoneScreenId = screenId;
        m_currentAdjacentZoneIds.clear();
        m_isMultiZoneMode = false;
        m_zoneDetector->highlightZone(zone);
        m_overlayService->highlightZone(zoneId);

        m_currentZoneGeometry = GeometryUtils::getZoneGeometryForScreen(m_screenManager, zone, screen, screenId, layout,
                                                                        m_settings, m_layoutManager);
        m_currentMultiZoneGeometry = QRect();
    }
}

void WindowDragAdaptor::clearZoneTarget()
{
    // No zone under the cursor: drop the target only when there was one, so
    // an idle tick does not re-clear the overlay every frame.
    if (!m_currentZoneId.isEmpty() || m_isMultiZoneMode) {
        m_currentZoneId.clear();
        m_currentZoneScreenId.clear();
        m_currentAdjacentZoneIds.clear();
        m_isMultiZoneMode = false;
        m_currentZoneGeometry = QRect();
        m_currentMultiZoneGeometry = QRect();
        m_zoneDetector->clearHighlights();
        m_overlayService->clearHighlight();
    }
}

DropProxyRegistry::Resolution WindowDragAdaptor::resolveDropProxyAt(QScreen* screen, const QString& screenId, int x,
                                                                    int y) const
{
    if (!screen || m_dropProxies.count() == 0) {
        return {};
    }
    // Proxies are registered per physical output (the shell's bar lives on
    // one), in that output's own pixels. A virtual-screen id resolves to its
    // physical output, and the cursor is re-based onto the output's origin.
    const QString physicalId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
    return m_dropProxies.resolve(physicalId, QPoint(x, y) - screen->geometry().topLeft());
}

void WindowDragAdaptor::watchDropProxyOwner(const QString& service, const QString& screenId)
{
    m_dropProxyOwners.insert(screenId, service);
    // One watcher per registering peer. Registering again for the same screen
    // replaces the owner entry above, and the stale watcher then finds no
    // screen still attributed to it and retires quietly.
    auto* watcher = new QDBusServiceWatcher(service, QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForUnregistration, this);
    QObject::connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this, watcher](const QString& gone) {
        const QStringList orphaned = m_dropProxyOwners.keys(gone);
        for (const QString& screenId : orphaned) {
            qCInfo(lcDbusWindow) << "drop proxy owner" << gone << "went away; retiring the proxy on" << screenId;
            m_dropProxies.unregisterProxy(screenId);
            m_dropProxyOwners.remove(screenId);
        }
        watcher->deleteLater();
    });
}

void WindowDragAdaptor::registerDropProxy(const QString& screenId, const QString& proxyJson)
{
    if (!m_dropProxies.registerProxy(screenId, proxyJson)) {
        return;
    }
    qCDebug(lcDbusWindow) << "registerDropProxy: screen" << screenId;
    // A proxy outlives every individual drag, so it has to be tied to the life
    // of the peer that asked for it. Direct (non-D-Bus) calls — the unit tests
    // invoke this slot through QMetaObject — have no peer to watch.
    if (calledFromDBus()) {
        const QString sender = message().service();
        if (!sender.isEmpty()) {
            watchDropProxyOwner(sender, screenId);
        }
    }
}

void WindowDragAdaptor::unregisterDropProxy(const QString& screenId)
{
    m_dropProxies.unregisterProxy(screenId);
    m_dropProxyOwners.remove(screenId);
}

} // namespace PlasmaZones
