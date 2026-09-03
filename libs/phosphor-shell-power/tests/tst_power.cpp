// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Ties the Phosphor.Power module's published types to the files that
// actually ship in it.
//
// PowerMenu cannot be LOADED here: it imports Phosphor.Service.Session, which
// only the shell process registers. For it the assertion is on
// QQmlComponent::url(), which is set as soon as the type name resolves to a
// file and before its own imports are evaluated, cleanly separating "this type
// is not in the module" from "its imports are unavailable in a unit test".
//
// PowerTile has no such import and does reach Ready, so it gets the stronger
// assertion: the file parses, every type it names resolves, and its own
// imports are satisfiable.
//
// What Ready does NOT buy, stated plainly because an earlier version of this
// comment claimed otherwise and mutation testing disproved it: it does not
// cover the IMPORTS-vs-DEPENDENCIES Kirigami-shadows-Theme hazard the module's
// CMakeLists warns about. Swapping DEPENDENCIES for IMPORTS leaves this test
// green, because a qmldir-injected unqualified import ranks BELOW a file's own
// import list and PowerTile imports Phosphor.Theme directly. Ready is a
// compile-time status and never evaluates a binding. Catching that class needs
// the bar's approach: create the object and assert a resolved Theme value.

#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

class TestPower : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyPublishedTypeResolves_data()
    {
        QTest::addColumn<QString>("typeName");
        // loadable: false for a type whose imports the shell registers at
        // runtime and a unit test therefore cannot satisfy.
        QTest::addColumn<bool>("loadable");
        QTest::newRow("PowerMenu") << QStringLiteral("PowerMenu") << false;
        QTest::newRow("PowerTile") << QStringLiteral("PowerTile") << true;
    }

    void everyPublishedTypeResolves()
    {
        QFETCH(QString, typeName);
        QFETCH(bool, loadable);

        QQmlEngine engine;
        QQmlComponent component(&engine, QStringLiteral("Phosphor.Power"), typeName);

        // url() is non-empty once the module supplies a file for this name,
        // and stays empty when the name is absent — exactly the
        // rename / dropped-from-QML_FILES failure.
        QVERIFY2(!component.url().isEmpty(),
                 qPrintable(QStringLiteral("type '%1' does not resolve in Phosphor.Power: %2")
                                .arg(typeName, component.errorString())));

        if (loadable) {
            QVERIFY2(
                component.status() == QQmlComponent::Ready,
                qPrintable(
                    QStringLiteral("type '%1' resolves but does not load: %2").arg(typeName, component.errorString())));
        }
    }

    void anUnknownTypeIsRefused()
    {
        // Guard the guard: if url() were non-empty for everything, the case
        // above would pass no matter what the module contained.
        QQmlEngine engine;
        QQmlComponent component(&engine, QStringLiteral("Phosphor.Power"), QStringLiteral("NoSuchType"));
        QVERIFY(component.url().isEmpty());
    }
};

QTEST_MAIN(TestPower)

#include "tst_power.moc"
