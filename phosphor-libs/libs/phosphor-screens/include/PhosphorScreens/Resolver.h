// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorscreens_export.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <QString>

class QPoint;

namespace PhosphorScreens {

/**
 * @brief D-Bus endpoint that ScreenResolver queries.
 *
 * Defaults match the Phosphor daemon's canonical `org.plasmazones.Screen`
 * interface (sourced from `PhosphorProtocol::Service::*`). A consumer that
 * runs against a different service overrides them per call.
 *
 * Top-level (not nested in ScreenResolver) so its default member initialisers
 * are usable as default-argument values on `ScreenResolver::effectiveScreenAt`
 * — nested types' member initialisers are unreachable at the enclosing
 * class's default-arg parse site.
 *
 * `interfaceName` (not `interface`) sidesteps the Microsoft `interface` SDK
 * macro that Qt's AUTOMOC pre-processor occasionally mishandles.
 */
struct ResolverEndpoint
{
    QString service = QString(PhosphorProtocol::Service::Name);
    QString path = QString(PhosphorProtocol::Service::ObjectPath);
    QString interfaceName = QString(PhosphorProtocol::Service::Interface::Screen);
    QString method = QStringLiteral("getEffectiveScreenAt");
};

/**
 * @brief Resolve a global cursor position to the effective screen ID.
 *
 * On a plain single-monitor setup "effective screen ID" is just the physical
 * `QScreen::name()`. On multi-monitor or virtual-screen setups, a Phosphor
 * daemon (or any compatible host) tracks a richer mapping (virtual-screen
 * regions carved out of physical outputs, duplicate-connector
 * disambiguation) that Qt itself doesn't know about — so we ask the daemon
 * over D-Bus.
 *
 * The resolver is a thin static-method facade over a single D-Bus call with
 * a Qt-native fallback. No state, no lifetime; the caller decides which
 * service / object / interface to ask.
 */
class PHOSPHORSCREENS_EXPORT ScreenResolver
{
public:
    using Endpoint = ResolverEndpoint;

    /**
     * @brief Resolve a screen coordinate to an effective screen ID.
     *
     * Tries the daemon's virtual-screen-aware lookup first (skipped entirely
     * if the daemon isn't already on the bus, so the call never triggers
     * D-Bus auto-activation). Falls back to `QGuiApplication::screenAt(pos)`,
     * then to `QGuiApplication::primaryScreen()`. Returns an empty string
     * only if Qt reports no screens at all (headless / test environments).
     *
     * @note Reentrancy: while waiting for the daemon's reply, this method
     *       spins a local QEventLoop. Queued signals, posted events, and
     *       pending UI paints continue to run during the wait (that's the
     *       whole point — QDBus::Block would freeze the thread). Callers
     *       MUST NOT hold mutable state across the call that another slot
     *       could invalidate; treat the return as a snapshot taken after
     *       arbitrary event-loop activity.
     *
     * @param pos          Position in global screen coordinates.
     * @param endpoint     D-Bus endpoint to query. Defaults to the
     *                     Phosphor daemon.
     * @param timeoutMs    D-Bus call timeout in milliseconds. Keep this
     *                     short — the caller is typically blocking the
     *                     user's shortcut keypress.
     */
    static QString effectiveScreenAt(const QPoint& pos, const ResolverEndpoint& endpoint = ResolverEndpoint{},
                                     int timeoutMs = PhosphorProtocol::Service::SyncCallTimeoutMs);

    /// Convenience wrapper: resolve at the current `QCursor::pos()`.
    static QString effectiveScreenAtCursor(const ResolverEndpoint& endpoint = ResolverEndpoint{},
                                           int timeoutMs = PhosphorProtocol::Service::SyncCallTimeoutMs);
};

} // namespace PhosphorScreens
