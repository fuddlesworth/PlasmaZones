// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorShellLock/LockKeyboard.h>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QTest>

struct LayoutNames
{
    QString shortName;
    QString displayName;
    QString longName;
};
Q_DECLARE_METATYPE(LayoutNames)
Q_DECLARE_METATYPE(QList<LayoutNames>)
QDBusArgument& operator<<(QDBusArgument& argument, const LayoutNames& names)
{
    argument.beginStructure();
    argument << names.shortName << names.displayName << names.longName;
    argument.endStructure();
    return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, LayoutNames& names)
{
    argument.beginStructure();
    argument >> names.shortName >> names.displayName >> names.longName;
    argument.endStructure();
    return argument;
}

class FakeLayouts : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KeyboardLayouts")
public:
    uint index = 0;
    int switches = 0;
    QList<LayoutNames> names{{QStringLiteral("us"), {}, QStringLiteral("English (US)")},
                             {QStringLiteral("de"), QStringLiteral("DE"), QStringLiteral("German")}};
public Q_SLOTS:
    uint getLayout() const
    {
        return index;
    }
    QList<LayoutNames> getLayoutsList() const
    {
        return names;
    }
    void switchToNextLayout()
    {
        ++switches;
        index = (index + 1) % uint(names.size());
        Q_EMIT layoutChanged(index);
    }
Q_SIGNALS:
    void layoutChanged(uint index);
    void layoutListChanged();
};

class TestLockKeyboard : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void followsRealLayoutAndSurvivesProviderLoss()
    {
        qDBusRegisterMetaType<LayoutNames>();
        qDBusRegisterMetaType<QList<LayoutNames>>();
        auto bus = QDBusConnection::sessionBus();
        const QString service = QStringLiteral("org.phosphor.test.lockkeyboard");
        FakeLayouts layouts;
        QVERIFY(bus.registerService(service));
        QVERIFY(bus.registerObject(QStringLiteral("/Layouts"), &layouts,
                                   QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        PhosphorShellLock::LockKeyboard keyboard(bus, service);
        QTRY_COMPARE(keyboard.layoutName(), QStringLiteral("US"));
        QVERIFY(keyboard.canCycle());
        keyboard.nextLayout();
        QTRY_COMPARE(keyboard.layoutName(), QStringLiteral("DE"));
        QCOMPARE(layouts.switches, 1);
        layouts.names.removeLast();
        layouts.index = 0;
        Q_EMIT layouts.layoutListChanged();
        QTRY_COMPARE(keyboard.layoutName(), QStringLiteral("US"));
        QVERIFY(!keyboard.canCycle());
        keyboard.nextLayout();
        QCOMPARE(layouts.switches, 1);
        QVERIFY(bus.unregisterService(service));
        QTRY_VERIFY(keyboard.layoutName().isEmpty());
        QVERIFY(!keyboard.canCycle());
        QVERIFY(bus.registerService(service));
        QTRY_COMPARE(keyboard.layoutName(), QStringLiteral("US"));
        layouts.index = 99;
        Q_EMIT layouts.layoutChanged(layouts.index);
        QTRY_VERIFY(keyboard.layoutName().isEmpty());
        QVERIFY(!keyboard.canCycle());
        bus.unregisterObject(QStringLiteral("/Layouts"));
        QVERIFY(bus.unregisterService(service));
    }
};
QTEST_MAIN(TestLockKeyboard)
#include "test_lockkeyboard.moc"
