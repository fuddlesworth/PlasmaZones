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
        TransportAttachArgs args;
        args.screen = qGuiApp->primaryScreen();
        args.layer = Layer::Top;
        args.scope = QStringLiteral("xdg-test");
        auto handle = t.attach(&win, args);
        QVERIFY(handle);
        QCOMPARE(handle->window(), &win);
        QCOMPARE(win.title(), QStringLiteral("xdg-test"));
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
    }

    void xdgTransport_rejectsNullWindow()
    {
        XdgToplevelTransport t;
        auto handle = t.attach(nullptr, TransportAttachArgs{});
        QCOMPARE(handle, nullptr);
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
