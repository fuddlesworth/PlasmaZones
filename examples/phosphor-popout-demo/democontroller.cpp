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
    , m_transport(new InAppPopoutTransport(this))
    , m_controller(new PopoutController(m_transport, this))
{
    // Mirror controller state into our Q_PROPERTYs. QML status bar
    // binds to openPopoutIds and modalActive.
    QObject::connect(m_controller, &PopoutController::popoutOpened, this, [this](const QString& id, const QString&) {
        if (!m_openIds.contains(id)) {
            m_openIds.append(id);
            Q_EMIT openPopoutIdsChanged();
        }
    });
    QObject::connect(m_controller, &PopoutController::popoutClosed, this, [this](const QString& id, const QString&) {
        if (m_openIds.removeAll(id) > 0) {
            Q_EMIT openPopoutIdsChanged();
        }
    });
    QObject::connect(m_controller, &PopoutController::modalActiveChanged, this, &DemoController::modalActiveChanged);
}

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

void DemoController::toggleCooperativeA()
{
    if (!m_calendarComponent) {
        m_calendarComponent =
            buildContentComponent(QStringLiteral("qrc:/qt/qml/Phosphor/PopoutDemo/CalendarPopout.qml"));
    }
    PopoutRequest req;
    req.popoutId = QStringLiteral("calendar");
    req.content = m_calendarComponent;
    req.exclusive = ExclusiveMode::Cooperative;
    req.scope = QStringLiteral("default");
    req.dismissOnFocusLoss = true;
    m_controller->toggle(req);
}

void DemoController::toggleCooperativeB()
{
    if (!m_noteComponent) {
        m_noteComponent = buildContentComponent(QStringLiteral("qrc:/qt/qml/Phosphor/PopoutDemo/NotePopout.qml"));
    }
    PopoutRequest req;
    req.popoutId = QStringLiteral("quick-note");
    req.content = m_noteComponent;
    req.exclusive = ExclusiveMode::Cooperative;
    req.scope = QStringLiteral("default");
    req.dismissOnFocusLoss = true;
    m_controller->toggle(req);
}

void DemoController::toggleModal()
{
    if (!m_alertComponent) {
        m_alertComponent = buildContentComponent(QStringLiteral("qrc:/qt/qml/Phosphor/PopoutDemo/AlertPopout.qml"));
    }
    PopoutRequest req;
    req.popoutId = QStringLiteral("alert");
    req.content = m_alertComponent;
    req.exclusive = ExclusiveMode::Modal;
    req.dismissOnFocusLoss = false;
    m_controller->toggle(req);
}

void DemoController::toggleDetached()
{
    if (!m_noteComponent) {
        m_noteComponent = buildContentComponent(QStringLiteral("qrc:/qt/qml/Phosphor/PopoutDemo/NotePopout.qml"));
    }
    PopoutRequest req;
    req.popoutId = QStringLiteral("pinned-note");
    req.content = m_noteComponent;
    req.exclusive = ExclusiveMode::Detached;
    req.dismissOnFocusLoss = false;
    // NotePopout is shared with toggleCooperativeB. The `pinned` prop
    // gives the Detached instance distinct chrome (tertiary border,
    // different header) so the two can be on screen at the same time
    // without looking identical.
    req.props.insert(QStringLiteral("pinned"), true);
    m_controller->toggle(req);
}

void DemoController::closeAll()
{
    m_controller->closeAll();
}

QStringList DemoController::openPopoutIds() const
{
    return m_openIds;
}

bool DemoController::modalActive() const
{
    return m_controller && m_controller->isModalActive();
}

} // namespace PhosphorPopoutDemo
