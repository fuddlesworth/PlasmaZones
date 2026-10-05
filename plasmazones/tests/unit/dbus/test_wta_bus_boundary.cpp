// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_bus_boundary.cpp
 * @brief What WindowTrackingAdaptor takes from the effect's reports: a screen
 *        the daemon does not know is held until it arrives, a split monitor's
 *        physical id lands in the right virtual screen, and a metadata push is
 *        normalised to WindowMetadata's invariants (F81, F852).
 */

#include "wta_float_back_fixture.h"
#include "helpers/VirtualScreenTestHelpers.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowTypeEnum.h>

#include <QSignalSpy>

namespace Key = PhosphorProtocol::Service::WindowMetadataKey;

class TestWtaBusBoundary : public QObject
{
    Q_OBJECT

    static void push(WindowTrackingAdaptor* wta, const QString& instance, const QString& appId, int desktop,
                     const QVariantMap& extended,
                     int windowType = static_cast<int>(PhosphorProtocol::WindowType::Normal))
    {
        wta->setWindowMetadata(instance, appId, QString(), QStringLiteral("t"), QString(), 100, desktop, QString(),
                               windowType, extended);
    }

private Q_SLOTS:
    // KWin's output can reach the effect before the daemon's QScreen. The
    // report is held, not stored, and lands when the screen does.
    void cursorReportOnAnUnknownScreenIsHeldUntilItArrives()
    {
        FloatBackFixture f;
        QCoreApplication::processEvents(); // the adaptor wires its screen hooks on the first loop turn
        f.wta->cursorScreenChanged(kLeft);
        QCOMPARE(f.wta->lastCursorScreenName(), kLeft);
        f.wta->cursorScreenChanged(QStringLiteral("DP-9"));
        QCOMPARE(f.wta->lastCursorScreenName(), kLeft);

        f.fake.addScreen(QStringLiteral("DP-9"), QRect(3840, 0, 1920, 1080), QStringLiteral("DP-9"));
        QTRY_COMPARE(f.wta->lastCursorScreenName(), QStringLiteral("DP-9"));
    }

    // A later report supersedes a held one.
    void aLaterCursorReportDropsTheHeldOne()
    {
        FloatBackFixture f;
        QCoreApplication::processEvents();
        f.wta->cursorScreenChanged(QStringLiteral("DP-9"));
        f.wta->cursorScreenChanged(kRight);
        f.fake.addScreen(QStringLiteral("DP-9"), QRect(3840, 0, 1920, 1080), QStringLiteral("DP-9"));
        QTest::qWait(20);
        QCOMPARE(f.wta->lastCursorScreenName(), kRight);
    }

    // The effect can send a split monitor's physical id; the focused-window
    // record takes the virtual screen the window's frame is in.
    void activationOnAPhysicalIdOfASplitMonitorTakesTheFramesVirtualScreen()
    {
        FloatBackFixture f;
        QVERIFY(f.screenMgr->setVirtualScreenConfig(kRight, TestHelpers::makeSplitConfig(kRight)));
        const QString windowId = f.registerWindow(QStringLiteral("split-1"));
        f.setFrame(windowId, QRect(3000, 100, 400, 300)); // the right half of DP-2
        f.wta->windowActivated(windowId, kRight);
        QCOMPARE(f.wta->lastActiveScreenName(), QStringLiteral("DP-2/vs:1"));
    }

    // A move onto a screen nobody knows changes nothing yet: the snap leave
    // would run against an id nothing can place on.
    void screenChangeOnAnUnknownScreenMovesNothing()
    {
        FloatBackFixture f;
        const QString windowId = f.registerWindow(QStringLiteral("snapped-1"));
        f.snap->commitSnap(windowId, f.zone(0), kLeft);
        QCOMPARE(f.snap->screenForTrackedWindow(windowId), kLeft);
        QSignalSpy states(f.wta, &WindowTrackingAdaptor::windowStateChanged);

        f.wta->windowScreenChanged(windowId, QStringLiteral("DP-9"));

        QCOMPARE(f.snap->screenForTrackedWindow(windowId), kLeft);
        QCOMPARE(f.snap->zoneForWindow(windowId), f.zone(0));
        QCOMPARE(states.count(), 0);
    }

    // WindowMetadata's invariant: a span lists two or more desktops and
    // starts with the scalar desktop.
    void metadataOneEntrySpanIsCleared()
    {
        FloatBackFixture f;
        QVariantMap ext{{QString(Key::Width), 640}, {QString(Key::VirtualDesktops), QVariantList{2}}};
        push(f.wta, QStringLiteral("span-1"), QStringLiteral("app"), 2, ext);
        QVERIFY(f.registry.metadata(QStringLiteral("span-1"))->virtualDesktops.isEmpty());

        ext.insert(QString(Key::VirtualDesktops), QVariantList{3, 2});
        push(f.wta, QStringLiteral("span-1"), QStringLiteral("app"), 2, ext);
        QVERIFY2(f.registry.metadata(QStringLiteral("span-1"))->virtualDesktops.isEmpty(),
                 "a span not led by the scalar desktop is no span");

        ext.insert(QString(Key::VirtualDesktops), QVariantList{2, 3});
        push(f.wta, QStringLiteral("span-1"), QStringLiteral("app"), 2, ext);
        QCOMPARE(f.registry.metadata(QStringLiteral("span-1"))->virtualDesktops, (QList<int>{2, 3}));
    }

    // A non-positive size is unknown, not one the min-size gate compares.
    void metadataNonPositiveSizeIsUnknown()
    {
        FloatBackFixture f;
        push(f.wta, QStringLiteral("size-1"), QStringLiteral("app"), 1,
             {{QString(Key::Width), 0}, {QString(Key::Height), -5}});
        const auto meta = f.registry.metadata(QStringLiteral("size-1"));
        QVERIFY(meta);
        QVERIFY(!meta->width.has_value());
        QVERIFY(!meta->height.has_value());
    }

    // F852 pins: the first-seen composite is frozen as the canonical id, and
    // the negative-desktop and window-type clamps hold.
    void metadataSeedsTheCanonicalId()
    {
        FloatBackFixture f;
        push(f.wta, QStringLiteral("seed-1"), QStringLiteral("old"), 1, {{QString(Key::Width), 640}});
        push(f.wta, QStringLiteral("seed-1"), QStringLiteral("new"), 1, {{QString(Key::Width), 640}});
        QCOMPARE(f.registry.canonicalizeForLookup(QStringLiteral("new|seed-1")), QStringLiteral("old|seed-1"));
    }

    void metadataNegativeDesktopReadsUnknown()
    {
        FloatBackFixture f;
        push(f.wta, QStringLiteral("neg-1"), QStringLiteral("app"), -3, {{QString(Key::Width), 640}});
        QCOMPARE(f.registry.metadata(QStringLiteral("neg-1"))->virtualDesktop, 0);
    }

    void metadataOutOfRangeTypeReadsUnknown()
    {
        FloatBackFixture f;
        push(f.wta, QStringLiteral("type-1"), QStringLiteral("app"), 1, {{QString(Key::Width), 640}}, 9999);
        QCOMPARE(f.registry.metadata(QStringLiteral("type-1"))->windowType, PhosphorProtocol::WindowType::Unknown);
    }
};

QTEST_MAIN(TestWtaBusBoundary)
#include "test_wta_bus_boundary.moc"
