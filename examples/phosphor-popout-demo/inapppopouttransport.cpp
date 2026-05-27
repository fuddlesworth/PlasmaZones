// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "InAppPopoutTransport.h"

#include <QColor>
#include <QDebug>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QVariant>

namespace PhosphorPopoutDemo {

InAppPopoutTransport::InAppPopoutTransport(QObject* parent)
    : QObject(parent)
{
}

InAppPopoutTransport::~InAppPopoutTransport()
{
    // shutdown() is the canonical drain path: it runs while the QML
    // engine is still alive (wired from QGuiApplication::aboutToQuit
    // via DemoController::shutdown). Reaching the destructor with
    // entries still alive means shutdown was skipped (e.g., an early
    // main() return before app.exec()), and the QML engine has
    // already torn down by the time this dtor runs. Deleting QQuickItems
    // synchronously at that point hits the pure-virtual-method-called
    // failure mode that motivated shutdown() in the first place.
    //
    // Assert in debug to catch the contract violation; in release,
    // leak the stranded entries — the process is exiting, and a leak
    // is preferable to a SIGABRT in the dtor.
    Q_ASSERT_X(m_entries.isEmpty(), "~InAppPopoutTransport",
               "shutdown() must run before transport destruction; "
               "stranded entries cannot be safely deleted after engine teardown");
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
    // its bindings. Mirrors the layer-shell transport's contract.
    QQmlComponent* contentComponent = request.content;
    QObject* contentObj = contentComponent->beginCreate(engine->rootContext());
    auto* contentItem = qobject_cast<QQuickItem*>(contentObj);
    if (!contentItem) {
        // Pair beginCreate with completeCreate before discarding the
        // object. Qt's QQmlComponent state machine assumes every
        // beginCreate sees a matching completeCreate so its internal
        // bookkeeping stays consistent for subsequent create calls
        // against the same shared component.
        contentComponent->completeCreate();
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

    // Build the wrapper host using two-phase create so contentItem,
    // backdropColor, and dismissOnClickOutside are applied BEFORE the
    // host's bindings settle. One-phase create would let
    // PopoutHost.qml's Component.onCompleted and onContentItemChanged
    // run against null/default values once, then re-run on every
    // setProperty below. Two-phase matches the content delegate path
    // and keeps the open animation from briefly observing the wrong
    // initial state.
    QObject* hostObj = m_hostComponent->beginCreate(engine->rootContext());
    auto* hostItem = qobject_cast<QQuickItem*>(hostObj);
    if (!hostItem) {
        // Pair beginCreate with completeCreate before discarding the
        // object, same reasoning as the contentItem failure path
        // above.
        m_hostComponent->completeCreate();
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
    // Cooperative popouts use a dim translucent backdrop. Modal
    // uses a heavier scrim. Detached stays transparent. The demo
    // wires these values directly here so the visual difference
    // is obvious.
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
    // dismissOnFocusLoss collapses to dismissOnClickOutside inside a
    // single window. The in-window demo has no real focus model that
    // would distinguish them; a real layer-shell transport handles
    // both concepts separately.
    hostItem->setProperty("dismissOnClickOutside", request.dismissOnFocusLoss);
    m_hostComponent->completeCreate();

    const QString handle = QStringLiteral("popout-%1").arg(++m_counter);
    m_entries.insert(handle, Entry{hostItem});

    // Wire self-dismiss. When PopoutHost emits `dismissed` after a
    // click-outside drains its close animation, route the handle to
    // the controller's registered callback and tear the wrapper down.
    // SIGNAL/SLOT string form is the legitimate path here. `dismissed`
    // is a QML-defined signal on PopoutHost.qml and the C++ side has
    // no compile-time method pointer for it; the function-pointer
    // connect would require forward-declaring a QML symbol. Explicit
    // Qt::DirectConnection enforces the IPopoutTransport thread-affinity
    // contract: dismissed must run synchronously on the controller's
    // thread so the entries-table mutation completes before the next
    // GUI-thread iteration.
    QObject::connect(hostItem, SIGNAL(dismissed()), this, SLOT(onHostDismissed()), Qt::DirectConnection);
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
    if (!it->hostItem) {
        m_entries.erase(it);
        return;
    }

    // Mark the entry as closing and let the host's existing
    // dismissed signal route into onHostDismissed when the close
    // animation settles. The closing flag tells onHostDismissed to
    // tear down resources without routing back to the controller's
    // dismissed callback, since the controller is the one that
    // initiated this close. This avoids duplicating PopoutHost's
    // animation duration as a hardcoded timer interval.
    it->closing = true;
    it->hostItem->setProperty("open", false);
}

void InAppPopoutTransport::setSurfaceDismissedCallback(std::function<void(const QString&)> callback)
{
    m_dismissedCb = std::move(callback);
}

void InAppPopoutTransport::shutdown()
{
    // Disarm the dismissed callback first. Any subsequent host-item
    // destruction must not route through the (likely already
    // disconnected) controller.
    m_dismissedCb = {};

    // Snapshot, clear, then delete with the dismissed signal
    // disconnected so Component.onDestruction's emit cannot re-enter
    // onHostDismissed. Same erase-before-delete invariant the
    // destructor maintains, but run now while the QML engine is
    // still alive to handle the QQuickItem teardown cleanly.
    const auto victims = m_entries.values();
    m_entries.clear();
    for (const auto& entry : victims) {
        if (entry.hostItem) {
            QObject::disconnect(entry.hostItem.data(), nullptr, this, nullptr);
            delete entry.hostItem.data();
        }
        // contentItem is a QObject child of hostItem (Qt's
        // QQmlComponent::beginCreate sets the parent), so it was
        // deleted by the line above. The QPointer is null here.
    }
    // Drop the host/component references so any post-shutdown openSurface
    // is rejected by the null guard at the top of openSurface rather
    // than racing the QQmlEngine teardown.
    m_host.clear();
    m_hostComponent.clear();
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
        // contentItem is a QObject child of hostItem (PopoutHost.qml's
        // contentFrame.rebindContentItem reparented it under the host),
        // so deleteLater on hostItem cascades to contentItem. Mirrors
        // the destructor / shutdown teardown model; no second
        // contentItem deleteLater needed.
        copy.hostItem->deleteLater();
    }
    // Only fire the controller callback for self-dismisses. A
    // controller-initiated close marks the entry with `closing` and
    // the controller already knows the popout is gone, so firing
    // again would loop.
    if (!copy.closing && m_dismissedCb) {
        m_dismissedCb(handle);
    }
}

} // namespace PhosphorPopoutDemo
