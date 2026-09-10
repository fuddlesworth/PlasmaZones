// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The workspace overview's one WRITE into a context that is not on screen:
// ScrollEngine::panStoredView. The claims are about a stored strip nobody can
// see: that a pan lands in it, that the pan is what the context shows once it
// is current, that a key with no strip is refused without inventing one, and
// that the desktop axis's reap and renumber treat the stored pan like any
// other strip state (the reap drops it, the renumber carries it).
//
// Modelled on test_scrollengine_desktopreap.cpp (same stub settings, same
// screen fixture), kept apart because that suite asks what a dying context
// destroys and this one asks what an invisible context keeps.

#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"

#include <QHash>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::engineParams;
using ScrollTestUtils::makeProviderEngine;
using ScrollTestUtils::StubScrollSettings;

namespace {
const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kS3 = QStringLiteral("S3");

PhosphorEngine::PlacementStateKey keyFor(const QString& screenId, int desktop)
{
    return PhosphorEngine::PlacementStateKey{screenId, desktop, QString()};
}
} // namespace

class TestScrollEngineWorkspaces : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void panStoredViewLandsOnceTheContextIsCurrent();
    void panStoredViewEmitsNoGeometry();
    void panStoredViewRefusesANeverCreatedKey();
    void reapDesktopStateDropsThePannedContext();
    void renumberDesktopStateCarriesThePan();

private:
    static ScrollEngine* makeEngine(QObject* parent, StubScrollSettings* settings)
    {
        // The stub's default column width is a quarter of the work area, under
        // which three columns FIT the viewport and every pan clamps to zero.
        // Half-width columns are what the overflow the pans below rely on.
        settings->widthValue = 0.5;
        ScrollEngine* engine = makeProviderEngine(parent, {kS1, kS2, kS3});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();
        return engine;
    }

    static ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screenId));
    }

    /// Three half-width columns on @p desktop of S1, so the strip overflows
    /// the 1200px viewport by one column and there is room to pan. Leaves
    /// desktop 1 current. Returns the view offset the strip held while its
    /// desktop was current, which is the value a pan is measured from.
    static int populateDesktopThenLeave(ScrollEngine* engine, int desktop)
    {
        engine->setCurrentDesktopForScreen(kS1, desktop);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        ScrollState* state = stateFor(engine, kS1);
        const int viewBefore = state ? state->strip().relayout(engineParams()).viewOffset : -1;
        engine->setCurrentDesktopForScreen(kS1, 1);
        return viewBefore;
    }

    /// A pan the strip cannot refuse for being at an end: backwards when
    /// the view sits past the start, forwards otherwise. The strip has 600px
    /// of travel (1800px of columns over a 1200px viewport), so 100px in the
    /// open direction always moves.
    static int openDelta(int viewBefore)
    {
        return viewBefore >= 100 ? -100 : 100;
    }
};

void TestScrollEngineWorkspaces::panStoredViewLandsOnceTheContextIsCurrent()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);
    const int viewBefore = populateDesktopThenLeave(engine, 2);
    QVERIFY(viewBefore >= 0);
    // The current context is desktop 1 and empty; desktop 2 is the stored
    // strip the overview pans.
    QVERIFY(engine->stripSnapshot(kS1).columns.isEmpty());

    const int delta = openDelta(viewBefore);
    QVERIFY(engine->panStoredView(keyFor(kS1, 2), delta));
    // Visible to the overview's read straight away, without the context
    // ever having been shown.
    QCOMPARE(engine->stripSnapshot(keyFor(kS1, 2)).viewX, viewBefore + delta);

    // And what the context shows once it IS current: the pan, not the
    // centering policy's own idea of where the view should be.
    engine->setCurrentDesktopForScreen(kS1, 2);
    ScrollState* state = stateFor(engine, kS1);
    QVERIFY(state);
    QCOMPARE(state->strip().relayout(engineParams()).viewOffset, viewBefore + delta);
    QCOMPARE(engine->stripSnapshot(kS1).columns.size(), 3);
}

void TestScrollEngineWorkspaces::panStoredViewEmitsNoGeometry()
{
    // The context is invisible, so the pan applies no geometry and raises
    // none of the engine's per-screen change signals: those fan out to
    // consumers describing the CURRENT context (the popup refresh, the
    // tilingChanged broadcast), which the pan did not touch.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);
    const int viewBefore = populateDesktopThenLeave(engine, 2);
    QVERIFY(viewBefore >= 0);

    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    QSignalSpy placementSpy(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
    QVERIFY(engine->panStoredView(keyFor(kS1, 2), openDelta(viewBefore)));
    QCOMPARE(tiledSpy.count(), 0);
    QCOMPARE(placementSpy.count(), 0);
    // A refused pan (already pinned at the end it is pushed toward) is a
    // false with no side effect either: pin the view at the strip's end
    // (the result of that one does not matter), then push once more.
    engine->panStoredView(keyFor(kS1, 2), ScrollTestUtils::kMainExtent * 2);
    const int pinned = engine->stripSnapshot(keyFor(kS1, 2)).viewX;
    QVERIFY(!engine->panStoredView(keyFor(kS1, 2), ScrollTestUtils::kMainExtent * 2));
    QCOMPARE(engine->stripSnapshot(keyFor(kS1, 2)).viewX, pinned);
    QCOMPARE(tiledSpy.count(), 0);
    QCOMPARE(placementSpy.count(), 0);
}

void TestScrollEngineWorkspaces::panStoredViewRefusesANeverCreatedKey()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);

    // A desktop S1 never visited, and a screen the engine does not own:
    // both refused, and neither read afterwards finds a state the refusal
    // created on the way out.
    QVERIFY(!engine->panStoredView(keyFor(kS1, 5), 100));
    QVERIFY(!engine->panStoredView(keyFor(QStringLiteral("S9"), 1), 100));
    QVERIFY(!engine->overviewWindowsFor(keyFor(kS1, 5)).has_value());
    QVERIFY(!engine->overviewWindowsFor(keyFor(QStringLiteral("S9"), 1)).has_value());
    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{1}));

    // The current context still works as a stored key too: the verb keys
    // on state, not on currency (the daemon's routing is what keeps the
    // current context on scrollViewByPercent).
    ScrollState* state = stateFor(engine, kS1);
    QVERIFY(state);
    const int viewBefore = state->strip().relayout(engineParams()).viewOffset;
    const int delta = openDelta(viewBefore);
    QVERIFY(engine->panStoredView(keyFor(kS1, 1), delta));
    QCOMPARE(state->strip().relayout(engineParams()).viewOffset, viewBefore + delta);
    QCOMPARE(engine->stripSnapshot(kS1).columns.size(), 3);
}

void TestScrollEngineWorkspaces::reapDesktopStateDropsThePannedContext()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);
    const int viewBefore = populateDesktopThenLeave(engine, 2);
    QVERIFY(viewBefore >= 0);
    QVERIFY(engine->panStoredView(keyFor(kS1, 2), openDelta(viewBefore)));
    QVERIFY(engine->overviewWindowsFor(keyFor(kS1, 2)).has_value());
    QVERIFY(engine->overviewStripFor(keyFor(kS1, 2)).has_value());

    engine->reapDesktopState(2);
    QVERIFY(!engine->overviewWindowsFor(keyFor(kS1, 2)).has_value());
    QVERIFY(!engine->overviewStripFor(keyFor(kS1, 2)).has_value());
    QVERIFY(!engine->stripSnapshot(keyFor(kS1, 2)).valid);
    QVERIFY(!engine->panStoredView(keyFor(kS1, 2), 100));
    QVERIFY(!engine->desktopsWithActiveState().contains(2));
}

void TestScrollEngineWorkspaces::renumberDesktopStateCarriesThePan()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);
    const int viewBefore = populateDesktopThenLeave(engine, 3);
    QVERIFY(viewBefore >= 0);
    const int delta = openDelta(viewBefore);
    QVERIFY(engine->panStoredView(keyFor(kS1, 3), delta));

    // Desktop 2 died elsewhere: 3 becomes 2. The stored strip moves with
    // its key, pan included, and nothing answers under the old number.
    QHash<int, int> mapping;
    mapping.insert(3, 2);
    engine->renumberDesktopState(mapping);
    QVERIFY(!engine->overviewStripFor(keyFor(kS1, 3)).has_value());
    const auto moved = engine->overviewStripFor(keyFor(kS1, 2));
    QVERIFY(moved.has_value());
    QCOMPARE(moved->columns.size(), 3);
    QCOMPARE(moved->viewOffset, viewBefore + delta);

    // And it is what the renumbered desktop shows once current.
    engine->setCurrentDesktopForScreen(kS1, 2);
    ScrollState* state = stateFor(engine, kS1);
    QVERIFY(state);
    QCOMPARE(state->strip().relayout(engineParams()).viewOffset, viewBefore + delta);
}

QTEST_GUILESS_MAIN(TestScrollEngineWorkspaces)
#include "test_scroll_engine_workspaces.moc"
