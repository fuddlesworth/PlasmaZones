// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "InAppPopoutTransport.h"

#include <QColor>
#include <QDebug>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTimer>
#include <QVariant>

namespace PhosphorPopoutDemo {

InAppPopoutTransport::InAppPopoutTransport(QObject* parent)
    : QObject(parent)
{
}

InAppPopoutTransport::~InAppPopoutTransport()
{
    // Tear down any surviving entries directly. The controller calls
    // closeAll on shutdown via its own destructor sequence. This is
    // a belt-and-suspenders cleanup for the case where the transport
    // outlives the controller. Use synchronous delete rather than
    // deleteLater because by the time this destructor runs the event
    // loop may already have exited, in which case queued deleteLater
    // events never fire and the items leak.
    for (auto& entry : m_entries) {
        if (entry.hostItem) {
            delete entry.hostItem.data();
        }
        if (entry.contentItem) {
            delete entry.contentItem.data();
        }
    }
    m_entries.clear();
}

void InAppPopoutTransport::setHostItem(QQuickItem* host)
{
    m_host = host;
}

void InAppPopoutTransport::setHostComponent(QQmlComponent* component)
{
    m_hostComponent = component;
}

QString InAppPopoutTransport::openSurface(const PhosphorPopout::PopoutRequest& request)
{
    if (!m_host || !m_hostComponent || !request.content) {
        qWarning() << "InAppPopoutTransport: refusing open, missing host/component/content for" << request.popoutId;
        return {};
    }

    QQmlEngine* engine = qmlEngine(m_host);
    if (!engine) {
        qWarning() << "InAppPopoutTransport: host item has no QML engine";
        return {};
    }

    // Instantiate the content delegate first so the host wrapper can
    // reparent it on Component.onCompleted. Two-phase create gives us
    // a chance to apply the request's props before the delegate runs
    // its bindings; mirrors the layer-shell transport's contract.
    QQmlComponent* contentComponent = request.content;
    QObject* contentObj = contentComponent->beginCreate(engine->rootContext());
    auto* contentItem = qobject_cast<QQuickItem*>(contentObj);
    if (!contentItem) {
        if (contentObj) {
            contentObj->deleteLater();
        }
        qWarning() << "InAppPopoutTransport: content component is not a QQuickItem for" << request.popoutId;
        return {};
    }
    for (auto it = request.props.constBegin(); it != request.props.constEnd(); ++it) {
        contentItem->setProperty(it.key().toUtf8().constData(), it.value());
    }
    contentComponent->completeCreate();

    // Build the wrapper host. The controller-assigned handle is the
    // dictionary key the transport uses to look up the wrapper later.
    QObject* hostObj = m_hostComponent->create(engine->rootContext());
    auto* hostItem = qobject_cast<QQuickItem*>(hostObj);
    if (!hostItem) {
        if (hostObj) {
            hostObj->deleteLater();
        }
        contentItem->deleteLater();
        qWarning() << "InAppPopoutTransport: PopoutHost.qml create failed";
        return {};
    }
    hostItem->setParentItem(m_host);
    hostItem->setProperty("contentItem", QVariant::fromValue(contentItem));
    // Inject a back-reference so content delegates can dismiss
    // themselves. AlertPopout's Dismiss button uses this to call
    // _popoutHost.dismiss() rather than walking the parent chain.
    contentItem->setProperty("_popoutHost", QVariant::fromValue<QObject*>(hostItem));
    // Cooperative popouts use a dim translucent backdrop; modal uses a
    // heavier scrim; detached stays transparent. The demo wires these
    // values directly here so the visual difference is obvious.
    switch (request.exclusive) {
    case PhosphorPopout::ExclusiveMode::Cooperative:
        hostItem->setProperty("backdropColor", QColor(0, 0, 0, 60));
        break;
    case PhosphorPopout::ExclusiveMode::Modal:
        hostItem->setProperty("backdropColor", QColor(0, 0, 0, 160));
        break;
    case PhosphorPopout::ExclusiveMode::Detached:
        hostItem->setProperty("backdropColor", QColor(0, 0, 0, 0));
        break;
    }
    hostItem->setProperty("dismissOnClickOutside", request.dismissOnFocusLoss);

    const QString handle = QStringLiteral("popout-%1").arg(++m_counter);
    m_entries.insert(handle, Entry{hostItem, contentItem});

    // Wire self-dismiss. When PopoutHost emits `dismissed` after a
    // click-outside drains its close animation, route the handle to
    // the controller's registered callback and tear the wrapper down.
    // Handle captured by value so we identify the right entry even if
    // other popouts open and close in the meantime.
    QObject::connect(hostItem, SIGNAL(dismissed()), this, SLOT(onHostDismissed()));
    // Stash the handle on the host so the slot can route it back.
    hostItem->setProperty("_popoutHandle", handle);

    // Trigger the open animation. PopoutHost listens to its `open`
    // property and animates opacity + scale.
    hostItem->setProperty("open", true);

    return handle;
}

void InAppPopoutTransport::closeSurface(const QString& handle)
{
    auto it = m_entries.find(handle);
    if (it == m_entries.end()) {
        return;
    }
    if (it->hostItem) {
        // Trigger the close animation. The dismissed signal will
        // fire when it finishes, but we DO NOT route it back to the
        // controller because this close was controller-initiated.
        // Snapshot pointers, drop the entry now, then deleteLater
        // when the animation drains.
        Entry copy = it.value();
        m_entries.erase(it);
        copy.hostItem->setProperty("open", false);
        // Defer the actual destroy past the animation. PopoutHost's
        // dismiss timer is 300ms. 500ms gives slack for compositor
        // jitter. Capture QPointers, not raw pointers, so the lambda
        // resolves to no-ops if either item is already destroyed by
        // the time it fires.
        QPointer<QQuickItem> hostPtr = copy.hostItem;
        QPointer<QQuickItem> contentPtr = copy.contentItem;
        QTimer::singleShot(500, this, [hostPtr, contentPtr]() {
            if (hostPtr) {
                hostPtr->deleteLater();
            }
            if (contentPtr) {
                contentPtr->deleteLater();
            }
        });
    } else {
        m_entries.erase(it);
    }
}

bool InAppPopoutTransport::isSurfaceAlive(const QString& handle) const
{
    return m_entries.contains(handle);
}

void InAppPopoutTransport::setSurfaceDismissedCallback(std::function<void(const QString&)> callback)
{
    m_dismissedCb = std::move(callback);
}

void InAppPopoutTransport::onHostDismissed()
{
    QObject* s = sender();
    if (!s) {
        return;
    }
    const QString handle = s->property("_popoutHandle").toString();
    if (handle.isEmpty()) {
        return;
    }
    auto it = m_entries.find(handle);
    if (it == m_entries.end()) {
        return;
    }
    Entry copy = it.value();
    m_entries.erase(it);
    if (copy.hostItem) {
        copy.hostItem->deleteLater();
    }
    if (copy.contentItem) {
        copy.contentItem->deleteLater();
    }
    if (m_dismissedCb) {
        m_dismissedCb(handle);
    }
}

} // namespace PhosphorPopoutDemo
