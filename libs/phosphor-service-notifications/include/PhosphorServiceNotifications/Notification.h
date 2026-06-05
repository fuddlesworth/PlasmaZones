// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNotifications/phosphorservicenotifications_export.h>

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace PhosphorServiceNotifications {

class NotificationServer;

/**
 * @brief One live notification, decoded from an `org.freedesktop.Notifications`
 * `Notify` call.
 *
 * Owned by `NotificationServer` (which is its parent and sole mutator: the
 * setters are private and `NotificationServer` is a friend). A notification is
 * mutable for its whole life: a `Notify` carrying a `replaces_id` updates the
 * matching object in place rather than allocating a new one, so every field
 * except `id` can change and is reported through the single `changed()` signal.
 *
 * The object decodes the spec hint set into typed fields. `image` is the rich
 * notification image (from the `image-data` / `image-path` hints), decoded once
 * here so no consumer re-derives the fiddly `(iiibiiay)` struct; the raw hint
 * map stays available via `hints()` for advanced bindings. The body's optional
 * markup is stored raw: this library never renders.
 */
class PHOSPHORSERVICENOTIFICATIONS_EXPORT Notification : public QObject
{
    Q_OBJECT
    Q_PROPERTY(uint id READ id CONSTANT)
    Q_PROPERTY(QString appName READ appName NOTIFY changed)
    Q_PROPERTY(QString appIcon READ appIcon NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString body READ body NOTIFY changed)
    Q_PROPERTY(QStringList actions READ actions NOTIFY changed)
    Q_PROPERTY(Urgency urgency READ urgency NOTIFY changed)
    Q_PROPERTY(QString category READ category NOTIFY changed)
    Q_PROPERTY(QString desktopEntry READ desktopEntry NOTIFY changed)
    Q_PROPERTY(QImage image READ image NOTIFY changed)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY changed)
    Q_PROPERTY(bool resident READ resident NOTIFY changed)
    Q_PROPERTY(bool transient READ transient NOTIFY changed)
    Q_PROPERTY(bool suppressSound READ suppressSound NOTIFY changed)
    Q_PROPERTY(int value READ value NOTIFY changed)
    Q_PROPERTY(int expireTimeout READ expireTimeout NOTIFY changed)
    Q_PROPERTY(QDateTime timestamp READ timestamp NOTIFY changed)
    Q_PROPERTY(QVariantMap hints READ hints NOTIFY changed)

public:
    /// Spec urgency levels (the `urgency` hint, a byte).
    enum Urgency {
        Low = 0,
        Normal = 1,
        Critical = 2,
    };
    Q_ENUM(Urgency)

    [[nodiscard]] uint id() const
    {
        return m_id;
    }
    [[nodiscard]] QString appName() const
    {
        return m_appName;
    }
    [[nodiscard]] QString appIcon() const
    {
        return m_appIcon;
    }
    [[nodiscard]] QString summary() const
    {
        return m_summary;
    }
    [[nodiscard]] QString body() const
    {
        return m_body;
    }
    [[nodiscard]] QStringList actions() const
    {
        return m_actions;
    }
    [[nodiscard]] Urgency urgency() const
    {
        return m_urgency;
    }
    [[nodiscard]] QString category() const
    {
        return m_category;
    }
    [[nodiscard]] QString desktopEntry() const
    {
        return m_desktopEntry;
    }
    [[nodiscard]] QImage image() const
    {
        return m_image;
    }
    [[nodiscard]] bool hasImage() const
    {
        return !m_image.isNull();
    }
    [[nodiscard]] bool resident() const
    {
        return m_resident;
    }
    [[nodiscard]] bool transient() const
    {
        return m_transient;
    }
    [[nodiscard]] bool suppressSound() const
    {
        return m_suppressSound;
    }
    /// The `value` hint (a progress percentage by convention), or -1 when absent.
    /// Stored as sent; the server does not clamp it.
    [[nodiscard]] int value() const
    {
        return m_value;
    }
    /// The raw `expire_timeout` from `Notify` (-1 server-default, 0 never, >0 ms).
    [[nodiscard]] int expireTimeout() const
    {
        return m_expireTimeout;
    }
    /// When the notification was first received (does not move on replace).
    [[nodiscard]] QDateTime timestamp() const
    {
        return m_timestamp;
    }
    [[nodiscard]] QVariantMap hints() const
    {
        return m_hints;
    }

Q_SIGNALS:
    /// Emitted once per `replaces_id` update: the server re-applies the full
    /// field set in place (a replacing Notify re-specifies the notification, so
    /// this fires even if the new values happen to match the old).
    void changed();

private:
    Q_DISABLE_COPY_MOVE(Notification)
    friend class NotificationServer;

    explicit Notification(uint id, QObject* parent = nullptr);

    uint m_id = 0;
    QString m_appName;
    QString m_appIcon;
    QString m_summary;
    QString m_body;
    QStringList m_actions;
    Urgency m_urgency = Normal;
    QString m_category;
    QString m_desktopEntry;
    QImage m_image;
    bool m_resident = false;
    bool m_transient = false;
    bool m_suppressSound = false;
    int m_value = -1;
    int m_expireTimeout = -1;
    QDateTime m_timestamp;
    QVariantMap m_hints;
};

} // namespace PhosphorServiceNotifications
