// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>

#include <functional>

class QDBusContext;

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for the compositor bridge protocol
 *
 * Provides D-Bus interface: org.plasmazones.CompositorBridge
 *
 * Compositor-agnostic bridge protocol: the registration handshake, and the
 * compositor's modifier-state and gesture reports (the latter only for a
 * bridge that registered the 'gestures' capability). Window manipulation
 * commands do NOT flow over
 * this interface — they ride org.plasmazones.WindowTracking
 * (applyGeometryRequested, applyGeometriesBatch, ...),
 * which bridges subscribe to after a successful registration.
 *
 * The bus name registerBridge records is the identity every compositor-only
 * method on the daemon admits (DbusHelpers::admitCompositorReport, #1155):
 * reportModifierState and reportGesture here, and the compositor reports on
 * the other interfaces. Qt sets a call's QDBusContext on the exported object,
 * not on its adaptor, so the caller is read through DbusHelpers. The
 * registered capability list is daemon-wide state, not a property of whoever
 * is calling, so checking it alone would let any peer on the session bus drive
 * the shell's gesture surfaces.
 *
 * Registration itself is held to the compositor's process (see
 * setCompositorIdentityCheck), so no other peer can register first and keep
 * the compositor out.
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
    /// Whether @p busName is the unique bus name the registered bridge called
    /// from. Never true for a registration that did not arrive over D-Bus.
    bool isBridgeService(const QString& busName) const
    {
        return !m_bridgeService.isEmpty() && busName == m_bridgeService;
    }
    bool hasCapability(const QString& cap) const
    {
        return m_capabilities.contains(cap);
    }

    /// Whether a unique bus name belongs to the compositor's own process.
    using CompositorIdentityCheck = std::function<bool(const QString& busName)>;
    /// The check a registration over the bus has to pass: Daemon holds it to
    /// the process serving its Wayland connection
    /// (OverlayAdaptor::isCompositorPeer). Unset, every registration over the
    /// bus is refused. A direct call (the unit tests) is never checked.
    void setCompositorIdentityCheck(CompositorIdentityCheck check)
    {
        m_compositorIdentityCheck = std::move(check);
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
     *         rejected registration the caller MUST check: invalid input
     *         (empty compositorName), a protocol version below the minimum,
     *         a caller that is not the compositor's process, or another
     *         bridge still on the bus
     *
     * Capabilities:
     *   "borderless"  — bridge manages window decorations (title-bar hiding
     *                   via its compositor-local DecorationManager)
     *   "animation"   — bridge supports skipAnimation flag
     *   "borders"     — bridge supports native window border rendering
     *   "modifiers"   — bridge reports keyboard modifier state via
     *                   reportModifierState
     *   "gestures"    — bridge forwards the shell's touchpad gestures via
     *                   reportGesture
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
     * @note Compositor-only (DbusHelpers::admitCompositorReport): any other
     *       bus caller is answered with AccessDenied.
     */
    void reportModifierState(int modifiers, int mouseButtons);

    /**
     * @brief Report a completed touchpad gesture recognised for the shell
     * @param kind "swipe" or "pinch"
     * @param direction swipe: up/down/left/right; pinch: expanding/contracting
     * @param fingerCount 1 to 5 (the KWin bridge sends 3 or 4)
     * @note Compositor-only (DbusHelpers::admitCompositorReport): any other
     *       bus caller is answered with AccessDenied, checked before
     *       anything else.
     * @note Re-emitted verbatim as gestureReported. The bridge registers
     *       the gestures it forwards under the "gestures" capability; the
     *       daemon keeps no state, it is the relay between the compositor
     *       (which alone sees touchpad gestures) and the shell surface that
     *       answers them. Input is validated here: an unknown kind or
     *       direction, or a finger count outside 1..5, is dropped.
     */
    void reportGesture(const QString& kind, const QString& direction, uint fingerCount);

Q_SIGNALS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Bridge lifecycle
    //
    // NOTE: this interface deliberately carries NO window-manipulation
    // command signals. Geometry and focus commands flow over
    // org.plasmazones.WindowTracking (applyGeometryRequested,
    // applyGeometriesBatch, ...) — a set of dead,
    // never-emitted command signals used to live here and misled bridge
    // implementers into subscribing to a channel that never fired.
    // ═══════════════════════════════════════════════════════════════════════════

    void bridgeRegistered(const QString& compositorName, const QString& version, const QStringList& capabilities);
    void modifierStateChanged(int modifiers, int mouseButtons);
    void gestureReported(const QString& kind, const QString& direction, uint fingerCount);

private:
    /// Whether the bus @p context's call arrived on says m_bridgeService has
    /// no owner any more. False on any other answer, an error or no answer.
    bool bridgeServiceGone(const QDBusContext* context) const;

    QString m_bridgeName;
    QString m_bridgeVersion;
    QStringList m_capabilities;
    // Unique bus name of the peer that registered, empty when the
    // registration did not arrive over D-Bus. It is the identity every
    // compositor-only method admits (DbusHelpers::admitCompositorReport), so
    // with it empty every such bus call is refused until a bridge registers
    // over the bus. The tests call the slots directly and bypass the
    // comparison entirely, because a direct call has no bus caller.
    QString m_bridgeService;
    /// See setCompositorIdentityCheck.
    CompositorIdentityCheck m_compositorIdentityCheck;
    /// The refusal warnings go out once each: a refused peer can call as
    /// often as it likes, and REJECTED is the per-call answer.
    bool m_identityRefusalLogged = false;
    bool m_occupiedRefusalLogged = false;
};

} // namespace PlasmaZones
