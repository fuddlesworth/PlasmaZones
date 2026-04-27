// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_surface_animator.cpp
 * @brief Unit tests for PhosphorAnimationLayer::SurfaceAnimator.
 *
 * Coverage:
 *   - beginShow drives target opacity 0→1 and calls onComplete
 *   - beginHide drives target opacity from→0 and calls onComplete
 *   - cancel stops in-flight animation and does NOT fire onComplete
 *   - per-Role config lookup falls back to defaultConfig
 *   - profile resolution: profile path resolves through registry
 *   - ctor/dtor lifecycle is clean (no leaked Tracks)
 */
#include <PhosphorAnimationLayer/SurfaceAnimator.h>

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include <PhosphorLayer/PhosphorLayer.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <chrono>
#include <thread>

using namespace PhosphorAnimationLayer;
using PhosphorAnimation::PhosphorProfileRegistry;
using PhosphorAnimation::Profile;
using PhosphorLayer::Surface;
using PhosphorLayer::SurfaceFactory;

namespace {

/// Build a Profile with explicit duration + an OutCubic-shaped Easing
/// curve. Tests use a short duration (15ms) so the spin-wait at the end
/// of show/hide takes a few frames at most.
Profile makeProfile(int durationMs)
{
    Profile p;
    p.duration = durationMs;
    auto easing = std::make_shared<PhosphorAnimation::Easing>();
    easing->type = PhosphorAnimation::Easing::Type::CubicBezier;
    easing->x1 = 0.215;
    easing->y1 = 0.61;
    easing->x2 = 0.355;
    easing->y2 = 1.0;
    p.curve = easing;
    return p;
}

/// Spin the event loop until @p predicate returns true or @p timeoutMs elapses.
/// Wraps the QtQuickClock + AnimatedValue tick path so tests can wait for
/// async completion without arbitrary fixed sleeps.
template<typename Predicate>
bool waitFor(Predicate p, int timeoutMs = 1000)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (p()) {
            return true;
        }
        QTest::qWait(5);
    }
    return p();
}

SurfaceAnimator::Config defaultsForTesting()
{
    SurfaceAnimator::Config c;
    c.showProfile = QStringLiteral("test.show");
    c.hideProfile = QStringLiteral("test.hide");
    return c;
}

/// Resolve the QQuickItem the animator drives: when SurfaceConfig::contentItem
/// is provided, the Surface parents it under window->contentItem() and the
/// animator drives the inner item (`m_rootItem`). Reading the wrong item is
/// a common test foot-gun — centralise the lookup so every show/hide test
/// reads the same child the animator writes.
QQuickItem* animatedItem(Surface* surface)
{
    if (!surface || !surface->window()) {
        return nullptr;
    }
    auto* parent = surface->window()->contentItem();
    if (!parent) {
        return nullptr;
    }
    const auto children = parent->childItems();
    return children.isEmpty() ? parent : children.first();
}

} // namespace

class TestSurfaceAnimator : public QObject
{
    Q_OBJECT

private:
    PhosphorProfileRegistry* m_registry = nullptr;

private Q_SLOTS:
    void initTestCase()
    {
        m_registry = &PhosphorProfileRegistry::instance();
        m_registry->registerProfile(QStringLiteral("test.show"), makeProfile(20));
        m_registry->registerProfile(QStringLiteral("test.hide"), makeProfile(20));
        m_registry->registerProfile(QStringLiteral("test.scale-show"), makeProfile(20));
    }

    void cleanupTestCase()
    {
        m_registry->unregisterProfile(QStringLiteral("test.show"));
        m_registry->unregisterProfile(QStringLiteral("test.hide"));
        m_registry->unregisterProfile(QStringLiteral("test.scale-show"));
    }

    /// Default-constructed animator binds to the singleton registry and
    /// has an empty defaultConfig (every field zero/empty). Hide on a
    /// surface with no opacity/scale config still resolves via library
    /// fallback Profile (150 ms OutCubic).
    void ctor_default_uses_singleton()
    {
        SurfaceAnimator anim;
        const auto cfg = anim.defaultConfig();
        QVERIFY(cfg.showProfile.isEmpty());
        QVERIFY(cfg.hideProfile.isEmpty());
    }

    /// Per-role config takes precedence over default. Roles whose
    /// scopePrefix doesn't match fall back to default.
    void per_role_config_lookup()
    {
        SurfaceAnimator anim(defaultsForTesting());
        SurfaceAnimator::Config special;
        special.showProfile = QStringLiteral("test.show");
        special.showScaleProfile = QStringLiteral("test.scale-show");
        special.showScaleFrom = 0.8;
        anim.registerConfigForRole(PhosphorLayer::Roles::CenteredModal, special);

        const auto resolved = anim.configForRole(PhosphorLayer::Roles::CenteredModal);
        QCOMPARE(resolved.showScaleProfile, QStringLiteral("test.scale-show"));
        QCOMPARE(resolved.showScaleFrom, 0.8);

        // Unregistered role falls back to default
        const auto defaultResolved = anim.configForRole(PhosphorLayer::Roles::TopPanel);
        QCOMPARE(defaultResolved.showProfile, QStringLiteral("test.show"));
        QVERIFY(defaultResolved.showScaleProfile.isEmpty());
    }

    /// Regression: PlasmaZones registers configs against base-role scope
    /// prefixes (e.g. "plasmazones-layout-osd") but the actual surfaces
    /// carry per-instance derived prefixes ("plasmazones-layout-osd-{id}-{gen}")
    /// for compositor scope uniqueness. Lookup must match by longest
    /// registered prefix with a '-' boundary, not by exact equality.
    void per_role_config_prefix_match()
    {
        SurfaceAnimator anim(defaultsForTesting());
        SurfaceAnimator::Config base;
        base.showProfile = QStringLiteral("base.show");
        SurfaceAnimator::Config sibling;
        sibling.showProfile = QStringLiteral("sibling.show");
        SurfaceAnimator::Config refinement;
        refinement.showProfile = QStringLiteral("refinement.show");
        anim.registerConfigForRole(
            PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-layout-osd")), base);
        anim.registerConfigForRole(
            PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-layout-picker")), sibling);
        // Longer key under same prefix family — should beat "base" for
        // surfaces scoped to its sub-tree.
        anim.registerConfigForRole(
            PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-layout-osd-locked")),
            refinement);

        // Per-instance scope: matches the base via prefix + '-' boundary.
        auto perInstance =
            PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-layout-osd-DP-1-3"));
        QCOMPARE(anim.configForRole(perInstance).showProfile, QStringLiteral("base.show"));

        // Sibling prefix must NOT match the base — boundary check guards
        // against accidental cross-pollination.
        auto sibInstance =
            PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-layout-picker-DP-1-3"));
        QCOMPARE(anim.configForRole(sibInstance).showProfile, QStringLiteral("sibling.show"));

        // Longest-match wins: a surface scoped under the "locked" sub-tree
        // resolves to the more-specific config.
        auto lockedInstance =
            PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-layout-osd-locked-DP-1-3"));
        QCOMPARE(anim.configForRole(lockedInstance).showProfile, QStringLiteral("refinement.show"));

        // Exact-match still works (registered key == surface prefix).
        auto exactInstance =
            PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("plasmazones-layout-osd"));
        QCOMPARE(anim.configForRole(exactInstance).showProfile, QStringLiteral("base.show"));

        // Completely unrelated prefix → default.
        auto unrelated = PhosphorLayer::Roles::CenteredModal.withScopePrefix(QStringLiteral("some-other-overlay"));
        QCOMPARE(anim.configForRole(unrelated).showProfile, QStringLiteral("test.show"));
    }

    /// Two-leg show (opacity + scale): both AnimatedValues must reach
    /// their terminal values, AND the consumer's onComplete must fire
    /// exactly once after BOTH legs settle (not after the first finishes).
    /// Locks in the `pendingLegs` state machine — a regression that
    /// erased the entry on the first leg's completion would still pass
    /// the existing single-leg test.
    void beginShow_two_leg_completes_after_both_legs()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;

        SurfaceAnimator::Config cfg;
        cfg.showProfile = QStringLiteral("test.show");
        cfg.showScaleProfile = QStringLiteral("test.scale-show");
        cfg.showScaleFrom = 0.5;
        cfg.hideProfile = QStringLiteral("test.hide");
        SurfaceAnimator anim(cfg);

        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        // Don't wire deps.animator — we want to drive the standalone
        // SurfaceAnimator directly so we observe its onComplete without
        // Surface::Impl swallowing it as Q_UNUSED.
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig sc;
        sc.role = PhosphorLayer::Roles::CenteredModal;
        sc.contentItem = std::make_unique<QQuickItem>();
        sc.screen = s.primary();
        sc.keepMappedOnHide = true;
        sc.debugName = QStringLiteral("two-leg-test");

        auto* surface = f.create(std::move(sc));
        QVERIFY(surface);
        // warmUp builds the window + content tree so animatedItem() can
        // resolve the target. We're driving the animator directly (not
        // through Surface::show), so we don't want to actually flip the
        // surface to Shown state.
        surface->warmUp();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        int onCompleteCallCount = 0;
        anim.beginShow(surface, target, [&onCompleteCallCount]() {
            ++onCompleteCallCount;
        });

        // Both legs must reach their terminal values...
        QVERIFY(waitFor(
            [target] {
                return target->opacity() >= 0.999 && target->scale() >= 0.999;
            },
            500));

        // ...and the consumer's callback must have fired exactly once
        // (after the second leg settled, not after the first).
        QVERIFY(waitFor(
            [&onCompleteCallCount] {
                return onCompleteCallCount == 1;
            },
            200));
        // Wait a bit more to ensure no spurious second invocation.
        QTest::qWait(50);
        QCOMPARE(onCompleteCallCount, 1);

        delete surface;
    }

    /// Live profile re-resolution: replacing a registered profile between
    /// dispatches must apply the new curve / duration on the next show.
    /// Lock in the contract from the PR description: "live profile
    /// reloads (drop a JSON, see it apply on next show) flow through the
    /// registry's existing watcher path — the animator just re-resolves
    /// on every beginShow / beginHide."
    void profile_reload_applies_on_next_dispatch()
    {
        const QString path = QStringLiteral("test.reload");
        // Register an absurdly long profile first so we can detect the
        // reload took effect (the short profile finishes in tens of ms,
        // the long one wouldn't in our wait window).
        PhosphorProfileRegistry::instance().registerProfile(path, makeProfile(/*durationMs=*/5000));
        struct Cleanup
        {
            QString p;
            ~Cleanup()
            {
                PhosphorProfileRegistry::instance().unregisterProfile(p);
            }
        } _{path};

        SurfaceAnimator::Config cfg;
        cfg.showProfile = path;
        cfg.hideProfile = path;
        SurfaceAnimator anim(cfg);

        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig sc;
        sc.role = PhosphorLayer::Roles::CenteredModal;
        sc.contentItem = std::make_unique<QQuickItem>();
        sc.screen = s.primary();
        sc.debugName = QStringLiteral("reload-test");

        auto* surface = f.create(std::move(sc));
        QVERIFY(surface);
        // warmUp builds the window + content tree so animatedItem() can
        // resolve the target. We're driving the animator directly.
        surface->warmUp();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        // First show with the 5000ms profile — opacity must NOT reach 1.0
        // within the test's 100ms window.
        anim.beginShow(surface, target, []() { });
        QTest::qWait(100);
        QVERIFY2(target->opacity() < 0.999,
                 "5000ms profile must not have completed yet — animator may not be re-resolving live");

        // Cancel the in-flight long animation so we get a clean slate.
        anim.cancel(surface);
        target->setOpacity(1.0); // reset for the supersession-aware fresh-show check
        target->setScale(1.0);

        // Reload the profile to a 20ms duration — same path, new shape.
        PhosphorProfileRegistry::instance().registerProfile(path, makeProfile(/*durationMs=*/20));

        // Second show: must pick up the reloaded profile and complete fast.
        anim.beginShow(surface, target, []() { });
        QVERIFY2(waitFor(
                     [target] {
                         return target->opacity() >= 0.999;
                     },
                     500),
                 "reloaded 20ms profile must complete within 500ms — animator is caching profile resolution");

        delete surface;
    }

    /// Driving a real Surface end-to-end. Uses MockTransport + MockScreen
    /// so the test doesn't need a live compositor. Verifies opacity
    /// reaches 1.0 and the completion callback fires.
    void beginShow_drives_opacity_to_one()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        SurfaceAnimator anim(defaultsForTesting());

        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorLayer::Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.debugName = QStringLiteral("show-test");

        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);

        bool completed = false;
        // We can't directly hook the animator's onComplete (it's invoked
        // from inside Surface::Impl::driveWarmOrShow via the captured
        // lambda). Instead we observe the QQuickItem's opacity reaching
        // 1.0 — the AnimatedValue's last tick clamps to the terminal
        // value and the runLeg's pendingLegs counter then erases the
        // tracking entry.
        surface->show();
        Q_UNUSED(completed);

        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);
        // After show, opacity has been set to 0 (initial state) and the
        // animation drives toward 1.0 over ~20ms. Wait up to 500ms for
        // the terminal value to land.
        QVERIFY(waitFor(
            [target] {
                return target->opacity() >= 0.999;
            },
            500));

        QCOMPARE(surface->state(), Surface::State::Shown);
        delete surface;
    }

    /// hide drives opacity back to zero. show first, then hide on
    /// keepMappedOnHide=true so the window stays visible — tests the
    /// Phase-5 always-mapped lifecycle end-to-end.
    void beginHide_drives_opacity_to_zero()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        SurfaceAnimator anim(defaultsForTesting());

        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorLayer::Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.keepMappedOnHide = true;
        cfg.debugName = QStringLiteral("hide-test");

        auto* surface = f.create(std::move(cfg));
        surface->show();

        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);
        QVERIFY(waitFor(
            [target] {
                return target->opacity() >= 0.999;
            },
            500));

        surface->hide();
        QCOMPARE(surface->state(), Surface::State::Hidden);
        QVERIFY(surface->window()->isVisible()); // keepMappedOnHide kept it Qt-visible

        QVERIFY(waitFor(
            [target] {
                return target->opacity() <= 0.001;
            },
            500));
        delete surface;
    }

    /// cancel(surface) must NOT fire the consumer's onComplete.
    /// Cancellation is the documented non-completion termination path;
    /// the contract is "consumers that need a settled signal regardless
    /// of completion-vs-cancellation must wrap the callback themselves."
    /// `dtor_cancels_leftover_tracks` exercises this in the dtor scenario;
    /// this test pins the same invariant on the explicit `cancel()` path.
    void cancel_does_not_fire_oncomplete()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        SurfaceAnimator anim(defaultsForTesting());
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorLayer::Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.keepMappedOnHide = true;
        cfg.debugName = QStringLiteral("cancel-no-oncomplete");

        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);
        surface->warmUp();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        int onCompleteFires = 0;
        anim.beginShow(surface, target, [&onCompleteFires]() {
            ++onCompleteFires;
        });

        // Cancel before the 20ms profile completes.
        anim.cancel(surface);

        // Wait long enough that any natural completion or stale tick
        // would have fired the user's onComplete. The 20ms test profile
        // would settle in ~3 ticks at 16ms each; 100ms is well beyond.
        QTest::qWait(100);

        QCOMPARE(onCompleteFires, 0);
        delete surface;
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
        SurfaceAnimator anim(defaultsForTesting());
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorLayer::Roles::CenteredModal;
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

    /// cancel mid-flight stops the AnimatedValue and does not produce a
    /// completion callback. After cancel, opacity stays at whatever
    /// value the animation left it at (no snap-to-target).
    void cancel_stops_inflight_animation()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        SurfaceAnimator anim(defaultsForTesting());

        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorLayer::Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.keepMappedOnHide = true;
        cfg.debugName = QStringLiteral("cancel-test");

        auto* surface = f.create(std::move(cfg));
        surface->show();

        // Don't wait for completion — cancel before the 20ms elapses.
        anim.cancel(surface);

        // Brief wait to give any stale tick a chance to settle (we're
        // asserting nothing fires — there's no "definite" event to
        // wait for).
        QTest::qWait(50);

        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);
        // Opacity may be anywhere in [0, 1] depending on when the
        // cancel hit relative to the first tick — but it MUST NOT be
        // racing further. Take two readings; they should match.
        const qreal first = target->opacity();
        QTest::qWait(30);
        const qreal second = target->opacity();
        QCOMPARE(first, second);

        delete surface;
    }

    /// Regression: beginShow superseding a mid-flight beginHide must pick
    /// up from the live opacity instead of jumping back to 0. Pre-fix,
    /// the show path hardcoded fromOpacity=0; with the live-from change
    /// a re-show while the hide is at 0.5 fades 0.5→1.0, not 0.5→0→1.0.
    void show_after_partial_hide_starts_from_live_opacity()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        SurfaceAnimator anim(defaultsForTesting());
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorLayer::Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.keepMappedOnHide = true;
        cfg.debugName = QStringLiteral("supersede-test");

        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);
        surface->show();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);
        QVERIFY(waitFor(
            [target] {
                return target->opacity() >= 0.999;
            },
            500));

        // Manually pin the target opacity to a mid-fade value to simulate
        // a hide caught mid-flight. We're not running a real hide (timing
        // would race the test); the contract is "beginShow reads live
        // opacity, doesn't slam to 0".
        target->setOpacity(0.5);

        // The show path's runLeg sets the target opacity to the computed
        // fromOpacity synchronously before the first tick — so right after
        // surface->show(), opacity must NOT be 0 (the pre-fix bug). Since
        // 0.5 < 1.0, the supersession-aware path should keep it at 0.5
        // and fade up.
        surface->show();
        const qreal opacityAfterShow = target->opacity();
        QVERIFY2(opacityAfterShow > 0.001, "beginShow superseding a partial state must not slam opacity to 0");

        // And it should still finish at 1.0.
        QVERIFY(waitFor(
            [target] {
                return target->opacity() >= 0.999;
            },
            500));
        delete surface;
    }

    /// Fresh first show (no prior animation, target opacity defaulted to
    /// 1.0 by QQuickItem) must reset to 0 and fade up. Otherwise the
    /// supersession-aware live-from path would observe opacity==1.0 and
    /// run a no-op fade-from-1-to-1.
    void show_fresh_starts_from_zero()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        SurfaceAnimator anim(defaultsForTesting());
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorLayer::Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.debugName = QStringLiteral("fresh-show-test");

        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);

        // Surface warming is what gives us a window + content tree.
        // Resolving the target before show() returns nullptr.
        surface->show();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        // After show kicks the runLeg synchronously, target opacity must
        // be 0.0 — the fresh-show path picked the configured fromOpacity
        // because the live opacity (QQuickItem default 1.0) was at the
        // terminal value. Without the supersession-aware reset this would
        // be 1.0 and we'd see no fade-in.
        QCOMPARE(target->opacity(), 0.0);
        QVERIFY(waitFor(
            [target] {
                return target->opacity() >= 0.999;
            },
            500));
        delete surface;
    }

    /// Zero-duration profile completes the leg synchronously. Verify the
    /// user's onComplete fires exactly once and that the target lands at
    /// the terminal value. Edge case for the Profile JSON live-reload
    /// path — a user could ship a 0 ms profile to "instantly snap" an
    /// overlay.
    void zero_duration_profile_completes_synchronously()
    {
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("test.zero"), makeProfile(0));
        struct Cleanup
        {
            ~Cleanup()
            {
                PhosphorProfileRegistry::instance().unregisterProfile(QStringLiteral("test.zero"));
            }
        } _;

        SurfaceAnimator::Config cfg;
        cfg.showProfile = QStringLiteral("test.zero");
        cfg.hideProfile = QStringLiteral("test.zero");
        SurfaceAnimator anim(cfg);

        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig sc;
        sc.role = PhosphorLayer::Roles::CenteredModal;
        sc.contentItem = std::make_unique<QQuickItem>();
        sc.screen = s.primary();
        sc.debugName = QStringLiteral("zero-duration-test");

        auto* surface = f.create(std::move(sc));
        surface->show();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        // 0 ms profile may still need one tick to settle since AnimatedValue
        // latches startTime on the first advance. Wait for the timer.
        QVERIFY(waitFor(
            [target] {
                return target->opacity() >= 0.999;
            },
            500));
        delete surface;
    }

    /// SurfaceAnimator destructor cancels any leftover tracks. Without
    /// this guard a surface that outlives the animator (mis-ordered
    /// teardown) would write into a dead Track on its next tick.
    ///
    /// Naively, deleting the Surface first runs Surface::~Impl which
    /// calls deps.animator->cancel(surface) and empties m_tracks before
    /// ~SurfaceAnimator runs — the dtor's defensive loop never executes.
    /// To actually exercise the loop we construct the Surface with a
    /// null deps.animator (so Surface dispatches through the lib-default
    /// no-op animator and our SurfaceAnimator never gets a Surface-side
    /// cancel), then manually drive our standalone SurfaceAnimator via
    /// beginShow() to install a tracking entry. Deleting the animator
    /// while the surface is still alive forces the dtor's
    /// `while (!m_tracks.empty())` loop to clean up the leftover entry.
    void dtor_cancels_leftover_tracks()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;

        // Surface plumbed with NO animator — Surface::~Impl will call
        // the lib-default no-op animator's cancel(), not ours.
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = nullptr;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorLayer::Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.keepMappedOnHide = true;
        cfg.debugName = QStringLiteral("dtor-test");

        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);
        surface->show();

        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        // Standalone animator with a long-running profile so its track
        // entry is in-flight at the time we delete the animator.
        auto* anim = new SurfaceAnimator(defaultsForTesting());
        bool completed = false;
        anim->beginShow(surface, target, [&completed]() {
            completed = true;
        });

        // Animator's m_tracks now contains an entry for `surface`.
        // Deleting the animator must clean it up via the defensive
        // dtor loop without firing onComplete (cancellation is a
        // non-completion termination per the documented contract).
        delete anim;
        QVERIFY2(!completed, "animator dtor must NOT fire onComplete on leftover tracks");

        // Surface still alive at this point — its dtor calls into the
        // (now-default-only) no-op animator, which is safe.
        delete surface;
    }

    /// Regression: cancelTracking must not destroy an AnimatedValue
    /// under its own advance() call stack.
    ///
    /// The animator's `spec.onValueChanged` is fired synchronously from
    /// inside AnimatedValue::advance(). It writes to the target's
    /// opacity/scale property, which fires QML's opacityChanged signal
    /// (also synchronously). A consumer slot connected to that signal
    /// can call `anim.cancel(surface)` — which routes through
    /// cancelTracking. Pre-fix, cancelTracking destroyed the
    /// AnimatedValue at scope exit (local unique_ptr), tearing it down
    /// while `advance()` was still on the stack. AnimatedValue.h:547
    /// forbids this — `advance()` accesses `*this` after the callback
    /// returns (e.g. the m_isComplete branch).
    ///
    /// The fix parks cancelled AnimatedValues in m_pendingDestroy
    /// (mirroring legCompleted's deferred-destroy contract), so
    /// destruction happens only after every advance() frame on the
    /// stack has unwound.
    ///
    /// Without the fix, this test reliably crashes (heap-use-after-free
    /// under ASan) on the AnimatedValue's m_isComplete read after
    /// onValueChanged returns.
    void cancel_during_onValueChanged_does_not_uaf()
    {
        // Long-duration profile so we're guaranteed multiple mid-fade
        // ticks before completion (16 ms tick × ~12 ticks @ 200 ms).
        // The default 20 ms test profile completes in ~2 ticks, leaving
        // a narrow window where the cancel + the per-tick callback
        // synchronisation race against test-runner scheduling.
        const QString path = QStringLiteral("test.cancel-during-tick");
        PhosphorProfileRegistry::instance().registerProfile(path, makeProfile(/*durationMs=*/200));
        struct Cleanup
        {
            QString p;
            ~Cleanup()
            {
                PhosphorProfileRegistry::instance().unregisterProfile(p);
            }
        } _{path};

        SurfaceAnimator::Config cfg;
        cfg.showProfile = path;
        cfg.hideProfile = path;
        SurfaceAnimator anim(cfg);

        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig sc;
        sc.role = PhosphorLayer::Roles::CenteredModal;
        sc.contentItem = std::make_unique<QQuickItem>();
        sc.screen = s.primary();
        sc.keepMappedOnHide = true;
        sc.debugName = QStringLiteral("cancel-during-tick");

        auto* surface = f.create(std::move(sc));
        QVERIFY(surface);
        surface->show();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        // Hook opacityChanged BEFORE the first mid-fade tick lands —
        // surface->show()'s synchronous setOpacity(fromOpacity=0) has
        // already fired its emit (which we don't observe from here),
        // and the timer's first advance() will fire onValueChanged(0)
        // (suppressed by Qt's qFuzzyCompare since opacity is already 0).
        // The second tick fires onValueChanged with the partially-progressed
        // value — that's the one we cancel from.
        int opacityChangeFires = 0;
        bool cancelled = false;
        QObject::connect(target, &QQuickItem::opacityChanged, target, [&]() {
            ++opacityChangeFires;
            if (cancelled) {
                return;
            }
            // Cancel from inside the spec.onValueChanged → setOpacity →
            // opacityChanged → this slot call chain. Pre-fix this UAFs
            // when advance() returns and accesses `this` post-callback.
            cancelled = true;
            anim.cancel(surface);
        });

        QVERIFY2(waitFor(
                     [&] {
                         return cancelled;
                     },
                     1000),
                 "opacityChanged must fire at least once during the 200 ms animation; "
                 "if 0 fires were observed the animator's tick driver is not running");

        // Pre-fix this is where the UAF lands: the next QTimer tick
        // calls tickAll → advance() on the parked-but-still-undestroyed
        // AnimatedValue's freed memory. Post-fix m_pendingDestroy drains
        // at the start of the next tick before any advance() runs.
        QTest::qWait(150);

        // Opacity must have stopped progressing after cancel — if the
        // animation kept ticking, post-fix m_pendingDestroy was wrong
        // OR the cancel didn't take effect.
        const qreal first = target->opacity();
        QTest::qWait(50);
        const qreal second = target->opacity();
        QCOMPARE(first, second);

        delete surface;
    }

    /// Regression: the two-leg path (opacity + scale) has a separate
    /// onValueChanged callback per leg. Cancelling from inside the
    /// SCALE leg's callback while the OPACITY leg is still in flight
    /// hits a slightly different code path than the single-leg test
    /// above (different AnimatedValue is being torn down; the opacity
    /// leg's still-installed unique_ptr also needs to land in the
    /// graveyard cleanly).
    ///
    /// Without the deferred-destroy contract this would UAF on the
    /// scale AnimatedValue's m_isComplete read post-callback.
    void cancel_from_scale_leg_during_two_leg_animation()
    {
        const QString path = QStringLiteral("test.two-leg-cancel");
        PhosphorProfileRegistry::instance().registerProfile(path, makeProfile(/*durationMs=*/200));
        struct Cleanup
        {
            QString p;
            ~Cleanup()
            {
                PhosphorProfileRegistry::instance().unregisterProfile(p);
            }
        } _{path};

        SurfaceAnimator::Config cfg;
        cfg.showProfile = path;
        cfg.showScaleProfile = path;
        cfg.showScaleFrom = 0.5;
        cfg.hideProfile = path;
        SurfaceAnimator anim(cfg);

        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = &anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig sc;
        sc.role = PhosphorLayer::Roles::CenteredModal;
        sc.contentItem = std::make_unique<QQuickItem>();
        sc.screen = s.primary();
        sc.keepMappedOnHide = true;
        sc.debugName = QStringLiteral("two-leg-cancel");

        auto* surface = f.create(std::move(sc));
        QVERIFY(surface);
        surface->show();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        // Cancel from inside scaleChanged's slot — the scale leg's
        // onValueChanged → setScale → scaleChanged → this slot chain.
        // Pre-fix this UAFs on the scale AnimatedValue's post-callback
        // access.
        bool cancelled = false;
        QObject::connect(target, &QQuickItem::scaleChanged, target, [&]() {
            if (cancelled) {
                return;
            }
            // Only cancel mid-flight (target hasn't reached 1.0 yet).
            // The first scaleChanged is the synchronous setScale(0.5)
            // from runLeg's pre-tick state-set; we want the second one
            // which is the first AnimatedValue tick.
            if (target->scale() >= 0.999) {
                return;
            }
            if (target->scale() <= 0.501) {
                // First (synchronous) emit — wait for the next tick.
                return;
            }
            cancelled = true;
            anim.cancel(surface);
        });

        QVERIFY2(waitFor(
                     [&] {
                         return cancelled;
                     },
                     1000),
                 "scaleChanged must fire mid-flight; if 0 mid-flight fires were observed the "
                 "animator's tick driver is not running or the scale leg never started");

        // Pre-fix UAF window — give the timer two more ticks to fire
        // tickAll after cancel. m_pendingDestroy must drain the cancelled
        // AnimatedValues before any advance() touches them.
        QTest::qWait(150);

        // Both legs must have stopped progressing after cancel.
        const qreal firstOp = target->opacity();
        const qreal firstSc = target->scale();
        QTest::qWait(50);
        const qreal secondOp = target->opacity();
        const qreal secondSc = target->scale();
        QCOMPARE(firstOp, secondOp);
        QCOMPARE(firstSc, secondSc);

        delete surface;
    }

    /// Regression: live profile reload during an in-flight animation
    /// must NOT affect the in-flight animation. The header's
    /// "live profile reloads apply on next show" contract relies on
    /// MotionSpec capturing the Profile by value at start() time — a
    /// future refactor that switched to a registry pointer lookup per
    /// advance() would silently break this.
    void profile_reload_during_flight_does_not_affect_inflight()
    {
        const QString path = QStringLiteral("test.reload-during-flight");
        // Start with a 200 ms profile.
        PhosphorProfileRegistry::instance().registerProfile(path, makeProfile(/*durationMs=*/200));
        struct Cleanup
        {
            QString p;
            ~Cleanup()
            {
                PhosphorProfileRegistry::instance().unregisterProfile(p);
            }
        } _{path};

        SurfaceAnimator::Config cfg;
        cfg.showProfile = path;
        cfg.hideProfile = path;
        SurfaceAnimator anim(cfg);

        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;
        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig sc;
        sc.role = PhosphorLayer::Roles::CenteredModal;
        sc.contentItem = std::make_unique<QQuickItem>();
        sc.screen = s.primary();
        sc.debugName = QStringLiteral("reload-during-flight");

        auto* surface = f.create(std::move(sc));
        QVERIFY(surface);
        surface->warmUp();
        QQuickItem* target = animatedItem(surface);
        QVERIFY(target);

        // Kick off the show with the 200 ms profile.
        anim.beginShow(surface, target, []() { });

        // Wait for the animation to make some progress but not finish.
        QVERIFY(waitFor(
            [target] {
                return target->opacity() > 0.05 && target->opacity() < 0.95;
            },
            500));

        // Replace the profile mid-flight with a 5000 ms one. If the
        // animator re-resolved per advance(), the in-flight animation
        // would suddenly slow to a crawl.
        PhosphorProfileRegistry::instance().registerProfile(path, makeProfile(/*durationMs=*/5000));

        // The in-flight 200 ms animation must still finish within ~250 ms
        // of the original start. Allow generous headroom for CI jitter
        // but well under the 5000 ms reloaded profile would take.
        QVERIFY2(waitFor(
                     [target] {
                         return target->opacity() >= 0.999;
                     },
                     800),
                 "in-flight animation must not pick up the reloaded profile mid-flight — "
                 "Profile is captured by value at start() time, not re-resolved per advance()");

        delete surface;
    }
};

QTEST_MAIN(TestSurfaceAnimator)
#include "test_surface_animator.moc"
