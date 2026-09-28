// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The desktop style writes the window gaps through the daemon's Settings
// interface. A fake daemon on the test's private session bus stands in for
// it, holding resolved values the way the real one answers getSettings.
#include "DesktopStyleController.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <QDBusConnection>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>
#include <utility>
using PhosphorShellApp::DesktopStyleController;
namespace Key = PhosphorProtocol::Service::SettingProperty;

namespace DesktopStyleTestSupport {

const QString kFakeService = QStringLiteral("org.plasmazones.test.desktopstyle");
const QString kLateService = QStringLiteral("org.plasmazones.test.desktopstyle.late");

class FakeDaemonSettings : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Settings")
public:
    static QVariantMap defaults()
    {
        return {{QString(Key::InnerGap), 8},
                {QString(Key::OuterGap), 8},
                {QString(Key::UsePerSideOuterGap), false},
                {QString(Key::OuterGapTop), 8},
                {QString(Key::OuterGapBottom), 8},
                {QString(Key::OuterGapLeft), 8},
                {QString(Key::OuterGapRight), 8}};
    }
    QVariantMap values = defaults();
    int writes = 0;
    bool failNextWrite = false;
    std::function<void()> afterWrite;
    int value(QLatin1String key) const
    {
        return values.value(QString(key)).toInt();
    }

public Q_SLOTS:
    QVariantMap getSettings(const QStringList& keys)
    {
        QVariantMap out;
        for (const auto& key : keys) {
            if (values.contains(key))
                out.insert(key, values.value(key));
        }
        return out;
    }
    bool setSettings(const QVariantMap& update)
    {
        ++writes;
        if (std::exchange(failNextWrite, false))
            return false;
        for (auto it = update.cbegin(); it != update.cend(); ++it) {
            const auto value = it.value();
            values.insert(it.key(), value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant() : value);
        }
        if (afterWrite)
            std::exchange(afterWrite, {})();
        return true;
    }
};

} // namespace DesktopStyleTestSupport

using namespace DesktopStyleTestSupport;

class TestDesktopStyleController : public QObject
{
    Q_OBJECT
private:
    // The fake daemon is registered on this same connection, so the bus
    // delivers the controller's blocking calls to it in-process instead of
    // waiting on an event loop the blocked thread cannot run.
    std::unique_ptr<DesktopStyleController> make(const QTemporaryDir& dir, QString journal = {}, bool available = true,
                                                 const QString& service = kFakeService)
    {
        if (journal.isEmpty())
            journal = dir.filePath(QStringLiteral("session.json"));
        return std::make_unique<DesktopStyleController>(dir.filePath(QStringLiteral("kwinrc")), journal, service,
                                                        available, QDBusConnection::sessionBus());
    }
    FakeDaemonSettings m_daemon;
    const QString m_libraryKey = QStringLiteral("org.kde.kdecoration2/library");

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(QDBusConnection::sessionBus().isConnected(), "no session bus (run under dbus-run-session)");
        QVERIFY(QDBusConnection::sessionBus().registerService(kFakeService));
        QVERIFY(QDBusConnection::sessionBus().registerObject(QString(PhosphorProtocol::Service::ObjectPath), &m_daemon,
                                                             QDBusConnection::ExportAllSlots));
    }
    void init()
    {
        m_daemon.values = FakeDaemonSettings::defaults();
        m_daemon.writes = 0;
        m_daemon.failNextWrite = false;
        m_daemon.afterWrite = {};
    }

    void followsBarEdgeAndGap()
    {
        auto gaps = DesktopStyleController::gaps({{QStringLiteral("gap"), 16}});
        QCOMPARE(gaps.value(QString(Key::InnerGap)).toInt(), 16);
        QCOMPARE(gaps.value(QString(Key::OuterGapLeft)).toInt(), 44);
        QCOMPARE(gaps.value(QString(Key::OuterGapTop)).toInt(), 22);
        QCOMPARE(gaps.value(QString(Key::OuterGapBottom)).toInt(), 62);
        gaps = DesktopStyleController::gaps(
            {{QStringLiteral("gap"), 8}, {QStringLiteral("edge"), QStringLiteral("bottom")}});
        QCOMPARE(gaps.value(QString(Key::OuterGapTop)).toInt(), 40);
        QCOMPARE(gaps.value(QString(Key::OuterGapBottom)).toInt(), 18);
    }

    void restoresExistingSettingsAndDefaults()
    {
        QTemporaryDir dir;
        m_daemon.values[QString(Key::InnerGap)] = 9;
        m_daemon.values[QString(Key::OuterGap)] = 12;
        QSettings kwin(dir.filePath(QStringLiteral("kwinrc")), QSettings::IniFormat);
        kwin.setValue(m_libraryKey, QStringLiteral("original"));
        kwin.sync();
        std::unique_ptr<DesktopStyleController> controller(make(dir));
        controller->apply({{QStringLiteral("gap"), 16}});
        kwin.sync();
        QCOMPARE(kwin.value(m_libraryKey).toString(), QStringLiteral("org.phosphor.decoration"));
        QCOMPARE(m_daemon.value(Key::InnerGap), 16);
        QCOMPARE(m_daemon.value(Key::OuterGap), 12);
        // Multiple engine reloads and style changes keep the original baseline.
        controller->apply({{QStringLiteral("gap"), 24}});
        controller->apply({{QStringLiteral("gap"), 24}});
        QVERIFY(controller->restore());
        kwin.sync();
        QCOMPARE(kwin.value(m_libraryKey).toString(), QStringLiteral("original"));
        auto expected = FakeDaemonSettings::defaults();
        expected[QString(Key::InnerGap)] = 9;
        expected[QString(Key::OuterGap)] = 12;
        QCOMPARE(m_daemon.values, expected);
    }

    void preservesExternalChangesOnDisable()
    {
        QTemporaryDir dir;
        std::unique_ptr<DesktopStyleController> controller(make(dir));
        controller->apply({});
        QSettings kwin(dir.filePath(QStringLiteral("kwinrc")), QSettings::IniFormat);
        kwin.setValue(m_libraryKey, QStringLiteral("user-picked"));
        kwin.sync();
        m_daemon.values[QString(Key::InnerGap)] = 11;
        controller->apply({{QStringLiteral("desktopStyle"), false}});
        kwin.sync();
        QCOMPARE(kwin.value(m_libraryKey).toString(), QStringLiteral("user-picked"));
        QCOMPARE(m_daemon.value(Key::InnerGap), 11);
        QCOMPARE(m_daemon.value(Key::OuterGapTop), 8);
        QCOMPARE(m_daemon.values.value(QString(Key::UsePerSideOuterGap)).toBool(), false);
    }

    void failedUpdateStillRestoresLastSuccessfulValues()
    {
        QTemporaryDir dir;
        m_daemon.values[QString(Key::InnerGap)] = 9;
        std::unique_ptr<DesktopStyleController> controller(make(dir));
        controller->apply({{QStringLiteral("gap"), 16}});
        m_daemon.failNextWrite = true;
        QTest::ignoreMessage(QtWarningMsg, "Could not apply the shell window spacing");
        controller->apply({{QStringLiteral("gap"), 24}});
        QCOMPARE(m_daemon.value(Key::InnerGap), 16);
        QVERIFY(controller->restore());
        QCOMPARE(m_daemon.value(Key::InnerGap), 9);
        QCOMPARE(m_daemon.value(Key::OuterGapTop), 8);
    }

    void retryAfterFailedUpdateKeepsOriginalBaseline()
    {
        QTemporaryDir dir;
        m_daemon.values[QString(Key::InnerGap)] = 9;
        std::unique_ptr<DesktopStyleController> controller(make(dir));
        controller->apply({{QStringLiteral("gap"), 16}});
        m_daemon.failNextWrite = true;
        QTest::ignoreMessage(QtWarningMsg, "Could not apply the shell window spacing");
        controller->apply({{QStringLiteral("gap"), 24}});
        controller->apply({{QStringLiteral("gap"), 24}});
        QCOMPARE(m_daemon.value(Key::InnerGap), 24);
        QVERIFY(controller->restore());
        QCOMPARE(m_daemon.value(Key::InnerGap), 9);
        QCOMPARE(m_daemon.value(Key::OuterGapTop), 8);
    }

    void failedCompletionRecordDoesNotRetainStaleApplyCache()
    {
        QTemporaryDir dir;
        const auto journalPath = dir.filePath(QStringLiteral("session.json"));
        const auto backup = dir.filePath(QStringLiteral("journal-backup.json"));
        m_daemon.values[QString(Key::InnerGap)] = 9;
        std::unique_ptr<DesktopStyleController> controller(make(dir, journalPath));
        controller->apply({{QStringLiteral("gap"), 16}});
        m_daemon.afterWrite = [&] {
            QVERIFY(QFile::rename(journalPath, backup));
            QVERIFY(QDir().mkpath(journalPath));
        };
        QTest::ignoreMessage(QtWarningMsg, "Could not record the applied shell window spacing");
        controller->apply({{QStringLiteral("gap"), 24}});
        QVERIFY(QDir().rmdir(journalPath));
        QVERIFY(QFile::rename(backup, journalPath));
        controller->apply({{QStringLiteral("gap"), 16}});
        QCOMPARE(m_daemon.value(Key::InnerGap), 16);
        QVERIFY(controller->restore());
        QCOMPARE(m_daemon.value(Key::InnerGap), 9);
    }

    void interruptedUpdateRecoversAcrossRestart_data()
    {
        QTest::addColumn<bool>("committed");
        QTest::newRow("before-daemon-write") << false;
        QTest::newRow("after-daemon-write") << true;
    }

    void interruptedUpdateRecoversAcrossRestart()
    {
        QFETCH(bool, committed);
        QTemporaryDir dir;
        const auto journalPath = dir.filePath(QStringLiteral("session.json"));
        QByteArray interruptedJournal;
        m_daemon.values[QString(Key::InnerGap)] = 9;
        {
            std::unique_ptr<DesktopStyleController> controller(make(dir, journalPath));
            controller->apply({{QStringLiteral("gap"), 16}});
            m_daemon.afterWrite = [&] {
                QFile journal(journalPath);
                QVERIFY(journal.open(QIODevice::ReadOnly));
                interruptedJournal = journal.readAll();
            };
            controller->apply({{QStringLiteral("gap"), 24}});
        }
        QVERIFY(!interruptedJournal.isEmpty());
        // Recreate either daemon outcome with the journal captured between
        // the daemon's write and the completion record, plus an unrelated
        // change someone made since.
        const auto gaps = DesktopStyleController::gaps({{QStringLiteral("gap"), committed ? 24 : 16}});
        for (auto it = gaps.cbegin(); it != gaps.cend(); ++it)
            m_daemon.values.insert(it.key(), it.value());
        m_daemon.values[QString(Key::OuterGapRight)] = 11;
        {
            QFile journal(journalPath);
            QVERIFY(journal.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(journal.write(interruptedJournal), interruptedJournal.size());
        }
        std::unique_ptr<DesktopStyleController> recovered(make(dir, journalPath));
        QVERIFY(recovered->restore());
        QCOMPARE(m_daemon.value(Key::InnerGap), 9);
        QCOMPARE(m_daemon.value(Key::OuterGapRight), 11);
        QCOMPARE(m_daemon.value(Key::OuterGapTop), 8);
    }

    void failedRestoreCompletionDoesNotRetainStaleApplyCache()
    {
        QTemporaryDir dir;
        const auto journalPath = dir.filePath(QStringLiteral("session.json"));
        const auto backup = dir.filePath(QStringLiteral("journal-backup.json"));
        m_daemon.values[QString(Key::InnerGap)] = 9;
        std::unique_ptr<DesktopStyleController> controller(make(dir, journalPath));
        controller->apply({{QStringLiteral("gap"), 16}});
        m_daemon.afterWrite = [&] {
            QVERIFY(QFile::rename(journalPath, backup));
            QVERIFY(QDir().mkpath(journalPath));
        };
        QVERIFY(!controller->restore());
        QVERIFY(QDir().rmdir(journalPath));
        QVERIFY(QFile::rename(backup, journalPath));
        controller->apply({{QStringLiteral("gap"), 16}});
        QCOMPARE(m_daemon.value(Key::InnerGap), 16);
        QVERIFY(controller->restore());
        QCOMPARE(m_daemon.value(Key::InnerGap), 9);
    }

    void missingPluginLeavesDesktopUntouched()
    {
        QTemporaryDir dir;
        std::unique_ptr<DesktopStyleController> controller(make(dir, {}, false));
        controller->apply({});
        QCOMPARE(m_daemon.writes, 0);
        QCOMPARE(m_daemon.values, FakeDaemonSettings::defaults());
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("kwinrc"))));
    }

    void absentDaemonIsRetriedWhenItRegisters()
    {
        QTemporaryDir dir;
        const auto kwinPath = dir.filePath(QStringLiteral("kwinrc"));
        auto controller = make(dir, {}, true, kLateService);
        QTest::ignoreMessage(QtWarningMsg, "Could not read the window spacing from PlasmaZones");
        controller->apply({{QStringLiteral("gap"), 16}});
        // Nothing was journaled, written or styled while the daemon was away.
        QCOMPARE(m_daemon.writes, 0);
        QVERIFY(!QFileInfo::exists(kwinPath));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("session.json"))));

        // The daemon arrives: the same fake object answers under the new name.
        QVERIFY(QDBusConnection::sessionBus().registerService(kLateService));
        QTRY_COMPARE(m_daemon.value(Key::InnerGap), 16);
        QSettings kwin(kwinPath, QSettings::IniFormat);
        QCOMPARE(kwin.value(m_libraryKey).toString(), QStringLiteral("org.phosphor.decoration"));
        QVERIFY(controller->restore());
        QCOMPARE(m_daemon.values, FakeDaemonSettings::defaults());
        QVERIFY(QDBusConnection::sessionBus().unregisterService(kLateService));
    }
};
QTEST_GUILESS_MAIN(TestDesktopStyleController)
#include "test_desktop_style_controller.moc"
