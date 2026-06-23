// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for the compositor bridge protocol
 *
 * Provides D-Bus interface: org.plasmazones.CompositorBridge
 *
 * Compositor-agnostic bridge protocol: the registration handshake and
 * modifier-state reporting. Window manipulation commands do NOT flow over
 * this interface — they ride org.plasmazones.WindowTracking
 * (applyGeometryRequested, applyGeometriesBatch, raiseWindowsRequested, ...),
 * which bridges subscribe to after a successful registration.
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
     *         rejected registration the caller MUST check — protocol-version
     *         mismatch or invalid input (empty compositorName)
     *
     * Capabilities:
     *   "borderless"  — bridge manages window decorations (title-bar hiding
     *                   via its compositor-local DecorationManager)
     *   "animation"   — bridge supports skipAnimation flag
     *   "borders"     — bridge supports native window border rendering
     *   "modifiers"   — bridge reports keyboard modifier state via
     *                   reportModifierState
     */
    PhosphorProtocol::BridgeRegistrationResult registerBridge(const QString& compositorName, const QString& version,
                                                              const QStringList& capabilities);

    /**
     * @brief Report keyboard modifier and mouse button state
     * @param modifiers Qt::KeyboardModifiers bitmask
     * @param mouseButtons Qt::MouseButtons bitmask
     * @note For non-drag contexts (focus-follows-mouse, etc.)
     * @note Reserved forward surface: the KWin bridge does not call this
     *       (it reports modifiers through the drag pipeline) and nothing
     *       daemon-side consumes modifierStateChanged yet — the method
     *       exists for non-KWin bridges that lack a drag channel. The
     *       interface is EXPERIMENTAL; revisit before v2.
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
