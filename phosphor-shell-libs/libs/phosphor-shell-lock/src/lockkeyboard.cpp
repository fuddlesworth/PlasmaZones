// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorShellLock/LockKeyboard.h>

#include <KModifierKeyInfo>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>

namespace PhosphorShellLock {
namespace {
const QString layoutPath = QStringLiteral("/Layouts");
const QString layoutInterface = QStringLiteral("org.kde.KeyboardLayouts");
}

LockKeyboard::LockKeyboard(QObject* parent)
    : LockKeyboard(QDBusConnection::sessionBus(), QStringLiteral("org.kde.keyboard"), parent)
{
}

LockKeyboard::LockKeyboard(const QDBusConnection& bus, const QString& service, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_service(service)
    , m_modifiers(new KModifierKeyInfo(this))
{
    connect(m_modifiers, &KModifierKeyInfo::keyLocked, this, [this](Qt::Key key, bool) {
        if (key == Qt::Key_CapsLock)
            Q_EMIT capsLockChanged();
    });
    connect(m_modifiers, &KModifierKeyInfo::keyAdded, this, [this](Qt::Key key) {
        if (key == Qt::Key_CapsLock)
            Q_EMIT capsLockChanged();
    });
    connect(m_modifiers, &KModifierKeyInfo::keyRemoved, this, [this](Qt::Key key) {
        if (key == Qt::Key_CapsLock)
            Q_EMIT capsLockChanged();
    });
    m_bus.connect(service, layoutPath, layoutInterface, QStringLiteral("layoutChanged"), this, SLOT(refresh()));
    m_bus.connect(service, layoutPath, layoutInterface, QStringLiteral("layoutListChanged"), this, SLOT(refresh()));
    auto* watcher = new QDBusServiceWatcher(service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &LockKeyboard::refresh);
    refresh();
}

QString LockKeyboard::layoutName() const
{
    return m_index < uint(m_layouts.size()) ? m_layouts.at(m_index) : QString();
}

bool LockKeyboard::canCycle() const
{
    return m_layouts.size() > 1;
}

bool LockKeyboard::capsLock() const
{
    return m_modifiers->isKeyLocked(Qt::Key_CapsLock);
}

void LockKeyboard::publish(const QStringList& names, uint index)
{
    if (m_layouts == names && m_index == index)
        return;
    m_layouts = names;
    m_index = index;
    Q_EMIT layoutChanged();
}

void LockKeyboard::refresh()
{
    const auto revision = ++m_revision;
    const auto message =
        QDBusMessage::createMethodCall(m_service, layoutPath, layoutInterface, QStringLiteral("getLayoutsList"));
    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, revision](QDBusPendingCallWatcher* call) {
        const auto reply = call->reply();
        call->deleteLater();
        if (revision != m_revision)
            return;
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
            publish({}, 0);
            return;
        }
        QStringList names;
        const auto array = qvariant_cast<QDBusArgument>(reply.arguments().first());
        if (array.currentSignature() != QLatin1String("a(sss)")) {
            publish({}, 0);
            return;
        }
        array.beginArray();
        while (!array.atEnd()) {
            QString shortName, displayName, longName;
            array.beginStructure();
            array >> shortName >> displayName >> longName;
            array.endStructure();
            names.append(displayName.isEmpty() ? shortName.toUpper() : displayName);
        }
        array.endArray();
        const auto indexMessage =
            QDBusMessage::createMethodCall(m_service, layoutPath, layoutInterface, QStringLiteral("getLayout"));
        auto* indexWatcher = new QDBusPendingCallWatcher(m_bus.asyncCall(indexMessage), this);
        connect(indexWatcher, &QDBusPendingCallWatcher::finished, this,
                [this, revision, names](QDBusPendingCallWatcher* indexCall) {
                    const auto indexReply = indexCall->reply();
                    indexCall->deleteLater();
                    if (revision != m_revision)
                        return;
                    if (indexReply.type() == QDBusMessage::ErrorMessage || indexReply.arguments().isEmpty()) {
                        publish({}, 0);
                        return;
                    }
                    bool valid = false;
                    const auto index = indexReply.arguments().first().toUInt(&valid);
                    publish(valid && index < uint(names.size()) ? names : QStringList(), index);
                });
    });
}

void LockKeyboard::nextLayout()
{
    if (!canCycle())
        return;
    const auto message =
        QDBusMessage::createMethodCall(m_service, layoutPath, layoutInterface, QStringLiteral("switchToNextLayout"));
    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        refresh();
    });
}
}
