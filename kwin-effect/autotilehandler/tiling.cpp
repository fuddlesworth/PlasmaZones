// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tiling request handling and window centering for AutotileHandler.
// Part of AutotileHandler — split from autotilehandler.cpp for SRP.

#include "../autotilehandler.h"
#include "../plasmazoneseffect.h"
#include "../windowanimator.h"
#include "../dbus_constants.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void AutotileHandler::slotWindowsTileRequested(const QString& tileRequestsJson)
{
    if (tileRequestsJson.isEmpty()) {
        return;
    }

    ++m_autotileStaggerGeneration;
    m_autotileTargetZones.clear();

    // Snapshot the full global stacking order before tiling. After all
    // moveResize calls (which implicitly raise on KWin 6 / Wayland),
    // the onComplete callback re-raises in this order so non-tiled
    // windows (e.g. Settings) retain their stacking position.
    const auto allWindows = KWin::effects->stackingOrder();
    QVector<QPointer<KWin::EffectWindow>> savedGlobalStack;
    for (KWin::EffectWindow* w : allWindows) {
        savedGlobalStack.append(QPointer<KWin::EffectWindow>(w));
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(tileRequestsJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcEffect) << "Autotile windowsTileRequested: invalid JSON:" << parseError.errorString();
        return;
    }

    struct Entry
    {
        QString windowId;
        QRect geometry;
        KWin::EffectWindow* window = nullptr;
        QVector<KWin::EffectWindow*> candidates;
        bool isMonocle = false;
    };
    QVector<Entry> entries;

    const QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        QString windowId = obj.value(QLatin1String("windowId")).toString();

        // Float entries: overflow windows that should be restored to pre-autotile geometry.
        // Process inline — same cleanup as slotWindowFloatingChanged(windowId, true, ...).
        // Geometry is restored from the effect's local pre-autotile cache, avoiding
        // the per-window D-Bus roundtrip through the daemon's applyGeometryForFloat.
        if (obj.value(QLatin1String("floating")).toBool(false)) {
            const QString screenId = obj.value(QLatin1String("screenId")).toString();
            qCInfo(lcEffect) << "Autotile batch float:" << windowId << "screen:" << screenId;
            applyFloatCleanup(windowId);

            // Restore pre-autotile geometry from effect's local cache
            KWin::EffectWindow* floatWin = m_effect->findWindowById(windowId);
            if (floatWin && !screenId.isEmpty()) {
                auto screenIt = m_preAutotileGeometries.constFind(screenId);
                if (screenIt != m_preAutotileGeometries.constEnd()) {
                    const QString geoKey = findSavedGeometryKey(screenIt.value(), windowId);
                    if (!geoKey.isEmpty()) {
                        const QRectF& savedGeo = screenIt.value().value(geoKey);
                        m_effect->applySnapGeometry(floatWin, savedGeo.toRect());
                        qCInfo(lcEffect) << "Restored pre-autotile geometry for overflow" << windowId
                                         << savedGeo.toRect();
                    }
                }
            }
            continue;
        }

        QRect geo(obj.value(QLatin1String("x")).toInt(), obj.value(QLatin1String("y")).toInt(),
                  obj.value(QLatin1String("width")).toInt(), obj.value(QLatin1String("height")).toInt());
        QRect normalizedGeometry = geo.normalized();

        if (normalizedGeometry.width() <= 0 || normalizedGeometry.height() <= 0) {
            qCWarning(lcEffect) << "Autotile tile request: invalid geometry for" << windowId << normalizedGeometry;
            continue;
        }

        QVector<KWin::EffectWindow*> candidates = m_effect->findAllWindowsById(windowId);
        if (candidates.isEmpty()) {
            qCDebug(lcEffect) << "Autotile: window not found:" << windowId;
            continue;
        }
        KWin::EffectWindow* w = nullptr;
        if (candidates.size() == 1) {
            w = candidates.first();
        }
        Entry entry;
        entry.windowId = windowId;
        entry.geometry = normalizedGeometry;
        entry.window = w;
        entry.isMonocle = obj.value(QLatin1String("monocle")).toBool(false);
        if (candidates.size() > 1) {
            entry.candidates = candidates;
        }
        entries.append(entry);
    }

    // Disambiguate entries with multiple candidates (same appId)
    QHash<QString, QVector<int>> appIdToEntryIndices;
    for (int i = 0; i < entries.size(); ++i) {
        if (!entries[i].candidates.isEmpty()) {
            appIdToEntryIndices[PlasmaZonesEffect::extractAppId(entries[i].windowId)].append(i);
        }
    }
    for (const QVector<int>& indices : std::as_const(appIdToEntryIndices)) {
        if (indices.size() <= 1) {
            if (indices.size() == 1 && entries[indices[0]].candidates.size() > 1) {
                Entry& e = entries[indices[0]];
                QPoint targetCenter = e.geometry.center();
                KWin::EffectWindow* best = nullptr;
                qreal bestDist = 1e9;
                for (KWin::EffectWindow* c : std::as_const(e.candidates)) {
                    QPointF cf = c->frameGeometry().center();
                    qreal d = QPointF(targetCenter - cf).manhattanLength();
                    if (d < bestDist) {
                        bestDist = d;
                        best = c;
                    }
                }
                e.window = best;
            }
            continue;
        }
        QVector<KWin::EffectWindow*> candidates = entries[indices[0]].candidates;
        if (candidates.size() != indices.size()) {
            qCDebug(lcEffect) << "Autotile: stableId has" << indices.size() << "entries and" << candidates.size()
                              << "candidates; assigning by position";
        }
        QVector<int> sortedIndices = indices;
        std::sort(sortedIndices.begin(), sortedIndices.end(), [&entries](int a, int b) {
            return entries[a].geometry.x() < entries[b].geometry.x();
        });
        std::sort(candidates.begin(), candidates.end(), [](KWin::EffectWindow* a, KWin::EffectWindow* b) {
            return a->frameGeometry().x() < b->frameGeometry().x();
        });
        const int n = qMin(sortedIndices.size(), candidates.size());
        for (int i = 0; i < n; ++i) {
            entries[sortedIndices[i]].window = candidates[i];
        }
    }

    // Build snapshot with QPointer for safe deferred access
    struct TileSnap
    {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
        QString windowId;
        QString screenId;
        bool isMonocle = false;
    };
    QVector<TileSnap> toApply;
    QSet<QString> tiledWindowIds;
    QSet<QString> tileScreenIds;
    for (Entry& e : entries) {
        if (!e.window) {
            continue;
        }
        tiledWindowIds.insert(e.windowId);
        QString screenId = m_effect->getWindowScreenId(e.window);
        tileScreenIds.insert(screenId);
        toApply.append({QPointer<KWin::EffectWindow>(e.window), e.geometry, e.windowId, screenId, e.isMonocle});
    }

    const uint64_t gen = m_autotileStaggerGeneration;

    auto onComplete = [this, toApply, tiledWindowIds, tileScreenIds, savedGlobalStack, gen]() {
        if (m_autotileStaggerGeneration != gen) {
            return;
        }
        // Clean up windows that are no longer tiled: restore title bars and
        // remove from tiledWindows tracking. This subsumes the old borderless-
        // only cleanup since tiledWindows is a superset of borderlessWindows.
        const QSet<QString> untiled = m_border.tiledWindows - tiledWindowIds;
        for (const QString& wid : untiled) {
            KWin::EffectWindow* win = m_effect->findWindowById(wid);
            if (win && tileScreenIds.contains(m_effect->getWindowScreenId(win)) && !win->isMinimized()) {
                if (m_border.borderlessWindows.contains(wid)) {
                    setWindowBorderless(win, wid, false);
                }
            }
        }
        m_border.tiledWindows -= untiled;
        auto* ws = KWin::Workspace::self();
        if (ws) {
            // Restore the full global stacking order (all screens, all windows).
            // This ensures non-tiled windows (e.g. Settings KCM, windows on
            // other screens) retain their position instead of being buried.
            for (const auto& wPtr : savedGlobalStack) {
                if (wPtr && !wPtr->isDeleted()) {
                    KWin::Window* kw = wPtr->window();
                    if (kw) {
                        ws->raiseWindow(kw);
                    }
                }
            }

            // Restore saved autotile stacking order from previous session.
            // These raises go ON TOP of the global restore, preserving user's
            // z-order choices (e.g. floated window raised to front) across
            // mode toggles.
            for (const QString& screenId : tileScreenIds) {
                const QStringList savedOrder = m_savedAutotileStackingOrder.value(screenId);
                if (savedOrder.isEmpty()) {
                    continue;
                }
                for (const QString& windowId : savedOrder) {
                    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
                    if (w && !w->isDeleted()) {
                        KWin::Window* kw = w->window();
                        if (kw) {
                            ws->raiseWindow(kw);
                        }
                    }
                }
                m_savedAutotileStackingOrder.remove(screenId);
            }

            if (!m_pendingAutotileFocusWindowId.isEmpty()) {
                KWin::EffectWindow* focusWin = m_effect->findWindowById(m_pendingAutotileFocusWindowId);
                m_pendingAutotileFocusWindowId.clear();
                if (focusWin) {
                    KWin::Window* kw = focusWin->window();
                    if (kw) {
                        ws->raiseWindow(kw);
                    }
                }
            }
        }

        // After daemon restart, the raise loop above puts all tiled windows on
        // top, burying non-tiled windows (e.g. System Settings KCM) that had
        // focus. Re-activate the previously focused window to restore stacking.
        if (m_pendingReactivateWindow && !m_pendingReactivateWindow->isDeleted()) {
            KWin::effects->activateWindow(m_pendingReactivateWindow);
            m_pendingReactivateWindow = nullptr;
        }

        // Wayland centering is handled reactively by slotWindowFrameGeometryChanged
        // as soon as the client commits its constrained size — no deferred timer needed.

        // Refresh the active border for the focused window (tiledWindows may have changed)
        m_effect->updateAllBorders();
    };

    m_effect->applyStaggeredOrImmediate(
        toApply.size(),
        [this, toApply, gen](int i) {
            if (m_autotileStaggerGeneration != gen) {
                return;
            }
            const TileSnap& snap = toApply[i];
            if (!snap.window || snap.window->isDeleted()) {
                return;
            }
            saveAndRecordPreAutotileGeometry(snap.windowId, snap.screenId, snap.window->frameGeometry());
            qCInfo(lcEffect) << "Autotile tile request:" << snap.windowId << "QRect=" << snap.geometry;
            m_border.tiledWindows.insert(snap.windowId);
            if (m_border.hideTitleBars) {
                setWindowBorderless(snap.window, snap.windowId, true);
            }

            if (snap.isMonocle) {
                KWin::Window* kw = snap.window->window();
                if (kw) {
                    const bool wasAlreadyMaximized = (kw->maximizeMode() == KWin::MaximizeFull);
                    ++m_suppressMaximizeChanged;
                    kw->maximize(KWin::MaximizeFull);
                    if (!wasAlreadyMaximized) {
                        m_monocleMaximizedWindows.insert(snap.windowId);
                    }
                    m_effect->applySnapGeometry(snap.window, snap.geometry);
                    --m_suppressMaximizeChanged;
                }
            } else {
                unmaximizeMonocleWindow(snap.windowId);
                QRect geo = snap.geometry;

                // For Wayland windows being retiled to the same zone, skip the
                // moveResize if the window was previously centered in this zone.
                // This prevents flicker where the window jumps from its centered
                // position back to the zone origin, then gets re-centered 200ms later.
                // It also avoids flooding the Wayland client with configure events
                // which can freeze terminals like Ghostty.
                bool skipMoveResize = false;
                if (snap.window->isWaylandClient()) {
                    auto prevIt = m_centeredWaylandZones.find(snap.windowId);
                    if (prevIt != m_centeredWaylandZones.end() && prevIt.value() == geo) {
                        const QRectF actual = snap.window->frameGeometry();
                        // Window is still within the zone bounds — already centered
                        if (actual.x() >= geo.x() - 1 && actual.y() >= geo.y() - 1 && actual.right() <= geo.right() + 2
                            && actual.bottom() <= geo.bottom() + 2) {
                            skipMoveResize = true;
                            qCDebug(lcEffect) << "Skipping redundant moveResize for centered Wayland window"
                                              << snap.windowId << "zone=" << geo;
                        }
                    }
                }

                if (!skipMoveResize) {
                    m_centeredWaylandZones.remove(snap.windowId);
                    m_effect->applySnapGeometry(snap.window, geo);
                }
            }

            if (!snap.isMonocle && snap.window->isWaylandClient()) {
                m_autotileTargetZones[snap.windowId] = snap.geometry;
            }
        },
        onComplete);
}

void AutotileHandler::slotWindowFrameGeometryChanged(KWin::EffectWindow* w, const QRectF& oldGeometry)
{
    Q_UNUSED(oldGeometry)
    if (!w || m_autotileTargetZones.isEmpty()) {
        return;
    }

    const QString windowId = m_effect->getWindowId(w);
    auto it = m_autotileTargetZones.find(windowId);
    if (it == m_autotileTargetZones.end()) {
        return;
    }

    const QRect& targetZone = it.value();
    const QRectF actual = w->frameGeometry();

    constexpr qreal MinCenteringDelta = 3.0;

    const qreal dw = targetZone.width() - actual.width();
    const qreal dh = targetZone.height() - actual.height();

    // Window fills the zone (or close enough) — no centering needed; consume entry
    if (qAbs(dw) <= MinCenteringDelta && qAbs(dh) <= MinCenteringDelta) {
        qCDebug(lcEffect) << "Autotile centering: matched" << windowId << "dw=" << dw << "dh=" << dh;
        m_autotileTargetZones.erase(it);
        return;
    }

    // Window doesn't match zone — center it within the zone so it's visually
    // balanced rather than stuck at the zone origin.
    // Clamp offsets to non-negative: when the window is LARGER than the zone
    // (oversized, dx < 0), left/top-align instead of centering. Centering an
    // oversized window pushes it to a negative position (off-screen left/top),
    // which is worse than a slight overflow to the right/bottom. The daemon
    // receives the min-size report below and will retile with adjusted zones.
    const qreal dx = qMax(0.0, dw / 2.0);
    const qreal dy = qMax(0.0, dh / 2.0);
    const QRectF centered(targetZone.x() + dx, targetZone.y() + dy, actual.width(), actual.height());

    // Already at the centered position — record and consume
    if (qAbs(actual.x() - centered.x()) < 1.0 && qAbs(actual.y() - centered.y()) < 1.0) {
        m_centeredWaylandZones[windowId] = targetZone;
        m_autotileTargetZones.erase(it);
        return;
    }

    KWin::Window* kw = w->window();
    if (!kw) {
        // No KWin::Window — consume stale entry to prevent perpetual lookups
        m_autotileTargetZones.erase(it);
        return;
    }

    qCInfo(lcEffect) << "Centering autotile window" << windowId << "actual=" << actual.size()
                     << "zone=" << targetZone.size() << "offset=(" << dx << "," << dy << ")";

    // Window refused to shrink below its actual size — report its declared
    // minimum to the daemon so future retiles can account for it. Only report
    // when the window is larger than the zone (negative delta = oversized).
    //
    // IMPORTANT: Only use the window's declared minSize() from the compositor.
    // The frame geometry is the current size, which may be transiently larger
    // during resize animations (Wayland configure round-trips) or media player
    // loading. Reporting the frame geometry as the min-size creates a feedback
    // loop: inflated min → expanded zone → window fills expanded zone →
    // inflated min confirmed → ratio stuck.
    //
    // Previously, windows without a declared min-size fell back to
    // targetZone.width() as a bounded hint. This caused the same feedback
    // loop: the zone width became the stored min-size, which then prevented
    // the algorithm from reducing the zone on subsequent retiles — even when
    // the user adjusted the split ratio or a screen geometry change required
    // reflow. The stale min-size persisted until the window was removed or
    // unfloated (minimize+restore), making the ratio appear "stuck."
    //
    // Without the fallback, apps that don't declare a min-size simply won't
    // get min-size enforcement from this path. They still get the initial
    // min-size from the windowOpened D-Bus call (kw->minSize() at open time),
    // and the centering code handles the visual placement correctly.
    if (dw < -MinCenteringDelta || dh < -MinCenteringDelta) {
        const QSizeF declaredMin = kw->minSize();
        int discoveredMinW = 0;
        int discoveredMinH = 0;
        if (dw < -MinCenteringDelta && declaredMin.width() > 0) {
            discoveredMinW = qCeil(declaredMin.width());
        }
        if (dh < -MinCenteringDelta && declaredMin.height() > 0) {
            discoveredMinH = qCeil(declaredMin.height());
        }
        if (discoveredMinW > 0 || discoveredMinH > 0) {
            reportDiscoveredMinSize(windowId, discoveredMinW, discoveredMinH);
        }
    }

    // Erase BEFORE moveResize to prevent re-entrancy: moveResize emits
    // windowFrameGeometryChanged synchronously, which would re-enter
    // this slot and find the entry still present → infinite recursion → crash.
    m_centeredWaylandZones[windowId] = targetZone;
    m_autotileTargetZones.erase(it);
    m_effect->m_windowAnimator->removeAnimation(w);
    kw->moveResize(centered);
}

void AutotileHandler::slotFocusWindowRequested(const QString& windowId)
{
    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "Autotile: window not found for focus request:" << windowId;
        return;
    }

    m_pendingAutotileFocusWindowId = windowId;
    KWin::effects->activateWindow(w);
}

QRect AutotileHandler::applyBorderInset(const QRect& geo) const
{
    return geo.adjusted(m_border.width, m_border.width, -m_border.width, -m_border.width);
}

bool AutotileHandler::shouldInsetForBorder(const QString& windowId, const QRect& geo) const
{
    return shouldApplyBorderInset(windowId) && geo.width() > 2 * m_border.width && geo.height() > 2 * m_border.width;
}

void AutotileHandler::reportDiscoveredMinSize(const QString& windowId, int minWidth, int minHeight)
{
    if (minWidth <= 0 && minHeight <= 0) {
        return;
    }

    qCInfo(lcEffect) << "Discovered min size for" << windowId << ":" << minWidth << "x" << minHeight
                     << "- reporting to daemon for future retiles";

    m_effect->fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("windowMinSizeUpdated"),
                                    {windowId, minWidth, minHeight}, QStringLiteral("windowMinSizeUpdated"));
}

} // namespace PlasmaZones
