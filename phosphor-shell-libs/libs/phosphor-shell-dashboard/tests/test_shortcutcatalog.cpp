// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "../src/shortcutcatalog.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>

using PhosphorProtocol::Service::Name;
using PhosphorProtocol::Service::ObjectPath;
using PhosphorShellDashboard::ShortcutCatalog;

namespace {
QString document(const QString& id)
{
    return QStringLiteral(
               "[{\"id\":\"%1\",\"label\":\"An action\",\"mode\":\"all\",\"triggers\":[\"Meta+H\",\"Alt+Left\"]}]")
        .arg(id);
}
QString firstId(const ShortcutCatalog& catalog)
{
    return catalog.rows().isEmpty() ? QString() : catalog.rows().first().toMap().value(QLatin1String("id")).toString();
}
}

class ShortcutService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Control")
public:
    bool delayed = false;
    bool fail = false;
    QString payload = document(QStringLiteral("first"));
    QList<QDBusMessage> pending;
    QDBusConnection bus;

    explicit ShortcutService(const QDBusConnection& connection)
        : bus(connection)
    {
    }

    bool publish()
    {
        return bus.registerObject(ObjectPath, this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)
            && bus.registerService(Name);
    }
    void withdraw()
    {
        bus.unregisterService(Name);
        bus.unregisterObject(ObjectPath);
    }
    void reply(qsizetype index, const QString& value)
    {
        const auto request = pending.takeAt(index);
        bus.send(request.createReply(QVariantList{value}));
    }

public Q_SLOTS:
    QString getShortcutsJson()
    {
        if (delayed) {
            setDelayedReply(true);
            pending.append(message());
            return {};
        }
        if (fail) {
            sendErrorReply(QStringLiteral("org.plasmazones.Error.Unavailable"), QStringLiteral("Test failure"));
            return {};
        }
        return payload;
    }
Q_SIGNALS:
    void shortcutsChanged();
};

class TestShortcutCatalog : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initialAbsenceLoadingAndRetry()
    {
        ShortcutCatalog catalog;
        QVERIFY(catalog.isLoading());
        QTRY_VERIFY(!catalog.isLoading());
        QVERIFY(!catalog.isAvailable());
        QVERIFY(!catalog.error().isEmpty());
        QVERIFY(catalog.rows().isEmpty());

        const auto connection =
            QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("shortcut-fixture-absence"));
        QVERIFY(connection.isConnected());
        {
            ShortcutService service(connection);
            QVERIFY(service.publish());
            QTRY_COMPARE(firstId(catalog), QStringLiteral("first"));
            QVERIFY(catalog.isAvailable());
            QVERIFY(!catalog.isLoading());
            QVERIFY(catalog.error().isEmpty());
            QCOMPARE(catalog.rows().first().toMap().value(QLatin1String("triggers")).toStringList().size(), 2);
            service.withdraw();
            QTRY_VERIFY(!catalog.isAvailable());
            QVERIFY(catalog.rows().isEmpty());
            QVERIFY(!catalog.error().isEmpty());
            catalog.retry();
            QTRY_VERIFY(!catalog.isLoading());
            QVERIFY(!catalog.isAvailable());
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("shortcut-fixture-absence"));
    }

    void failureMalformedEmptyAndRecoveryAreDistinct()
    {
        const auto connection =
            QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("shortcut-fixture-failure"));
        {
            ShortcutService service(connection);
            service.fail = true;
            QVERIFY(service.publish());
            ShortcutCatalog catalog;
            QTRY_VERIFY(catalog.isAvailable());
            QTRY_VERIFY(!catalog.isLoading());
            QVERIFY(!catalog.error().isEmpty());
            service.fail = false;
            service.payload = QStringLiteral("[]");
            catalog.retry();
            QTRY_VERIFY(!catalog.isLoading());
            QVERIFY(catalog.error().isEmpty());
            QVERIFY(catalog.rows().isEmpty());
            for (const auto& invalid : {QStringLiteral("not json"), QStringLiteral("{}"), QStringLiteral("[7]"),
                                        QStringLiteral("[{\"id\":\"x\",\"triggers\":[12]}]")}) {
                service.payload = invalid;
                catalog.refresh();
                QTRY_VERIFY(!catalog.isLoading());
                QVERIFY(!catalog.error().isEmpty());
                QVERIFY(catalog.rows().isEmpty());
            }
            service.payload = document(QStringLiteral("recovered"));
            Q_EMIT service.shortcutsChanged();
            QTRY_COMPARE(firstId(catalog), QStringLiteral("recovered"));
            QVERIFY(catalog.error().isEmpty());
            QSignalSpy rows(&catalog, &ShortcutCatalog::rowsChanged);
            catalog.refresh();
            QTRY_VERIFY(!catalog.isLoading());
            QCOMPARE(rows.count(), 0); // Same bindings do not rebuild the UI.
            service.withdraw();
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("shortcut-fixture-failure"));
    }

    void staleFetchAndOwnerReplacementCannotRestoreOldBindings()
    {
        const auto firstConnection =
            QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("shortcut-fixture-first"));
        const auto secondConnection =
            QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("shortcut-fixture-second"));
        {
            ShortcutService first(firstConnection);
            first.delayed = true;
            QVERIFY(first.publish());
            ShortcutCatalog catalog;
            QTRY_COMPARE(first.pending.size(), 1);
            catalog.refresh();
            QTRY_COMPARE(first.pending.size(), 2);
            first.reply(1, document(QStringLiteral("newer")));
            QTRY_COMPARE(firstId(catalog), QStringLiteral("newer"));
            first.reply(0, document(QStringLiteral("stale")));
            // Wait for a subsequent roundtrip on the same connection to ensure
            // the stale reply has been delivered before checking its effect.
            catalog.refresh();
            QTRY_COMPARE(first.pending.size(), 1);
            QCOMPARE(firstId(catalog), QStringLiteral("newer"));

            first.withdraw();
            ShortcutService second(secondConnection);
            second.payload = document(QStringLiteral("replacement"));
            QVERIFY(second.publish());
            QTRY_COMPARE(firstId(catalog), QStringLiteral("replacement"));
            QVERIFY(catalog.error().isEmpty());
            first.reply(0, document(QStringLiteral("departed")));
            second.delayed = true;
            catalog.refresh();
            QTRY_COMPARE(second.pending.size(), 1);
            QCOMPARE(firstId(catalog), QStringLiteral("replacement"));
            second.withdraw();
            QTRY_VERIFY(!catalog.isAvailable());
            QVERIFY(catalog.rows().isEmpty());
            second.reply(0, document(QStringLiteral("withdrawn")));
            catalog.retry();
            QTRY_VERIFY(!catalog.isLoading());
            QVERIFY(catalog.rows().isEmpty());
            QVERIFY(!catalog.error().isEmpty());
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("shortcut-fixture-first"));
        QDBusConnection::disconnectFromBus(QStringLiteral("shortcut-fixture-second"));
    }

    void refreshFromResponseNotificationKeepsLoadingState()
    {
        const auto connection =
            QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("shortcut-fixture-reentrant"));
        {
            ShortcutService service(connection);
            service.delayed = true;
            QVERIFY(service.publish());
            ShortcutCatalog catalog;
            QTRY_COMPARE(service.pending.size(), 1);
            bool refreshed = false;
            connect(&catalog, &ShortcutCatalog::rowsChanged, &catalog, [&catalog, &refreshed] {
                if (!refreshed && !catalog.rows().isEmpty()) {
                    refreshed = true;
                    catalog.refresh();
                }
            });
            service.reply(0, document(QStringLiteral("first")));
            QTRY_COMPARE(service.pending.size(), 1);
            QVERIFY(catalog.isLoading());
            QCOMPARE(firstId(catalog), QStringLiteral("first"));
            service.reply(0, document(QStringLiteral("second")));
            QTRY_VERIFY(!catalog.isLoading());
            QCOMPARE(firstId(catalog), QStringLiteral("second"));
            service.withdraw();
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("shortcut-fixture-reentrant"));
    }
};
QTEST_GUILESS_MAIN(TestShortcutCatalog)
#include "test_shortcutcatalog.moc"
