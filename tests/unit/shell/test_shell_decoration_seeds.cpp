// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/shelldecorationseeds.h"
#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorSurface/DecorationSupportedPaths.h>
#include <PhosphorTheme/AppearanceStore.h>
#include <PhosphorTheme/AppearanceWatcher.h>
#include <QFile>
#include <QSettings>
#include <QTest>

using namespace PlasmaZones;
using namespace PhosphorSurfaceShaders;
using namespace PhosphorTheme;

class TestShellDecorationSeeds : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void windowDefaultsRequireActiveDesktopStyle()
    {
        auto values = AppearanceStore::defaults();
        QVERIFY(
            shellDecorationSeedTree(values, false).resolve(QStringLiteral("window.floating")).enabledChain().isEmpty());
        const auto active = shellDecorationSeedTree(values, true);
        for (const auto& path :
             {QStringLiteral("window.floating"), QStringLiteral("window.snapped"), QStringLiteral("window.tiled")}) {
            QCOMPARE(active.resolve(path).enabledChain(),
                     (QStringList{QStringLiteral("border"), QStringLiteral("top-rail"), QStringLiteral("shadow"),
                                  QStringLiteral("glow")}));
        }
        const auto rail = active.resolve(QStringLiteral("window.floating"))
                              .effectiveParameters()
                              .value(QStringLiteral("top-rail"))
                              .toMap();
        QCOMPARE(rail.value(QStringLiteral("cornerRadius")).toInt(), values.value(QStringLiteral("radius")).toInt());
        QCOMPARE(rail.value(QStringLiteral("activeHeight")).toInt(), 2);
        QCOMPARE(rail.value(QStringLiteral("inactiveHeight")).toInt(), 1);
        values[QStringLiteral("glow")] = false;
        QCOMPARE(shellDecorationSeedTree(values, true).resolve(QStringLiteral("window.floating")).enabledChain(),
                 (QStringList{QStringLiteral("border"), QStringLiteral("top-rail"), QStringLiteral("shadow")}));
    }

    void livePreviewUpdatesBothViewsWithoutWritingDecorationConfig()
    {
        TestHelpers::IsolatedConfigGuard guard;
        const auto appearancePath = guard.configPath() + QStringLiteral("/appearance.json");
        const auto kwinPath = guard.configPath() + QStringLiteral("/kwinrc");
        QSettings kwin(kwinPath, QSettings::IniFormat);
        kwin.setValue(QStringLiteral("org.kde.kdecoration2/library"), QStringLiteral("org.phosphor.decoration"));
        kwin.sync();
        AppearanceStore appearance(appearancePath);
        QVERIFY(appearance.setValue(QStringLiteral("surfacePacks"), true));
        QVERIFY(appearance.setValue(QStringLiteral("surfaceEffect"), QStringLiteral("glass")));
        AppearanceWatcher watcher(appearancePath, kwinPath);
        Settings settings;
        bindShellDecorationSeeds(settings, watcher);
        settings.setInnerGap(9);
        QVERIFY(settings.save());
        const auto readConfig = [&guard] {
            QFile file(guard.configPath() + QStringLiteral("/plasmazones/config.json"));
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
        };
        const auto config = readConfig();
        QVERIFY(!config.isEmpty());
        const auto saved = settings.decorationProfileTree();
        const auto bar = decorationShellPhosphorBarPath();
        QCOMPARE(saved.resolve(bar).enabledChain(), QStringList{QStringLiteral("phosphor-glass")});
        QVERIFY(appearance.beginPreview());
        QVERIFY(appearance.setValue(QStringLiteral("surfaceEffect"), QStringLiteral("motes")));
        QVERIFY(appearance.setValue(QStringLiteral("radius"), 30));
        QTRY_COMPARE(settings.decorationProfileTree().resolve(bar).enabledChain(),
                     QStringList{QStringLiteral("phosphor-motes")});
        QCOMPARE(settings.decorationProfileTree(), settings.committedDecorationProfileTree());
        QCOMPARE(settings.decorationProfileTree()
                     .resolve(QStringLiteral("window.floating"))
                     .effectiveParameters()
                     .value(QStringLiteral("border"))
                     .toMap()
                     .value(QStringLiteral("cornerRadius"))
                     .toInt(),
                 30);
        QCOMPARE(readConfig(), config);
        appearance.revertPreview();
        QTRY_COMPARE(settings.decorationProfileTree(), saved);
        QVERIFY(appearance.setValue(QStringLiteral("surfaceEffect"), QStringLiteral("motes")));
        QVERIFY(appearance.applyPreview());
        appearance.endPreview();
        QTRY_COMPARE(settings.decorationProfileTree().resolve(bar).enabledChain(),
                     QStringList{QStringLiteral("phosphor-motes")});
        QCOMPARE(readConfig(), config);
    }

    void authoredChainsParametersAndOffAlwaysWinOverAppearance()
    {
        TestHelpers::IsolatedConfigGuard guard;
        Settings settings;
        auto values = AppearanceStore::defaults();
        values[QStringLiteral("surfacePacks")] = true;
        values[QStringLiteral("surfaceEffect")] = QStringLiteral("glass");
        settings.setDecorationSeedTree(shellDecorationSeedTree(values, true));
        auto user = settings.decorationProfileTree();
        DecorationProfile custom;
        custom.chain = QStringList{QStringLiteral("glow")};
        custom.parameters = QVariantMap{{QStringLiteral("glow"),
                                         QVariantMap{{QStringLiteral("glowSize"), 17},
                                                     {QStringLiteral("useWindowAccent"), false},
                                                     {QStringLiteral("glowColor"), QStringLiteral("#803377aa")}}}};
        user.setOverride(decorationShellPhosphorBarPath(), custom.withDefaults());
        user.setOverride(QStringLiteral("window.floating"), custom.withDefaults());
        DecorationProfile off;
        off.chain = QStringList{};
        user.setOverride(decorationShellPhosphorPopoutPath(), off);
        // A disabled seed pack is also an explicit user decision.
        auto disabled = user.directOverride(decorationShellPhosphorOsdPath());
        disabled.disabledPacks = QStringList{QStringLiteral("phosphor-glass")};
        user.setOverride(decorationShellPhosphorOsdPath(), disabled);
        settings.setDecorationProfileTree(user);
        settings.save();

        values[QStringLiteral("palette")] = QStringLiteral("ember");
        values[QStringLiteral("radius")] = 29;
        settings.setDecorationSeedTree(shellDecorationSeedTree(values, true));
        const auto resolved = settings.decorationProfileTree();
        QCOMPARE(resolved.resolve(decorationShellPhosphorBarPath()), custom.withDefaults());
        QCOMPARE(resolved.resolve(QStringLiteral("window.floating")), custom.withDefaults());
        QVERIFY(resolved.resolve(decorationShellPhosphorPopoutPath()).enabledChain().isEmpty());
        QVERIFY(resolved.resolve(decorationShellPhosphorOsdPath()).enabledChain().isEmpty());
        values[QStringLiteral("surfacePacks")] = false;
        settings.setDecorationSeedTree(shellDecorationSeedTree(values, false));
        QCOMPARE(settings.decorationProfileTree().resolve(decorationShellPhosphorBarPath()), custom.withDefaults());
        QCOMPARE(settings.decorationProfileTree().resolve(QStringLiteral("window.floating")), custom.withDefaults());
        Settings reloaded;
        reloaded.setDecorationSeedTree(shellDecorationSeedTree(values, true));
        QCOMPARE(reloaded.decorationProfileTree().resolve(decorationShellPhosphorBarPath()), custom.withDefaults());
        QVERIFY(reloaded.decorationProfileTree().resolve(decorationShellPhosphorPopoutPath()).enabledChain().isEmpty());
    }
};

QTEST_MAIN(TestShellDecorationSeeds)
#include "test_shell_decoration_seeds.moc"
