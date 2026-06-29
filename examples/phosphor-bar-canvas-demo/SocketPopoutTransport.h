// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorPopout/IPopoutTransport.h>

#include <QString>

#include <functional>

namespace PhosphorBarCanvasDemo {

// Minimal IPopoutTransport for the connected-corner demo. In the
// connected-corner design the popout body is drawn inside the bar's own
// painted Shape (the socket), so there is no separate layer-shell surface
// to create: this transport only hands back unique non-empty handles so
// the PopoutController accepts the open. The visible open/close is the
// BarCanvas socket growing, driven off the controller's popoutOpened /
// popoutClosed signals.
//
// This is the same seam the production shell fills with a real
// layer-shell-backed transport; swapping is a dependency-injection flip.
class SocketPopoutTransport : public PhosphorPopout::IPopoutTransport
{
public:
    [[nodiscard]] QString openSurface(const PhosphorPopout::PopoutRequest& request) override;
    void closeSurface(const QString& handle) override;
    void setSurfaceDismissedCallback(std::function<void(const QString&)> callback) override;

private:
    int m_counter = 0;
    // Stored to honour the contract (the controller installs one and
    // detaches it on teardown). This transport never self-dismisses, so
    // it is never invoked.
    std::function<void(const QString&)> m_dismissed;
};

} // namespace PhosphorBarCanvasDemo
