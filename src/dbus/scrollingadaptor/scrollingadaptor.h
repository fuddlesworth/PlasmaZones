// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QStringList>

namespace PhosphorScrollEngine {
class ScrollEngine;
}

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for the scrolling placement engine
 *
 * Provides D-Bus interface: org.plasmazones.Scrolling
 *
 * The scroll-SPECIFIC wire surface: the scrolling screen set the KWin
 * effect uses as its Mode-stamp discriminator, and the home for future
 * columnar methods. Window lifecycle and tile-request traffic for
 * scrolling screens deliberately stays on org.plasmazones.Tiling — the
 * effect keeps ONE engine-managed screen set and one geometry pipeline
 * for both tiling-family engines, and TilingAdaptor routes per screen.
 */
class PLASMAZONES_EXPORT ScrollingAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Scrolling")

    Q_PROPERTY(QStringList scrollingScreens READ scrollingScreens NOTIFY scrollingScreensChanged)

public:
    explicit ScrollingAdaptor(PhosphorScrollEngine::ScrollEngine* engine, QObject* parent = nullptr);
    ~ScrollingAdaptor() override = default;

    QStringList scrollingScreens() const;

    /// Clear the engine pointer during shutdown (same late-D-Bus-call
    /// contract as the sibling adaptors' clearEngine).
    void clearEngine();

Q_SIGNALS:
    /**
     * @brief Emitted when the set of screens using the scrolling engine changes
     * @param screenIds List of screen IDs currently in scrolling mode
     */
    void scrollingScreensChanged(const QStringList& screenIds);

private:
    PhosphorScrollEngine::ScrollEngine* m_engine = nullptr;
    /// Last set broadcast on the bus (the change gate's memory; the engine
    /// re-emits identical sets on desktop switches for the tiling channel).
    QStringList m_lastBroadcastScreens;
};

} // namespace PlasmaZones
