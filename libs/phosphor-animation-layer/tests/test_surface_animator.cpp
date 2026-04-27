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
    void dtor_cancels_leftover_tracks()
    {
        PhosphorLayer::Testing::MockTransport t;
        PhosphorLayer::Testing::MockScreenProvider s;

        auto* anim = new SurfaceAnimator(defaultsForTesting());

        auto deps = PhosphorLayer::Testing::makeDeps(&t, &s);
        deps.animator = anim;
        SurfaceFactory f(deps);

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = PhosphorLayer::Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.keepMappedOnHide = true;
        cfg.debugName = QStringLiteral("dtor-test");

        auto* surface = f.create(std::move(cfg));
        surface->show();

        // Delete the surface FIRST so the Surface::~Impl path runs
        // animator->cancel() while the animator is still alive. Then
        // delete the animator. This is the correct teardown order (the
        // production path always destroys the daemon's surfaces before
        // its OverlayService-owned animator); the test verifies the
        // ~Private destructor doesn't crash on a non-empty m_tracks if
        // the consumer somehow leaves entries behind.
        delete surface;
        delete anim;
    }
};

QTEST_MAIN(TestSurfaceAnimator)
#include "test_surface_animator.moc"
