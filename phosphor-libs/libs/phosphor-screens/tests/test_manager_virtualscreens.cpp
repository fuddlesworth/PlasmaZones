// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// ScreenManager's virtual-screen config surface: setVirtualScreenConfig's
// change/removal signalling and virtualScreenIdsFor's resolution. These need
// no QScreen — setVirtualScreenConfig validates the config and writes
// m_virtualConfigs, and virtualScreenIdsFor reads it back. Virtual GEOMETRY
// does need backing screen geometry, and is covered by
// test_screenmanager_geometry.

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <QSignalSpy>
#include <QTest>

using PhosphorScreens::ScreenManager;
using PhosphorScreens::VirtualScreenConfig;
using PhosphorScreens::VirtualScreenDef;

namespace {

constexpr QLatin1String kPhys{"Dell:U2722D:115107"};

VirtualScreenDef makeDef(const QString& physId, int index, const QString& name, const QRectF& region)
{
    VirtualScreenDef def;
    def.id = PhosphorIdentity::VirtualScreenId::make(physId, index);
    def.physicalScreenId = physId;
    def.displayName = name;
    def.region = region;
    def.index = index;
    return def;
}

/// A two-way 50/50 horizontal split.
VirtualScreenConfig makeSplitConfig(const QString& physId)
{
    VirtualScreenConfig config;
    config.physicalScreenId = physId;
    config.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.5, 1.0)));
    config.screens.append(makeDef(physId, 1, QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1.0)));
    return config;
}

} // namespace

class TestManagerVirtualScreens : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void virtualScreenConfigChange_signalEmitted()
    {
        ScreenManager screenMgr;

        const QString physId = kPhys;
        VirtualScreenConfig config = makeSplitConfig(physId);

        QSignalSpy vsChangedSpy(&screenMgr, &ScreenManager::virtualScreensChanged);

        bool result = screenMgr.setVirtualScreenConfig(physId, config);
        QVERIFY(result);
        QCOMPARE(vsChangedSpy.count(), 1);
        QCOMPARE(vsChangedSpy.first().first().toString(), physId);
    }

    void virtualScreenRemoval_signalEmitted()
    {
        ScreenManager screenMgr;

        const QString physId = kPhys;
        VirtualScreenConfig config = makeSplitConfig(physId);

        screenMgr.setVirtualScreenConfig(physId, config);

        QSignalSpy vsChangedSpy(&screenMgr, &ScreenManager::virtualScreensChanged);

        // Remove by setting empty config
        VirtualScreenConfig emptyConfig;
        screenMgr.setVirtualScreenConfig(physId, emptyConfig);

        QCOMPARE(vsChangedSpy.count(), 1);
        QCOMPARE(vsChangedSpy.first().first().toString(), physId);
    }

    void virtualScreenIdsFor_returnsCorrectIds()
    {
        ScreenManager screenMgr;

        const QString physId = kPhys;
        VirtualScreenConfig config = makeSplitConfig(physId);

        screenMgr.setVirtualScreenConfig(physId, config);

        QStringList vsIds = screenMgr.virtualScreenIdsFor(physId);
        QCOMPARE(vsIds.size(), 2);
        QVERIFY(vsIds.contains(PhosphorIdentity::VirtualScreenId::make(physId, 0)));
        QVERIFY(vsIds.contains(PhosphorIdentity::VirtualScreenId::make(physId, 1)));
    }

    void virtualScreenIdsFor_noConfig_returnsPhysicalId()
    {
        ScreenManager screenMgr;

        const QString physId = kPhys;
        QStringList ids = screenMgr.virtualScreenIdsFor(physId);

        QCOMPARE(ids.size(), 1);
        QCOMPARE(ids.first(), physId);
    }

    void virtualScreenIdsFor_afterRemoval_returnsPhysicalId()
    {
        ScreenManager screenMgr;

        const QString physId = kPhys;
        VirtualScreenConfig config = makeSplitConfig(physId);
        screenMgr.setVirtualScreenConfig(physId, config);

        // Verify VS IDs exist
        QCOMPARE(screenMgr.virtualScreenIdsFor(physId).size(), 2);

        // Remove config
        VirtualScreenConfig emptyConfig;
        screenMgr.setVirtualScreenConfig(physId, emptyConfig);

        // Should fall back to physical ID
        QStringList ids = screenMgr.virtualScreenIdsFor(physId);
        QCOMPARE(ids.size(), 1);
        QCOMPARE(ids.first(), physId);
    }
};

QTEST_MAIN(TestManagerVirtualScreens)
#include "test_manager_virtualscreens.moc"
