// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "shell/DesktopStyleController.h"
#include "config/configdefaults.h"
#include <PhosphorConfig/JsonBackend.h>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
using PhosphorShellApp::DesktopStyleController;
using CD = PlasmaZones::ConfigDefaults;
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
