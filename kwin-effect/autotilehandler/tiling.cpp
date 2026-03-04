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
#include <QTimer>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void AutotileHandler::slotWindowsTileRequested(const QString& tileRequestsJson)
{
    if (tileRequestsJson.isEmpty()) {
        return;
    }

    ++m_autotileStaggerGeneration;
    m_autotileTargetZones.clear();

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

    // Disambiguate entries with multiple candidates (same stableId)
    QHash<QString, QVector<int>> stableIdToEntryIndices;
    for (int i = 0; i < entries.size(); ++i) {
        if (!entries[i].candidates.isEmpty()) {
            stableIdToEntryIndices[PlasmaZonesEffect::extractStableId(entries[i].windowId)].append(i);
        }
    }
    for (const QVector<int>& indices : std::as_const(stableIdToEntryIndices)) {
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
    struct TileSnap {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
        QString windowId;
        QString screenName;
        bool isMonocle = false;
    };
    QVector<TileSnap> toApply;
    QSet<QString> tiledWindowIds;
    QString tileScreenName;
    for (Entry& e : entries) {
        if (!e.window) {
            continue;
        }
        tiledWindowIds.insert(e.windowId);
        QString screenName = m_effect->getWindowScreenName(e.window);
        if (tileScreenName.isEmpty()) {
            tileScreenName = screenName;
        }
        toApply.append({QPointer<KWin::EffectWindow>(e.window), e.geometry, e.windowId, screenName, e.isMonocle});
    }

    const uint64_t gen = m_autotileStaggerGeneration;

    auto onComplete = [this, toApply, tiledWindowIds, tileScreenName, gen]() {
        if (m_autotileStaggerGeneration != gen) {
            return;
        }
        if (m_autotileHideTitleBars && !m_borderlessWindows.isEmpty()) {
            const QSet<QString> toRestore = m_borderlessWindows - tiledWindowIds;
            for (const QString& wid : toRestore) {
                KWin::EffectWindow* win = m_effect->findWindowById(wid);
                if (win && m_effect->getWindowScreenName(win) == tileScreenName && !win->isMinimized()) {
                    setWindowBorderless(win, wid, false);
                }
            }
        }
        auto* ws = KWin::Workspace::self();
        if (ws) {
            for (int i = toApply.size() - 1; i >= 0; --i) {
                const TileSnap& snap = toApply[i];
                if (!snap.window) {
                    continue;
                }
                KWin::Window* kw = snap.window->window();
                if (kw) {
                    ws->raiseWindow(kw);
                }
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

        if (!m_autotileTargetZones.isEmpty()) {
            QTimer::singleShot(150, this, [this, gen]() {
                if (m_autotileStaggerGeneration != gen) {
                    return;
                }
                centerUndersizedAutotileWindows();
                if (!m_autotileTargetZones.isEmpty()) {
                    QTimer::singleShot(250, this, [this, gen]() {
                        if (m_autotileStaggerGeneration != gen) {
                            return;
                        }
                        centerUndersizedAutotileWindows();
                    });
                }
            });
        }
    };

    m_effect->applyStaggeredOrImmediate(toApply.size(), [this, toApply, gen](int i) {
        if (m_autotileStaggerGeneration != gen) {
            return;
        }
        const TileSnap& snap = toApply[i];
        if (!snap.window || snap.window->isDeleted()) {
            return;
        }
        saveAndRecordPreAutotileGeometry(snap.windowId, snap.screenName, snap.window->frameGeometry());
        qCInfo(lcEffect) << "Autotile tile request:" << snap.windowId << "QRect=" << snap.geometry;
        if (m_autotileHideTitleBars) {
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
            m_effect->applySnapGeometry(snap.window, snap.geometry);
        }

        if (!snap.isMonocle && snap.window->isWaylandClient()) {
            m_autotileTargetZones[snap.windowId] = snap.geometry;
        }
    }, onComplete);
}

void AutotileHandler::centerUndersizedAutotileWindows()
{
    QHash<QString, QRect> targets;
    targets.swap(m_autotileTargetZones);

    constexpr qreal MinCenteringDelta = 3.0;
    constexpr qreal MaxCenteringDelta = 64.0;

    for (auto it = targets.constBegin(); it != targets.constEnd(); ++it) {
        const QString& windowId = it.key();
        const QRect& targetZone = it.value();

        KWin::EffectWindow* w = m_effect->findWindowById(windowId);
        if (!w || w->isDeleted() || w->isFullScreen()) {
            continue;
        }

        const QRectF actual = w->frameGeometry();
        const qreal dw = targetZone.width() - actual.width();
        const qreal dh = targetZone.height() - actual.height();

        if (qAbs(dw) <= MinCenteringDelta && qAbs(dh) <= MinCenteringDelta) {
            m_autotileTargetZones[windowId] = targetZone;
            continue;
        }

        if ((dw > MinCenteringDelta || dh > MinCenteringDelta)
            && qAbs(dw) < MaxCenteringDelta && qAbs(dh) < MaxCenteringDelta) {
            const qreal dx = qMax(0.0, dw) / 2.0;
            const qreal dy = qMax(0.0, dh) / 2.0;
            const QRectF centered(targetZone.x() + dx, targetZone.y() + dy,
                                  actual.width(), actual.height());

            KWin::Window* kw = w->window();
            if (kw) {
                qCInfo(lcEffect) << "Centering undersized autotile window" << windowId
                                 << "actual=" << actual.size() << "zone=" << targetZone.size()
                                 << "offset=(" << dx << "," << dy << ")";
                m_effect->m_windowAnimator->removeAnimation(w);
                kw->moveResize(centered);
            }
        }
    }
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

} // namespace PlasmaZones
