// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include <QJsonObject>
#include <QTest>

using namespace PhosphorAnimation;

namespace {
// Counted via a message handler rather than N ignoreMessage calls: the exact
// per-key count below the cap is the limiter's contract, and ignoreMessage
// cannot count. File-scope because a local class cannot carry static members.
int g_announcements = 0;
int g_perKeyWarnings = 0;
void countingHandler(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    if (msg.contains(QLatin1String("further ones are suppressed"))) {
        ++g_announcements;
    } else if (msg.contains(QLatin1String("did not resolve"))) {
        ++g_perKeyWarnings;
    }
}
} // namespace

/// Pins the BOUNDED half of Profile::fromJson's warn-once limiter: past 256
/// distinct diagnostic keys it stops inserting, announces the cap exactly
/// once, and stays silent thereafter. The limiter is a security-shaped guard
/// (the key space includes curve specs a QML script can feed through
/// PhosphorProfile::fromJson), and deleting the cap block passes every other
/// test in the suite.
///
/// OWN BINARY, deliberately: the limiter is process-wide static state, so a
/// slot that exhausts the cap would poison the rate-limit assertions in
/// test_profile.cpp's sibling slots (testFromJsonReportsANonNumericFieldOnce
/// and the out-of-range sequenceMode slot). Nothing else may run in this
/// executable.
class TestProfileWarnCap : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testWarnOnceCapAnnouncesExactlyOnceAndThenSuppresses()
    {
        const CurveRegistry curves;

        // Drive well past the 256-key cap with distinct unresolvable curve
        // specs. Each one below the cap produces its own "did not resolve"
        // warning; the cap announcement fires exactly once at the boundary.
        const QtMessageHandler prev = qInstallMessageHandler(&countingHandler);
        constexpr int kProbes = 300; // comfortably past the 256-key cap
        for (int i = 0; i < kProbes; ++i) {
            QJsonObject obj;
            obj.insert(QLatin1String("curve"), QStringLiteral("bogus-curve-%1:1,2").arg(i));
            const Profile p = Profile::fromJson(obj, curves);
            QVERIFY(!p.curve);
        }
        qInstallMessageHandler(prev);

        // Exactly one cap announcement, and the per-key warnings stopped at
        // the cap instead of tracking kProbes. The exact figure IS the cap
        // value; a drift here means the cap moved without this test.
        QCOMPARE(g_announcements, 1);
        QCOMPARE(g_perKeyWarnings, 256);

        // And a repeat of an already-seen key stays silent (the announcement
        // does not re-fire either).
        g_announcements = 0;
        g_perKeyWarnings = 0;
        QJsonObject repeat;
        repeat.insert(QLatin1String("curve"), QStringLiteral("bogus-curve-0:1,2"));
        const QtMessageHandler prev2 = qInstallMessageHandler(&countingHandler);
        const Profile p = Profile::fromJson(repeat, curves);
        qInstallMessageHandler(prev2);
        QVERIFY(!p.curve);
        QCOMPARE(g_announcements, 0);
        QCOMPARE(g_perKeyWarnings, 0);
    }
};

QTEST_MAIN(TestProfileWarnCap)
#include "test_profile_warncap.moc"
