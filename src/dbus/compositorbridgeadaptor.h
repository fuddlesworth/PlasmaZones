// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>
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
     * @param version Protocol API version as a decimal integer string
     *        (PhosphorProtocol::Service::ApiVersion) — NOT the compositor's
     *        release version; peers below the daemon's minimum are rejected
     * @param capabilities List of supported capabilities
     * @return PhosphorProtocol::BridgeRegistrationResult struct: {apiVersion,
     *         bridgeName, sessionId}; sessionId == "REJECTED" signals a
     *         protocol-version mismatch the caller MUST check
     *
     * Capabilities:
     *   "borderless"  — bridge manages window decorations (title-bar hiding
     *                   via its compositor-local DecorationManager)
     *   "maximize"    — bridge supports maximizeWindow
     *   "animation"   — bridge supports skipAnimation flag
     *   "borders"     — bridge supports native window border rendering
     *   "modifiers"   — bridge reports keyboard modifier state
     */
    PhosphorProtocol::BridgeRegistrationResult registerBridge(const QString& compositorName, const QString& version,
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
    // Bridge lifecycle
    //
    // NOTE: this interface deliberately carries NO window-manipulation
    // command signals. Geometry/focus/stacking commands flow over
    // org.plasmazones.WindowTracking (applyGeometryRequested,
    // applyGeometriesBatch, raiseWindowsRequested, ...) — a set of dead,
    // never-emitted command signals used to live here and misled bridge
    // implementers into subscribing to a channel that never fired.
    // ═══════════════════════════════════════════════════════════════════════════

    void bridgeRegistered(const QString& compositorName, const QString& version, const QStringList& capabilities);
    void modifierStateChanged(int modifiers, int mouseButtons);

private:
    QString m_bridgeName;
    QString m_bridgeVersion;
    QStringList m_capabilities;
};

} // namespace PlasmaZones
