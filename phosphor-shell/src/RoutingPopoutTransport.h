// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorPopout/IPopoutTransport.h>

#include <QHash>
#include <QSet>
#include <QString>
#include <QtCore/qtclasshelpermacros.h>

#include <functional>

namespace PhosphorShellApp {

// One IPopoutTransport in front of two, so PopoutController sees a single
// transport while popouts land on different presentation paths.
//
// The shell has two ways to show a popout. Most get their own layer-shell
// surface (LayerPopoutTransport). The control center instead grows out of
// the bar's own capsule as one painted shape, and there is no surface to
// create for it (SocketPopoutTransport). Both must be arbitrated by ONE
// controller, or the Modal power menu cannot close the control center and
// a Cooperative open is not suppressed while a modal is up — which is the
// bug this router exists to prevent. Without it the shell closed the
// socket by hand from togglePowerMenu, a rule every future modal surface
// would have had to remember.
//
// Routing is by POPOUT ID, never by anchor: Anchor::BarCenter is the
// request's default, so routing on it would silently hijack every caller
// that never expressed an opinion. The shell declares which ids are
// socket-hosted; everything else is a layer surface.
//
// Handles from the two inner transports are disjoint by construction
// ("popout-N" vs "socket-N"), and the router remembers which transport
// issued each one so closeSurface goes back to the right side.
class RoutingPopoutTransport : public PhosphorPopout::IPopoutTransport
{
public:
    // Neither transport is owned; both must outlive the router (in
    // main.cpp all three are stack objects declared before the
    // controller, and reverse destruction handles it).
    RoutingPopoutTransport(PhosphorPopout::IPopoutTransport* layer, PhosphorPopout::IPopoutTransport* socket,
                           QSet<QString> socketHostedIds);
    ~RoutingPopoutTransport() override;
    Q_DISABLE_COPY_MOVE(RoutingPopoutTransport)

    [[nodiscard]] QString openSurface(const PhosphorPopout::PopoutRequest& request) override;
    void closeSurface(const QString& handle) override;
    void setSurfaceDismissedCallback(std::function<void(const QString&)> callback) override;

private:
    PhosphorPopout::IPopoutTransport* m_layer;
    PhosphorPopout::IPopoutTransport* m_socket;
    QSet<QString> m_socketHostedIds;
    // handle -> the transport that issued it, so close routes back.
    QHash<QString, PhosphorPopout::IPopoutTransport*> m_owners;
    std::function<void(const QString&)> m_dismissed;
};

} // namespace PhosphorShellApp
