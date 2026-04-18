// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "IPanelSource.h"
#include "phosphorscreens_export.h"

#include <QHash>
#include <QString>
#include <QTimer>

class QDBusServiceWatcher;

namespace Phosphor::Screens {

/**
 * @brief @ref IPanelSource implementation that queries KDE Plasma Shell
 *        over D-Bus for panel-reservation offsets.
 *
 * Pure D-Bus client — links Qt6::DBus only, no KF6, no KDE Frameworks.
 * Built into PhosphorScreens unconditionally; consumers on non-KDE
 * desktops just wire a different IPanelSource (typically @ref
 * NoOpPanelSource).
 *
 * Lifecycle:
 *   - On @ref start(), watches `org.kde.plasmashell` registration. Kicks
 *     off an initial async query immediately if the service is already
 *     on the bus, otherwise waits for the registration signal.
 *   - Async D-Bus calls only — the GUI thread is never blocked.
 *   - Per-screen offsets keyed by `QScreen::name()` (the connector name).
 *     Plasma's panel API exposes a screen INDEX which can disagree with
 *     Qt's screen list ordering on multi-monitor setups, so we match by
 *     screen geometry instead.
 *
 * Coalescing: rapid `requestRequery` calls coalesce into a single in-flight
 * D-Bus call. The watcher slot fires `requeryCompleted` exactly once per
 * outstanding request after the reply lands.
 */
class PHOSPHORSCREENS_EXPORT PlasmaPanelSource final : public IPanelSource
{
    Q_OBJECT
public:
    explicit PlasmaPanelSource(QObject* parent = nullptr);
    ~PlasmaPanelSource() override;

    void start() override;
    void stop() override;
    Offsets currentOffsets(QScreen* screen) const override;
    bool ready() const override;
    void requestRequery(int delayMs = 0) override;

private:
    /// Issue an async D-Bus call to org.kde.plasmashell evaluateScript.
    /// Falls back to "no panels" + emits @ref requeryCompleted if the
    /// service isn't reachable.
    /// @param emitRequeryCompleted true to emit the completion signal
    ///        when the reply lands; false suppresses it (used for the
    ///        initial query during start()).
    void issueQuery(bool emitRequeryCompleted);

    bool m_running = false;
    bool m_ready = false;
    bool m_queryPending = false;

    /// Per-connector-name reserved-edge offsets.
    QHash<QString, Offsets> m_offsets;

    /// Re-query after a delay (e.g. to let panel-editor UI settle).
    QTimer m_requeryTimer;

    /// Fires when org.kde.plasmashell appears on the bus so we can
    /// query panels as soon as Plasma Shell is available, instead of
    /// guessing with arbitrary timer delays.
    QDBusServiceWatcher* m_plasmaShellWatcher = nullptr;
};

} // namespace Phosphor::Screens
