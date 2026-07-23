// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_surface_animator_reentrancy.cpp
 * @brief Re-entrancy tests for PhosphorAnimationLayer::SurfaceAnimator.
 *
 * Split out of test_surface_animator.cpp (file-size ceiling): the tests
 * here drive synchronous consumer re-entry from inside runLeg's QML
 * signal windows — cancel() and beginShow/beginHide re-entered from the
 * pre-state setOpacity chain — pinning the slot-install ordering, the
 * post-write re-find, and the Track::generation replacement guard.
 */
#include "SurfaceAnimatorTestHarness.h"

class TestSurfaceAnimatorReentrancy : public QObject
{
    Q_OBJECT

private:
    /// Fixture-owned profile registry, mirroring test_surface_animator.
    PhosphorProfileRegistry m_registry;

private Q_SLOTS:
    void initTestCase()
    {
        m_registry.registerProfile(QStringLiteral("test.show"), makeProfile(20));
        m_registry.registerProfile(QStringLiteral("test.hide"), makeProfile(20));
        m_registry.registerProfile(QStringLiteral("test.scale-show"), makeProfile(20));
    }

    void cleanupTestCase()
    {
        m_registry.unregisterProfile(QStringLiteral("test.show"));
        m_registry.unregisterProfile(QStringLiteral("test.hide"));
        m_registry.unregisterProfile(QStringLiteral("test.scale-show"));
    }

    /// Regression: a synchronous cancel(surface) re-entered from inside
    /// the pre-state-set QML chain (target->setOpacity firing
    /// opacityChanged → consumer slot → cancel) must be honoured.
    /// Pre-fix the slot was installed AFTER the synchronous setOpacity,
    /// so cancelTracking saw an empty m_tracks, did nothing, and runLeg
    /// then installed a fresh slot the consumer wanted gone — silently
    /// dropping the cancel intent and starting an unwanted animation.
    /// Post-fix the slot is installed BEFORE the pre-state writes, so
    /// the re-entrant cancel finds an AV-less entry, erases it cleanly,
    /// and the post-write re-find detects the erasure and bails.
    void cancel_during_prestate_setOpacity_is_honoured()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        SurfaceAnimator anim(m_registry, defaultsForTesting());
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorShellPatterns::Modal();
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.keepMappedOnHide = true;
        cfg.debugName = QStringLiteral("cancel-during-prestate");

        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);
        surface->warmUp();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        // Force opacity ≠ 0 so the upcoming runLeg's setOpacity(0) actually
        // emits opacityChanged (Qt's qFuzzyCompare suppresses no-op writes).
        target->setOpacity(1.0);

        // Connect a one-shot opacityChanged handler that calls cancel
        // synchronously from inside the QML signal chain — exactly the
        // re-entry pattern this fix is designed to honour.
        bool cancelled = false;
        int onCompleteFires = 0;
        QMetaObject::Connection conn = QObject::connect(target, &QQuickItem::opacityChanged, target, [&]() {
            if (cancelled) {
                return;
            }
            cancelled = true;
            anim.cancel(surface);
        });

        anim.beginShow(surface, target, [&onCompleteFires]() {
            ++onCompleteFires;
        });

        QObject::disconnect(conn);

        // The synchronous setOpacity inside runLeg fired opacityChanged,
        // our slot called cancel, and runLeg should have detected the
        // erasure on its post-state-set re-find and bailed. So:
        //   - cancelled is true (the slot ran)
        //   - opacity has NOT been driven further (no animation started)
        //   - onComplete never fires
        QVERIFY(cancelled);
        const qreal opAfter = target->opacity();
        QTest::qWait(100); // would let any started animation tick
        QCOMPARE(target->opacity(), opAfter);
        QCOMPARE(onCompleteFires, 0);

        delete surface;
    }

    /// A synchronous beginHide re-entered from inside the outer
    /// beginShow's pre-state setOpacity chain installs a FRESH slot for
    /// the same (surface, target). The outer leg's post-write re-find
    /// must NOT adopt it (Track::generation mismatch): the outer leg
    /// bails, only the nested hide runs, and only the nested leg's
    /// onComplete fires. Pre-guard, the outer frame overwrote the
    /// nested slot's AVs and completed the wrong leg.
    void restart_during_prestate_setOpacity_defers_to_nested_leg()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        SurfaceAnimator anim(m_registry, defaultsForTesting());
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorShellPatterns::Modal();
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.keepMappedOnHide = true;
        cfg.debugName = QStringLiteral("restart-during-prestate");

        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);
        surface->warmUp();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        // Force opacity ≠ 0 so the outer beginShow's pre-state
        // setOpacity(0) genuinely emits opacityChanged.
        target->setOpacity(1.0);

        bool restarted = false;
        int outerCompleteFires = 0;
        int nestedCompleteFires = 0;
        QMetaObject::Connection conn = QObject::connect(target, &QQuickItem::opacityChanged, target, [&]() {
            if (restarted) {
                return;
            }
            restarted = true;
            // Re-enter with a REPLACEMENT leg instead of a bare cancel:
            // beginHide cancels the outer slot and installs its own. Lift
            // opacity first so the nested hide is a GENUINE animation
            // (0.7 → 0): a zero-distance hide would complete synchronously
            // via the false-start path and erase its slot before the outer
            // frame resumes, and the outer re-find would then bail on the
            // plain missing-entry clause — never exercising the generation
            // guard this test exists to pin. With a live nested slot, only
            // the generation mismatch stops the outer frame from adopting
            // it and overwriting the nested AV with show endpoints.
            target->setOpacity(0.7);
            anim.beginHide(surface, target, [&nestedCompleteFires]() {
                ++nestedCompleteFires;
            });
        });

        anim.beginShow(surface, target, [&outerCompleteFires]() {
            ++outerCompleteFires;
        });

        QObject::disconnect(conn);
        QVERIFY(restarted);

        // Let the nested hide animate to completion. Only its onComplete
        // may fire; the superseded outer show must never complete, and the
        // nested leg must end at its own terminal opacity (0.0), proving
        // the outer frame neither overwrote its running AV nor restarted
        // it with the show leg's 0 → 1 endpoints (deleting the generation
        // check flips all three assertions).
        QTRY_COMPARE_WITH_TIMEOUT(nestedCompleteFires, 1, 1000);
        QCOMPARE(outerCompleteFires, 0);
        QCOMPARE(target->opacity(), qreal(0.0));

        delete surface;
    }
};

QTEST_MAIN(TestSurfaceAnimatorReentrancy)
#include "test_surface_animator_reentrancy.moc"
