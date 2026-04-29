// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationQml/PhosphorAnimatedReal.h>
#include <PhosphorAnimationQml/PhosphorProfile.h>
#include <PhosphorAnimationQml/QtQuickClockManager.h>

#include <QGuiApplication>
#include <QObject>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace PhosphorAnimation;

/**
 * @brief Real-window beforeSynchronizing integration test.
 *
 * Every existing PhosphorAnimatedValue test calls `advance()` manually to
 * step the underlying `AnimatedValue<T>` — which validates the typed
 * surface but bypasses the production auto-advance wire (Qt's
 * `QQuickWindow::beforeSynchronizing`). This test exercises the real
 * signal path: a shown QQuickWindow drives `valueChanged` emits during
 * its frame cycle without any manual `advance()`.
 *
 * Software backend chosen because:
 *   - Headless CI typically lacks an OpenGL context.
 *   - `QSGRendererInterface::Software` works offscreen + with the
 *     `offscreen` QPA without a GPU driver.
 *   - The render-loop / signal-emission contract of QQuickWindow is
 *     identical across SG backends, so software is a faithful proxy
 *     for the production OpenGL/Vulkan paths.
 *
 * Without this test, a regression that broke the
 * `setWindow → connect(beforeSynchronizing) → onSync → advance()` chain
 * would pass every existing test (manual advance still works) and only
 * surface as "animations don't move" in interactive use.
 */
class TestAnimatedValueRealWindow : public QObject
{
    Q_OBJECT

private:
    /// Set up the software SG backend before any QQuickWindow is
    /// constructed. Must run BEFORE QGuiApplication is fully initialised
    /// — calling it from `initTestCase` is the documented hook (per
    /// QQuickWindow class docs and Qt SG backend selection).
    static void selectSoftwareBackend()
    {
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    }

    /// Per-test clock manager. PhosphorAnimatedValueBase::resolveClock
    /// reads through `QtQuickClockManager::defaultManager()` — we
    /// publish this instance in init() so the QML wrappers under test
    /// resolve through a fresh fixture-owned manager. Phase A3 of the
    /// architecture refactor retired `QtQuickClockManager::instance()`.
    std::unique_ptr<QtQuickClockManager> m_clockManager;

private Q_SLOTS:

    void initTestCase()
    {
        // Software backend lets us run on `offscreen` QPA without GPU
        // drivers. Doing this in initTestCase is early enough — the
        // first QQuickWindow construction below picks it up.
        selectSoftwareBackend();
    }

    void init()
    {
        m_clockManager = std::make_unique<QtQuickClockManager>();
        QtQuickClockManager::setDefaultManager(m_clockManager.get());
    }

    void cleanup()
    {
        QtQuickClockManager::setDefaultManager(nullptr);
        m_clockManager.reset();
    }

    /// A shown QQuickWindow must drive `valueChanged` emits via its
    /// real `beforeSynchronizing` signal — no manual `advance()` calls.
    /// Confirms the production auto-advance wire works end-to-end.
    void testValueChangedFiresFromBeforeSynchronizing()
    {
        auto window = std::make_unique<QQuickWindow>();
        window->resize(50, 50);
        window->show();
        // qWaitForWindowExposed posts a paint event; the window needs
        // to actually be exposed before its render loop will start
        // emitting beforeSynchronizing. On the software backend under
        // offscreen QPA, this wraps to a synchronous frame schedule.
        if (!QTest::qWaitForWindowExposed(window.get(), 2000)) {
            QSKIP("Window did not become exposed within 2s — software SG backend / offscreen QPA unavailable");
        }

        PhosphorAnimatedReal a;
        a.setWindow(window.get());

        // Profile with a non-trivial duration so multiple frames land
        // before completion; gives the auto-advance wire time to fire
        // valueChanged at least once between start and finish.
        PhosphorProfile p;
        p.setDuration(200.0);
        a.setProfile(p);

        QSignalSpy valueSpy(&a, &PhosphorAnimatedReal::valueChanged);
        QVERIFY(a.start(0.0, 1.0));
        QVERIFY(a.isAnimating());

        // Wait for at least one frame's worth of beforeSynchronizing to
        // tick the animation. The render loop fires the signal as part
        // of normal frame rendering — under offscreen + software it
        // still ticks at the platform's nominal rate. A 1-second
        // timeout is generous compared to the 16 ms frame interval at
        // 60 Hz. If the wire is broken, valueChanged never fires and
        // the spy.wait times out.
        QVERIFY2(valueSpy.wait(1000),
                 "valueChanged did not fire from a real window's beforeSynchronizing — auto-advance wire broken");

        // The animation eventually completes via the same wire. Cap
        // wait at 2 s — well above the 200 ms profile duration plus
        // any frame-scheduling slack.
        QTRY_VERIFY_WITH_TIMEOUT(!a.isAnimating(), 2000);
        QVERIFY(a.isComplete());
        QCOMPARE(a.value(), 1.0);
    }

    /// Tearing down the window MUST cancel the in-flight animation —
    /// the `handleWindowDestroying` hook routes `cancel()` through
    /// before Qt drops the QtQuickClock. Without this, the AnimatedValue's
    /// MotionSpec retains a raw clock pointer that UAFs on the next
    /// (manual) advance.
    void testWindowDestroyCancelsInFlightAnimation()
    {
        auto window = std::make_unique<QQuickWindow>();
        window->resize(50, 50);
        window->show();
        if (!QTest::qWaitForWindowExposed(window.get(), 2000)) {
            QSKIP("Window did not become exposed — software SG backend unavailable");
        }

        PhosphorAnimatedReal a;
        a.setWindow(window.get());
        PhosphorProfile p;
        p.setDuration(2000.0); // long enough that we destroy mid-flight
        a.setProfile(p);
        QVERIFY(a.start(0.0, 1.0));
        QVERIFY(a.isAnimating());

        // Destroy the window — `handleWindowDestroying` cancels the
        // animation synchronously (DirectConnection) before the
        // QtQuickClock evicts.
        window.reset();

        QVERIFY2(!a.isAnimating(), "in-flight animation was not cancelled when window destroyed");
        QVERIFY(a.window() == nullptr);
        // A subsequent manual advance is safe — the clock pointer was
        // dropped via cancel() rather than left dangling. We don't
        // assert specific value semantics here; the assertion is "no
        // crash".
        a.advance();
    }
};

QTEST_MAIN(TestAnimatedValueRealWindow)
#include "test_animatedvalue_realwindow.moc"
