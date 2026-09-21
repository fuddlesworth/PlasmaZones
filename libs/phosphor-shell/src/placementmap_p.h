// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Private to placementmap.cpp and placementmap_actions.cpp: the daemon's
// signal fan-in and the generation-guarded async call the screens issue
// through it. AUTOMOC picks this header up beside placementmap.cpp.

#include <PhosphorShell/PlacementMap.h>

#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/AutotileTypes.h>
#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/WindowTypes.h>

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

#include <type_traits>
#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcPlacementMap)

namespace PhosphorShell {

namespace PlacementMapIface {
using PhosphorProtocol::Service::Name;
using PhosphorProtocol::Service::ObjectPath;
namespace Iface = PhosphorProtocol::Service::Interface;
} // namespace PlacementMapIface

/**
 * The daemon's signal fan-in. Owned by the PlacementMap singleton; every
 * PlacementMapScreen connects to the typed signals here rather than to
 * the bus, so hot-reloading the QML engine never re-subscribes.
 */
class PlacementMapBus : public QObject
{
    Q_OBJECT

public:
    explicit PlacementMapBus(QObject* parent)
        : QObject(parent)
    {
        using namespace PlacementMapIface;
        PhosphorProtocol::registerWireTypes();
        auto bus = QDBusConnection::sessionBus();
        bus.connect(Name, ObjectPath, Iface::LayoutRegistry, QStringLiteral("screenLayoutChanged"), this,
                    SLOT(onScreenLayoutChanged(QString)));
        bus.connect(Name, ObjectPath, Iface::LayoutRegistry, QStringLiteral("activeLayoutForScreenChanged"), this,
                    SLOT(onScreenLayoutChanged(QString)));
        bus.connect(Name, ObjectPath, Iface::LayoutRegistry, QStringLiteral("assignmentChangesApplied"), this,
                    SLOT(onAssignmentsApplied(QStringList)));
        bus.connect(Name, ObjectPath, Iface::LayoutRegistry, QStringLiteral("currentActivityChanged"), this,
                    SLOT(onCurrentActivityChanged(QString)));
        bus.connect(Name, ObjectPath, Iface::WindowTracking, QStringLiteral("windowStateChanged"), this,
                    SLOT(onWindowStateChanged(QString, PhosphorProtocol::WindowStateEntry)));
        // Phase-3 surfaces: an older daemon never emits them, which is harmless.
        bus.connect(Name, ObjectPath, Iface::WindowTracking, QStringLiteral("windowMetadataChanged"), this,
                    SLOT(onWindowMetadataChanged(QString, QString, QString)));
        bus.connect(Name, ObjectPath, Iface::WindowTracking, QStringLiteral("windowUrgencyChanged"), this,
                    SLOT(onWindowUrgencyChanged(QString, bool)));
        bus.connect(Name, ObjectPath, Iface::Tiling, QStringLiteral("windowsTileRequested"), this,
                    SLOT(onWindowsTileRequested(PhosphorProtocol::TileRequestList)));
        bus.connect(Name, ObjectPath, Iface::Tiling, QStringLiteral("tilingChanged"), this,
                    SLOT(onTilingChanged(QString)));
        bus.connect(Name, ObjectPath, Iface::Tiling, QStringLiteral("focusWindowRequested"), this,
                    SLOT(onFocusWindowRequested(QString)));
        // Phase-2 surface: an older daemon never emits it, which is harmless.
        bus.connect(Name, ObjectPath, Iface::Tiling, QStringLiteral("focusedWindowChanged"), this,
                    SLOT(onFocusedWindowChanged(QString, QString)));
        bus.connect(Name, ObjectPath, Iface::Scrolling, QStringLiteral("stripChanged"), this,
                    SLOT(onStripChanged(QString)));
        bus.connect(Name, ObjectPath, Iface::Scrolling, QStringLiteral("stripContextChanged"), this,
                    SLOT(onStripChanged(QString)));
    }

    QDBusPendingCall call(const QString& interface, const QString& method, const QVariantList& args) const
    {
        using namespace PlacementMapIface;
        QDBusMessage msg = QDBusMessage::createMethodCall(Name, ObjectPath, interface, method);
        msg.setArguments(args);
        return QDBusConnection::sessionBus().asyncCall(msg);
    }

Q_SIGNALS:
    void layoutChanged(const QString& screenId);
    void currentActivityChanged(const QString& activityId);
    void windowStateChanged(const PhosphorProtocol::WindowStateEntry& entry);
    void windowMetadataChanged(const QString& windowId, const QString& appId, const QString& title);
    void windowUrgencyChanged(const QString& windowId, bool urgent);
    void tileBatch(const QList<PlacementMapParser::TileRect>& tiles);
    void tilingChanged(const QString& screenId);
    void focusRequested(const QString& windowId);
    void focusedWindowChanged(const QString& screenId, const QString& windowId);
    void stripChanged(const QString& screenId);

private Q_SLOTS:
    void onScreenLayoutChanged(const QString& screenId)
    {
        Q_EMIT layoutChanged(screenId);
    }
    void onAssignmentsApplied(const QStringList& screenIds)
    {
        for (const QString& id : screenIds) {
            Q_EMIT layoutChanged(id);
        }
    }
    void onCurrentActivityChanged(const QString& activityId)
    {
        Q_EMIT currentActivityChanged(activityId);
    }
    void onWindowStateChanged(const QString& windowId, const PhosphorProtocol::WindowStateEntry& entry)
    {
        Q_UNUSED(windowId)
        if (!entry.validationError().isEmpty()) {
            return;
        }
        Q_EMIT windowStateChanged(entry);
    }
    void onWindowMetadataChanged(const QString& windowId, const QString& appId, const QString& title)
    {
        if (windowId.isEmpty()) {
            return;
        }
        Q_EMIT windowMetadataChanged(windowId, appId, title);
    }
    void onWindowUrgencyChanged(const QString& windowId, bool urgent)
    {
        if (windowId.isEmpty()) {
            return;
        }
        Q_EMIT windowUrgencyChanged(windowId, urgent);
    }
    void onWindowsTileRequested(const PhosphorProtocol::TileRequestList& requests)
    {
        QList<PlacementMapParser::TileRect> tiles;
        tiles.reserve(requests.size());
        for (const auto& r : requests) {
            if (!r.validationError().isEmpty()) {
                continue;
            }
            PlacementMapParser::TileRect t;
            t.windowId = r.windowId;
            t.screenId = r.screenId;
            t.rect = r.toRect();
            t.floating = r.floating;
            t.monocle = r.monocle;
            tiles.append(t);
        }
        Q_EMIT tileBatch(tiles);
    }
    void onTilingChanged(const QString& screenId)
    {
        Q_EMIT tilingChanged(screenId);
    }
    void onFocusWindowRequested(const QString& windowId)
    {
        Q_EMIT focusRequested(windowId);
    }
    void onFocusedWindowChanged(const QString& screenId, const QString& windowId)
    {
        Q_EMIT focusedWindowChanged(screenId, windowId);
    }
    void onStripChanged(const QString& screenId)
    {
        Q_EMIT stripChanged(screenId);
    }
};

template<typename Reply, typename Fn, typename ErrFn>
void PlacementMapScreen::call(const QString& interface, const QString& method, const QVariantList& args, Fn&& onReply,
                              ErrFn&& onError)
{
    const int generation = m_generation;
    auto* watcher = new QDBusPendingCallWatcher(m_map->m_bus->call(interface, method, args), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation, fn = std::forward<Fn>(onReply),
             err = std::forward<ErrFn>(onError)](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (generation != m_generation) {
                    return;
                }
                using ReplyT = std::conditional_t<std::is_void_v<Reply>, QDBusPendingReply<>, QDBusPendingReply<Reply>>;
                ReplyT reply = *w;
                if (reply.isError()) {
                    qCDebug(lcPlacementMap) << m_screenName << reply.error().message();
                    err(reply.error());
                    return;
                }
                if constexpr (std::is_void_v<Reply>) {
                    fn();
                } else {
                    fn(reply.value());
                }
            });
}

template<typename Reply, typename Fn>
void PlacementMapScreen::call(const QString& interface, const QString& method, const QVariantList& args, Fn&& onReply)
{
    call<Reply>(interface, method, args, std::forward<Fn>(onReply), [](const QDBusError&) { });
}

/// Latch a capability off when a call failed with UnknownMethod (an older
/// daemon without the surface). Returns true when it did.
inline bool latchUnknownMethod(bool& capability, const QDBusError& error)
{
    if (error.type() != QDBusError::UnknownMethod) {
        return false;
    }
    capability = false;
    return true;
}

} // namespace PhosphorShell
