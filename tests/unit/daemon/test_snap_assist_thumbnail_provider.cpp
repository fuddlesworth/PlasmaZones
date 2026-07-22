// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins the SnapAssistThumbnailProvider contract: bounded LRU cache, URL
// uniqueness across inserts, eviction-tied URL state, and the brace-handed
// braced-UUID lookup path.

#include "daemon/rendering/snapassistthumbnailprovider.h"

#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QUuid>

#include <atomic>
#include <thread>

using PlasmaZones::SnapAssistThumbnailProvider;

namespace {

QImage solid(int w, int h, QColor color)
{
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(color);
    return img;
}

QString brace(const QUuid& u)
{
    return u.toString(); // QUuid::WithBraces is the default — matches EffectWindow::internalId().toString()
}

} // namespace

class TestSnapAssistThumbnailProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void insertAndRequestRoundTrip();
    void requestImageMissingReturnsNull();
    void urlForUnknownHandleEmpty();
    void emptyHandleRejected();
    void nullImageRejected();
    void reinsertProducesNewUrl();
    void lruEvictionAtCapacity();
    void evictionDropsUrlState();
    void requestImageHonoursRequestedSize();
    void bracedUuidHandleLookup();
    void concurrentReadWriteIsRaceFree();
};

void TestSnapAssistThumbnailProvider::insertAndRequestRoundTrip()
{
    SnapAssistThumbnailProvider p;
    const QString h = brace(QUuid::createUuid());
    const QImage in = solid(64, 48, Qt::red);
    const QString url = p.insert(h, in);
    QVERIFY(!url.isEmpty());

    QSize sz;
    // requestImage receives `<handle>/<gen>` as id (no scheme/host)
    const QString id = url.section(QLatin1Char('/'), 3, -1);
    const QImage out = p.requestImage(id, &sz, QSize());
    QCOMPARE(sz, in.size());
    QCOMPARE(out.size(), in.size());
    QCOMPARE(out.pixelColor(0, 0), QColor(Qt::red));
}

void TestSnapAssistThumbnailProvider::requestImageMissingReturnsNull()
{
    SnapAssistThumbnailProvider p;
    QSize sz(7, 7);
    const QImage out = p.requestImage(QStringLiteral("{nonexistent}/1"), &sz, QSize());
    QVERIFY(out.isNull());
    QCOMPARE(sz, QSize(0, 0));
}

void TestSnapAssistThumbnailProvider::urlForUnknownHandleEmpty()
{
    SnapAssistThumbnailProvider p;
    QVERIFY(p.urlFor(QStringLiteral("{unseen}")).isEmpty());
    QVERIFY(p.urlFor(QString()).isEmpty()); // empty handle short-circuits
}

void TestSnapAssistThumbnailProvider::emptyHandleRejected()
{
    SnapAssistThumbnailProvider p;
    QVERIFY(p.insert(QString(), solid(4, 4, Qt::blue)).isEmpty());
}

void TestSnapAssistThumbnailProvider::nullImageRejected()
{
    SnapAssistThumbnailProvider p;
    const QString h = brace(QUuid::createUuid());
    QVERIFY(p.insert(h, QImage()).isEmpty());
    QVERIFY(p.urlFor(h).isEmpty()); // nothing landed in the cache
}

void TestSnapAssistThumbnailProvider::reinsertProducesNewUrl()
{
    SnapAssistThumbnailProvider p;
    const QString h = brace(QUuid::createUuid());
    const QString u1 = p.insert(h, solid(8, 8, Qt::red));
    const QString u2 = p.insert(h, solid(8, 8, Qt::green));
    QVERIFY(!u1.isEmpty());
    QVERIFY(!u2.isEmpty());
    QVERIFY2(u1 != u2, "Re-inserting the same handle must yield a distinct URL so QML's pixmap cache re-fetches.");
    QCOMPARE(p.urlFor(h), u2); // current URL = latest insert
}

void TestSnapAssistThumbnailProvider::lruEvictionAtCapacity()
{
    SnapAssistThumbnailProvider p;
    const int cap = SnapAssistThumbnailProvider::CacheCapacity;

    QStringList handles;
    handles.reserve(cap + 4);
    for (int i = 0; i < cap; ++i) {
        const QString h = brace(QUuid::createUuid());
        handles.append(h);
        QVERIFY(!p.insert(h, solid(2, 2, Qt::red)).isEmpty());
    }
    // All in cache.
    for (const auto& h : handles) {
        QVERIFY(!p.urlFor(h).isEmpty());
    }

    // Insert 4 more — the 4 oldest must evict.
    QStringList freshHandles;
    for (int i = 0; i < 4; ++i) {
        const QString h = brace(QUuid::createUuid());
        freshHandles.append(h);
        QVERIFY(!p.insert(h, solid(2, 2, Qt::blue)).isEmpty());
    }

    int evicted = 0;
    for (int i = 0; i < 4; ++i) {
        if (p.urlFor(handles[i]).isEmpty()) {
            ++evicted;
        }
    }
    QCOMPARE(evicted, 4); // exactly the four oldest

    // Recent ones still present.
    for (int i = 4; i < cap; ++i) {
        QVERIFY(!p.urlFor(handles[i]).isEmpty());
    }
    for (const auto& h : freshHandles) {
        QVERIFY(!p.urlFor(h).isEmpty());
    }
}

void TestSnapAssistThumbnailProvider::evictionDropsUrlState()
{
    // Regression guard for the unbounded-growth bug fixed alongside this
    // class: per-handle URL state must live inside the cache entry so LRU
    // eviction reclaims it. After eviction, urlFor and requestImage both
    // return empty — there is no out-of-band map preserving the old URL.
    SnapAssistThumbnailProvider p;
    const int cap = SnapAssistThumbnailProvider::CacheCapacity;

    const QString victim = brace(QUuid::createUuid());
    const QString victimUrl = p.insert(victim, solid(2, 2, Qt::red));
    QVERIFY(!victimUrl.isEmpty());

    // Push the victim out by inserting cap+1 fresh handles (the +1 is the
    // one that bumps victim out of the LRU window).
    for (int i = 0; i < cap; ++i) {
        QVERIFY(!p.insert(brace(QUuid::createUuid()), solid(2, 2, Qt::green)).isEmpty());
    }

    QVERIFY(p.urlFor(victim).isEmpty());
    QSize sz(9, 9);
    const QString victimId = victimUrl.section(QLatin1Char('/'), 3, -1);
    QVERIFY(p.requestImage(victimId, &sz, QSize()).isNull());
    QCOMPARE(sz, QSize(0, 0));
}

void TestSnapAssistThumbnailProvider::requestImageHonoursRequestedSize()
{
    SnapAssistThumbnailProvider p;
    const QString h = brace(QUuid::createUuid());
    const QImage in = solid(256, 128, Qt::red);
    const QString url = p.insert(h, in);
    const QString id = url.section(QLatin1Char('/'), 3, -1);

    QSize natural;
    const QImage natural_img = p.requestImage(id, &natural, QSize());
    QCOMPARE(natural, in.size());
    QCOMPARE(natural_img.size(), in.size());

    QSize ignored;
    const QImage scaled = p.requestImage(id, &ignored, QSize(64, 64));
    QVERIFY(scaled.width() <= 64);
    QVERIFY(scaled.height() <= 64);
    // KeepAspectRatio: 256x128 → 64x32 fits inside 64x64 with original 2:1 ratio
    QCOMPARE(scaled.size(), QSize(64, 32));

    // requestedSize larger than natural → no upscale (provider returns natural)
    const QImage notUpscaled = p.requestImage(id, &ignored, QSize(1024, 1024));
    QCOMPARE(notUpscaled.size(), in.size());
}

void TestSnapAssistThumbnailProvider::bracedUuidHandleLookup()
{
    // Compositor handles arrive as braced UUIDs ("{xxxxxxxx-...}"). The
    // provider normalises to the unbraced form internally so the URL path
    // component contains only RFC-3986 unreserved characters and survives
    // QUrl percent-encoding policy changes unchanged. Both braced and
    // unbraced lookups must resolve to the same cache slot.
    SnapAssistThumbnailProvider p;
    const QUuid u = QUuid::createUuid();
    const QString braced = u.toString();
    const QString unbraced = u.toString(QUuid::WithoutBraces);
    QVERIFY(braced.startsWith(QLatin1Char('{')));
    QVERIFY(braced.endsWith(QLatin1Char('}')));

    const QString url = p.insert(braced, solid(4, 4, Qt::yellow));
    QVERIFY2(!url.contains(QLatin1Char('{')) && !url.contains(QLatin1Char('}')),
             "URL must use the unbraced UUID form so QUrl percent-encoding can't break the round-trip.");
    QVERIFY(url.contains(unbraced));

    // Both spellings hit the same slot.
    QCOMPARE(p.urlFor(braced), url);
    QCOMPARE(p.urlFor(unbraced), url);

    const QString id = url.section(QLatin1Char('/'), 3, -1);
    QSize sz;
    QVERIFY(!p.requestImage(id, &sz, QSize()).isNull());
}

void TestSnapAssistThumbnailProvider::concurrentReadWriteIsRaceFree()
{
    // Provider's @c m_mutex serialises @c insert (main thread, on D-Bus
    // dispatch) against @c requestImage / @c urlFor (QML image-loader
    // worker thread). This stress test runs a writer and a reader
    // against the same handles in parallel; under TSan / ASan the
    // expectation is no race report and no torn read of the QImage
    // implicitly-shared refcount.
    SnapAssistThumbnailProvider p;
    const QString h1 = brace(QUuid::createUuid());
    const QString h2 = brace(QUuid::createUuid());
    p.insert(h1, solid(32, 32, Qt::red));
    p.insert(h2, solid(32, 32, Qt::blue));

    constexpr int kIterations = 5000;
    std::atomic<int> validReads{0};
    std::atomic<int> nullReads{0};
    std::atomic<bool> sawWrongSize{false};

    std::thread writer([&p, &h1, &h2]() {
        for (int i = 0; i < kIterations; ++i) {
            const QString& h = (i % 2) ? h1 : h2;
            const Qt::GlobalColor c = (i % 3 == 0) ? Qt::red : ((i % 3 == 1) ? Qt::green : Qt::blue);
            p.insert(h, solid(32, 32, c));
        }
    });

    std::thread reader([&]() {
        for (int i = 0; i < kIterations; ++i) {
            const QString& h = (i % 2) ? h1 : h2;
            const QString u = p.urlFor(h);
            if (u.isEmpty()) {
                ++nullReads;
                continue;
            }
            QSize sz;
            const QString id = u.section(QLatin1Char('/'), 3, -1);
            const QImage img = p.requestImage(id, &sz, QSize());
            if (img.isNull()) {
                // Eviction can race urlFor → requestImage at LRU boundary;
                // the provider's contract is "either the image or a null
                // result," not "always the image." A null reply here is
                // valid; record it separately so a wedge would still show
                // up as zero validReads.
                ++nullReads;
            } else {
                ++validReads;
                if (img.size() != QSize(32, 32)) {
                    sawWrongSize = true;
                }
            }
        }
    });

    writer.join();
    reader.join();

    // QVERIFY/QCOMPARE on the test thread; no calls into QtTest from the
    // worker threads (those macros aren't documented as thread-safe).
    QVERIFY2(!sawWrongSize, "requestImage returned an image with the wrong size — torn read across the mutex.");
    QCOMPARE(validReads + nullReads, kIterations);
    QVERIFY2(validReads.load() > 0,
             "Reader observed zero valid images across 5000 iterations — provider is not serving concurrent readers.");
}

QTEST_MAIN(TestSnapAssistThumbnailProvider)
#include "test_snap_assist_thumbnail_provider.moc"
