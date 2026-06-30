// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BarDemoController.h"

#include "SocketPopoutTransport.h"

#include <PhosphorPopout/PopoutController.h>
#include <PhosphorPopout/PopoutRequest.h>

#include <QStringLiteral>

namespace PhosphorBarCanvasDemo {

namespace {
const QString kControlCenterId = QStringLiteral("control-center");
}

BarDemoController::BarDemoController(QObject* parent)
    : QObject(parent)
    , m_transport(std::make_unique<SocketPopoutTransport>())
    , m_controller(std::make_unique<PhosphorPopout::PopoutController>(m_transport.get()))
{
    // Mirror the controller's open-state for our one demo popout into the
    // Q_PROPERTY. popoutClosed fires regardless of who closed it (button,
    // toggle, or arbitration), so the bound socket always reflects truth.
    connect(m_controller.get(), &PhosphorPopout::PopoutController::popoutOpened, this,
            [this](const QString& popoutId, const QString&) {
                if (popoutId == kControlCenterId && !m_controlCenterOpen) {
                    m_controlCenterOpen = true;
                    Q_EMIT controlCenterOpenChanged();
                }
            });
    connect(m_controller.get(), &PhosphorPopout::PopoutController::popoutClosed, this,
            [this](const QString& popoutId, const QString&) {
                if (popoutId == kControlCenterId && m_controlCenterOpen) {
                    m_controlCenterOpen = false;
                    Q_EMIT controlCenterOpenChanged();
                }
            });
}

BarDemoController::~BarDemoController() = default;

void BarDemoController::toggleControlCenter()
{
    PhosphorPopout::PopoutRequest request;
    request.popoutId = kControlCenterId;
    request.exclusive = PhosphorPopout::ExclusiveMode::Cooperative;
    request.anchor = PhosphorPopout::Anchor::BarCenter;
    // Content is painted by the BarCanvas socket, not a transport surface.
    request.content = nullptr;
    // The demo drives open/close from the button, not focus loss.
    request.dismissOnFocusLoss = false;
    (void)m_controller->toggle(request);
}

bool BarDemoController::controlCenterOpen() const
{
    return m_controlCenterOpen;
}

void BarDemoController::shutdown()
{
    if (m_controller)
        m_controller->closeAll();
}

} // namespace PhosphorBarCanvasDemo
