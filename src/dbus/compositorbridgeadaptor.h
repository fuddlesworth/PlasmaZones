// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <dbus_types.h>
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for the compositor bridge protocol
 *
 * Provides D-Bus interface: org.plasmazones.CompositorBridge
 *
 * This interface defines the daemon→compositor command protocol. Compositor
 * bridges (KWin effect, Hyprland bridge, Sway bridge) subscribe to the
 * signals to receive window manipulation commands.
 *
 * The interface also accepts bridge registration and modifier state reports
 * from the compositor side.
 *
 * @note This is an EXPERIMENTAL interface — may change before v2.
 */
class PLASMAZONES_EXPORT CompositorBridgeAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.CompositorBridge")

public:
    explicit CompositorBridgeAdaptor(QObject* parent = nullptr);
    ~CompositorBridgeAdaptor() override = default;

    // ═══════════════════════════════════════════════════════════════════════════
    // Bridge state
    // ═══════════════════════════════════════════════════════════════════════════

    QString bridgeName() const
    {
        return m_bridgeName;
    }
    QString bridgeVersion() const
    {
        return m_bridgeVersion;
    }
    QStringList bridgeCapabilities() const
    {
        return m_capabilities;
    }
    bool isBridgeRegistered() const
    {
        return !m_bridgeName.isEmpty();
    }
    bool hasCapability(const QString& cap) const
    {
        return m_capabilities.contains(cap);
    }

public Q_SLOTS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Bridge registration (compositor → daemon)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Register a compositor bridge
     * @param compositorName Name of the compositor (e.g. "kwin", "hyprland", "sway")
     * @param version Compositor version string
     * @param capabilities List of supported capabilities
     * @return BridgeRegistrationResult struct: {apiVersion, bridgeName, sessionId}
     *
     * Capabilities:
     *   "borderless"  — bridge supports setWindowBorderless
     *   "maximize"    — bridge supports maximizeWindow
     *   "animation"   — bridge supports skipAnimation flag
     *   "borders"     — bridge supports native window border rendering
     *   "modifiers"   — bridge reports keyboard modifier state
     */
    BridgeRegistrationResult registerBridge(const QString& compositorName, const QString& version,
                                            const QStringList& capabilities);

    /**
     * @brief Report keyboard modifier and mouse button state
     * @param modifiers Qt::KeyboardModifiers bitmask
     * @param mouseButtons Qt::MouseButtons bitmask
     * @note For non-drag contexts (focus-follows-mouse, etc.)
     */
    void reportModifierState(int modifiers, int mouseButtons);

Q_SIGNALS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Window geometry commands (daemon → compositor)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Apply geometry to a single window
     */
    void applyWindowGeometry(const QString& windowId, int x, int y, int width, int height, const QString& zoneId,
                             bool skipAnimation);

    /**
     * @brief Apply geometry to a batch of windows
     * @param batchJson JSON array of [{windowId, x, y, width, height, targetZoneId, ...}]
     * @param action Operation type ("rotate", "resnap", "snap_all")
     */
    void applyWindowGeometriesBatch(const WindowGeometryList& geometries, const QString& action);

    // ═══════════════════════════════════════════════════════════════════════════
    // Window focus and stacking commands (daemon → compositor)
    // ═══════════════════════════════════════════════════════════════════════════

    void activateWindow(const QString& windowId);
    void raiseWindows(const QStringList& windowIds);

    // ═══════════════════════════════════════════════════════════════════════════
    // Window decoration commands (daemon → compositor)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set window borderless state
     * @param windowId Window to modify
     * @param borderless True to hide title bar, false to show
     */
    void setWindowBorderless(const QString& windowId, bool borderless);

    /**
     * @brief Maximize or restore a window
     * @param windowId Window to modify
     * @param mode 0=restore, 3=full maximize
     */
    void maximizeWindow(const QString& windowId, int mode);

    // ═══════════════════════════════════════════════════════════════════════════
    // Bridge lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    void bridgeRegistered(const QString& compositorName, const QString& version, const QStringList& capabilities);
    void modifierStateChanged(int modifiers, int mouseButtons);

private:
    QString m_bridgeName;
    QString m_bridgeVersion;
    QStringList m_capabilities;
};

} // namespace PlasmaZones
