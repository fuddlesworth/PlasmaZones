// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrolladaptor.h"

#include "core/logging.h"

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QSet>

namespace PlasmaZones {

ScrollAdaptor::ScrollAdaptor(PhosphorScrollEngine::ScrollEngine* engine, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_engine(engine)
{
    if (!m_engine) {
        qCWarning(lcDaemon) << "ScrollAdaptor created with null engine";
    }
}

QStringList ScrollAdaptor::scrollScreens() const
{
    if (!m_engine) {
        return {};
    }
    const QSet<QString> screens = m_engine->activeScreens();
    // Sorted for a deterministic property value — QSet iteration order is
    // unspecified.
    QStringList ids(screens.cbegin(), screens.cend());
    ids.sort();
    return ids;
}

void ScrollAdaptor::clearEngine()
{
    m_engine = nullptr;
}

bool ScrollAdaptor::ensureEngine(const char* methodName) const
{
    if (!m_engine) {
        qCWarning(lcDaemon) << "ScrollAdaptor::" << methodName << "called with no engine";
        return false;
    }
    return true;
}

void ScrollAdaptor::windowOpened(const QString& windowId, const QString& screenId)
{
    if (!ensureEngine("windowOpened")) {
        return;
    }
    m_engine->windowOpened(windowId, screenId);
}

void ScrollAdaptor::windowsOpenedBatch(const PhosphorProtocol::WindowOpenedList& entries)
{
    if (!ensureEngine("windowsOpenedBatch")) {
        return;
    }
    // WindowOpenedList is shared with org.plasmazones.Autotile and carries
    // per-entry minWidth/minHeight; scroll's strip model is size-agnostic and
    // ignores them (non-resizable windows are fitted to the tile slot
    // effect-side), so only windowId/screenId are forwarded.
    QSet<QString> liveWindowIds;
    liveWindowIds.reserve(entries.size());
    for (const PhosphorProtocol::WindowOpenedEntry& entry : entries) {
        m_engine->windowOpened(entry.windowId, entry.screenId);
        liveWindowIds.insert(entry.windowId);
    }
    // The effect's first batch after a daemon (re)connect is the complete live
    // scroll-window set across every scroll screen — reconcile a just-restored
    // strip against it so a window closed while the daemon was down leaves no
    // phantom column. A no-op on every later (routine) batch.
    m_engine->reconcileRestoredWindows(liveWindowIds);
}

void ScrollAdaptor::windowClosed(const QString& windowId)
{
    if (!ensureEngine("windowClosed")) {
        return;
    }
    m_engine->windowClosed(windowId);
}

void ScrollAdaptor::notifyWindowFocused(const QString& windowId, const QString& screenId)
{
    if (!ensureEngine("notifyWindowFocused")) {
        return;
    }
    m_engine->windowFocused(windowId, screenId);
}

void ScrollAdaptor::windowMinimizedChanged(const QString& windowId, bool minimized)
{
    if (!ensureEngine("windowMinimizedChanged")) {
        return;
    }
    m_engine->windowMinimizedChanged(windowId, minimized);
}

void ScrollAdaptor::windowDropped(const QString& draggedWindowId, const QString& anchorWindowId, bool placeAfter)
{
    if (!ensureEngine("windowDropped")) {
        return;
    }
    m_engine->windowDropped(draggedWindowId, anchorWindowId, placeAfter);
}

} // namespace PlasmaZones
