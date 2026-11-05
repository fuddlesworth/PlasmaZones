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
class PolkitAgent;

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
 * The request's description and identities are decoded here; the live PAM
 * `prompt` / `echo` are updated on this object by the agent as the conversation
 * progresses, and the response goes back through `PolkitAgent::respond()`.
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
    Q_PROPERTY(QString prompt READ prompt NOTIFY promptChanged)
    Q_PROPERTY(bool echo READ echo NOTIFY echoChanged)

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
    /// (default 0). `authenticate()` uses it to build the PAM session.
    [[nodiscard]] int selectedIdentity() const
    {
        return m_selectedIdentity;
    }
    /// Out-of-range indices are ignored (the current selection is kept).
    void setSelectedIdentity(int index);

    /// The current PAM prompt to show the user (e.g. "Password: "), updated by
    /// the agent as the conversation progresses. Empty until authentication
    /// starts.
    [[nodiscard]] QString prompt() const
    {
        return m_prompt;
    }
    /// Whether the response to `prompt` should be echoed (false for passwords).
    [[nodiscard]] bool echo() const
    {
        return m_echo;
    }

Q_SIGNALS:
    void selectedIdentityChanged();
    void promptChanged();
    void echoChanged();

private:
    Q_DISABLE_COPY_MOVE(AuthRequest)
    friend class ListenerImpl;
    friend class PolkitAgent;

    AuthRequest(QString actionId, QString message, QString iconName, QVariantMap details, QString cookie,
                QStringList identities, QObject* parent);

    QString m_actionId;
    QString m_message;
    QString m_iconName;
    QVariantMap m_details;
    QString m_cookie;
    QStringList m_identities;
    int m_selectedIdentity = 0;
    QString m_prompt;
    bool m_echo = false;
};

} // namespace PhosphorServicePolkit
