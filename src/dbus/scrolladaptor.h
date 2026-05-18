// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorProtocol/WindowMarshalling.h>
#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QStringList>

namespace PhosphorScrollEngine {
class ScrollEngine;
}

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for niri-style scrollable tiling.
 *
 * Provides the @c org.plasmazones.Scroll interface. The KWin effect reports
 * window lifecycle (open / close / focus) for scroll-mode screens here, and
 * the adaptor forwards each event to the ScrollEngine. The effect learns
 * which screens are scroll-mode from the @c scrollScreensChanged signal.
 *
 * Window geometry flows back to the effect over the shared
 * WindowTrackingAdaptor::applyGeometriesBatch path, not through this adaptor.
 *
 * Mirrors AutotileAdaptor's lifecycle-input surface; algorithm / master /
 * gap controls have no scroll equivalent and are intentionally absent.
 */
class PLASMAZONES_EXPORT ScrollAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Scroll")

    Q_PROPERTY(QStringList scrollScreens READ scrollScreens NOTIFY scrollScreensChanged)

public:
    /**
     * @brief Construct a ScrollAdaptor.
     * @param engine The ScrollEngine to feed; borrowed, must outlive the adaptor.
     * @param parent Parent QObject (the daemon — its registerObject() exposes this).
     */
    explicit ScrollAdaptor(PhosphorScrollEngine::ScrollEngine* engine, QObject* parent = nullptr);
    ~ScrollAdaptor() override = default;

    /// Screens currently using scroll mode.
    QStringList scrollScreens() const;

    /**
     * @brief Clear the engine pointer during shutdown.
     *
     * Called by Daemon::stop() before the ScrollEngine is destroyed, so a
     * late D-Bus call hits the null guard instead of a dangling pointer.
     */
    void clearEngine();

public Q_SLOTS:
    void windowOpened(const QString& windowId, const QString& screenId);
    void windowsOpenedBatch(const PhosphorProtocol::WindowOpenedList& entries);
    void windowClosed(const QString& windowId);
    void notifyWindowFocused(const QString& windowId, const QString& screenId);
    void windowMinimizedChanged(const QString& windowId, bool minimized);
    void windowDropped(const QString& draggedWindowId, const QString& anchorWindowId, bool placeAfter);

Q_SIGNALS:
    /// Emitted when the set of scroll-mode screens changes; the KWin effect
    /// subscribes so it reports the right screens' windows to this interface.
    void scrollScreensChanged(const QStringList& screenIds);

private:
    bool ensureEngine(const char* methodName) const;

    PhosphorScrollEngine::ScrollEngine* m_engine = nullptr;
};

} // namespace PlasmaZones
