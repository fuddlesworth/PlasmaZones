// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

namespace PhosphorShellApp {

// The toast hosts' broker, installed as the `ToastRegistry` context
// property by src/shell/main.cpp. There is one ToastHost per output, each
// built by PerScreenPanels in a context that cannot see shell.qml's ids,
// so the root's `notify` IpcTarget has no direct path to them: every host
// attaches itself here with its screen name and primary flag, and send()
// picks the primary output's host. Hosts are held through QPointer so a
// hot reload that destroys them leaves no dangling entry.
class ToastController : public QObject
{
    Q_OBJECT

public:
    explicit ToastController(QObject* parent = nullptr);
    ~ToastController() override;

    // A host is any object with ToastHost's show(object) -> int. Attaching
    // an already-attached host updates its screen name and primary flag.
    Q_INVOKABLE void attachHost(QObject* host, const QString& screenName, bool primary);
    Q_INVOKABLE void detachHost(QObject* host);
    [[nodiscard]] int hostCount() const;

    // Show a toast on the primary output's host, or on the first attached
    // host when none is marked primary. Returns the toast id the host
    // assigned, or -1 when no host is attached or a rule suppressed it.
    Q_INVOKABLE int send(const QString& summary, const QString& body);

private:
    struct Host
    {
        QPointer<QObject> object;
        QString screenName;
        bool primary = false;
    };

    [[nodiscard]] QObject* primaryHost();

    QList<Host> m_hosts;
};

} // namespace PhosphorShellApp
