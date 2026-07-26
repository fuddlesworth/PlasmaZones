// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollingadaptor.h"

#include "core/platform/logging.h"

#include <PhosphorScrollEngine/ScrollEngine.h>

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
    const QSet<QString> screens = m_engine->activeScreens();
    return QStringList(screens.cbegin(), screens.cend());
}

void ScrollingAdaptor::clearEngine()
{
    if (m_engine) {
        disconnect(m_engine, nullptr, this, nullptr);
        m_engine = nullptr;
    }
}

} // namespace PlasmaZones
