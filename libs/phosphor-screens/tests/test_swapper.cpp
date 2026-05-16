// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/InMemoryConfigStore.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <QTest>

namespace Direction = Phosphor::Screens::Direction;
using Phosphor::Screens::InMemoryConfigStore;
using Phosphor::Screens::VirtualScreenConfig;
using Phosphor::Screens::VirtualScreenDef;
using Phosphor::Screens::VirtualScreenSwapper;

namespace {

constexpr QLatin1String kPhys{"Dell:U2722D:115107"};

VirtualScreenDef makeDef(int index, const QRectF& region, const QString& name)
{
    VirtualScreenDef d;
    d.index = index;
    d.id = PhosphorIdentity::VirtualScreenId::make(kPhys, index);
    d.physicalScreenId = kPhys;
    d.displayName = name;
    d.region = region;
    return d;
}

// 4-up grid (clockwise from top-left: 0=TL, 1=TR, 2=BL, 3=BR).
VirtualScreenConfig makeGridConfig()
{
    VirtualScreenConfig cfg;
    cfg.physicalScreenId = kPhys;
    cfg.screens.append(makeDef(0, QRectF(0.0, 0.0, 0.5, 0.5), QStringLiteral("TL")));
    cfg.screens.append(makeDef(1, QRectF(0.5, 0.0, 0.5, 0.5), QStringLiteral("TR")));
    cfg.screens.append(makeDef(2, QRectF(0.0, 0.5, 0.5, 0.5), QStringLiteral("BL")));
    cfg.screens.append(makeDef(3, QRectF(0.5, 0.5, 0.5, 0.5), QStringLiteral("BR")));
    return cfg;
}

VirtualScreenConfig makeHorizontalHalves()
{
    VirtualScreenConfig cfg;
    cfg.physicalScreenId = kPhys;
    cfg.screens.append(makeDef(0, QRectF(0.0, 0.0, 0.5, 1.0), QStringLiteral("Left")));
    cfg.screens.append(makeDef(1, QRectF(0.5, 0.0, 0.5, 1.0), QStringLiteral("Right")));
    return cfg;
}

} // namespace

class TestSwapper : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── swapInDirection ───

    void testSwapRightOnHorizontalHalves()
    {
        InMemoryConfigStore store;
        QVERIFY(store.save(kPhys, makeHorizontalHalves()));
        VirtualScreenSwapper s(&store);

        const QString leftId = PhosphorIdentity::VirtualScreenId::make(kPhys, 0);
        QCOMPARE(s.swapInDirection(leftId, QString(Direction::Right)), VirtualScreenSwapper::Result::Ok);

        const VirtualScreenConfig after = store.get(kPhys);
        // Left VS now owns the right half and vice versa.
        QCOMPARE(after.screens[0].id, leftId);
        QCOMPARE(after.screens[0].region, QRectF(0.5, 0.0, 0.5, 1.0));
        QCOMPARE(after.screens[1].region, QRectF(0.0, 0.0, 0.5, 1.0));
    }

    void testSwapNoSiblingInDirectionReturnsSpecificReason()
    {
        InMemoryConfigStore store;
        QVERIFY(store.save(kPhys, makeHorizontalHalves()));
        VirtualScreenSwapper s(&store);

        const QString leftId = PhosphorIdentity::VirtualScreenId::make(kPhys, 0);
        QCOMPARE(s.swapInDirection(leftId, QString(Direction::Up)), VirtualScreenSwapper::Result::NoSiblingInDirection);
    }

    void testSwapRejectsPhysicalInput()
    {
        InMemoryConfigStore store;
        VirtualScreenSwapper s(&store);
        QCOMPARE(s.swapInDirection(kPhys, QString(Direction::Right)), VirtualScreenSwapper::Result::NotVirtual);
    }

    void testSwapRejectsInvalidDirection()
    {
        InMemoryConfigStore store;
        QVERIFY(store.save(kPhys, makeHorizontalHalves()));
        VirtualScreenSwapper s(&store);

        const QString leftId = PhosphorIdentity::VirtualScreenId::make(kPhys, 0);
        QCOMPARE(s.swapInDirection(leftId, QStringLiteral("diagonal")), VirtualScreenSwapper::Result::InvalidDirection);
    }

    void testSwapNoSubdivisionWhenPhysOnly()
    {
        InMemoryConfigStore store;
        VirtualScreenSwapper s(&store);

        const QString leftId = PhosphorIdentity::VirtualScreenId::make(kPhys, 0);
        QCOMPARE(s.swapInDirection(leftId, QString(Direction::Right)), VirtualScreenSwapper::Result::NoSubdivision);
    }

    void testSwapUnknownVirtualScreen()
    {
        InMemoryConfigStore store;
        QVERIFY(store.save(kPhys, makeHorizontalHalves()));
        VirtualScreenSwapper s(&store);

        const QString ghost = PhosphorIdentity::VirtualScreenId::make(kPhys, 99);
        QCOMPARE(s.swapInDirection(ghost, QString(Direction::Right)),
                 VirtualScreenSwapper::Result::UnknownVirtualScreen);
    }

    // ─── rotate ───

    void testRotateClockwiseOn4UpGrid()
    {
        InMemoryConfigStore store;
        QVERIFY(store.save(kPhys, makeGridConfig()));
        VirtualScreenSwapper s(&store);

        QCOMPARE(s.rotate(kPhys, /*clockwise=*/true), VirtualScreenSwapper::Result::Ok);

        const VirtualScreenConfig after = store.get(kPhys);
        // IDs preserved at their original positions; regions cycled CW in the
        // ring (TL ← TR, TR ← BR, BR ← BL, BL ← TL).
        QCOMPARE(after.screens[0].region, QRectF(0.5, 0.0, 0.5, 0.5)); // TL now holds TR
        QCOMPARE(after.screens[1].region, QRectF(0.5, 0.5, 0.5, 0.5)); // TR now holds BR
        QCOMPARE(after.screens[3].region, QRectF(0.0, 0.5, 0.5, 0.5)); // BR now holds BL
        QCOMPARE(after.screens[2].region, QRectF(0.0, 0.0, 0.5, 0.5)); // BL now holds TL
    }

    void testRotateCounterClockwiseIsInverse()
    {
        InMemoryConfigStore store;
        QVERIFY(store.save(kPhys, makeGridConfig()));
        VirtualScreenSwapper s(&store);

        QCOMPARE(s.rotate(kPhys, true), VirtualScreenSwapper::Result::Ok);
        QCOMPARE(s.rotate(kPhys, false), VirtualScreenSwapper::Result::Ok);

        const VirtualScreenConfig after = store.get(kPhys);
        const VirtualScreenConfig original = makeGridConfig();
        QCOMPARE(after.screens, original.screens);
    }

    void testRotateRejectsVirtualId()
    {
        InMemoryConfigStore store;
        VirtualScreenSwapper s(&store);
        const QString vsId = PhosphorIdentity::VirtualScreenId::make(kPhys, 0);
        QCOMPARE(s.rotate(vsId, true), VirtualScreenSwapper::Result::NotVirtual);
    }

    void testRotateNoSubdivision()
    {
        InMemoryConfigStore store;
        VirtualScreenSwapper s(&store);
        QCOMPARE(s.rotate(kPhys, true), VirtualScreenSwapper::Result::NoSubdivision);
    }

    // ─── reasonString ───

    void testReasonStringTokensAreStable()
    {
        // Consumers (KCM, editor) match on these exact strings — assertion
        // catches accidental renames.
        QCOMPARE(VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::Ok), QString());
        QCOMPARE(VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::NotVirtual),
                 QStringLiteral("not_virtual"));
        QCOMPARE(VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::NoSubdivision),
                 QStringLiteral("no_subdivision"));
        QCOMPARE(VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::UnknownVirtualScreen),
                 QStringLiteral("unknown_vs"));
        QCOMPARE(VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::NoSiblingInDirection),
                 QStringLiteral("no_sibling"));
        QCOMPARE(VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::InvalidDirection),
                 QStringLiteral("invalid_direction"));
        QCOMPARE(VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::SwapFailed),
                 QStringLiteral("swap_failed"));
        QCOMPARE(VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::SettingsRejected),
                 QStringLiteral("settings_rejected"));
    }
};

QTEST_APPLESS_MAIN(TestSwapper)
#include "test_swapper.moc"
