// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// QTEST_MAIN (not GUILESS), like test_snap_unfloat_fallback: under the
// offscreen QPA QGuiApplication::primaryScreen() is a real screen, so
// WindowTrackingService::zoneGeometry() resolves and the zone-frame assertions
// below are reachable. Under a guiless main every rect would be null and a
// broken geometry arm would pass green.

#include "helpers/SnapEngineTestFixture.h"

#include <PhosphorEngine/IOverviewModelSource.h>

#include <memory>

/**
 * @brief SnapEngine::overviewWindowsFor: the per-key read surface the
 *        workspace overview consumes. A key with no store answers nullopt
 *        (and the global holder never leaks into a per-key read), a snapped
 *        window is listed under its own desktop's key with its zone frame,
 *        a float is listed with its float bit, and the read leaves the store
 *        exactly as it found it.
 */
class TestSnapEngineOverview : public SnapEngineTestFixture
{
    Q_OBJECT

private:
    const QString m_screen = QStringLiteral("DP-1");
    std::unique_ptr<SnapEngine> m_engine;

    /// A fresh engine wired the way the daemon wires it: the FULL per-key
    /// resolver on the service, so commitSnap lands in the per-(screen,
    /// desktop) store rather than the global holder the fixture's default
    /// single-store resolver would route to.
    SnapEngine* makeEngine()
    {
        m_engine = std::make_unique<SnapEngine>(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        SnapEngine* e = m_engine.get();
        PhosphorPlacement::WindowTrackingService::SnapStateResolver resolver;
        resolver.forWindow = [e](const QString& id) {
            return e->stateForWindow(id);
        };
        resolver.forWindowOnScreen = [e](const QString& id, const QString& s) {
            return e->stateForWindowOnScreen(id, s);
        };
        resolver.forScreen = [e](const QString& s) {
            return static_cast<SnapState*>(e->stateForScreen(s));
        };
        resolver.globals = [e]() {
            return e->globalState();
        };
        resolver.allStates = [e]() {
            return e->allSnapStates();
        };
        resolver.forgetWindow = [e](const QString& id) {
            e->forgetWindow(id);
        };
        m_wts->setSnapStateResolver(resolver);
        return e;
    }

    /// Detach the resolver BEFORE the engine it points at goes away. The
    /// fixture's cleanup() detaches again afterwards, which is a no-op.
    void dropEngine()
    {
        m_wts->setSnapState(nullptr);
        m_engine.reset();
    }

    PhosphorZones::Layout* installLayout(int zoneCount)
    {
        auto* layout = createTestLayout(zoneCount, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);
        return layout;
    }

    static PhosphorEngine::PlacementStateKey keyFor(const QString& screen, int desktop)
    {
        PhosphorEngine::PlacementStateKey key;
        key.screenId = screen;
        key.desktop = desktop;
        return key;
    }

    static const PhosphorEngine::OverviewWindowEntry* find(const QList<PhosphorEngine::OverviewWindowEntry>& entries,
                                                           const QString& windowId)
    {
        for (const auto& entry : entries) {
            if (entry.windowId == windowId) {
                return &entry;
            }
        }
        return nullptr;
    }

private Q_SLOTS:
    /// Shadows the fixture's cleanup() (QTest invokes the most-derived slot by
    /// name) so a slot that failed before its own dropEngine() still tears the
    /// engine down BEFORE the fixture deletes the service it borrows.
    void cleanup()
    {
        dropEngine();
        SnapEngineTestFixture::cleanup();
    }

    // (a) No store under the key answers nullopt, and residence the GLOBAL
    // holder carries for that very (screen, desktop) never surfaces through a
    // per-key read.
    void neverCreatedKey_answersNullopt_andHolderNeverLeaks()
    {
        SnapEngine* engine = makeEngine();
        SnapState* globals = engine->snapState();
        QVERIFY(globals);
        globals->recordResidence(QStringLiteral("holderWin"), m_screen, 2);

        const int storesBefore = engine->allSnapStates().size();
        QVERIFY(!engine->overviewWindowsFor(keyFor(m_screen, 2)).has_value());
        QVERIFY(!engine->overviewWindowsFor(keyFor(m_screen, 1)).has_value());
        // The holder's own sentinel key is refused too.
        QVERIFY(!engine->overviewWindowsFor(keyFor(QString(), 1)).has_value());
        // The read created nothing.
        QCOMPARE(engine->allSnapStates().size(), storesBefore);
        dropEngine();
    }

    // (b) A window snapped on desktop 2 is listed under {screen, 2} with its
    // zone's geometry and floating=false, and is absent from {screen, 1}.
    void snappedWindow_listedUnderItsDesktopWithZoneFrame()
    {
        auto* layout = installLayout(2);
        const QString zoneId = layout->zones().first()->id().toString();
        const QRect expected = m_wts->zoneGeometry(zoneId, m_screen);
        QVERIFY2(expected.isValid(), "offscreen primary screen must yield valid zone geometry");

        SnapEngine* engine = makeEngine();
        const QString windowId = QStringLiteral("app|w2");
        engine->setCurrentDesktopForScreen(m_screen, 2);
        // The desktop is pinned explicitly: the fixture has no virtual desktop
        // manager, so an unpinned commit would tag the window with 0 (the
        // all-desktops sentinel) even though the store it lands in is desktop 2's.
        engine->commitSnap(windowId, zoneId, m_screen, PhosphorEngine::SnapIntent::UserInitiated, 2);
        QVERIFY(engine->isWindowTracked(windowId));
        QVERIFY(engine->stateForWindow(windowId) != engine->snapState());

        const auto onTwo = engine->overviewWindowsFor(keyFor(m_screen, 2));
        QVERIFY(onTwo.has_value());
        QCOMPARE(onTwo->size(), 1);
        const auto* entry = find(*onTwo, windowId);
        QVERIFY(entry);
        QCOMPARE(entry->rect, expected);
        QVERIFY(!entry->floating);
        QVERIFY(!entry->minimized);
        QCOMPARE(entry->column, -1);
        QCOMPARE(entry->tile, -1);

        // Desktop 1 never got a store, so it answers nullopt; a store that
        // exists but holds nothing for its desktop answers an engaged empty
        // list. Either way the desktop-2 window is not there.
        const auto onOne = engine->overviewWindowsFor(keyFor(m_screen, 1));
        QVERIFY(!onOne.has_value() || find(*onOne, windowId) == nullptr);

        // A window tagged 0 (on all desktops) in the same store shows on this
        // desktop too, and is listed exactly once.
        const QString sticky = QStringLiteral("app|sticky");
        engine->commitSnap(sticky, zoneId, m_screen);
        QCOMPARE(engine->stateForWindow(sticky)->desktopAssignments().value(sticky, -1), 0);
        const auto withSticky = engine->overviewWindowsFor(keyFor(m_screen, 2));
        QVERIFY(withSticky.has_value());
        QCOMPARE(withSticky->size(), 2);
        QVERIFY(find(*withSticky, sticky));
        dropEngine();
    }

    // A window spanning two zones reports the union of their frames.
    void spannedWindow_reportsUnionOfZoneFrames()
    {
        auto* layout = installLayout(2);
        const QString zoneA = layout->zones().at(0)->id().toString();
        const QString zoneB = layout->zones().at(1)->id().toString();
        const QRect expected = m_wts->zoneGeometry(zoneA, m_screen).united(m_wts->zoneGeometry(zoneB, m_screen));
        QVERIFY(expected.isValid());

        SnapEngine* engine = makeEngine();
        const QString windowId = QStringLiteral("app|span");
        engine->setCurrentDesktopForScreen(m_screen, 2);
        engine->commitMultiZoneSnap(windowId, QStringList{zoneA, zoneB}, m_screen,
                                    PhosphorEngine::SnapIntent::UserInitiated, 2);

        const auto entries = engine->overviewWindowsFor(keyFor(m_screen, 2));
        QVERIFY(entries.has_value());
        const auto* entry = find(*entries, windowId);
        QVERIFY(entry);
        QCOMPARE(entry->rect, expected);
        QVERIFY(!entry->floating);
        dropEngine();
    }

    // (c) A floating window on the key is listed with floating=true and a
    // null rect. Both float shapes are covered: floated with a residence slot
    // and floated screen-agnostically. Each appears exactly once.
    void floatingWindow_listedAsFloating()
    {
        SnapEngine* engine = makeEngine();
        engine->setCurrentDesktopForScreen(m_screen, 2);

        const QString resident = QStringLiteral("app|floatResident");
        SnapState* state = engine->stateForWindowOnScreen(resident, m_screen);
        QVERIFY(state);
        QVERIFY(state != engine->snapState());
        state->setFloatingOnScreen(resident, m_screen, 2);

        const QString untagged = QStringLiteral("app|floatUntagged");
        QCOMPARE(engine->stateForWindowOnScreen(untagged, m_screen), state);
        engine->setFloating(untagged, true);
        QVERIFY(engine->isFloating(untagged));

        const auto entries = engine->overviewWindowsFor(keyFor(m_screen, 2));
        QVERIFY(entries.has_value());
        QCOMPARE(entries->size(), 2);
        for (const QString& id : {resident, untagged}) {
            const auto* entry = find(*entries, id);
            QVERIFY2(entry, qPrintable(id));
            QVERIFY(entry->floating);
            QVERIFY(!entry->minimized);
            QVERIFY(entry->rect.isNull());
        }
        dropEngine();
    }

    // (d) The read is invisible: zone assignment, residence, float set and the
    // store count are identical before and after, and nothing was emitted.
    void read_doesNotMutateState()
    {
        auto* layout = installLayout(2);
        const QString zoneId = layout->zones().first()->id().toString();

        SnapEngine* engine = makeEngine();
        engine->setCurrentDesktopForScreen(m_screen, 2);
        const QString snapped = QStringLiteral("app|snapped");
        const QString floated = QStringLiteral("app|floated");
        engine->commitSnap(snapped, zoneId, m_screen, PhosphorEngine::SnapIntent::UserInitiated, 2);
        SnapState* state = engine->stateForWindowOnScreen(floated, m_screen);
        QVERIFY(state);
        state->setFloatingOnScreen(floated, m_screen, 2);

        const QString zoneBefore = state->zoneForWindow(snapped);
        const auto screensBefore = state->screenAssignments();
        const auto desktopsBefore = state->desktopAssignments();
        const QStringList floatingBefore = state->floatingWindows();
        const int storesBefore = engine->allSnapStates().size();
        QVERIFY(!zoneBefore.isEmpty());

        QSignalSpy stateSpy(state, &SnapState::stateChanged);
        QSignalSpy floatSpy(engine, &SnapEngine::windowFloatingChanged);

        const auto first = engine->overviewWindowsFor(keyFor(m_screen, 2));
        const auto second = engine->overviewWindowsFor(keyFor(m_screen, 2));
        QVERIFY(first.has_value());
        QVERIFY(second.has_value());
        QCOMPARE(first->size(), 2);
        QCOMPARE(second->size(), 2);

        QCOMPARE(state->zoneForWindow(snapped), zoneBefore);
        QCOMPARE(state->screenAssignments(), screensBefore);
        QCOMPARE(state->desktopAssignments(), desktopsBefore);
        QCOMPARE(state->floatingWindows(), floatingBefore);
        QCOMPARE(engine->allSnapStates().size(), storesBefore);
        QCOMPARE(stateSpy.count(), 0);
        QCOMPARE(floatSpy.count(), 0);
        dropEngine();
    }
};

QTEST_MAIN(TestSnapEngineOverview)
#include "test_snap_engine_overview.moc"
