// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePolkit/phosphorservicepolkit_export.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace PhosphorServicePolkit {

class ListenerImpl;

/**
 * @brief One active PolicyKit authentication request, decoded from polkit's
 * `initiateAuthentication` callback.
 *
 * Owned by `PolkitAgent` (its parent). polkit serialises authentication, so at
 * most one of these is active at a time. The descriptive fields are fixed for
 * the life of the request (`CONSTANT`); only `selectedIdentity` is writable, so
 * a dialog can offer a chooser when the user may authenticate as more than one
 * identity (usually just themselves, sometimes root).
 *
 * The PAM prompt and the response path land on this object / the agent in
 * milestone 4; milestone 3 surfaces the request's description and identities.
 */
class PHOSPHORSERVICEPOLKIT_EXPORT AuthRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString actionId READ actionId CONSTANT)
    Q_PROPERTY(QString message READ message CONSTANT)
    Q_PROPERTY(QString iconName READ iconName CONSTANT)
    Q_PROPERTY(QVariantMap details READ details CONSTANT)
    Q_PROPERTY(QString cookie READ cookie CONSTANT)
    Q_PROPERTY(QStringList identities READ identities CONSTANT)
    Q_PROPERTY(int selectedIdentity READ selectedIdentity WRITE setSelectedIdentity NOTIFY selectedIdentityChanged)

public:
    [[nodiscard]] QString actionId() const
    {
        return m_actionId;
    }
    [[nodiscard]] QString message() const
    {
        return m_message;
    }
    [[nodiscard]] QString iconName() const
    {
        return m_iconName;
    }
    [[nodiscard]] QVariantMap details() const
    {
        return m_details;
    }
    /// The opaque polkit cookie that ties this request to its authentication.
    [[nodiscard]] QString cookie() const
    {
        return m_cookie;
    }
    /// Display strings for the identities the user may authenticate as.
    [[nodiscard]] QStringList identities() const
    {
        return m_identities;
    }
    /// Index into `identities` of the identity the user will authenticate as
    /// (default 0). The agent uses it to build the PAM session in milestone 4.
    [[nodiscard]] int selectedIdentity() const
    {
        return m_selectedIdentity;
    }
    /// Clamped to a valid index; out-of-range values are ignored.
    void setSelectedIdentity(int index);

Q_SIGNALS:
    void selectedIdentityChanged();

private:
    Q_DISABLE_COPY_MOVE(AuthRequest)
    friend class ListenerImpl;

    AuthRequest(QString actionId, QString message, QString iconName, QVariantMap details, QString cookie,
                QStringList identities, QObject* parent);

    QString m_actionId;
    QString m_message;
    QString m_iconName;
    QVariantMap m_details;
    QString m_cookie;
    QStringList m_identities;
    int m_selectedIdentity = 0;
};

} // namespace PhosphorServicePolkit
