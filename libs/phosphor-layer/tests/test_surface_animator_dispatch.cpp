// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_surface_animator_dispatch.cpp
 * @brief Verifies Surface dispatches show/hide transitions through the
 *        injected ISurfaceAnimator. Phase-5 scope: the *plumbing* — the
 *        concrete fade/scale animator lives in phosphor-animation-layer
 *        and has its own tests there.
 *
 * Scope:
 *   - beginShow fires on Hidden→Shown with the right Surface + rootItem
 *   - beginHide fires on Shown→Hidden
 *   - cancel fires on the Surface being destroyed mid-flight
 *   - cancel fires on supersession (show-while-hiding, hide-while-showing)
 *   - keepMappedOnHide preserves QQuickWindow visibility across hide
 *   - SurfaceConfig.keepMappedOnHide=false (default) calls window->hide()
 */

#include <PhosphorLayer/PhosphorLayer.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <vector>

using namespace PhosphorLayer;
using PhosphorLayer::Testing::MockScreenProvider;
using PhosphorLayer::Testing::MockTransport;

namespace {

/// Records every interaction so tests can assert on call sequences.
/// onComplete is intentionally NOT invoked — we want to verify the
/// library's behaviour both with and without completion, and tests that
/// need completion can call it explicitly via the recorded callback.
class RecordingAnimator : public ISurfaceAnimator
{
public:
    struct Call
    {
        enum class Kind {
            Show,
            Hide,
            Cancel
        } kind;
        Surface* surface;
        QQuickItem* rootItem;
        CompletionCallback onComplete;
    };

    void beginShow(Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete) override
    {
        m_calls.push_back({Call::Kind::Show, surface, rootItem, std::move(onComplete)});
    }
    void beginHide(Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete) override
    {
        m_calls.push_back({Call::Kind::Hide, surface, rootItem, std::move(onComplete)});
    }
    void cancel(Surface* surface) override
    {
        m_calls.push_back({Call::Kind::Cancel, surface, nullptr, {}});
    }

    std::vector<Call> m_calls;
};

/// Minimal QObject with a parameter-less signal so the QML-style
/// string-based connect path from an external signal source to
/// `Surface::hide()` can be exercised in a unit test. Mirrors the
/// way LayoutOsd.qml's dismissTimer fires `dismissRequested()`.
class SignalEmitter : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
Q_SIGNALS:
    void fire();
};

SurfaceConfig buildConfig(QScreen* screen, bool keepMapped = false)
{
    SurfaceConfig cfg;
    cfg.role = Roles::CenteredModal;
    cfg.contentItem = std::make_unique<QQuickItem>();
    cfg.screen = screen;
    cfg.keepMappedOnHide = keepMapped;
    cfg.debugName = QStringLiteral("animator-dispatch-test");
    return cfg;
}

SurfaceFactory::Deps depsWithAnimator(MockTransport* t, MockScreenProvider* s, ISurfaceAnimator* anim)
{
    auto d = PhosphorLayer::Testing::makeDeps(t, s);
    d.animator = anim;
    return d;
}

} // namespace

class TestSurfaceAnimatorDispatch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Show without an injected animator must keep working — every existing
    /// consumer passes nullptr today and Phase 5 must not regress them.
    void noAnimator_show_uses_window_show()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));

        auto* surface = f.create(buildConfig(s.primary()));
        QVERIFY(surface);
        surface->show();
        QCOMPARE(surface->state(), Surface::State::Shown);
        QVERIFY(surface->window());
        QVERIFY(surface->window()->isVisible());
    }

    /// Inject an animator: Show must call beginShow on it with the same
    /// Surface and a non-null rootItem (the contentItem the QML/Item content
    /// is parented to).
    void show_calls_beginShow()
    {
        MockTransport t;
        MockScreenProvider s;
        RecordingAnimator anim;
        SurfaceFactory f(depsWithAnimator(&t, &s, &anim));

        auto* surface = f.create(buildConfig(s.primary()));
        surface->show();

        // Should see exactly one Show call. cancel-before-show does fire if
        // the impl decides to clear an in-flight hide (it does), so we
        // tolerate a leading Cancel and verify the Show is present.
        bool sawShow = false;
        for (const auto& call : anim.m_calls) {
            if (call.kind == RecordingAnimator::Call::Kind::Show) {
                QCOMPARE(call.surface, surface);
                QVERIFY(call.rootItem);
                sawShow = true;
            }
        }
        QVERIFY(sawShow);
    }

    /// Hide must call beginHide with the same Surface and rootItem.
    void hide_calls_beginHide()
    {
        MockTransport t;
        MockScreenProvider s;
        RecordingAnimator anim;
        SurfaceFactory f(depsWithAnimator(&t, &s, &anim));

        auto* surface = f.create(buildConfig(s.primary()));
        surface->show();
        anim.m_calls.clear();
        surface->hide();

        bool sawHide = false;
        for (const auto& call : anim.m_calls) {
            if (call.kind == RecordingAnimator::Call::Kind::Hide) {
                QCOMPARE(call.surface, surface);
                QVERIFY(call.rootItem);
                sawHide = true;
            }
        }
        QVERIFY(sawHide);
    }

    /// Default keepMappedOnHide=false: Surface::hide() unmaps the QQuickWindow
    /// (preserving the pre-Phase-5 lifecycle for callers who haven't migrated).
    void defaultLifecycle_hide_unmaps_window()
    {
        MockTransport t;
        MockScreenProvider s;
        RecordingAnimator anim;
        SurfaceFactory f(depsWithAnimator(&t, &s, &anim));

        auto* surface = f.create(buildConfig(s.primary(), /*keepMapped=*/false));
        surface->show();
        QVERIFY(surface->window()->isVisible());
        surface->hide();
        QVERIFY(!surface->window()->isVisible());
    }

    /// keepMappedOnHide=true: Surface::hide() leaves the QQuickWindow visible
    /// (Phase-5 always-mapped lifecycle for OSDs/popups). The library still
    /// flips Qt::WindowTransparentForInput so the still-mapped surface stops
    /// intercepting clicks.
    void keepMapped_hide_preserves_window_visible()
    {
        MockTransport t;
        MockScreenProvider s;
        RecordingAnimator anim;
        SurfaceFactory f(depsWithAnimator(&t, &s, &anim));

        auto* surface = f.create(buildConfig(s.primary(), /*keepMapped=*/true));
        surface->show();
        QVERIFY(surface->window()->isVisible());
        QVERIFY(!surface->window()->flags().testFlag(Qt::WindowTransparentForInput));

        surface->hide();
        QCOMPARE(surface->state(), Surface::State::Hidden);
        QVERIFY(surface->window()->isVisible()); // still mapped
        QVERIFY(surface->window()->flags().testFlag(Qt::WindowTransparentForInput));

        // Re-show clears the flag and animator sees a fresh beginShow.
        anim.m_calls.clear();
        surface->show();
        QVERIFY(!surface->window()->flags().testFlag(Qt::WindowTransparentForInput));

        bool sawShow = false;
        for (const auto& call : anim.m_calls) {
            if (call.kind == RecordingAnimator::Call::Kind::Show) {
                sawShow = true;
            }
        }
        QVERIFY(sawShow);
    }

    /// show-while-hiding: cancel() must fire so the in-flight hide animation
    /// stops driving opacity to zero. Without this guard the animator would
    /// fight the show transition.
    void supersession_show_while_hiding_cancels_hide()
    {
        MockTransport t;
        MockScreenProvider s;
        RecordingAnimator anim;
        SurfaceFactory f(depsWithAnimator(&t, &s, &anim));

        auto* surface = f.create(buildConfig(s.primary(), /*keepMapped=*/true));
        surface->show();
        surface->hide();
        anim.m_calls.clear();
        surface->show();

        bool sawCancelBeforeShow = false;
        for (const auto& call : anim.m_calls) {
            if (call.kind == RecordingAnimator::Call::Kind::Cancel) {
                sawCancelBeforeShow = true;
            }
            if (call.kind == RecordingAnimator::Call::Kind::Show) {
                QVERIFY2(sawCancelBeforeShow, "cancel must precede the new beginShow");
                break;
            }
        }
        QVERIFY(sawCancelBeforeShow);
    }

    /// Regression: rapid show-while-shown (e.g. user switches layout
    /// while the previous OSD's fade-in is still in progress, or
    /// keyboard nav re-fires before the dismiss timer expires) must
    /// re-dispatch beginShow with a cancel of the prior in-flight
    /// animation. Without this the visual fade does not replay on
    /// repeated show() calls and the OSD just statically updates,
    /// regressing the pre-Phase-5 QML behaviour where the show()
    /// function reset opacity to 0 and re-ran showAnimation each time.
    void supersession_show_while_shown_replays_animation()
    {
        MockTransport t;
        MockScreenProvider s;
        RecordingAnimator anim;
        SurfaceFactory f(depsWithAnimator(&t, &s, &anim));

        auto* surface = f.create(buildConfig(s.primary(), /*keepMapped=*/true));
        surface->show();
        QCOMPARE(surface->state(), Surface::State::Shown);
        anim.m_calls.clear();

        // Second show() while still in Shown state — must cancel the
        // (possibly-still-running) prior show and dispatch a fresh
        // beginShow so the animator replays the fade.
        surface->show();
        QCOMPARE(surface->state(), Surface::State::Shown);

        bool sawCancel = false;
        bool sawShowAfterCancel = false;
        for (const auto& call : anim.m_calls) {
            if (call.kind == RecordingAnimator::Call::Kind::Cancel) {
                sawCancel = true;
            }
            if (call.kind == RecordingAnimator::Call::Kind::Show) {
                QVERIFY2(sawCancel, "cancel must precede the replayed beginShow");
                QCOMPARE(call.surface, surface);
                QVERIFY(call.rootItem);
                sawShowAfterCancel = true;
            }
        }
        QVERIFY(sawCancel);
        QVERIFY(sawShowAfterCancel);
    }

    /// Regression: show() / hide() / warmUp() must be registered as
    /// Q_SLOTS so QML-defined signals (e.g. an OSD dismissTimer's
    /// `dismissRequested()`) can target them via the string-based
    /// QObject::connect(src, SIGNAL(...), surface, SLOT(hide())) syntax.
    /// QML signals can't be addressed via Qt5 &-pointer connect; the
    /// string form is the only path. Without Q_SLOT registration the
    /// connect silently fails at runtime and the OSD never dismisses.
    void lifecycle_methods_registered_as_slots()
    {
        const QMetaObject* mo = &Surface::staticMetaObject;
        QVERIFY2(mo->indexOfSlot("show()") != -1, "Surface::show must be a Q_SLOT");
        QVERIFY2(mo->indexOfSlot("hide()") != -1, "Surface::hide must be a Q_SLOT");
        QVERIFY2(mo->indexOfSlot("warmUp()") != -1, "Surface::warmUp must be a Q_SLOT");
    }

    /// End-to-end: a QML-style string-based connect from an external
    /// signal source must reach Surface::hide. This is exactly the
    /// pattern the OSD dismiss path uses.
    void hide_via_string_based_connect_from_signal()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));

        auto* surface = f.create(buildConfig(s.primary(), /*keepMapped=*/true));
        QVERIFY(surface);
        surface->show();
        QCOMPARE(surface->state(), Surface::State::Shown);

        // Emit dismissed() via the same string-based connect the daemon's
        // osd.cpp uses to wire QML dismissTimer.dismissRequested →
        // surface->hide().
        SignalEmitter emitter;
        const bool connected =
            QObject::connect(&emitter, SIGNAL(fire()), surface, SLOT(hide()));
        QVERIFY2(connected, "string-based connect to Surface::hide must succeed");
        Q_EMIT emitter.fire();

        QCOMPARE(surface->state(), Surface::State::Hidden);
    }

    /// Surface destruction cancels any in-flight animation. Without this,
    /// the animator could still hold a QPropertyAnimation pointing at a
    /// torn-down QQuickItem.
    void destruction_cancels_inflight_animation()
    {
        MockTransport t;
        MockScreenProvider s;
        RecordingAnimator anim;
        SurfaceFactory f(depsWithAnimator(&t, &s, &anim));

        Surface* surface = f.create(buildConfig(s.primary(), /*keepMapped=*/true));
        surface->show();
        anim.m_calls.clear();

        delete surface;

        bool sawCancel = false;
        for (const auto& call : anim.m_calls) {
            if (call.kind == RecordingAnimator::Call::Kind::Cancel) {
                sawCancel = true;
            }
        }
        QVERIFY(sawCancel);
    }
};

QTEST_MAIN(TestSurfaceAnimatorDispatch)
#include "test_surface_animator_dispatch.moc"
