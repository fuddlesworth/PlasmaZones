// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "shell/DesktopStyleController.h"
#include "config/configdefaults.h"
#include <PhosphorConfig/JsonBackend.h>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <utility>
using PhosphorShellApp::DesktopStyleController;
using CD = PlasmaZones::ConfigDefaults;

class FailingBackend : public PhosphorConfig::JsonBackend
{
public:
    using JsonBackend::JsonBackend;
    bool failNextCommit = false;
    std::function<void()> afterCommit;
    bool commit() override
    {
        if (std::exchange(failNextCommit, false))
            return false;
        const bool committed = JsonBackend::commit();
        if (committed && afterCommit)
            std::exchange(afterCommit, {})();
        return committed;
    }
};

class TestDesktopStyleController : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void followsBarEdgeAndGap()
    {
        auto gaps = DesktopStyleController::gaps({{QStringLiteral("gap"), 16}});
        QCOMPARE(gaps.value(CD::innerGapKey()).toInt(), 16);
        QCOMPARE(gaps.value(CD::outerGapLeftKey()).toInt(), 44);
        QCOMPARE(gaps.value(CD::outerGapTopKey()).toInt(), 22);
        QCOMPARE(gaps.value(CD::outerGapBottomKey()).toInt(), 62);
        gaps = DesktopStyleController::gaps(
            {{QStringLiteral("gap"), 8}, {QStringLiteral("edge"), QStringLiteral("bottom")}});
        QCOMPARE(gaps.value(CD::outerGapTopKey()).toInt(), 40);
        QCOMPARE(gaps.value(CD::outerGapBottomKey()).toInt(), 18);
    }
    void restoresExistingSettingsAndAbsentKeys()
    {
        QTemporaryDir dir;
        auto backend = std::make_unique<PhosphorConfig::JsonBackend>(dir.filePath(QStringLiteral("config.json")));
        auto* read = backend.get();
        {
            auto group = read->group(CD::gapsGroup());
            group->writeInt(CD::innerGapKey(), 9);
            group->writeInt(CD::outerGapKey(), 12);
        }
        QVERIFY(read->commit());
        QSettings kwin(dir.filePath(QStringLiteral("kwinrc")), QSettings::IniFormat);
        const auto key = QStringLiteral("org.kde.kdecoration2/library");
        kwin.setValue(key, QStringLiteral("original"));
        kwin.sync();
        DesktopStyleController controller(kwin.fileName(), dir.filePath(QStringLiteral("session.json")),
                                          std::move(backend), true, QDBusConnection(QString()));
        controller.apply({{QStringLiteral("gap"), 16}});
        kwin.sync();
        QCOMPARE(kwin.value(key).toString(), QStringLiteral("org.phosphor.decoration"));
        {
            auto group = read->group(CD::gapsGroup());
            QCOMPARE(group->readInt(CD::innerGapKey()), 16);
            QCOMPARE(group->readInt(CD::outerGapKey()), 12);
        }
        // Multiple engine reloads and style changes keep the original baseline.
        controller.apply({{QStringLiteral("gap"), 24}});
        controller.apply({{QStringLiteral("gap"), 24}});
        QVERIFY(controller.restore());
        kwin.sync();
        QCOMPARE(kwin.value(key).toString(), QStringLiteral("original"));
        auto group = read->group(CD::gapsGroup());
        QCOMPARE(group->readInt(CD::innerGapKey()), 9);
        QVERIFY(!group->hasKey(CD::outerGapLeftKey()));
        QCOMPARE(group->readInt(CD::outerGapKey()), 12);
    }
    void preservesExternalChangesOnDisable()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("config.json"));
        DesktopStyleController controller(
            dir.filePath(QStringLiteral("kwinrc")), dir.filePath(QStringLiteral("session.json")),
            std::make_unique<PhosphorConfig::JsonBackend>(path), true, QDBusConnection(QString()));
        controller.apply({});
        QSettings kwin(dir.filePath(QStringLiteral("kwinrc")), QSettings::IniFormat);
        const auto key = QStringLiteral("org.kde.kdecoration2/library");
        kwin.setValue(key, QStringLiteral("user-picked"));
        kwin.sync();
        PhosphorConfig::JsonBackend external(path);
        {
            auto group = external.group(CD::gapsGroup());
            group->writeInt(CD::innerGapKey(), 11);
        }
        QVERIFY(external.commit());
        controller.apply({{QStringLiteral("desktopStyle"), false}});
        kwin.sync();
        QCOMPARE(kwin.value(key).toString(), QStringLiteral("user-picked"));
        external.reparseConfiguration();
        auto group = external.group(CD::gapsGroup());
        QCOMPARE(group->readInt(CD::innerGapKey()), 11);
        QVERIFY(!group->hasKey(CD::outerGapTopKey()));
    }
    void failedUpdateStillRestoresLastSuccessfulValues()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("config.json"));
        auto backend = std::make_unique<FailingBackend>(path);
        auto* read = backend.get();
        {
            auto group = read->group(CD::gapsGroup());
            group->writeInt(CD::innerGapKey(), 9);
        }
        QVERIFY(read->commit());
        DesktopStyleController controller(dir.filePath(QStringLiteral("kwinrc")),
                                          dir.filePath(QStringLiteral("session.json")), std::move(backend), true,
                                          QDBusConnection(QString()));
        controller.apply({{QStringLiteral("gap"), 16}});
        read->failNextCommit = true;
        QTest::ignoreMessage(QtWarningMsg, "Could not apply the shell window spacing");
        controller.apply({{QStringLiteral("gap"), 24}});

        PhosphorConfig::JsonBackend disk(path);
        {
            auto group = disk.group(CD::gapsGroup());
            QCOMPARE(group->readInt(CD::innerGapKey()), 16);
        }
        QVERIFY(controller.restore());
        disk.reparseConfiguration();
        auto group = disk.group(CD::gapsGroup());
        QCOMPARE(group->readInt(CD::innerGapKey()), 9);
        QVERIFY(!group->hasKey(CD::outerGapTopKey()));
    }

    void retryAfterFailedUpdateKeepsOriginalBaseline()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("config.json"));
        auto backend = std::make_unique<FailingBackend>(path);
        auto* read = backend.get();
        {
            auto group = read->group(CD::gapsGroup());
            group->writeInt(CD::innerGapKey(), 9);
        }
        QVERIFY(read->commit());
        DesktopStyleController controller(dir.filePath(QStringLiteral("kwinrc")),
                                          dir.filePath(QStringLiteral("session.json")), std::move(backend), true,
                                          QDBusConnection(QString()));
        controller.apply({{QStringLiteral("gap"), 16}});
        read->failNextCommit = true;
        QTest::ignoreMessage(QtWarningMsg, "Could not apply the shell window spacing");
        controller.apply({{QStringLiteral("gap"), 24}});
        controller.apply({{QStringLiteral("gap"), 24}});
        {
            auto group = read->group(CD::gapsGroup());
            QCOMPARE(group->readInt(CD::innerGapKey()), 24);
        }
        QVERIFY(controller.restore());
        auto group = read->group(CD::gapsGroup());
        QCOMPARE(group->readInt(CD::innerGapKey()), 9);
        QVERIFY(!group->hasKey(CD::outerGapTopKey()));
    }

    void interruptedUpdateRecoversAcrossRestart_data()
    {
        QTest::addColumn<bool>("committed");
        QTest::newRow("before-backend-commit") << false;
        QTest::newRow("after-backend-commit") << true;
    }

    void failedCompletionRecordDoesNotRetainStaleApplyCache()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("config.json"));
        const auto journalPath = dir.filePath(QStringLiteral("session.json"));
        const auto backup = dir.filePath(QStringLiteral("journal-backup.json"));
        auto backend = std::make_unique<FailingBackend>(path);
        auto* read = backend.get();
        {
            auto group = read->group(CD::gapsGroup());
            group->writeInt(CD::innerGapKey(), 9);
        }
        QVERIFY(read->commit());
        DesktopStyleController controller(dir.filePath(QStringLiteral("kwinrc")), journalPath, std::move(backend), true,
                                          QDBusConnection(QString()));
        controller.apply({{QStringLiteral("gap"), 16}});
        read->afterCommit = [&] {
            QVERIFY(QFile::rename(journalPath, backup));
            QVERIFY(QDir().mkpath(journalPath));
        };
        QTest::ignoreMessage(QtWarningMsg, "Could not record the applied shell window spacing");
        controller.apply({{QStringLiteral("gap"), 24}});
        QVERIFY(QDir().rmdir(journalPath));
        QVERIFY(QFile::rename(backup, journalPath));
        controller.apply({{QStringLiteral("gap"), 16}});
        {
            auto group = read->group(CD::gapsGroup());
            QCOMPARE(group->readInt(CD::innerGapKey()), 16);
        }
        QVERIFY(controller.restore());
        auto group = read->group(CD::gapsGroup());
        QCOMPARE(group->readInt(CD::innerGapKey()), 9);
    }

    void interruptedUpdateRecoversAcrossRestart()
    {
        QFETCH(bool, committed);
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("config.json"));
        const auto journalPath = dir.filePath(QStringLiteral("session.json"));
        const auto kwinPath = dir.filePath(QStringLiteral("kwinrc"));
        QByteArray interruptedJournal;
        {
            auto backend = std::make_unique<FailingBackend>(path);
            auto* read = backend.get();
            {
                auto group = read->group(CD::gapsGroup());
                group->writeInt(CD::innerGapKey(), 9);
            }
            QVERIFY(read->commit());
            DesktopStyleController controller(kwinPath, journalPath, std::move(backend), true,
                                              QDBusConnection(QString()));
            controller.apply({{QStringLiteral("gap"), 16}});
            read->afterCommit = [&] {
                QFile journal(journalPath);
                QVERIFY(journal.open(QIODevice::ReadOnly));
                interruptedJournal = journal.readAll();
            };
            controller.apply({{QStringLiteral("gap"), 24}});
        }
        QVERIFY(!interruptedJournal.isEmpty());
        // Recreate either atomic disk outcome with the journal captured
        // between the backend commit and the completion record.
        PhosphorConfig::JsonBackend disk(path);
        {
            auto group = disk.group(CD::gapsGroup());
            const auto gaps = DesktopStyleController::gaps({{QStringLiteral("gap"), committed ? 24 : 16}});
            for (auto it = gaps.cbegin(); it != gaps.cend(); ++it)
                group->writeJson(it.key(), QJsonValue::fromVariant(it.value()));
            group->writeInt(CD::outerGapRightKey(), 11);
        }
        QVERIFY(disk.commit());
        {
            QFile journal(journalPath);
            QVERIFY(journal.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(journal.write(interruptedJournal), interruptedJournal.size());
        }
        DesktopStyleController recovered(kwinPath, journalPath, std::make_unique<PhosphorConfig::JsonBackend>(path),
                                         true, QDBusConnection(QString()));
        QVERIFY(recovered.restore());
        disk.reparseConfiguration();
        auto group = disk.group(CD::gapsGroup());
        QCOMPARE(group->readInt(CD::innerGapKey()), 9);
        QCOMPARE(group->readInt(CD::outerGapRightKey()), 11);
        QVERIFY(!group->hasKey(CD::outerGapTopKey()));
    }

    void failedRestoreCompletionDoesNotRetainStaleApplyCache()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("config.json"));
        const auto journalPath = dir.filePath(QStringLiteral("session.json"));
        const auto backup = dir.filePath(QStringLiteral("journal-backup.json"));
        auto backend = std::make_unique<FailingBackend>(path);
        auto* read = backend.get();
        {
            auto group = read->group(CD::gapsGroup());
            group->writeInt(CD::innerGapKey(), 9);
        }
        QVERIFY(read->commit());
        DesktopStyleController controller(dir.filePath(QStringLiteral("kwinrc")), journalPath, std::move(backend), true,
                                          QDBusConnection(QString()));
        controller.apply({{QStringLiteral("gap"), 16}});
        read->afterCommit = [&] {
            QVERIFY(QFile::rename(journalPath, backup));
            QVERIFY(QDir().mkpath(journalPath));
        };
        QVERIFY(!controller.restore());
        QVERIFY(QDir().rmdir(journalPath));
        QVERIFY(QFile::rename(backup, journalPath));
        controller.apply({{QStringLiteral("gap"), 16}});
        {
            auto group = read->group(CD::gapsGroup());
            QCOMPARE(group->readInt(CD::innerGapKey()), 16);
        }
        QVERIFY(controller.restore());
        auto group = read->group(CD::gapsGroup());
        QCOMPARE(group->readInt(CD::innerGapKey()), 9);
    }

    void missingPluginLeavesDesktopUntouched()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("config.json"));
        DesktopStyleController controller(
            dir.filePath(QStringLiteral("kwinrc")), dir.filePath(QStringLiteral("session.json")),
            std::make_unique<PhosphorConfig::JsonBackend>(path), false, QDBusConnection(QString()));
        controller.apply({});
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("kwinrc"))));
    }
};
QTEST_GUILESS_MAIN(TestDesktopStyleController)
#include "test_desktop_style_controller.moc"
