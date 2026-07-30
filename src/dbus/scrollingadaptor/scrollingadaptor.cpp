// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollingadaptor.h"

#include "core/platform/logging.h"

#include <algorithm>

#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

ScrollingAdaptor::ScrollingAdaptor(PhosphorScrollEngine::ScrollEngine* engine, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_engine(engine)
{
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
    // resolveOperationScreen's fallback.
    if (!m_engine || screenId.isEmpty() || (delta != -1 && delta != 1)) {
        return;
    }
    if (!m_engine->isActiveOnScreen(screenId)) {
        return;
    }
    m_engine->focusInDirection(delta < 0 ? QStringLiteral("left") : QStringLiteral("right"),
                               PhosphorEngine::NavigationContext{QString(), screenId});
}

QString ScrollingAdaptor::visibleStripJson(const QString& screenId)
{
    if (!m_engine || screenId.isEmpty()) {
        return QStringLiteral("[]");
    }
    // Same screen gate as focusColumn: header and XML both promise an empty
    // array for a screen that is not scrolling, and a stale state left by an
    // earlier scrolling session would otherwise be described as current.
    if (!m_engine->isActiveOnScreen(screenId)) {
        return QStringLiteral("[]");
    }
    QJsonArray arr;
    QVector<int> columnNumbers;
    const QVector<QRectF> rects = m_engine->visibleTileRectsRelative(screenId, &columnNumbers);
    for (int i = 0; i < rects.size(); ++i) {
        const QRectF& r = rects.at(i);
        QJsonObject obj;
        obj[QLatin1String("x")] = r.x();
        obj[QLatin1String("y")] = r.y();
        obj[QLatin1String("width")] = r.width();
        obj[QLatin1String("height")] = r.height();
        // 1-based visible column slot — the scroll zone number.
        obj[QLatin1String("zoneNumber")] = (i < columnNumbers.size()) ? columnNumbers.at(i) : (i + 1);
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void ScrollingAdaptor::clearEngine()
{
    // Reset the dedup gate with the engine: the property read reports {}
    // from here on, and a stale gate value could suppress the restarted
    // session's first genuine broadcast.
    m_lastBroadcastScreens.clear();
    if (m_engine) {
        disconnect(m_engine, &PhosphorScrollEngine::ScrollEngine::scrollingScreensChanged, this, nullptr);
        m_engine = nullptr;
    }
}

} // namespace PlasmaZones
