// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollingadaptor.h"

#include "core/platform/logging.h"

#include <algorithm>

#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

ScrollingAdaptor::ScrollingAdaptor(PhosphorScrollEngine::ScrollEngine* engine, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_engine(engine)
{
    // PRECONDITION: the adaptor is constructed before the engine can push a
    // screen set, so the empty m_lastBroadcastScreens below is a true "nothing
    // broadcast yet" and not a gate seeded with a set we already missed. The
    // daemon guarantees it — initEnginesAndWiring news this adaptor in the same
    // pass that creates the engine, before updateEngineScreens runs. A
    // late-constructed adaptor would swallow a first broadcast of the empty
    // set, and there is no re-wire path that could reach that state (clearEngine
    // is terminal; the next cycle deletes and re-news the whole adaptor set).
    if (!m_engine) {
        qCWarning(lcDbusScrolling) << "ScrollingAdaptor created with null engine";
        return;
    }
    connect(m_engine, &PhosphorScrollEngine::ScrollEngine::scrollingScreensChanged, this,
            [this](const QStringList& screenIds, bool /*isDesktopSwitch*/) {
                // Change-gated: the engine's identical-set desktop-switch
                // re-emit exists for the TILING channel's catch-scan; this
                // interface is a pure Mode discriminator, so an unchanged
                // set must not hit the bus (emit-on-change rule). The
                // isDesktopSwitch flag is deliberately not carried on this
                // wire — the effect's handler has no per-screen transitions
                // to skip.
                if (screenIds == m_lastBroadcastScreens) {
                    return;
                }
                m_lastBroadcastScreens = screenIds;
                Q_EMIT scrollingScreensChanged(screenIds);
            });
}

QStringList ScrollingAdaptor::scrollingScreens() const
{
    if (!m_engine) {
        return {};
    }
    // Sorted like the scrollingScreensChanged signal payload, so property
    // reads and signal deltas compare equal for the same set.
    const QSet<QString> screens = m_engine->activeScreens();
    QStringList out(screens.cbegin(), screens.cend());
    std::sort(out.begin(), out.end());
    return out;
}

void ScrollingAdaptor::focusColumn(const QString& screenId, int delta)
{
    // Wire-boundary validation: only the two adjacent steps are meaningful,
    // and the screen gate keeps a wheel event from a non-scrolling monitor
    // from being redirected onto the active scrolling screen by
    // resolveOperationScreen's fallback. The isEmpty check is kept even though
    // isActiveOnScreen would reject "" anyway: rejecting a malformed argument
    // at the wire boundary should not depend on how a callee treats it.
    if (!m_engine || screenId.isEmpty() || (delta != -1 && delta != 1)) {
        return;
    }
    if (!m_engine->isActiveOnScreen(screenId)) {
        return;
    }
    m_engine->focusInDirection(delta < 0 ? QStringLiteral("left") : QStringLiteral("right"),
                               PhosphorEngine::NavigationContext{QString(), screenId});
}

QString ScrollingAdaptor::visibleStripJson(const QString& screenId) const
{
    // isEmpty kept for the same wire-boundary reason as in focusColumn.
    if (!m_engine || screenId.isEmpty()) {
        return QStringLiteral("[]");
    }
    // Same screen gate as focusColumn, making the "empty for a screen that is
    // not scrolling" half of the XML contract explicit at this call site.
    // LOAD-BEARING, not belt and braces: setActiveScreens prunes only the
    // CURRENT context's key, so a strip built under a sibling context (another
    // virtual desktop or activity) survives the screen leaving the active set
    // and resolves again the moment that context comes back (engine_core.cpp).
    // Without this gate a screen that is no longer scrolling would answer with
    // that surviving strip.
    if (!m_engine->isActiveOnScreen(screenId)) {
        return QStringLiteral("[]");
    }
    QJsonArray arr;
    const QVector<QRectF> rects = m_engine->visibleTileRectsRelative(screenId);
    for (int i = 0; i < rects.size(); ++i) {
        const QRectF& r = rects.at(i);
        QJsonObject obj;
        obj[PhosphorZones::ZoneJsonKeys::X] = r.x();
        obj[PhosphorZones::ZoneJsonKeys::Y] = r.y();
        obj[PhosphorZones::ZoneJsonKeys::Width] = r.width();
        obj[PhosphorZones::ZoneJsonKeys::Height] = r.height();
        // 1-based visible tile slot — the scroll zone number (rects arrive
        // in zone-number order per the visibleTiles contract, so the index
        // IS the number).
        obj[PhosphorZones::ZoneJsonKeys::ZoneNumber] = i + 1;
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void ScrollingAdaptor::clearEngine()
{
    if (m_engine) {
        disconnect(m_engine, &PhosphorScrollEngine::ScrollEngine::scrollingScreensChanged, this, nullptr);
        m_engine = nullptr;
    }
}

} // namespace PlasmaZones
