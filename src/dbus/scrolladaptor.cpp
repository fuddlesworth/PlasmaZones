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
    return QStringList(screens.cbegin(), screens.cend());
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

void ScrollAdaptor::windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight)
{
    if (!ensureEngine("windowOpened")) {
        return;
    }
    m_engine->windowOpened(windowId, screenId, qMax(0, minWidth), qMax(0, minHeight));
}

void ScrollAdaptor::windowsOpenedBatch(const PhosphorProtocol::WindowOpenedList& entries)
{
    if (!ensureEngine("windowsOpenedBatch")) {
        return;
    }
    for (const PhosphorProtocol::WindowOpenedEntry& entry : entries) {
        m_engine->windowOpened(entry.windowId, entry.screenId, qMax(0, entry.minWidth), qMax(0, entry.minHeight));
    }
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

} // namespace PlasmaZones
