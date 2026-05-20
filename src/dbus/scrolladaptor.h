// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorProtocol/WindowMarshalling.h>
#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

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

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(QStringList scrollScreens READ scrollScreens NOTIFY scrollScreensChanged)

public:
    /**
     * @brief Construct a ScrollAdaptor.
     * @param engine The ScrollEngine to feed; borrowed, must outlive the adaptor.
     * @param parent Parent QObject (the daemon — its registerObject() exposes this).
     */
    explicit ScrollAdaptor(PhosphorScrollEngine::ScrollEngine* engine, QObject* parent = nullptr);
    ~ScrollAdaptor() override = default;

    /// Whether scroll mode is globally enabled (master gate). Mirrors the
    /// shape of AutotileAdaptor::enabled — exposed so external tooling can
    /// query the master toggle without reading Settings directly. Sourced
    /// from the engine's isEnabled() (any-screen-active), which the daemon
    /// keeps in sync with `Settings::scrollingEnabled` via
    /// `updateScrollScreens` — the gate empties activeScreens() when the
    /// master toggle goes off.
    bool enabled() const;

    /// Screens currently using scroll mode.
    QStringList scrollScreens() const;

    /**
     * @brief Clear the engine pointer during shutdown.
     *
     * Called by Daemon::stop() before the ScrollEngine is destroyed, so a
     * late D-Bus call hits the null guard instead of a dangling pointer.
     */
    void clearEngine();

    /**
     * @brief Daemon-injected callback fired after windowsOpenedBatch's
     *        reconcileRestoredWindows completes.
     *
     * The daemon uses this to invalidate its per-screen geometry cache so
     * the next onScrollPlacementChanged push is NOT dedup-skipped. Without
     * this hook, drift detection silently stays disabled for all restored
     * scroll windows after a daemon restart: the daemon's first push (at
     * load time) reaches the effect before it knows the window IDs, the
     * effect's recordAppliedGeometry no-ops, and subsequent pushes get
     * dedup-skipped because the geometry didn't change. Set once via
     * @ref setBatchProcessedCallback at daemon::start; cleared in stop().
     */
    void setBatchProcessedCallback(std::function<void()> callback)
    {
        m_batchProcessedCallback = std::move(callback);
    }

public Q_SLOTS:
    void windowOpened(const QString& windowId, const QString& screenId);
    void windowsOpenedBatch(const PhosphorProtocol::WindowOpenedList& entries);
    void windowClosed(const QString& windowId);
    void notifyWindowFocused(const QString& windowId, const QString& screenId);
    void windowMinimizedChanged(const QString& windowId, bool minimized);
    void windowDropped(const QString& draggedWindowId, const QString& anchorWindowId, bool placeAfter);

Q_SIGNALS:
    /// Emitted when the master scroll-mode gate flips. The daemon emits this
    /// when `updateScrollScreens` sees the engine's active set transition
    /// between empty (gate off / no scroll-mode screens) and non-empty
    /// (any scroll-mode screen active).
    void enabledChanged(bool enabled);

    /// Emitted when the set of scroll-mode screens changes; the KWin effect
    /// subscribes so it reports the right screens' windows to this interface.
    void scrollScreensChanged(const QStringList& screenIds);

private:
    bool ensureEngine(const char* methodName) const;

    PhosphorScrollEngine::ScrollEngine* m_engine = nullptr;
    /// Daemon-injected callback — see setBatchProcessedCallback. Invoked from
    /// windowsOpenedBatch after reconcileRestoredWindows runs; null until set.
    std::function<void()> m_batchProcessedCallback;
};

} // namespace PlasmaZones
