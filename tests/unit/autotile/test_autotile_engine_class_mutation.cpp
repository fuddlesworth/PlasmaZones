// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_autotile_engine_class_mutation.cpp
 * @brief Regression tests for discussion #271: Electron/CEF apps (Emby) that
 *        mutate their resourceClass / desktopFileName after the surface is
 *        already mapped, so successive D-Bus calls arrive with different
 *        "appId|uuid" composites for the same underlying window.
 *
 * Before the fix, AutotileEngine stored m_windowToStateKey / PhosphorTiles::TilingState under
 * the first-seen composite; the second composite hit the "window not found in
 * any autotile state" path and the user's toggleWindowFloat / navigation
 * shortcuts silently failed until a mode toggle rebuilt state from scratch.
 *
 * These tests exercise the AutotileEngine contract directly — no KWin effect,
 * no D-Bus, just the function calls a real bridge would make when KWin fires
 * windowClassChanged mid-session.
 */

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "autotile/AutotileEngine.h"
#include "../helpers/AutotileTestHelpers.h"
#include <PhosphorTiles/TilingState.h>
#include "core/windowregistry.h"

using namespace PlasmaZones;

namespace {

// Build a legacy composite id. Kept in-test so we're not coupled to the
// Utils helpers. The bare-instance-id flow (the production format) is
// covered separately in bareInstanceId_wireFormat_worksEndToEnd below.
QString makeComposite(const QString& appId, const QString& instanceId)
{
    return appId + QLatin1Char('|') + instanceId;
}

} // namespace

class TestAutotileEngineClassMutation : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ────────────────────────────────────────────────────────────────────
    // The bug case: same window, two different composites in sequence.
    // ────────────────────────────────────────────────────────────────────

    void embyScenario_toggleFloatSurvivesClassMutation()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        WindowRegistry registry;
        engine.setWindowRegistry(&registry);

        const QString screen = QStringLiteral("HKC OVERSEAS LIMITED:34E6UC");
        const QString instanceId = QStringLiteral("cef1ba31-3316-4f05-84f5-ef627674b504");

        // Two observed classes — the exact strings from the discussion-271 log.
        const QString classA = QStringLiteral("emby-beta");
        const QString classB = QStringLiteral("media.emby.client.beta");

        const QString firstSeen = makeComposite(classA, instanceId);
        const QString afterRename = makeComposite(classB, instanceId);

        engine.setAutotileScreens({screen});
        QVERIFY(engine.isEnabled());

        // 1. Initial window open — under classA.
        engine.windowOpened(firstSeen, screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.stateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(firstSeen));

        // 2. KWin rebroadcasts the window with a NEW class. The engine must
        //    resolve this back to the canonical form rather than creating a
        //    phantom second window.
        registry.upsert(instanceId, {classB, QString(), QString()});

        // 3. The failing call from the discussion — toggleWindowFloat arrives
        //    with the rebranded composite. It must hit the already-tracked
        //    window, not the "window not found in any autotile state" path.
        QSignalSpy feedback(&engine, &AutotileEngine::navigationFeedbackRequested);
        QSignalSpy floatSpy(&engine, &AutotileEngine::windowFloatingChanged);

        engine.toggleWindowFloat(afterRename, screen);
        QCoreApplication::processEvents();

        // Zero failure feedback means the lookup succeeded.
        QCOMPARE(feedback.count(), 0);
        // The float state transition was actually emitted.
        QCOMPARE(floatSpy.count(), 1);

        // And PhosphorTiles::TilingState now marks the canonical (first-seen) entry as floating —
        // not a duplicate under the new composite.
        QVERIFY(state->isFloating(firstSeen));
        QVERIFY(!state->containsWindow(afterRename));
        QCOMPARE(state->windowCount(), 1);
    }

    // ────────────────────────────────────────────────────────────────────
    // Follow-up: a SECOND class mutation still routes to the same entry.
    // ────────────────────────────────────────────────────────────────────

    void multipleClassMutations_stayConvergent()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        WindowRegistry registry;
        engine.setWindowRegistry(&registry);

        const QString screen = QStringLiteral("DP-1");
        const QString instanceId = QStringLiteral("aaaa-bbbb");
        const QString classes[] = {
            QStringLiteral("loader"),
            QStringLiteral("app-beta"),
            QStringLiteral("app"),
        };
        engine.setAutotileScreens({screen});

        // First class opens the window and locks the canonical key.
        engine.windowOpened(makeComposite(classes[0], instanceId), screen);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* state = engine.stateForScreen(screen);
        QVERIFY(state);

        // Every subsequent rebroadcast must resolve to the same canonical entry.
        for (const QString& newClass : classes) {
            const QString composite = makeComposite(newClass, instanceId);
            registry.upsert(instanceId, {newClass, QString(), QString()});

            // windowFocused is the hottest post-rename path (the effect sends
            // it on every raise) — it must NOT insert a phantom entry.
            engine.windowFocused(composite, screen);
            QCoreApplication::processEvents();
        }

        // Still exactly one window tracked — no phantoms from renames.
        QCOMPARE(state->windowCount(), 1);
    }

    // ────────────────────────────────────────────────────────────────────
    // windowClosed must release the canonical entry so a brand-new window
    // with the same instance id (e.g. KWin re-using a UUID slot after a
    // process restart) is NOT resolved to stale state.
    // ────────────────────────────────────────────────────────────────────

    void windowClosed_releasesCanonicalEntry()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        const QString instanceId = QStringLiteral("deadbeef");

        engine.setAutotileScreens({screen});

        const QString firstOpen = makeComposite(QStringLiteral("firefox"), instanceId);
        engine.windowOpened(firstOpen, screen);
        engine.windowClosed(firstOpen);
        QCoreApplication::processEvents();

        // A new window arriving later with the same instance id but a
        // different class must be registered fresh, not aliased to stale
        // state from the first open.
        const QString reuse = makeComposite(QStringLiteral("kate"), instanceId);
        engine.windowOpened(reuse, screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.stateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(reuse));
        QVERIFY(!state->containsWindow(firstOpen));
        QCOMPARE(state->windowCount(), 1);
    }

    // ────────────────────────────────────────────────────────────────────
    // The production wire format: the kwin-effect bridge sends bare
    // instance ids, not composites. Metadata comes from the registry. The
    // engine must still find the right PhosphorTiles::TilingState entry when
    // toggleWindowFloat arrives.
    // ────────────────────────────────────────────────────────────────────

    void bareInstanceId_wireFormat_worksEndToEnd()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        WindowRegistry registry;
        engine.setWindowRegistry(&registry);

        const QString screen = QStringLiteral("HKC OVERSEAS LIMITED:34E6UC");
        const QString instanceId = QStringLiteral("cef1ba31-3316-4f05-84f5-ef627674b504");

        engine.setAutotileScreens({screen});

        // Bridge registers metadata before notifying the engine (mirrors
        // kwin-effect's slotWindowAdded order: pushWindowMetadata() runs
        // before the autotile windowOpened D-Bus call).
        registry.upsert(instanceId, {QStringLiteral("emby-beta"), QString(), QString()});

        // Engine receives a BARE instance id — no '|' separator. Any code
        // that assumes composite format should fail visibly here.
        engine.windowOpened(instanceId, screen);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.stateForScreen(screen);
        QVERIFY(state);
        QVERIFY(state->containsWindow(instanceId));

        // Class mutation: registry updates, but the engine's state key is the
        // instance id and doesn't care.
        registry.upsert(instanceId, {QStringLiteral("media.emby.client.beta"), QString(), QString()});

        QSignalSpy feedback(&engine, &AutotileEngine::navigationFeedbackRequested);
        QSignalSpy floatSpy(&engine, &AutotileEngine::windowFloatingChanged);

        // toggleWindowFloat arrives with the same bare instance id — exact match.
        engine.toggleWindowFloat(instanceId, screen);
        QCoreApplication::processEvents();

        QCOMPARE(feedback.count(), 0);
        QCOMPARE(floatSpy.count(), 1);
        QVERIFY(state->isFloating(instanceId));
        QCOMPARE(state->windowCount(), 1);
    }
};

QTEST_MAIN(TestAutotileEngineClassMutation)
#include "test_autotile_engine_class_mutation.moc"
