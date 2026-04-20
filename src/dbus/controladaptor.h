// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QFutureWatcher>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace PhosphorZones {
class Zone;
class LayoutRegistry;
}

namespace PlasmaZones {

class WindowTrackingAdaptor;
class LayoutAdaptor;
class AutotileEngine;

// Phosphor::Screens::ScreenManager moved to libs/phosphor-screens (Phosphor::Screens::ScreenManager).
} // namespace PlasmaZones
namespace Phosphor::Screens {
class ScreenManager;
}
namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for high-level convenience API
 *
 * Provides D-Bus interface: org.plasmazones.Control
 *
 * Thin facade over existing adaptors for third-party integrations and scripts.
 * Provides one-call operations, API version negotiation, and a complete
 * state snapshot for monitoring tools.
 */
class PLASMAZONES_EXPORT ControlAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Control")

public:
    explicit ControlAdaptor(WindowTrackingAdaptor* wta, LayoutAdaptor* layoutAdaptor,
                            PhosphorZones::LayoutRegistry* layoutManager, AutotileEngine* autotileEngine,
                            Phosphor::Screens::ScreenManager* screenManager, QObject* parent = nullptr);
    ~ControlAdaptor() override = default;

public Q_SLOTS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Version and capabilities
    // ═══════════════════════════════════════════════════════════════════════════
    // High-level operations
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Snap a window to a specific zone in a layout
     * @param windowId Window to snap
     * @param zoneNumber PhosphorZones::Zone number (1-indexed)
     * @param screenId Screen for geometry resolution (empty = primary)
     * @note Resolves the zone from the screen's current layout
     */
    void snapWindowToZone(const QString& windowId, int zoneNumber, const QString& screenId);

    /**
     * @brief Toggle autotile mode for a screen
     * @param screenId Screen to toggle
     * @note Switches between snapping mode and autotile mode
     */
    void toggleAutotileForScreen(const QString& screenId);

    /**
     * @brief Get a complete state snapshot
     * @return JSON object with all daemon state (layouts, zones, windows, screens, autotile)
     */
    QString getFullState();

    /**
     * @brief Generate a redacted support report for bug reports
     * @param sinceMinutes Minutes of journal logs to include (0 = default 30, max 120)
     * @return Empty string — the actual report is delivered asynchronously via delayed D-Bus reply.
     *         On error (concurrent call, shutdown), a D-Bus error reply is sent instead.
     */
    QString generateSupportReport(int sinceMinutes, const QDBusMessage& message);

private:
    WindowTrackingAdaptor* m_wta;
    LayoutAdaptor* m_layoutAdaptor;
    PhosphorZones::LayoutRegistry* m_layoutManager;
    AutotileEngine* m_autotileEngine;
    Phosphor::Screens::ScreenManager* m_screenManager;
    QPointer<QFutureWatcher<QString>> m_reportWatcher;
};

} // namespace PlasmaZones
