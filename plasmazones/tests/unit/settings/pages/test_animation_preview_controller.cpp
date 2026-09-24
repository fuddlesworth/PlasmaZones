// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animation_preview_controller.cpp
 * @brief AnimationPreviewController's per-frame drive against a real
 *        PhosphorRendering::ShaderEffect item, off-window.
 *
 * The pane's frame tick hands driveFrameClock a wall-clock delta every ~16 ms
 * and the controller turns it into the two book-keeping uniforms the
 * compositor advances on every paint: iTimeDelta (seconds, capped) and iFrame
 * (post-incremented, so a leg's first push reports 0). These are the numbers
 * every dt-integrating pack adds its motion up from, so the contract is pinned
 * here rather than left to the eye: the cap, the 0-first counter, and the two
 * resets that start a fresh leg (a configure, and a different item).
 *
 * The controller is built with no registry, which is its documented degraded
 * construction: configurePreviewItem then returns false, and that is enough
 * to exercise the reset, which happens before the registry lookup.
 */

#include "settings/pages/animationpreviewcontroller.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorRendering/ShaderEffect.h>

#include <QObject>
#include <QVariantMap>
#include <QtTest/QtTest>

using namespace PlasmaZones;

class TestAnimationPreviewController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void frameClockStartsAtZeroAndCounts();
    void frameClockCapsTheDelta();
    void frameClockRestartsOnConfigure();
    void frameClockRestartsOnItemChange();
    void frameClockIgnoresNonShaderItems();
};

void TestAnimationPreviewController::frameClockStartsAtZeroAndCounts()
{
    AnimationPreviewController c;
    PhosphorRendering::ShaderEffect item;

    c.driveFrameClock(&item, 16.0);
    QCOMPARE(item.iFrame(), 0);
    QCOMPARE(item.iTimeDelta(), 0.016);

    c.driveFrameClock(&item, 16.0);
    QCOMPARE(item.iFrame(), 1);
    c.driveFrameClock(&item, 16.0);
    QCOMPARE(item.iFrame(), 2);
}

void TestAnimationPreviewController::frameClockCapsTheDelta()
{
    AnimationPreviewController c;
    PhosphorRendering::ShaderEffect item;

    // A stalled pane must not hand a dt-integrated pack one multi-second
    // step: the same cap as every other push site.
    c.driveFrameClock(&item, 5000.0);
    QCOMPARE(item.iTimeDelta(), static_cast<qreal>(PhosphorAnimation::Limits::MaxShaderTimeDeltaSeconds));

    // A negative delta (clock going backwards) is floored at zero, not
    // passed through as a reverse step.
    c.driveFrameClock(&item, -40.0);
    QCOMPARE(item.iTimeDelta(), 0.0);
}

void TestAnimationPreviewController::frameClockRestartsOnConfigure()
{
    AnimationPreviewController c;
    PhosphorRendering::ShaderEffect item;

    c.driveFrameClock(&item, 16.0);
    c.driveFrameClock(&item, 16.0);
    c.driveFrameClock(&item, 16.0);
    QCOMPARE(item.iFrame(), 2);

    // The SAME item, reconfigured: a leg boundary, so the counter restarts
    // even though the item pointer never changed. (No registry, so the
    // configure itself reports failure; the reset precedes the lookup.)
    QVERIFY(!c.configurePreviewItem(&item, QStringLiteral("no-such-pack"), QVariantMap{}));
    c.driveFrameClock(&item, 16.0);
    QCOMPARE(item.iFrame(), 0);
    c.driveFrameClock(&item, 16.0);
    QCOMPARE(item.iFrame(), 1);
}

void TestAnimationPreviewController::frameClockRestartsOnItemChange()
{
    AnimationPreviewController c;
    PhosphorRendering::ShaderEffect first;
    PhosphorRendering::ShaderEffect second;

    c.driveFrameClock(&first, 16.0);
    c.driveFrameClock(&first, 16.0);
    QCOMPARE(first.iFrame(), 1);

    c.driveFrameClock(&second, 16.0);
    QCOMPARE(second.iFrame(), 0);
    // The first item is left where it was: the clock is one-item-at-a-time.
    QCOMPARE(first.iFrame(), 1);
}

void TestAnimationPreviewController::frameClockIgnoresNonShaderItems()
{
    AnimationPreviewController c;
    PhosphorRendering::ShaderEffect item;
    QQuickItem plain;

    c.driveFrameClock(&item, 16.0);
    c.driveFrameClock(&item, 16.0);
    QCOMPARE(item.iFrame(), 1);

    // A non-shader item is a no-op that does not disturb the running leg.
    c.driveFrameClock(&plain, 16.0);
    c.driveFrameClock(&item, 16.0);
    QCOMPARE(item.iFrame(), 2);
}

QTEST_MAIN(TestAnimationPreviewController)
#include "test_animation_preview_controller.moc"
