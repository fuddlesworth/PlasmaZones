// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class KModifierKeyInfo;

namespace PhosphorShellLock {

// KWin's active keyboard layout plus the compositor's modifier state. No
// guessed locale or key-toggle counter: Caps Lock may already be on at lock.
class LockKeyboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString layoutName READ layoutName NOTIFY layoutChanged)
    Q_PROPERTY(bool canCycle READ canCycle NOTIFY layoutChanged)
    Q_PROPERTY(bool capsLock READ capsLock NOTIFY capsLockChanged)

public:
    explicit LockKeyboard(QObject* parent = nullptr);
    LockKeyboard(const QDBusConnection& bus, const QString& service, QObject* parent = nullptr);
    [[nodiscard]] QString layoutName() const;
    [[nodiscard]] bool canCycle() const;
    [[nodiscard]] bool capsLock() const;
    Q_INVOKABLE void nextLayout();

Q_SIGNALS:
    void layoutChanged();
    void capsLockChanged();

private Q_SLOTS:
    void refresh();

private:
    void publish(const QStringList& names, uint index);
    QDBusConnection m_bus;
    QString m_service;
    KModifierKeyInfo* m_modifiers = nullptr;
    QStringList m_layouts;
    uint m_index = 0;
    uint m_revision = 0;
};
}
