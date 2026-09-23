// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ShellGestures subscribes to the daemon's gestureReported relay and
// splits it into swiped / pinched for QML. A fake daemon on the test's
// private session bus emits the signal by well-known name.

#include "ShellGestures.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorShellApp;

namespace ShellGesturesTestSupport {

constexpr QLatin1String kFakeService("org.plasmazones.test.gestures");
constexpr QLatin1String kFakePath("/PlasmaZones");

// The daemon side of the relay, reduced to the one signal.
class FakeBridge : public QObject
{
    Q_OBJECT
public:
    bool emitGesture(const QString& kind, const QString& direction, uint fingers)
    {
        QDBusMessage signal =
            QDBusMessage::createSignal(kFakePath, ShellGestures::interfaceName(), ShellGestures::signalName());
        signal << kind << direction << fingers;
        // Returns bool rather than QVERIFY-ing in a void helper: QVERIFY
        // expands to a return, so in a void helper it aborts only the send
        // and the real failure surfaces later as a confusing QTRY timeout.
        // test_control_center_controller.cpp does the same for importQtQuick.
        return QDBusConnection::sessionBus().send(signal);
    }
};

} // namespace ShellGesturesTestSupport

using namespace ShellGesturesTestSupport;

class TestShellGestures : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(QDBusConnection::sessionBus().isConnected(), "no session bus (run under dbus-run-session)");
        QVERIFY(QDBusConnection::sessionBus().registerService(kFakeService));
        QVERIFY(QDBusConnection::sessionBus().registerObject(kFakePath, &m_bridge));
    }

    void swipesAndPinchesAreSplit()
    {
        ShellGestures gestures(kFakeService, kFakePath, nullptr);
        QSignalSpy swiped(&gestures, &ShellGestures::swiped);
        QSignalSpy pinched(&gestures, &ShellGestures::pinched);

        QVERIFY(m_bridge.emitGesture(QStringLiteral("swipe"), QStringLiteral("up"), 3));
        QVERIFY(m_bridge.emitGesture(QStringLiteral("pinch"), QStringLiteral("expanding"), 4));
        QVERIFY(m_bridge.emitGesture(QStringLiteral("tap"), QStringLiteral("up"), 1));

        QTRY_COMPARE(swiped.count(), 1);
        QTRY_COMPARE(pinched.count(), 1);
        QCOMPARE(swiped.at(0).at(0).toString(), QStringLiteral("up"));
        QCOMPARE(swiped.at(0).at(1).toUInt(), 3u);
        QCOMPARE(pinched.at(0).at(0).toString(), QStringLiteral("expanding"));
        QCOMPARE(pinched.at(0).at(1).toUInt(), 4u);
        // The unknown kind reached neither.
        QTest::qWait(50);
        QCOMPARE(swiped.count(), 1);
        QCOMPARE(pinched.count(), 1);
    }

    void otherServicesAreIgnored()
    {
        ShellGestures gestures(QStringLiteral("org.plasmazones.test.other"), kFakePath, nullptr);
        QSignalSpy swiped(&gestures, &ShellGestures::swiped);
        QVERIFY(m_bridge.emitGesture(QStringLiteral("swipe"), QStringLiteral("up"), 3));
        QTest::qWait(100);
        QCOMPARE(swiped.count(), 0);
    }

private:
    FakeBridge m_bridge;
};

QTEST_GUILESS_MAIN(TestShellGestures)

#include "test_shell_gestures.moc"
