// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorPopout/IPopoutTransport.h>

#include <QString>
#include <QtCore/qtclasshelpermacros.h>

#include <functional>

namespace PhosphorShellApp {

class ControlCenterController;

// IPopoutTransport for a popout that is painted INTO the bar rather than
// given a surface of its own: the control center growing out of the
// capsule through BarCanvas's socket (the connected-corner design).
//
// There is nothing to create here. The visible open/close is BarHost
// animating its socket depth off ControlCenterController.openScreen, and
// this transport's whole job is to be the ONLY writer of that property,
// so the open state is driven by PopoutController's arbitration like
// every other popout's. That is what lets the Modal power menu close the
// control center, and a Cooperative open be refused while a modal is up,
// without shell.qml doing either by hand.
//
// Which output the pocket opens on comes from the request's targetScreen
// (the output whose bar button fired). Null falls back to the primary,
// the same fallback ControlCenterController::screenOf makes.
//
// One socket at a time. A second open while one is up is refused rather
// than moved: the bar has one pocket, and the controller already
// serialises opens per popout id, so this only triggers if a second
// socket-hosted id is ever registered without a second pocket to hold it.
//
// The pocket has no scrim, so nothing closes it on its own and every
// ordinary close is controller-initiated through closeSurface. The dismissed
// callback exists for the one case the transport must report upward on its
// own: the output holding the open pocket going away. Without that, the
// handle and the controller's openScreen would both stay set for an output
// that no longer exists, the arbiter would keep believing the popout is open,
// and every later open would be refused.
class SocketPopoutTransport : public PhosphorPopout::IPopoutTransport
{
public:
    explicit SocketPopoutTransport(ControlCenterController* controller);
    ~SocketPopoutTransport() override;
    Q_DISABLE_COPY_MOVE(SocketPopoutTransport)

    [[nodiscard]] QString openSurface(const PhosphorPopout::PopoutRequest& request) override;
    void closeSurface(const QString& handle) override;
    void setSurfaceDismissedCallback(std::function<void(const QString&)> callback) override;

    // Drop any open socket without notifying, for shutdown and hot reload.
    // The layer sibling carries the same verb for the same reason.
    void drain();

    /// Resolve the output name to open the pocket on, given the request's
    /// target screen (which may be null, meaning "the transport decides").
    /// An empty return refuses the open.
    ///
    /// Injectable for the same reason the layer transport takes a
    /// reserved-margins provider: the default reads QScreen::name(), and no
    /// headless Qt platform gives its screens a name, so the one-socket
    /// invariant would otherwise be untestable. Production installs nothing
    /// and gets the default.
    using ScreenNameResolver = std::function<QString(QScreen*)>;
    void setScreenNameResolver(ScreenNameResolver resolver);

private:
    // Clear the open state and report it upward, for a close this transport
    // decided on rather than one the controller asked for.
    void selfDismiss();

    ControlCenterController* m_controller;
    ScreenNameResolver m_resolveScreenName;
    QString m_openScreenName;
    QString m_openHandle;
    int m_counter = 0;
    std::function<void(const QString&)> m_dismissed;
};

} // namespace PhosphorShellApp
