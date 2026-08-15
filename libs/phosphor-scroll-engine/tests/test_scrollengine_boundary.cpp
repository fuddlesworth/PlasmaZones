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
namespace Ax = ScrollTestUtils::Ax;

namespace Ax = ScrollTestUtils::Ax;

using ScrollTestUtils::defaultScreenRect;
using ScrollTestUtils::makeProviderEngine;

namespace {

const QString kS1 = QStringLiteral("S1");
/// The comparison screen for the per-screen crop case: same geometry, no
/// override map, so the only difference between the two is the rule slot.
const QString kS2 = QStringLiteral("S2");

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
    // Off by default so the geometry tests stay indicator-free; the orphan
    // tab-bar test flips it on, since a null tabIndicatorRect would gate the
    // strip emitter shut before the skip under test could ever matter.
    bool tabIndicatorEnabled = false;

    bool scrollingTabIndicatorEnabled() const override
    {
        return tabIndicatorEnabled;
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

/// The entry's far edge ALONG THE STRIP, inclusive. Delegates to the harness
/// so it reads whichever physical key is the main one on the running axis.
int entryRight(const QJsonObject& o)
{
    return ScrollTestUtils::Ax::entryMainEnd(o);
}

} // namespace

class TestScrollEngineBoundary : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, then skips while the
    /// engine is horizontal-only.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    /// A monitor that ROTATES while the strip is populated. The geometry
    /// provider hands back a portrait rect on the second pass, so Auto
    /// resolves the other axis and the engine must not carry any old-axis
    /// state across the flip.
    ///
    /// Runs on both arms: the fixture flips whichever axis it started on, so
    /// there is nothing horizontal-specific to gate.
    void aRotationFlipsTheAxisAndDropsOldAxisState()
    {
        QObject owner;
        // Mutable geometry: the provider answers landscape until the test
        // rotates it, which is what a real output change looks like to the
        // engine (it subscribes to no screen signal and re-reads at relayout).
        auto geometry = std::make_shared<QRect>(QRect(0, 0, 1200, 800));
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1}, [geometry](const QString&) {
            return *geometry;
        });

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        QVERIFY(tiled.count() > 0);

        const auto rectOfEntry = [](const QJsonObject& o) {
            return QRect(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt(),
                         o.value(QLatin1String("width")).toInt(), o.value(QLatin1String("height")).toInt());
        };
        const QRect landscapeA = rectOfEntry(lastEntryFor(tiled, QStringLiteral("app|a")));
        QVERIFY2(!landscapeA.isEmpty(), "precondition: a is committed before the rotation");
        QVERIFY2(landscapeA.height() >= landscapeA.width(),
                 "precondition: a full-height column on a landscape strip is taller than it is wide");

        // Rotate. Same screen, transposed geometry.
        *geometry = QRect(0, 0, 800, 1200);
        engine->retile(kS1);

        const QRect portraitA = rectOfEntry(lastEntryFor(tiled, QStringLiteral("app|a")));
        QVERIFY2(!portraitA.isEmpty(), "a must still be committed after the rotation");
        // A column on a VERTICAL strip spans the full width and takes a share
        // of the height, so the aspect relationship inverts.
        QVERIFY2(portraitA.width() >= portraitA.height(),
                 qPrintable(QStringLiteral("expected a full-width row after the flip, got %1x%2")
                                .arg(portraitA.width())
                                .arg(portraitA.height())));
    }

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
        const QRect inset = Ax::t(QRect(100, 0, 1000, 800));
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
        QCOMPARE(Ax::entryMainPos(a), Ax::mainPos(screen));
        QCOMPARE(entryRight(c), Ax::mainEnd(screen));
        // And genuinely clamped, i.e. narrower than a full column.
        QVERIFY(Ax::entryMainLen(a) < 500);
        QVERIFY(Ax::entryMainLen(c) < 500);
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
        QCOMPARE(Ax::entryMainLen(a), 600);
        QCOMPARE(Ax::entryMainLen(c), 600);
        QVERIFY(Ax::entryMainPos(a) < Ax::mainPos(screen));
        QVERIFY(entryRight(c) > Ax::mainEnd(screen));
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
        QCOMPARE(b.value(QLatin1String("scrollEdge")).toString(), Ax::edgeTrail());
    }

    // Crop mode is a per-SCREEN rule slot, and the emit loop must read the
    // override rather than the config-seeded member. Global crop OFF, S1
    // scoped ON: S1's straddlers keep their true rects while the same
    // fixture on S2 commits clamped ones. Without the second screen this
    // reads identically to a member-only implementation, which is exactly
    // the regression the per-screen keys keep having.
    void cropStraddlersOverrideIsPerScreen()
    {
        QObject owner;
        auto* settings = new BoundaryStubSettings(&owner);
        settings->cropStraddlers = false;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1, kS2});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QVariantMap crop;
        crop.insert(PhosphorScrollEngine::ScrollPerScreenKeys::cropStraddlers(), true);
        engine->applyPerScreenConfig(kS1, crop);

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        for (const QString& id : {QStringLiteral("app|a1"), QStringLiteral("app|a2"), QStringLiteral("app|a3")}) {
            engine->windowOpened(id, kS1, 0, 0);
        }
        for (const QString& id : {QStringLiteral("app|b1"), QStringLiteral("app|b2"), QStringLiteral("app|b3")}) {
            engine->windowOpened(id, kS2, 0, 0);
        }
        // Center the middle column on each screen, so its two neighbours
        // straddle (the always-center stub is the straddler generator).
        engine->windowFocused(QStringLiteral("app|a2"), kS1);
        engine->windowFocused(QStringLiteral("app|b2"), kS2);
        engine->retile(kS1);
        engine->retile(kS2);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject cropped = lastEntryFor(tiled, QStringLiteral("app|a1"));
        const QJsonObject clamped = lastEntryFor(tiled, QStringLiteral("app|b1"));
        QVERIFY(!cropped.isEmpty());
        QVERIFY(!clamped.isEmpty());
        // S1 keeps the true 600px rect, overhanging the left screen edge.
        QCOMPARE(Ax::entryMainLen(cropped), 600);
        QVERIFY(Ax::entryMainPos(cropped) < Ax::mainPos(screen));
        // S2 is clamped at the edge and narrower for it.
        QCOMPARE(Ax::entryMainPos(clamped), Ax::mainPos(screen));
        QVERIFY(Ax::entryMainLen(clamped) < 600);
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
        // Two tiles stacked into one column, each with a 600px minimum
        // ACROSS the strip on an 800px cross extent: the min-size clamp lays
        // the second tile out past the work area (relayout documents the
        // overflow as standing). The minimum is transposed with the fixture,
        // because what overflows a stack is the extent the stack divides.
        const QSize minSize = Ax::t(QSize(0, 600));
        engine->windowOpened(QStringLiteral("app|a"), kS1, minSize.width(), minSize.height());
        engine->windowOpened(QStringLiteral("app|b"), kS1, minSize.width(), minSize.height());
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

    // A tabbed column's hidden tiles are parked so they cannot take input from
    // the visible tab, NOT because the strip scrolled them away. While the
    // column is ON screen they have made no departure, so they report no
    // scrollEdge — and the next tab switch must not slide the newly active tab
    // in from a side the column never left.
    void hiddenTabOfAnOnScreenColumnCarriesNoScrollEdge()
    {
        QObject owner;
        auto* settings = new BoundaryStubSettings(&owner);
        settings->centerFocused = 0;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->consumeOrExpelWindow(-1, kS1); // b joins a's column
        engine->windowFocused(QStringLiteral("app|a"), kS1);
        engine->toggleColumnTabbed(kS1);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject hidden = lastEntryFor(tiled, QStringLiteral("app|b"));
        QVERIFY2(!hidden.isEmpty(), "expected the hidden tab in the tile batch");
        QVERIFY2(hidden.value(QLatin1String("y")).toInt() > screen.bottom(),
                 "a hidden tab is parked off-canvas so it cannot take input");
        QVERIFY2(!hidden.contains(QLatin1String("scrollEdge")),
                 "a hidden tab of an on-screen column must not report a departure edge");

        // The discriminating leg: with a fallback edge recorded above, this
        // switch consumed it and slid the newly shown tab in from off the
        // right, a move the column never made.
        tiled.clear();
        engine->windowFocused(QStringLiteral("app|b"), kS1);
        QCoreApplication::processEvents();
        const QJsonObject shown = lastEntryFor(tiled, QStringLiteral("app|b"));
        QVERIFY2(!shown.isEmpty(), "expected the newly active tab in the tile batch");
        QVERIFY2(shown.value(QLatin1String("y")).toInt() <= screen.bottom(),
                 "the newly active tab must be placed on screen, not parked");
        QVERIFY2(!shown.contains(QLatin1String("scrollEdge")),
                 "a tab switch on an on-screen column must not slide the new tab in from an edge");
    }

    // A tabbed column whose peek falls below the floor parks every one of its
    // tiles, so nothing it labels is on screen — yet its TRUE rect still
    // intersects the work area, which is exactly what left an orphan tab bar
    // drawn at the edge. The strip emitter skips a column once every tile it
    // emitted was parked.
    void fullyParkedTabbedColumnEmitsNoTabStrip()
    {
        QObject owner;
        auto* settings = new BoundaryStubSettings(&owner);
        settings->tabIndicatorEnabled = true;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        // Same generator as peekFloorParksAThinRemainder: every column opens
        // at 0.98 of the work area, so centering the first leaves the second a
        // 12px sliver at the right edge, under the 48px floor.
        QVariantMap wide;
        wide.insert(PhosphorScrollEngine::ScrollPerScreenKeys::defaultColumnWidth(), 0.98);
        engine->applyPerScreenConfig(kS1, wide);

        QSignalSpy strips(engine, &ScrollEngine::tabStripsChanged);
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        engine->consumeOrExpelWindow(-1, kS1); // c joins b's column
        engine->windowFocused(QStringLiteral("app|b"), kS1);
        engine->toggleColumnTabbed(kS1);
        QCoreApplication::processEvents();

        // The tabbed column is on screen at this point, so it DOES own a bar.
        // Asserting that first is what makes the skip below discriminating
        // rather than vacuous.
        const auto namesB = [](const QJsonArray& payload) {
            for (const QJsonValue& v : payload) {
                if (v.toObject().value(QLatin1String("tabs")).toArray().contains(QJsonValue(QStringLiteral("app|b")))) {
                    return true;
                }
            }
            return false;
        };
        QVERIFY(!strips.isEmpty());
        QVERIFY2(namesB(QJsonDocument::fromJson(strips.last().at(1).toString().toUtf8()).array()),
                 "the fixture must first produce a tab bar for the on-screen tabbed column");

        // Centering the first column pushes the tabbed one to a below-floor
        // remainder, so every one of its tiles parks.
        engine->windowFocused(QStringLiteral("app|a"), kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject b = lastEntryFor(tiled, QStringLiteral("app|b"));
        QVERIFY2(!b.isEmpty(), "expected the tabbed column's active tile in the tile batch");
        QVERIFY2(b.value(QLatin1String("y")).toInt() > screen.bottom(),
                 "the fixture must actually park the tabbed column's every tile");

        QVERIFY2(!namesB(QJsonDocument::fromJson(strips.last().at(1).toString().toUtf8()).array()),
                 "a fully parked tabbed column must not leave an orphan tab bar at the edge");
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineBoundary)
#include "test_scrollengine_boundary.moc"
