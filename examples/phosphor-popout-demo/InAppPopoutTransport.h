// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorPopout/IPopoutTransport.h>
#include <PhosphorPopout/PopoutRequest.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QQmlComponent;
class QQuickItem;

namespace PhosphorPopoutDemo {

// In-window IPopoutTransport. Opens popouts as QQuickItems parented
// into a host Item inside the demo's main QQuickWindow. No
// layer-shell, no Wayland. Just enough surface management to
// exercise the controller's arbitration end-to-end with real QML
// animations.
//
// The real shell ships a LayerPopoutTransport that wraps
// PhosphorLayer::SurfaceFactory. This in-app variant is the lib's
// acceptance harness. Swapping the transport is a follow-up.
class InAppPopoutTransport : public QObject, public PhosphorPopout::IPopoutTransport
{
    Q_OBJECT
public:
    explicit InAppPopoutTransport(QObject* parent = nullptr);
    ~InAppPopoutTransport() override;

    // The QQuickItem that popouts get parented into. Set once at
    // demo startup from the QML main window's host item. The host
    // is full-window so PopoutHost.qml's anchors.fill: parent fills
    // the whole window. Anchor-aware positioning would override x/y;
    // the demo keeps every popout centered for simplicity.
    void setHostItem(QQuickItem* host);

    // QML component template the transport instantiates to wrap each
    // popout. This is PopoutHost.qml from the Phosphor.Popout module.
    // The transport calls component->create() per open and routes the
    // open/closed/dismissed signals into the controller.
    void setHostComponent(QQmlComponent* component);

    // IPopoutTransport.
    [[nodiscard]] QString openSurface(const PhosphorPopout::PopoutRequest& request) override;
    void closeSurface(const QString& handle) override;
    void setSurfaceDismissedCallback(std::function<void(const QString&)> callback) override;

private Q_SLOTS:
    // Routed from PopoutHost's dismissed signal. The sender's
    // `_popoutHandle` dynamic property identifies which entry to drop.
    void onHostDismissed();

private:
    struct Entry
    {
        QPointer<QQuickItem> hostItem; // PopoutHost.qml instance
        QPointer<QQuickItem> contentItem; // delegate instantiated by us
        // True once closeSurface has set open=false on the host.
        // onHostDismissed uses this to skip the dismissed-callback
        // routing back to the controller, since the controller is
        // the one that initiated this close.
        bool closing = false;
    };

    QPointer<QQuickItem> m_host;
    QPointer<QQmlComponent> m_hostComponent;
    QHash<QString, Entry> m_entries;
    int m_counter = 0;
    std::function<void(const QString&)> m_dismissedCb;
};

} // namespace PhosphorPopoutDemo
