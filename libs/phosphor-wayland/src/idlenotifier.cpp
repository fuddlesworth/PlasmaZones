// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/IdleNotifier.h>
#include "qpa/layershellintegration.h"
#include "qpa/idle_notify_protocol.h"

#include <QLoggingCategory>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandinputdevice_p.h>

#include <limits>

Q_LOGGING_CATEGORY(lcIdleNotifier, "phosphorwayland.idlenotifier")

namespace PhosphorWayland {

class IdleNotifier::Private
{
public:
    IdleNotifier* owner = nullptr;
    std::chrono::milliseconds timeout{0};
    bool idle = false;
    struct ext_idle_notification_v1* notification = nullptr;

    static void handleIdled(void* data, struct ext_idle_notification_v1* notification)
    {
        Q_UNUSED(notification)
        auto* self = static_cast<Private*>(data);
        if (self->idle)
            return;
        self->idle = true;
        Q_EMIT self->owner->idleChanged();
        Q_EMIT self->owner->idled();
    }

    static void handleResumed(void* data, struct ext_idle_notification_v1* notification)
    {
        Q_UNUSED(notification)
        auto* self = static_cast<Private*>(data);
        if (!self->idle)
            return;
        self->idle = false;
        Q_EMIT self->owner->idleChanged();
        Q_EMIT self->owner->resumed();
    }

    void createNotification()
    {
        destroyNotification();
        if (timeout.count() <= 0)
            return;
        auto* integration = LayerShellIntegration::instance();
        if (!integration)
            return;
        auto* notifier = integration->idleNotifier();
        if (!notifier)
            return;
        auto* display = integration->display();
        if (!display)
            return;
        auto seats = display->inputDevices();
        if (seats.isEmpty()) {
            qCWarning(lcIdleNotifier) << "No input seat available for idle notification";
            return;
        }
        struct wl_seat* seat = seats.first()->wl_seat();
        if (!seat)
            return;
        static const struct ext_idle_notification_v1_listener listener = {
            .idled = handleIdled,
            .resumed = handleResumed,
        };
        auto ms =
            static_cast<uint32_t>(qMin(timeout.count(), static_cast<int64_t>(std::numeric_limits<uint32_t>::max())));
        notification = ext_idle_notifier_v1_get_idle_notification(notifier, ms, seat);
        if (notification) {
            ext_idle_notification_v1_add_listener(notification, &listener, this);
        }
    }

    void destroyNotification()
    {
        if (notification) {
            ext_idle_notification_v1_destroy(notification);
            notification = nullptr;
        }
        if (idle && owner) {
            idle = false;
            Q_EMIT owner->idleChanged();
            Q_EMIT owner->resumed();
        }
        idle = false;
    }
};

IdleNotifier::IdleNotifier(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;
}

IdleNotifier::~IdleNotifier()
{
    d->destroyNotification();
}

void IdleNotifier::setTimeout(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0)
        timeout = std::chrono::milliseconds(0);
    if (d->timeout == timeout)
        return;
    d->timeout = timeout;
    d->createNotification();
    Q_EMIT timeoutChanged();
}

std::chrono::milliseconds IdleNotifier::timeout() const
{
    return d->timeout;
}

bool IdleNotifier::isArmed() const
{
    return d->notification != nullptr;
}

bool IdleNotifier::isIdle() const
{
    return d->idle;
}

bool IdleNotifier::isSupported()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->idleNotifier();
}

} // namespace PhosphorWayland
