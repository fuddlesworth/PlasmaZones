// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoController.h"

#include "InAppPopoutTransport.h"

#include <PhosphorPopout/PopoutController.h>
#include <PhosphorPopout/PopoutRequest.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QUrl>

using namespace PhosphorPopout;

namespace PhosphorPopoutDemo {

DemoController::DemoController(QObject* parent)
    : QObject(parent)
    , m_transport(std::make_unique<InAppPopoutTransport>())
    , m_controller(std::make_unique<PopoutController>(m_transport.get()))
{
    // Mirror controller state into our Q_PROPERTYs. QML status bar
    // binds to openPopoutIds and modalActive.
    QObject::connect(m_controller.get(), &PopoutController::popoutOpened, this,
                     [this](const QString& id, const QString&) {
                         if (!m_openIds.contains(id)) {
                             m_openIds.append(id);
                             Q_EMIT openPopoutIdsChanged();
                         }
                     });
    QObject::connect(m_controller.get(), &PopoutController::popoutClosed, this,
                     [this](const QString& id, const QString&) {
                         // removeOne matches the PopoutController invariant
                         // (at most one open entry per popoutId per
                         // arbitration). removeAll would silently paper
                         // over a future invariant violation.
                         if (m_openIds.removeOne(id)) {
                             Q_EMIT openPopoutIdsChanged();
                         }
                     });
    QObject::connect(m_controller.get(), &PopoutController::modalActiveChanged, this,
                     &DemoController::modalActiveChanged);
}

// Defined out-of-line so unique_ptr<InAppPopoutTransport>'s and
// unique_ptr<PopoutController>'s destructors instantiate here, where
// both forward-declared types are complete via the #includes above.
DemoController::~DemoController() = default;

void DemoController::wire(QQuickItem* hostItem, QQmlComponent* hostComponent)
{
    m_engine = hostItem ? qmlEngine(hostItem) : nullptr;
    m_transport->setHostItem(hostItem);
    m_transport->setHostComponent(hostComponent);
}

QQmlComponent* DemoController::buildContentComponent(const QString& qmlPath)
{
    if (!m_engine) {
        return nullptr;
    }
    return new QQmlComponent(m_engine.data(), QUrl(qmlPath), this);
}

void DemoController::toggleNamed(const QString& popoutId, QQmlComponent* content, ExclusiveMode mode,
                                 bool dismissOnFocusLoss, const QVariantMap& props)
{
    PopoutRequest req;
    req.popoutId = popoutId;
    req.content = content;
    req.exclusive = mode;
    req.dismissOnFocusLoss = dismissOnFocusLoss;
    req.props = props;
    // scope is left at PopoutRequest::DefaultScope. Modal and Detached
    // arbitration ignores scope; the two cooperatives both share the
    // default scope by design (the demo exercises same-scope cooperative
    // swap).
    m_controller->toggle(req);
}

void DemoController::toggleCooperativeA()
{
    if (!m_calendarComponent) {
        m_calendarComponent =
            buildContentComponent(QStringLiteral("qrc:/qt/qml/Phosphor/PopoutDemo/CalendarPopout.qml"));
    }
    toggleNamed(QStringLiteral("calendar"), m_calendarComponent, ExclusiveMode::Cooperative, true);
}

void DemoController::toggleCooperativeB()
{
    if (!m_noteComponent) {
        m_noteComponent = buildContentComponent(QStringLiteral("qrc:/qt/qml/Phosphor/PopoutDemo/NotePopout.qml"));
    }
    toggleNamed(QStringLiteral("quick-note"), m_noteComponent, ExclusiveMode::Cooperative, true);
}

void DemoController::toggleModal()
{
    if (!m_alertComponent) {
        m_alertComponent = buildContentComponent(QStringLiteral("qrc:/qt/qml/Phosphor/PopoutDemo/AlertPopout.qml"));
    }
    toggleNamed(QStringLiteral("alert"), m_alertComponent, ExclusiveMode::Modal, false);
}

void DemoController::toggleDetached()
{
    if (!m_noteComponent) {
        m_noteComponent = buildContentComponent(QStringLiteral("qrc:/qt/qml/Phosphor/PopoutDemo/NotePopout.qml"));
    }
    // NotePopout is shared with toggleCooperativeB. The `pinned` prop
    // gives the Detached instance distinct chrome (tertiary border,
    // different header) so the two can be on screen at the same time
    // without looking identical. The shared QQmlComponent is safe to
    // reuse here because openSurface is synchronous: a begin/complete
    // create cycle for each open runs to completion before the next.
    QVariantMap props;
    props.insert(QStringLiteral("pinned"), true);
    toggleNamed(QStringLiteral("pinned-note"), m_noteComponent, ExclusiveMode::Detached, false, props);
}

void DemoController::closeAll()
{
    m_controller->closeAll();
}

void DemoController::shutdown()
{
    // Snapshot modal-active state BEFORE disconnecting so we can
    // emit the matching NOTIFY by hand after closeAll if the drain
    // flipped modalActive false. Without this, a QML binding on
    // modalActive that was true at shutdown would never see the
    // transition (the forwarding lambda for modalActiveChanged was
    // installed via &DemoController::modalActiveChanged and the
    // disconnect below kills it).
    const bool wasModalActive = m_controller ? m_controller->isModalActive() : false;
    // Disconnect this object's slots from the controller's signals
    // before draining so the close storm doesn't re-enter Q_PROPERTY
    // notify handlers that bind into QML objects the engine is about
    // to destroy.
    if (m_controller) {
        m_controller->disconnect(this);
        m_controller->closeAll();
    }
    // Synchronously delete the transport's host items while the QML
    // engine is still alive. The transport's own destructor runs
    // AFTER the engine in stack-unwind order; deferring teardown
    // there crashes with "pure virtual method called" once the
    // engine has invalidated the host items' QML metadata.
    if (m_transport) {
        m_transport->shutdown();
    }
    // Drop the cached QQmlComponents while the engine is still alive.
    // They are QObject children of `this`, but `this` outlives the
    // engine (engine is declared after demoController in main.cpp).
    // Letting the parent-cascade run them during ~DemoController
    // would destruct QQmlComponents after their engine is gone;
    // typically safe in Qt 6 but fragile. Resetting now is explicit.
    delete m_calendarComponent;
    m_calendarComponent = nullptr;
    delete m_alertComponent;
    m_alertComponent = nullptr;
    delete m_noteComponent;
    m_noteComponent = nullptr;
    // The popoutClosed handler that normally maintains m_openIds was
    // disconnected above; do the final-drain emit by hand so any
    // remaining QML bindings on openPopoutIds see the empty state
    // before the engine tears down. Same pattern for modalActive
    // when shutdown drained a previously-active modal.
    if (!m_openIds.isEmpty()) {
        m_openIds.clear();
        Q_EMIT openPopoutIdsChanged();
    }
    if (wasModalActive && m_controller && !m_controller->isModalActive()) {
        Q_EMIT modalActiveChanged();
    }
}

QStringList DemoController::openPopoutIds() const
{
    return m_openIds;
}

bool DemoController::modalActive() const
{
    return m_controller->isModalActive();
}

} // namespace PhosphorPopoutDemo
