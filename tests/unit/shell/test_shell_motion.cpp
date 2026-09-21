// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell's motion root: the settle spring is registered under its
// profile path with a spring curve, the QML defaults publish and
// unpublish cleanly, and the portal's two motion keys decode to the
// reduced-motion verdict the theme expects.

#include "shell/ShellMotion.h"

#include <PhosphorAnimation/PhosphorCurve.h>
#include <PhosphorAnimation/Profile.h>

#include <QDBusVariant>
#include <QTest>
#include <QVariant>

using namespace PhosphorShellApp;

class TestShellMotion : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void settleIsASpringAtItsPath()
    {
        qputenv("PHOSPHOR_REDUCED_MOTION", "0");
        ShellMotion motion;
        const auto profile = motion.profileRegistry()->resolve(ShellMotion::settlePath());
        QVERIFY(profile.has_value());
        QVERIFY(profile->curve);
        QCOMPARE(profile->curve->typeId(), QStringLiteral("spring"));
        QCOMPARE(profile->effectiveDuration(), 250.0);
        // Inheritance-aware resolution (what a Behavior binding uses) lands
        // on the same curve.
        const auto resolved = motion.profileRegistry()->resolveWithInheritance(ShellMotion::settlePath());
        QVERIFY(resolved.curve);
        QCOMPARE(resolved.curve->typeId(), QStringLiteral("spring"));
    }

    void publishInstallsAndClearsTheDefaults()
    {
        qputenv("PHOSPHOR_REDUCED_MOTION", "0");
        {
            ShellMotion motion;
            QVERIFY(PhosphorAnimation::PhosphorProfileRegistry::defaultRegistry() == nullptr);
            motion.publish();
            QCOMPARE(PhosphorAnimation::PhosphorProfileRegistry::defaultRegistry(), motion.profileRegistry());
            QCOMPARE(PhosphorAnimation::PhosphorCurve::defaultRegistry(), motion.curveRegistry());
            motion.publish(); // idempotent
            QCOMPARE(PhosphorAnimation::PhosphorProfileRegistry::defaultRegistry(), motion.profileRegistry());
        }
        // Destruction unpublishes.
        QVERIFY(PhosphorAnimation::PhosphorProfileRegistry::defaultRegistry() == nullptr);
        QVERIFY(PhosphorAnimation::PhosphorCurve::defaultRegistry() == nullptr);
    }

    void environmentOverrideWins()
    {
        qputenv("PHOSPHOR_REDUCED_MOTION", "1");
        ShellMotion reduced;
        QVERIFY(reduced.reducedMotion());
        qputenv("PHOSPHOR_REDUCED_MOTION", "0");
        ShellMotion full;
        QVERIFY(!full.reducedMotion());
    }

    void portalKeysDecode_data()
    {
        QTest::addColumn<QString>("ns");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<bool>("known");
        QTest::addColumn<bool>("reduced");

        const QString kde = QStringLiteral("org.kde.kdeglobals.KDE");
        const QString kdeKey = QStringLiteral("AnimationDurationFactor");
        const QString gnome = QStringLiteral("org.gnome.desktop.interface");
        const QString gnomeKey = QStringLiteral("enable-animations");

        QTest::newRow("kde off") << kde << kdeKey << QVariant(0.0) << true << true;
        QTest::newRow("kde normal") << kde << kdeKey << QVariant(1.0) << true << false;
        QTest::newRow("kde wrapped") << kde << kdeKey << QVariant::fromValue(QDBusVariant(QVariant(0.0))) << true
                                     << true;
        QTest::newRow("kde double wrapped")
            << kde << kdeKey << QVariant::fromValue(QDBusVariant(QVariant::fromValue(QDBusVariant(QVariant(2.0)))))
            << true << false;
        QTest::newRow("gnome off") << gnome << gnomeKey << QVariant(false) << true << true;
        QTest::newRow("gnome on") << gnome << gnomeKey << QVariant(true) << true << false;
        QTest::newRow("other key") << kde << QStringLiteral("ColorScheme") << QVariant(QStringLiteral("x")) << false
                                   << false;
    }

    void portalKeysDecode()
    {
        QFETCH(QString, ns);
        QFETCH(QString, key);
        QFETCH(QVariant, value);
        QFETCH(bool, known);
        QFETCH(bool, reduced);
        const auto verdict = ShellMotion::reducedMotionFor(ns, key, value);
        QCOMPARE(verdict.has_value(), known);
        if (known) {
            QCOMPARE(*verdict, reduced);
        }
    }
};

QTEST_GUILESS_MAIN(TestShellMotion)

#include "test_shell_motion.moc"
