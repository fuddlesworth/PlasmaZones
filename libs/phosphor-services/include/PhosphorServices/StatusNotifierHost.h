// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServices/phosphorservices_export.h>

#include <QList>
#include <QObject>
#include <QPointer>

namespace PhosphorServices {

class StatusNotifierItem;

/// The shell-side counterpart to StatusNotifierWatcher. One instance
/// per process. Claims `org.kde.StatusNotifierHost-<pid>`, registers
/// with whichever watcher won the session-bus race, watches for
/// items, and exposes them as a flat collection of StatusNotifierItem
/// QObjects.
///
/// Lifetime is the shell's — typically constructed by the shell
/// engine and parented to it. Items emitted via itemAdded() are
/// owned by the host and will be deleted on itemRemoved().
class PHOSPHORSERVICES_EXPORT StatusNotifierHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int itemCount READ itemCount NOTIFY itemCountChanged)

public:
    explicit StatusNotifierHost(QObject* parent = nullptr);
    ~StatusNotifierHost() override;

    [[nodiscard]] QList<StatusNotifierItem*> items() const;
    [[nodiscard]] int itemCount() const;
    [[nodiscard]] StatusNotifierItem* itemAt(int index) const;

Q_SIGNALS:
    void itemAdded(StatusNotifierItem* item);
    void itemRemoved(StatusNotifierItem* item);
    void itemCountChanged();

private Q_SLOTS:
    // DBus-callback slots — invoked by name from QDBusConnection::connect()
    // for the cross-process StatusNotifierItemRegistered / Unregistered
    // signals. Kept private so they're not part of the public API; MOC
    // still picks them up.
    void _q_remoteItemRegistered(const QString& canonical);
    void _q_remoteItemUnregistered(const QString& canonical);

private:
    class Private;
    Private* const d;
};

} // namespace PhosphorServices
