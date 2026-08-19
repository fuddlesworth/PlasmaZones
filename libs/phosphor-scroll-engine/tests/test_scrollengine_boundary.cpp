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
#include "scrollstubsettings.h"

using PhosphorScrollEngine::ScrollEngine;
namespace Ax = ScrollTestUtils::Ax;

using ScrollTestUtils::defaultScreenRect;
using ScrollTestUtils::makeProviderEngine;

namespace {

const QString kS1 = QStringLiteral("S1");
/// The comparison screen for the per-screen crop case: same geometry, no
/// override map, so the only difference between the two is the rule slot.
const QString kS2 = QStringLiteral("S2");

/// The SHARED stub, re-seeded for this suite instead of a local subclass: a
/// second IScrollSettings copy here drifted from the shared one on the very
/// values the two suites' cases compare against (column width 0.5 vs 0.25,
/// the tab-indicator enable inverted), which is exactly the hazard the
/// shared header's comment warns about. Everything a case here mutates
/// (cropStraddlers, centerFocused, respectMinimumSize, insertPosition,
/// tabIndicatorEnabled) is a public field on the shared stub already.
ScrollTestUtils::StubScrollSettings* makeBoundarySettings(QObject* owner)
{
    auto* settings = new ScrollTestUtils::StubScrollSettings(owner);
    // Half-work-area columns: the straddler fixtures below reason in 500px
    // columns on the 1000px inset work area.
    settings->widthValue = 0.5;
    // Always-center is the straddler generator — it is what makes BOTH
    // neighbours of the focused column cross a screen edge.
    settings->centerFocused = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Always);
    // Off by default so the geometry tests stay indicator-free; the orphan
    // tab-bar test flips it on, since a null tabIndicatorRect would gate the
    // strip emitter shut before the skip under test could ever matter. The
    // remaining indicator values keep this suite's historical seeds.
    settings->tabIndicatorEnabled = false;
    settings->tabIndicatorHideWhenSingleTab = true;
    settings->tabIndicatorGap = 0;
    settings->tabIndicatorWidth = 2;
    return settings;
}

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

} // namespace

class TestScrollEngineBoundary : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    /// A monitor that ROTATES while the strip is populated. The geometry
    /// provider hands back a portrait rect on the second pass, so Auto
    /// resolves the other axis. What is ASSERTED is exactly the re-resolve
    /// (the committed aspect inverts); the axis-flip sweep's state drop
    /// (anchor re-derivation, pinned-intent conversion) has no clean
    /// observable at this level and is NOT claimed here — the name used to
    /// say "AndDropsOldAxisState", which nothing below ever checked.
    ///
    /// Both geometries are hardcoded PHYSICAL rects and neither is transposed
    /// with the fixture, so the two arms run this identically: it always
    /// starts landscape and always rotates to portrait. That is on purpose —
    /// what is under test is the ENGINE re-resolving Auto from the new
    /// geometry, which has nothing to do with the harness's axis.
    void aRotationFlipsTheResolvedAxis()
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
        auto* settings = makeBoundarySettings(&owner);
        // Panel inset: the work area is 100px narrower on each side than the
        // screen. The clamp bound is the SCREEN edge; a work-area clamp
        // would stop 100px short and this fixture is what tells them apart.
        const QRect screen = defaultScreenRect(); // 1200 along the strip, 800 across it (transposed with the arm)
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
        // Center b: a straddles the LEAD screen edge, c the TRAIL one
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
        QCOMPARE(Ax::entryMainEnd(c), Ax::mainEnd(screen));
        // And genuinely clamped, i.e. narrower than a full column.
        QVERIFY(Ax::entryMainLen(a) < 500);
        QVERIFY(Ax::entryMainLen(c) < 500);
    }

    // Crop mode: the same fixture commits the TRUE rects — the straddlers
    // keep their full width and their overhang crosses the screen edge.
    void cropModeCommitsTrueRectsForStraddlers()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
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
        QVERIFY(Ax::entryMainEnd(c) > Ax::mainEnd(screen));
    }

    // A remainder below the peek floor parks (with its departure edge)
    // instead of committing a sliver.
    void peekFloorParksAThinRemainder()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        // Every new column opens at 0.98 of the work area (the per-screen
        // rule channel): centering a leaves b a 12px sliver at the TRAIL
        // strip edge, under the 48px floor.
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
        auto* settings = makeBoundarySettings(&owner);
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
        // S1 keeps the true 600px rect, overhanging the LEAD screen edge.
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
        auto* settings = makeBoundarySettings(&owner);
        settings->centerFocused = 0; // plain edge-aligned view
        const QRect top = defaultScreenRect();
        // Stacked directly BENEATH the primary, keyed off it rather than
        // hardcoded. Ax::t() would be wrong here: transposing puts the second
        // output to the SIDE instead, the union's lower edge collapses back
        // onto the primary's, and the test stops discriminating a
        // below-the-union park from a below-this-screen one. The claim under
        // test is physical ("below every output"), so the fixture is too.
        const QRect bottom(top.left(), top.bottom() + 1, top.width(), top.height());
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

    // Cross-axis enforcement runs in BOTH modes: crop mode opts out of the
    // MAIN-axis straddler clamp only, and a stacked column whose minimum
    // cross extents overflow the work area still has its below-floor tail
    // parked (not committed onto whatever sits beneath the screen). The park
    // is edge-less — a stack overflow is stack layout, not strip motion.
    void crossOverflowParksEvenInCropMode()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
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
                 qPrintable(QStringLiteral("expected a cross-axis park below the screen even in crop mode, got y=%1")
                                .arg(b.value(QLatin1String("y")).toInt())));
        QVERIFY2(!b.contains(QLatin1String("scrollEdge")),
                 "a cross-axis park carries no scrollEdge (there is no side to animate from)");
    }

    // A tabbed column's hidden tiles are parked so they cannot take input from
    // the visible tab, NOT because the strip scrolled them away. While the
    // column is ON screen they have made no departure, so they report no
    // scrollEdge — and the next tab switch must not slide the newly active tab
    // in from a side the column never left.
    void hiddenTabOfAnOnScreenColumnCarriesNoScrollEdge()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
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
        // TRAIL strip edge, a move the column never made.
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
        auto* settings = makeBoundarySettings(&owner);
        settings->tabIndicatorEnabled = true;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        // Same generator as peekFloorParksAThinRemainder: every column opens
        // at 0.98 of the work area, so centering the first leaves the second a
        // 12px sliver at the TRAIL strip edge, under the 48px floor.
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
        // BOTH of the column's tiles, so the message's "every tile" claim is
        // what the assertions actually check — reading only the shown tab
        // left a hidden tab that failed to park invisible to the case.
        for (const char* id : {"app|b", "app|c"}) {
            const QJsonObject entry = lastEntryFor(tiled, QString::fromLatin1(id));
            QVERIFY2(
                !entry.isEmpty(),
                qPrintable(
                    QStringLiteral("expected %1 of the tabbed column in the tile batch").arg(QString::fromLatin1(id))));
            QVERIFY2(entry.value(QLatin1String("y")).toInt() > screen.bottom(),
                     qPrintable(QStringLiteral("the fixture must park the tabbed column's every tile, including %1")
                                    .arg(QString::fromLatin1(id))));
        }

        QVERIFY2(!namesB(QJsonDocument::fromJson(strips.last().at(1).toString().toUtf8()).array()),
                 "a fully parked tabbed column must not leave an orphan tab bar at the edge");
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineBoundary)
#include "test_scrollengine_boundary.moc"
