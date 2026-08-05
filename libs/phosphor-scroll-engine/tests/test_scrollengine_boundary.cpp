// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_scrollengine_boundary.cpp
 *
 * The screen-boundary contract: a straddling edge column is committed CLAMPED
 * at both screen edges in the default mode, keeps its TRUE rect in crop mode,
 * parks below its peek floor, and every park lands below the union of all
 * outputs. These are the applyLayout branches the smoke fixtures cannot reach
 * (their columns are either fully on screen or fully off), driven here by an
 * always-center settings stub so the neighbours of the focused column
 * genuinely straddle.
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"

using PhosphorScrollEngine::ScrollEngine;
using ScrollTestUtils::defaultScreenRect;
using ScrollTestUtils::makeProviderEngine;

namespace {

const QString kS1 = QStringLiteral("S1");

/// Minimal IScrollSettings whose centering and crop answers are test-set.
/// Everything else answers the engine's own defaults.
class BoundaryStubSettings : public QObject, public PhosphorEngine::IScrollSettings
{
    Q_OBJECT
    Q_INTERFACES(PhosphorEngine::IScrollSettings)

public:
    using QObject::QObject;

    bool cropStraddlers = false;
    int centerFocused = 1; // always — the straddler generator
    bool respectMinimumSize = false;
    int insertPosition = static_cast<int>(PhosphorScrollEngine::ScrollInsertPosition::RightOfActive);

    int scrollingInnerGap() const override
    {
        return 0;
    }
    bool scrollingUsePerSideOuterGap() const override
    {
        return false;
    }
    int scrollingOuterGap() const override
    {
        return 0;
    }
    int scrollingOuterGapTop() const override
    {
        return 0;
    }
    int scrollingOuterGapBottom() const override
    {
        return 0;
    }
    int scrollingOuterGapLeft() const override
    {
        return 0;
    }
    int scrollingOuterGapRight() const override
    {
        return 0;
    }
    bool scrollingFocusNewWindows() const override
    {
        return true;
    }
    int scrollingStickyWindowHandling() const override
    {
        return 0;
    }
    bool scrollingRespectMinimumSize() const override
    {
        return respectMinimumSize;
    }
    bool scrollingSmartGaps() const override
    {
        return false;
    }
    int scrollingCenterFocusedColumn() const override
    {
        return centerFocused;
    }
    bool scrollingAlwaysCenterSingleColumn() const override
    {
        return false;
    }
    bool scrollingCropStraddlers() const override
    {
        return cropStraddlers;
    }
    int scrollingDefaultColumnWidthKind() const override
    {
        return 0; // proportion
    }
    qreal scrollingDefaultColumnWidthValue() const override
    {
        return 0.5;
    }
    int scrollingDefaultColumnWidthPresetIndex() const override
    {
        return 0;
    }
    int scrollingDefaultWindowHeightKind() const override
    {
        return 0; // auto
    }
    qreal scrollingDefaultWindowHeightValue() const override
    {
        return 0.0;
    }
    int scrollingDefaultWindowHeightPresetIndex() const override
    {
        return 0;
    }
    QStringList scrollingPresetColumnWidths() const override
    {
        return {QStringLiteral("0.25"), QStringLiteral("0.5"), QStringLiteral("0.75")};
    }
    QStringList scrollingPresetWindowHeights() const override
    {
        return {QStringLiteral("0.25"), QStringLiteral("0.5"), QStringLiteral("0.75")};
    }
    int scrollingDefaultColumnDisplay() const override
    {
        return 0;
    }
    int scrollingInsertPosition() const override
    {
        return insertPosition;
    }
    bool scrollingTabIndicatorEnabled() const override
    {
        return false;
    }
    bool scrollingTabIndicatorHideWhenSingleTab() const override
    {
        return true;
    }
    bool scrollingTabIndicatorPlaceWithinColumn() const override
    {
        return false;
    }
    int scrollingTabIndicatorGap() const override
    {
        return 0;
    }
    int scrollingTabIndicatorWidth() const override
    {
        return 2;
    }
    qreal scrollingTabIndicatorLengthProportion() const override
    {
        return 0.5;
    }
    int scrollingTabIndicatorPosition() const override
    {
        return 0;
    }
};

/// The latest windowsTiled batch entry for @p windowId, or an empty object.
QJsonObject lastEntryFor(QSignalSpy& tiled, const QString& windowId)
{
    for (int sig = tiled.count() - 1; sig >= 0; --sig) {
        const QJsonArray batch = QJsonDocument::fromJson(tiled.at(sig).at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : batch) {
            if (v.toObject().value(QLatin1String("windowId")).toString() == windowId) {
                return v.toObject();
            }
        }
    }
    return {};
}

int entryRight(const QJsonObject& o)
{
    return o.value(QLatin1String("x")).toInt() + o.value(QLatin1String("width")).toInt() - 1;
}

} // namespace

class TestScrollEngineBoundary : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // Default mode: BOTH neighbours of a centered column straddle, and both
    // are committed clamped at the SCREEN edge — deleting either clamp
    // branch, or substituting the work area for the screen rect, fails this.
    void clampModeClampsBothEdgesAtTheScreenRect()
    {
        QObject owner;
        auto* settings = new BoundaryStubSettings(&owner);
        // Panel inset: the work area is 100px narrower on each side than the
        // screen. The clamp bound is the SCREEN edge; a work-area clamp
        // would stop 100px short and this fixture is what tells them apart.
        const QRect screen = defaultScreenRect(); // 0,0 1200x800
        const QRect inset(100, 0, 1000, 800);
        ScrollEngine* engine = makeProviderEngine(
            &owner, {kS1},
            [screen](const QString&) {
                return screen;
            },
            [inset](const QString&) {
                return inset;
            });
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        // Center b: a straddles the left screen edge, c the right one
        // (columns are 500px on the 1000px work area).
        engine->windowFocused(QStringLiteral("app|b"), kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QJsonObject a = lastEntryFor(tiled, QStringLiteral("app|a"));
        const QJsonObject c = lastEntryFor(tiled, QStringLiteral("app|c"));
        QVERIFY(!a.isEmpty());
        QVERIFY(!c.isEmpty());
        // Clamped exactly at the screen edges — not the work area's.
        QCOMPARE(a.value(QLatin1String("x")).toInt(), screen.left());
        QCOMPARE(entryRight(c), screen.right());
        // And genuinely clamped, i.e. narrower than a full column.
        QVERIFY(a.value(QLatin1String("width")).toInt() < 500);
        QVERIFY(c.value(QLatin1String("width")).toInt() < 500);
    }

    // Crop mode: the same fixture commits the TRUE rects — the straddlers
    // keep their full width and their overhang crosses the screen edge.
    void cropModeCommitsTrueRectsForStraddlers()
    {
        QObject owner;
        auto* settings = new BoundaryStubSettings(&owner);
        settings->cropStraddlers = true;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        engine->windowFocused(QStringLiteral("app|b"), kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject a = lastEntryFor(tiled, QStringLiteral("app|a"));
        const QJsonObject c = lastEntryFor(tiled, QStringLiteral("app|c"));
        QVERIFY(!a.isEmpty());
        QVERIFY(!c.isEmpty());
        // True 600px columns, overhanging both edges.
        QCOMPARE(a.value(QLatin1String("width")).toInt(), 600);
        QCOMPARE(c.value(QLatin1String("width")).toInt(), 600);
        QVERIFY(a.value(QLatin1String("x")).toInt() < screen.left());
        QVERIFY(entryRight(c) > screen.right());
    }

    // A remainder below the peek floor parks (with its departure edge)
    // instead of committing a sliver.
    void peekFloorParksAThinRemainder()
    {
        QObject owner;
        auto* settings = new BoundaryStubSettings(&owner);
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        // Every new column opens at 0.98 of the work area (the per-screen
        // rule channel): centering a leaves b a 12px sliver at the right
        // edge, under the 48px floor.
        QVariantMap wide;
        wide.insert(PhosphorScrollEngine::ScrollPerScreenKeys::defaultColumnWidth(), 0.98);
        engine->applyPerScreenConfig(kS1, wide);

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowFocused(QStringLiteral("app|a"), kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject b = lastEntryFor(tiled, QStringLiteral("app|b"));
        QVERIFY(!b.isEmpty());
        // Parked below the screen, not committed as a sliver on it.
        QVERIFY2(
            b.value(QLatin1String("y")).toInt() > screen.bottom(),
            qPrintable(
                QStringLiteral("expected a park below the screen, got y=%1").arg(b.value(QLatin1String("y")).toInt())));
        QCOMPARE(b.value(QLatin1String("scrollEdge")).toString(), QStringLiteral("right"));
    }

    // The park lands below the union of ALL outputs — the headline rule of
    // the boundary redesign. With a second output stacked underneath, a
    // park below only the strip's own screen would land INSIDE it.
    void parkLandsBelowTheUnionOfAllOutputs()
    {
        QObject owner;
        auto* settings = new BoundaryStubSettings(&owner);
        settings->centerFocused = 0; // plain edge-aligned view
        const QRect top = defaultScreenRect(); // 0,0 1200x800
        const QRect bottom(0, 800, 1200, 800); // stacked beneath, bottom = 1599
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1}, [top](const QString&) {
            return top;
        });
        engine->setAllScreenGeometriesProvider([top, bottom]() {
            return QList<QRect>{top, bottom};
        });
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        // Focus the last column: the first scrolls fully off and parks.
        engine->focusColumnLast(kS1);
        QCoreApplication::processEvents();

        const QJsonObject a = lastEntryFor(tiled, QStringLiteral("app|a"));
        QVERIFY(!a.isEmpty());
        const int y = a.value(QLatin1String("y")).toInt();
        QVERIFY2(y > bottom.bottom(),
                 qPrintable(
                     QStringLiteral("park y=%1 must clear the lower output (bottom=%2)").arg(y).arg(bottom.bottom())));
    }

    // Vertical enforcement runs in BOTH modes: crop mode opts out of the
    // horizontal straddler clamp only, and a stacked column whose minimum
    // heights overflow the work area still has its below-floor tail parked
    // (not committed onto whatever sits beneath the screen). The park is
    // edge-less — vertical overflow is stack layout, not strip motion.
    void verticalOverflowParksEvenInCropMode()
    {
        QObject owner;
        auto* settings = new BoundaryStubSettings(&owner);
        settings->cropStraddlers = true;
        settings->respectMinimumSize = true;
        settings->centerFocused = 0;
        settings->insertPosition = static_cast<int>(PhosphorScrollEngine::ScrollInsertPosition::IntoActiveColumn);
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        // Two tiles stacked into one column, each with a 600px minimum on an
        // 800px screen: the min-height clamp lays the second tile out below
        // the work area (relayout documents the overflow as standing).
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 600);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 600);
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject b = lastEntryFor(tiled, QStringLiteral("app|b"));
        QVERIFY(!b.isEmpty());
        QVERIFY2(b.value(QLatin1String("y")).toInt() > screen.bottom(),
                 qPrintable(QStringLiteral("expected a vertical park below the screen even in crop mode, got y=%1")
                                .arg(b.value(QLatin1String("y")).toInt())));
        QVERIFY2(!b.contains(QLatin1String("scrollEdge")),
                 "a vertical park carries no scrollEdge (there is no side to animate from)");
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineBoundary)
#include "test_scrollengine_boundary.moc"
