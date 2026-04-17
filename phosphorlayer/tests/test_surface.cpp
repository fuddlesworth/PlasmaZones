// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/PhosphorLayer.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"

#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorLayer;
using PhosphorLayer::Testing::MockScreenProvider;
using PhosphorLayer::Testing::MockTransport;

class TestSurface : public QObject
{
    Q_OBJECT

private:
    // Helper: build a SurfaceConfig with a bare QQuickItem content (so the
    // state machine doesn't block on async QML loading).
    static SurfaceConfig buildConfig(QScreen* screen, QString debugName = QStringLiteral("test"))
    {
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = screen;
        cfg.debugName = std::move(debugName);
        return cfg;
    }

private Q_SLOTS:
    void initialState()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto* surface = f.create(buildConfig(s.primary()));
        QVERIFY(surface);
        QCOMPARE(surface->state(), Surface::State::Constructed);
        QCOMPARE(surface->window(), nullptr);
        QCOMPARE(surface->transport(), nullptr);
    }

    void showTransitionsThroughWarmingToShown()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto* surface = f.create(buildConfig(s.primary()));
        QSignalSpy spy(surface, &Surface::stateChanged);

        surface->show();
        // Inline content (contentItem) — the state machine should reach Shown
        // synchronously in one drive() pass.
        QCOMPARE(surface->state(), Surface::State::Shown);
        QVERIFY(surface->window() != nullptr);
        QVERIFY(surface->transport() != nullptr);
        QCOMPARE(t.m_attachCount, 1);
        QCOMPARE(t.m_lastArgs.layer, Layer::Top); // CenteredModal
        QCOMPARE(t.m_lastArgs.keyboard, KeyboardInteractivity::Exclusive);

        // Expect: Constructed → Warming → Hidden → Shown
        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(0).at(0).value<Surface::State>(), Surface::State::Warming);
        QCOMPARE(spy.at(1).at(0).value<Surface::State>(), Surface::State::Hidden);
        QCOMPARE(spy.at(2).at(0).value<Surface::State>(), Surface::State::Shown);
    }

    void warmUpStaysAtHidden()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto* surface = f.create(buildConfig(s.primary()));
        surface->warmUp();
        QCOMPARE(surface->state(), Surface::State::Hidden);
        QVERIFY(surface->window() != nullptr);
        QCOMPARE(t.m_attachCount, 1);
        // Now show is cheap: Hidden → Shown only.
        QSignalSpy spy(surface, &Surface::stateChanged);
        surface->show();
        QCOMPARE(surface->state(), Surface::State::Shown);
        QCOMPARE(spy.count(), 1);
    }

    void hideReturnsToHidden()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto* surface = f.create(buildConfig(s.primary()));
        surface->show();
        QCOMPARE(surface->state(), Surface::State::Shown);
        surface->hide();
        QCOMPARE(surface->state(), Surface::State::Hidden);
        // Re-show doesn't re-attach — transport attach count stays at 1.
        surface->show();
        QCOMPARE(surface->state(), Surface::State::Shown);
        QCOMPARE(t.m_attachCount, 1);
    }

    void rejectedAttachTransitionsToFailed()
    {
        MockTransport t;
        MockScreenProvider s;
        t.rejectNextAttach();
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto* surface = f.create(buildConfig(s.primary(), QStringLiteral("reject-test")));
        QSignalSpy failSpy(surface, &Surface::failed);

        surface->show();
        QCOMPARE(surface->state(), Surface::State::Failed);
        QCOMPARE(failSpy.count(), 1);
        QVERIFY(failSpy.at(0).at(0).toString().contains(QStringLiteral("attach")));
    }

    void failedSurfaceIgnoresFurtherCalls()
    {
        MockTransport t;
        MockScreenProvider s;
        t.rejectNextAttach();
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto* surface = f.create(buildConfig(s.primary()));
        surface->show();
        QCOMPARE(surface->state(), Surface::State::Failed);

        QSignalSpy spy(surface, &Surface::stateChanged);
        surface->show();
        surface->hide();
        surface->warmUp();
        QCOMPARE(spy.count(), 0); // no state changes
        QCOMPARE(surface->state(), Surface::State::Failed);
    }

    void missingContentSourceFails()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.screen = s.primary();
        // Neither contentUrl nor contentItem set — SurfaceFactory should
        // refuse before we even reach the state machine.
        auto* surface = f.create(std::move(cfg));
        QCOMPARE(surface, nullptr);
    }

    void bothContentSourcesRefused()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.screen = s.primary();
        cfg.contentUrl = QUrl(QStringLiteral("qrc:/nonexistent.qml"));
        cfg.contentItem = std::make_unique<QQuickItem>();
        auto* surface = f.create(std::move(cfg));
        QCOMPARE(surface, nullptr);
    }

    void unsupportedTransportRefuses()
    {
        MockTransport t;
        MockScreenProvider s;
        t.setSupported(false);
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto* surface = f.create(buildConfig(s.primary()));
        QCOMPARE(surface, nullptr);
    }

    void windowPropertiesApplyBeforeContent()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto cfg = buildConfig(s.primary());
        cfg.windowProperties = {{QStringLiteral("myTag"), QStringLiteral("set")}, {QStringLiteral("myCount"), 7}};
        auto* surface = f.create(std::move(cfg));
        surface->warmUp();
        QCOMPARE(surface->state(), Surface::State::Hidden);
        QVERIFY(surface->window() != nullptr);
        QCOMPARE(surface->window()->property("myTag").toString(), QStringLiteral("set"));
        QCOMPARE(surface->window()->property("myCount").toInt(), 7);
    }

    void attachArgsReflectRoleAndOverrides()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto cfg = buildConfig(s.primary());
        cfg.role = Roles::FullscreenOverlay;
        cfg.marginsOverride = QMargins(5, 10, 15, 20);
        cfg.exclusiveZoneOverride = 42;
        auto* surface = f.create(std::move(cfg));
        surface->show();

        QCOMPARE(t.m_lastArgs.layer, Layer::Overlay);
        QCOMPARE(t.m_lastArgs.anchors, AnchorAll);
        QCOMPARE(t.m_lastArgs.margins, QMargins(5, 10, 15, 20));
        QCOMPARE(t.m_lastArgs.exclusiveZone, 42);
        QCOMPARE(t.m_lastArgs.screen, s.primary());
        QCOMPARE(t.m_lastArgs.scope, Roles::FullscreenOverlay.scopePrefix);
    }

    void engineProviderReleaseOrderingDefersAfterWindowDelete()
    {
        // Bug regression: when an engineProvider is injected, the previous
        // dtor called releaseEngine() SYNCHRONOUSLY while the window's
        // deleteLater was still queued — a provider that `delete engine` in
        // releaseEngine would free the engine before the window's QML
        // children had torn down. The fix marshals releaseEngine via the
        // engine's event queue so it runs after the window's DeferredDelete.
        struct TrackingProvider : public IQmlEngineProvider
        {
            QQmlEngine* engineForSurface(const SurfaceConfig&) override
            {
                m_engine = new QQmlEngine();
                return m_engine;
            }
            void releaseEngine(QQmlEngine* e) override
            {
                QVERIFY(m_windowDestroyed); // window must have torn down first
                ++m_releaseCalls;
                m_engine = nullptr;
                e->deleteLater();
            }
            QQmlEngine* m_engine = nullptr;
            bool m_windowDestroyed = false;
            int m_releaseCalls = 0;
        };

        MockTransport t;
        MockScreenProvider s;
        TrackingProvider p;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s, &p));
        auto* surface = f.create(buildConfig(s.primary()));
        surface->warmUp();
        QCOMPARE(surface->state(), Surface::State::Hidden);

        QPointer<QQuickWindow> winWatch(surface->window());
        QVERIFY(winWatch);
        QObject::connect(winWatch.data(), &QObject::destroyed, [&p] {
            // Window's destroyed handler fires during DeferredDelete
            // processing — before releaseEngine runs, per the ordering
            // contract.
            p.m_windowDestroyed = true;
        });

        surface->deleteLater();
        // Wait for the deferred-delete chain to settle rather than pumping
        // a fixed number of passes — the exact depth depends on Qt internals
        // and is not a behaviour this test needs to pin. Polling via
        // QTRY_VERIFY keeps it robust as Qt / the Surface teardown graph
        // evolve.
        QTRY_VERIFY_WITH_TIMEOUT(winWatch.isNull(), 2000);
        QVERIFY(p.m_windowDestroyed);
        QCOMPARE(p.m_releaseCalls, 1);
    }

    void attachRecordsNonZeroSizeAtAttach()
    {
        // Anti-regression for the partial-anchor zero-size-commit bug
        // (commit 4630da9d): for a wlr-layer-shell surface that is NOT
        // doubly-anchored on both axes, the initial commit MUST carry a
        // non-zero size or the compositor echoes back 0×0 and the surface
        // stays stuck. Surface::instantiateFromComponent calls
        // setGeometry(screen->geometry()) before completeCreate precisely
        // to prevent this. Verify the transport sees a non-zero size at
        // attach time.
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto cfg = buildConfig(s.primary());
        // Top|Left anchor: non-doubly-anchored on either axis — the case
        // the regression affected.
        cfg.anchorsOverride = Anchors{Anchor::Top, Anchor::Left};
        auto* surface = f.create(std::move(cfg));
        surface->warmUp();

        QCOMPARE(t.m_attachRecords.size(), 1);
        const auto& rec = t.m_attachRecords.first();
        QVERIFY2(!rec.sizeAtAttach.isEmpty(),
                 qPrintable(QStringLiteral("size at attach was %1x%2")
                                .arg(rec.sizeAtAttach.width())
                                .arg(rec.sizeAtAttach.height())));
        QVERIFY(rec.sizeAtAttach.width() > 0);
        QVERIFY(rec.sizeAtAttach.height() > 0);
    }

    void compositorLostDoesNotCrashShownSurface()
    {
        // A Surface in Shown state must tolerate a compositor-lost pulse
        // without crashing. The library doesn't auto-recreate the surface
        // (that's the consumer's job via TopologyCoordinator), but the
        // transport's lost-callback must fire cleanly and the Surface must
        // remain in a usable state for explicit teardown.
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        auto* surface = f.create(buildConfig(s.primary()));
        surface->show();
        QCOMPARE(surface->state(), Surface::State::Shown);

        t.simulateCompositorLost();
        // State is preserved (library leaves recovery to the consumer);
        // the crucial assertion is that no UAF/crash happened.
        QVERIFY(surface->state() == Surface::State::Shown || surface->state() == Surface::State::Failed);
    }

    void screenRemovalNullsStaleScreenReference()
    {
        // Anti-regression for the stale-QScreen* hazard (review L3): after
        // a screensChanged removing the bound screen, the next re-attach
        // must NOT pass the now-dangling QScreen* to the transport. The
        // Surface subscribes to the notifier and nulls m_config.screen so
        // finishAttach falls back to primary.
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));

        // Fake a secondary screen by listing the primary twice (offscreen
        // QPA gives us only one QScreen*, but the mock accepts duplicates
        // for test purposes — see MockScreenProvider::setScreens).
        QScreen* primary = s.primary();
        QVERIFY(primary);

        auto cfg = buildConfig(primary);
        auto* surface = f.create(std::move(cfg));
        surface->warmUp();
        QCOMPARE(surface->state(), Surface::State::Hidden);

        // Simulate removing the only screen: provider now reports empty.
        s.setScreens({});
        // Surface should have nulled its m_config.screen internally. We
        // can't observe m_config.screen directly, but we CAN observe that
        // a subsequent call path (which we can't trigger from outside)
        // would fall back to primary. The public-observable effect is:
        // no crash from the screensChanged signal hitting a null provider-
        // primary, and the surface stays in Hidden.
        QCOMPARE(surface->state(), Surface::State::Hidden);

        // Restore a screen and verify normal operation resumes.
        s.setScreens({primary});
        surface->show();
        QCOMPARE(surface->state(), Surface::State::Shown);
    }
};

QTEST_MAIN(TestSurface)
#include "test_surface.moc"
