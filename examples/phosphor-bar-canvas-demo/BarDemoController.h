// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

#include <memory>

namespace PhosphorPopout {
class PopoutController;
}

namespace PhosphorBarCanvasDemo {

class SocketPopoutTransport;

// QML-exposed glue for the bar-canvas demo. Owns a real PopoutController
// (the Phase-1.2 PopoutService) plus the demo's SocketPopoutTransport,
// and mirrors the controller's open-state for the one demo popout into a
// Q_PROPERTY the QML binds the socket animation to. The button calls
// toggleControlCenter(); arbitration (one popout per scope, toggle) is
// the controller's, exactly as the production shell will drive it.
class BarDemoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool controlCenterOpen READ controlCenterOpen NOTIFY controlCenterOpenChanged)

public:
    explicit BarDemoController(QObject* parent = nullptr);
    ~BarDemoController() override;

    // Toggle the Control Center popout through the PopoutController.
    Q_INVOKABLE void toggleControlCenter();

    [[nodiscard]] bool controlCenterOpen() const;

    // Close everything before the engine tears down QML bindings. Wired
    // to QGuiApplication::aboutToQuit in main.cpp.
    void shutdown();

Q_SIGNALS:
    void controlCenterOpenChanged();

private:
    // Declaration order is load-bearing: the controller's destructor
    // detaches its dismissed-callback from the transport, so the
    // transport must outlive the controller. C++ destroys members in
    // reverse-declaration order, so the transport is declared first.
    std::unique_ptr<SocketPopoutTransport> m_transport;
    std::unique_ptr<PhosphorPopout::PopoutController> m_controller;
    bool m_controlCenterOpen = false;
};

} // namespace PhosphorBarCanvasDemo
