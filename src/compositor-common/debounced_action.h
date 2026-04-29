// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QRect>
#include <QTimer>
#include <functional>

namespace PlasmaZones {

/**
 * @brief Compositor-agnostic debounced action with coalescing
 *
 * Debounces rapid-fire geometry change signals (e.g., screen connect/disconnect)
 * and coalesces re-apply requests while a previous apply is in progress.
 *
 * Shared by Wayfire plugin and future compositor integrations. KWin's
 * ScreenChangeHandler has its own specialized debounce logic that also
 * checks virtual screen SIZE (not just geometry) before triggering.
 *
 * Note: geometryChanged() only detects actual geometry changes. If a monitor
 * disconnects and reconnects at the same resolution, the geometry is unchanged
 * and no action is triggered. Callers needing screen-count-change detection
 * should handle that separately.
 *
 * @note Thread safety: All methods must be called from the QObject's owning
 * thread (typically the compositor's main thread). QTimer requires an event
 * loop on the owning thread. Do not call from worker threads.
 */
class DebouncedScreenAction : public QObject
{
    Q_OBJECT

public:
    /**
     * @param debounceMs    Debounce interval in milliseconds
     * @param parent        QObject parent for timer ownership
     */
    explicit DebouncedScreenAction(int debounceMs, QObject* parent = nullptr)
        : QObject(parent)
    {
        m_debounce.setSingleShot(true);
        m_debounce.setInterval(debounceMs);
        connect(&m_debounce, &QTimer::timeout, this, &DebouncedScreenAction::onDebounceTimeout);
    }

    void stop()
    {
        m_debounce.stop();
        m_pending = false;
    }

    /**
     * @brief Signal that the screen geometry has changed.
     *
     * Debounces and only triggers the action when geometry actually changes size.
     *
     * @param currentGeometry Current virtual screen geometry
     */
    void geometryChanged(const QRect& currentGeometry)
    {
        // If an apply is currently in flight, force a coalesced reapply even
        // when the geometry is unchanged (e.g., identical-resolution monitor
        // swap during an ongoing apply) — otherwise the change is silently
        // dropped because `currentGeometry == m_lastGeometry` and the
        // in-progress apply is still using pre-change state.
        if (currentGeometry == m_lastGeometry && !m_pending) {
            if (m_applyInProgress) {
                requestReapply();
            }
            return;
        }
        m_lastGeometry = currentGeometry;
        m_pending = true;
        m_debounce.start();
    }

    /**
     * @brief Request a re-apply (e.g., daemon-triggered).
     * Coalesces if an apply is already in progress.
     */
    void requestReapply()
    {
        if (m_applyInProgress) {
            m_reapplyPending = true;
            return;
        }
        Q_EMIT applyRequested();
    }

    /**
     * @brief Call when the apply operation starts (before async D-Bus call).
     */
    void markApplyStarted()
    {
        m_applyInProgress = true;
    }

    /**
     * @brief Call when the apply operation completes.
     * Triggers re-apply if one was coalesced.
     */
    void markApplyCompleted()
    {
        m_applyInProgress = false;
        if (m_reapplyPending) {
            m_reapplyPending = false;
            QTimer::singleShot(0, this, [this]() {
                Q_EMIT applyRequested();
            });
        }
    }

    bool isChangeInProgress() const
    {
        return m_pending || m_applyInProgress;
    }

    void setLastGeometry(const QRect& geo)
    {
        m_lastGeometry = geo;
    }

Q_SIGNALS:
    /// Emitted when an apply is requested (debounced change or explicit reapply)
    void applyRequested();

private Q_SLOTS:
    void onDebounceTimeout()
    {
        // Subclass or signal receiver provides currentGeometry
        // For now, just signal that we need to apply
        m_pending = false;
        Q_EMIT applyRequested();
    }

private:
    QTimer m_debounce;
    QRect m_lastGeometry;
    bool m_pending = false;
    bool m_applyInProgress = false;
    bool m_reapplyPending = false;
};

} // namespace PlasmaZones
