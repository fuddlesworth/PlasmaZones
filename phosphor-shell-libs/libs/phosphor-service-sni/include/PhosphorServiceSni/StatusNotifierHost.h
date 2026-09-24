// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceSni/phosphorservicesni_export.h>

// Qt's MOC needs fully-defined pointer types for Q_PROPERTY, signal
// parameters, and Q_INVOKABLE returns (qmetatype.h's
// `checkTypeIsSuitableForMetaType` static-asserts `is_complete<T>`).
// Forward decls compile in older Qt but fail with Qt ≥ 6.10.
#include <PhosphorServiceSni/StatusNotifierItem.h>

#include <QList>
#include <QObject>

#include <memory>

namespace PhosphorServiceSni {

/// The shell-side counterpart to StatusNotifierWatcher. One instance
/// per process. Claims `org.kde.StatusNotifierHost-<pid>`, registers
/// with whichever watcher won the session-bus race, watches for
/// items, and exposes them as a flat collection of StatusNotifierItem
/// QObjects.
///
/// Lifetime is the shell's: typically constructed by the shell
/// engine and parented to it. Items emitted via itemAdded() are
/// owned by the host and will be deleted on itemRemoved().
class PHOSPHORSERVICESNI_EXPORT StatusNotifierHost : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(StatusNotifierHost)
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
    // DBus-callback slots: invoked by name from QDBusConnection::connect()
    // for the cross-process StatusNotifierItemRegistered / Unregistered
    // signals. Kept private so they're not part of the public API; MOC
    // still picks them up.
    void _q_remoteItemRegistered(const QString& canonical);
    void _q_remoteItemUnregistered(const QString& canonical);

private:
    class Private;
    // Out-of-line destructor in the .cpp so unique_ptr<Private> can
    // see the complete type at destruction. CLAUDE.md forbids manual
    // `delete d;`.
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceSni
