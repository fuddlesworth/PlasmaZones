// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/JsonSurfaceStore.h>
#include <PhosphorLayer/defaults/NoOpSurfaceAnimator.h>
#include <PhosphorLayer/defaults/XdgToplevelTransport.h>

#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorLayer;

class TestExtensions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ── JsonSurfaceStore ───────────────────────────────────────────────

    void jsonStore_roundTripsKeysAndValues()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("store.json"));

        {
            JsonSurfaceStore s(path);
            QVERIFY(!s.has(QStringLiteral("k1")));
            QJsonObject v;
            v.insert(QStringLiteral("x"), 42);
            v.insert(QStringLiteral("s"), QStringLiteral("hi"));
            QVERIFY(s.save(QStringLiteral("k1"), v));
            QVERIFY(s.has(QStringLiteral("k1")));
        }
        // Re-open a fresh instance — data must survive the round-trip.
        {
            JsonSurfaceStore s(path);
            QVERIFY(s.has(QStringLiteral("k1")));
            const auto v = s.load(QStringLiteral("k1"));
            QCOMPARE(v.value(QStringLiteral("x")).toInt(), 42);
            QCOMPARE(v.value(QStringLiteral("s")).toString(), QStringLiteral("hi"));
        }
    }

    void jsonStore_removeEvicts()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("store.json"));
        JsonSurfaceStore s(path);
        s.save(QStringLiteral("a"), QJsonObject{{QStringLiteral("v"), 1}});
        s.save(QStringLiteral("b"), QJsonObject{{QStringLiteral("v"), 2}});
        QVERIFY(s.has(QStringLiteral("a")));
        s.remove(QStringLiteral("a"));
        QVERIFY(!s.has(QStringLiteral("a")));
        QVERIFY(s.has(QStringLiteral("b")));
    }

    void jsonStore_missingFileReturnsEmpty()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("does-not-exist.json"));
        JsonSurfaceStore s(path);
        QVERIFY(!s.has(QStringLiteral("anything")));
        QCOMPARE(s.load(QStringLiteral("anything")), QJsonObject());
    }

    void jsonStore_corruptFileFallsBackToEmpty()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("corrupt.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ not valid json");
        f.close();
        JsonSurfaceStore s(path);
        // Corrupt file is tolerated — empty state, no crash.
        QVERIFY(!s.has(QStringLiteral("anything")));
    }

    void jsonStore_rejectsLeadingDotDot()
    {
        // Bug regression: the traversal check originally only caught internal
        // "/../" segments and trailing "/..". A leading "../foo.json" passed
        // cleanly because QDir::cleanPath collapses internal "..", but leaves
        // a leading "../" intact. Reject all four forms now: bare "..",
        // "../foo", "/path/../foo", "/path/..".
        //
        // Unsafe paths produce an in-memory-only store (save returns false,
        // load/has return empty) so the behavior is observable from user
        // space without needing a filesystem symlink fixture.
        JsonSurfaceStore s(QStringLiteral("../outside.json"));
        QVERIFY(!s.save(QStringLiteral("k"), QJsonObject{{QStringLiteral("v"), 1}}));
        QVERIFY(!s.has(QStringLiteral("k")));

        JsonSurfaceStore bare(QStringLiteral(".."));
        QVERIFY(!bare.save(QStringLiteral("k"), QJsonObject{{QStringLiteral("v"), 1}}));
    }

    void jsonStore_corruptFileRecoversOnNextSave()
    {
        // Anti-regression: after loading a corrupt file, save() must
        // overwrite the garbage with well-formed JSON rather than
        // refusing to write.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("corrupt.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ not valid json");
        f.close();

        {
            JsonSurfaceStore s(path);
            QVERIFY(s.save(QStringLiteral("k"), QJsonObject{{QStringLiteral("v"), 7}}));
            QVERIFY(s.has(QStringLiteral("k")));
        }
        // Re-open from disk: the corrupt garbage should be gone, replaced
        // by the save()'d content.
        {
            JsonSurfaceStore s(path);
            QVERIFY(s.has(QStringLiteral("k")));
            QCOMPARE(s.load(QStringLiteral("k")).value(QStringLiteral("v")).toInt(), 7);
        }
    }

    // ── XdgToplevelTransport ───────────────────────────────────────────

    void xdgTransport_isSupportedWhenGuiApp()
    {
        XdgToplevelTransport t;
        QVERIFY(t.isSupported());
    }

    void xdgTransport_attachReturnsHandle()
    {
        XdgToplevelTransport t;
        QQuickWindow win;
        const QString originalTitle = win.title();
        TransportAttachArgs args;
        args.screen = qGuiApp->primaryScreen();
        args.layer = Layer::Top;
        args.scope = QStringLiteral("xdg-test");
        auto handle = t.attach(&win, args);
        QVERIFY(handle);
        QCOMPARE(handle->window(), &win);
        // Scope must not leak into the user-facing window title — it's a
        // machine identifier, not a display string.
        QCOMPARE(win.title(), originalTitle);
    }

    void xdgTransport_mutatorsAreBestEffort()
    {
        XdgToplevelTransport t;
        QQuickWindow win;
        TransportAttachArgs args;
        args.screen = qGuiApp->primaryScreen();
        auto handle = t.attach(&win, args);
        QVERIFY(handle);
        // No exceptions, no crashes — ignored mutators silently succeed.
        handle->setLayer(Layer::Background);
        handle->setExclusiveZone(100);
        handle->setKeyboardInteractivity(KeyboardInteractivity::Exclusive);
        handle->setMargins(QMargins(10, 20, 30, 40));
        handle->setAnchors(Anchors{Anchor::Top, Anchor::Left});
    }

    void xdgTransport_setMarginsDoesNotMoveWindow()
    {
        // Anti-regression: an earlier implementation called
        // QWindow::setPosition from setMargins(), which is a no-op on every
        // major Wayland compositor but gave consumers false feedback. Verify
        // the mutator leaves window position untouched.
        XdgToplevelTransport t;
        QQuickWindow win;
        win.setPosition(100, 200);
        TransportAttachArgs args;
        args.screen = qGuiApp->primaryScreen();
        auto handle = t.attach(&win, args);
        QVERIFY(handle);
        handle->setMargins(QMargins(50, 60, 70, 80));
        QCOMPARE(win.position(), QPoint(100, 200));
    }

    void xdgTransport_rejectsNullWindow()
    {
        XdgToplevelTransport t;
        auto handle = t.attach(nullptr, TransportAttachArgs{});
        QCOMPARE(handle, nullptr);
    }

    void xdgTransport_lateRegistrantAfterFireInvokesImmediately()
    {
        // Anti-regression for the symmetry-with-PhosphorShellTransport fix:
        // callbacks registered AFTER the broadcaster has already fired must
        // invoke immediately so a late consumer still sees the event.
        //
        // We can't exit the app in a unit test, but aboutToQuit is a plain
        // Qt signal — invoke it via QMetaObject to trigger the transport's
        // internal hook without tearing down QCoreApplication.
        XdgToplevelTransport t;
        int runs = 0;
        t.addCompositorLostCallback([&] {
            ++runs;
        });
        QCOMPARE(runs, 0);

        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "aboutToQuit"));
        QCOMPARE(runs, 1);

        // Late registrant fires synchronously.
        int lateRuns = 0;
        t.addCompositorLostCallback([&] {
            ++lateRuns;
        });
        QCOMPARE(lateRuns, 1);
    }

    void xdgTransport_nullCallbackIsIgnored()
    {
        XdgToplevelTransport t;
        t.addCompositorLostCallback(nullptr);
        // No crash, no observable effect when fired.
        QVERIFY(QMetaObject::invokeMethod(qGuiApp, "aboutToQuit"));
    }

    // ── NoOpSurfaceAnimator ────────────────────────────────────────────

    void noOpAnimator_firesCompletionSynchronously()
    {
        NoOpSurfaceAnimator a;
        QQuickItem item;
        bool showDone = false, hideDone = false;
        a.beginShow(nullptr, &item, [&] {
            showDone = true;
        });
        QVERIFY(showDone); // synchronous
        a.beginHide(nullptr, &item, [&] {
            hideDone = true;
        });
        QVERIFY(hideDone);
        a.cancel(nullptr); // no-throw
    }

    void noOpAnimator_tolleratesNullCallback()
    {
        NoOpSurfaceAnimator a;
        QQuickItem item;
        a.beginShow(nullptr, &item, nullptr);
        a.beginHide(nullptr, &item, nullptr);
    }
};

QTEST_MAIN(TestExtensions)
#include "test_extensions.moc"
