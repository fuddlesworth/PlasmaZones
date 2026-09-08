// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_group_write_bounds.cpp
 * @brief What the group writers REFUSE, BOUND, or fail to do — the arms that
 *        are reached when the caller misbehaves.
 *
 * Split out of test_animations_group_writes.cpp when that file reached the
 * project's size ceiling. The cut is along a real seam: everything HERE is a
 * bound, driven through the real writer rather than asserted at the guard. The converse does not hold — the sibling
 * keeps several refusal slots of its own (the async-discard refusals, the unrecognised-field refusal, the failed
 * clear), which sit beside the success paths they contrast with.
 *
 * Each of these pins a guard that previously survived deletion with the whole
 * suite still green, so each names the production line it fails on:
 *   - the knownFields allowlist, asserted against the STORE, because rawProfile
 *     sanitises on read and would drop a stray key whether or not the writer did
 *   - the non-string curve type test, without which a numeric becomes an
 *     engaged curve nobody chose
 *   - the value bounds, asserted against the STORE rather than the sanitising
 *     reader, so a writer that stopped bounding could not hide behind the read
 *     path
 *
 * Three slots retired with schema v8: the partial-failure count, the
 * Unchanged-path snapshot release, and the failure-toast latch reset. All
 * three drove the writer through a DISK failure — an unwritable profiles
 * directory, an outside writer restoring a file's bytes — and per-event
 * overrides are config keys now. One tree write either lands or does not, so
 * there is no per-path partial failure to count and no snapshot to strand.
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QVariant>

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include "phosphor_i18n.h"
#include "settings/pages/animationspagecontroller.h"
#include "helpers/AnimationsControllerFixture.h"

using namespace PlasmaZones;

namespace {

/// The same mirrored group the sibling file uses: the two window.appearance
/// legs, which is the real mirrored group in the shipped UI.
const QString kPrimary = QStringLiteral("window.appearance.open");

} // namespace

class TestAnimationsGroupWriteBounds : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Guards against a leaked process-wide registry publish from an earlier
    /// slot, the same way every sibling animations test file does.
    void init()
    {
        QCOMPARE(PhosphorAnimation::PhosphorProfileRegistry::defaultRegistry(), nullptr);
    }

    /// A field outside the allowlist never reaches the STORE.
    ///
    /// Asserted against the stored override rather than through `rawProfile`,
    /// and that is the whole point: `rawProfile` sanitises on read, so it drops
    /// an unknown key whether or not the writer did. A slot phrased against it
    /// would pass with the allowlist deleted, which is exactly the shape this
    /// audit went looking for. What the guard actually prevents is a stray key
    /// landing in the user's config and staying there until some later write
    /// happens to rewrite the object.
    void anUnknownFieldNeverReachesTheStore()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unknown profile field")));
        QCOMPARE(c.setOverrideMergedOnPaths(
                     QStringList{kPrimary},
                     QVariantMap{{QStringLiteral("bogusKey"), 1}, {QStringLiteral("duration"), 900}}, QVariant()),
                 1);

        const QJsonObject obj = TestHelpers::rawMotionOverride(fx.settings, kPrimary);
        QVERIFY2(!obj.isEmpty(), "the accepted field did not produce an override");
        QVERIFY2(!obj.contains(QStringLiteral("bogusKey")), "an unknown field was written to the store");
        QCOMPARE(obj.value(QStringLiteral("duration")).toInt(), 900);
    }

    /// An over-long value is dropped rather than written.
    ///
    /// The allowlist bounds which KEYS reach disk; nothing bounded the values
    /// riding with them, and `presetName` takes an arbitrary string. A profile
    /// file pushed past the read cap is skipped whole on the way back in, so
    /// the card renders every field as inherited while `hasOverride` still
    /// reports true — a card asserting something untrue about itself until the
    /// next write repairs it.
    void anOverLongValueIsDroppedRatherThanWritten()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("over-long value")));
        QCOMPARE(
            c.setOverrideMergedOnPaths(QStringList{kPrimary},
                                       QVariantMap{{QStringLiteral("presetName"), QString(64 * 1024, QLatin1Char('x'))},
                                                   {QStringLiteral("duration"), 850}},
                                       QVariant()),
            1);

        const QJsonObject obj = TestHelpers::rawMotionOverride(fx.settings, kPrimary);
        QVERIFY2(!obj.contains(QStringLiteral("presetName")), "an over-long value was written to the store");
        QCOMPARE(obj.value(QStringLiteral("duration")).toInt(), 850);
        // The override stayed readable, which is the point: rawProfile still
        // sees the field that did land.
        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("duration")).toInt(), 850);
    }

    /// A non-string curve is treated as "the user did not touch the curve",
    /// not stringified into one.
    ///
    /// Without the type check the QVariant is converted anyway, so a numeric 5
    /// becomes the curve "5" — an engaged curve nobody chose, pinned onto every
    /// path in the group and stopping each from inheriting.
    void aNonStringCurveIsTreatedAsUntouched()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        TestHelpers::TimingControllerFixture fx;
        auto& c = fx.c;
        c.setUserProfilesDirOverride(tmp.path());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("non-string curve")));
        QCOMPARE(c.setOverrideMergedOnPaths(QStringList{kPrimary}, QVariantMap{{QStringLiteral("duration"), 700}},
                                            QVariant(5)),
                 1);

        const QJsonObject obj = TestHelpers::rawMotionOverride(fx.settings, kPrimary);
        QCOMPARE(obj.value(QStringLiteral("duration")).toInt(), 700);
        QVERIFY2(!obj.contains(QStringLiteral("curve")), "a non-string curve was stringified into an engaged curve");
    }
};

QTEST_MAIN(TestAnimationsGroupWriteBounds)
#include "test_animations_group_write_bounds.moc"
