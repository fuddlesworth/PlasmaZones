// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceIconTheme/IconImageProvider.h>
#include <PhosphorServiceIconTheme/IconThemeResolver.h>
#include <PhosphorServiceIconTheme/QmlRegistration.h>

#include <QImage>
#include <QSize>
#include <QtEndian>
#include <QtTest/QtTest>

using PhosphorServiceIconTheme::IconImageProvider;
using PhosphorServiceIconTheme::IconThemeResolver;

class TestImageProvider : public QObject
{
    Q_OBJECT

private:
    IconImageProvider provider;

private Q_SLOTS:
    // Pin the URL host segment as part of the public ABI. The Phase
    // 2.0 rename moved this from "phosphor-services" to
    // "phosphor-service-icontheme"; any future regression that flips
    // it back should fail this test, not surface as a silent
    // "provider not found" at runtime.
    void urlHostIsPinned()
    {
        QCOMPARE(QString::fromLatin1(PhosphorServiceIconTheme::imageProviderUrlHost()),
                 QStringLiteral("phosphor-service-icontheme"));
    }

    // setImage / requestImage round-trip on a plain id.
    void plainKeyRoundTrip()
    {
        QImage src(8, 8, QImage::Format_ARGB32);
        src.fill(Qt::red);
        const QString id = QStringLiteral("plain-id");
        IconImageProvider::setImage(id, src);

        QSize sz;
        const QImage out = provider.requestImage(id, &sz, QSize());
        QVERIFY(!out.isNull());
        QCOMPARE(sz, QSize(8, 8));
        QCOMPARE(out.pixel(0, 0), src.pixel(0, 0));

        IconImageProvider::clearImage(id);
        QSize after;
        QVERIFY(provider.requestImage(id, &after, QSize()).isNull());
        QCOMPARE(after, QSize(0, 0));
    }

    // The provider strips ?v=cacheKey before lookup so that successive
    // setImage calls under the same id update the URL but reuse the
    // same registry entry.
    void cacheBustingQueryStringIsStripped()
    {
        QImage src(4, 4, QImage::Format_ARGB32);
        src.fill(Qt::green);
        const QString id = QStringLiteral("cache-bust");
        IconImageProvider::setImage(id, src);

        QSize sz;
        const QImage out = provider.requestImage(id + QStringLiteral("?v=12345"), &sz, QSize());
        QVERIFY(!out.isNull());
        QCOMPARE(sz, QSize(4, 4));

        IconImageProvider::clearImage(id);
    }

    // SNI's publisher key uses '|' as the service/path separator;
    // QUrl percent-encodes that to %7C in transit. The provider must
    // decode the path-form key before lookup so the publish-side key
    // matches the request-side lookup.
    void percentEncodedKeyDecodes()
    {
        QImage src(2, 2, QImage::Format_ARGB32);
        src.fill(Qt::blue);
        const QString id = QStringLiteral(":1.42/StatusNotifierItem|");
        IconImageProvider::setImage(id, src);

        QSize sz;
        const QImage out = provider.requestImage(QStringLiteral(":1.42%2FStatusNotifierItem%7C"), &sz, QSize());
        QVERIFY(!out.isNull());

        IconImageProvider::clearImage(id);
    }

    // Direct clearImage round-trip. Distinct from the setImage(null)
    // path because publishers can also invoke clearImage explicitly
    // (e.g. on item removal); pin both code paths separately so a
    // future refactor that diverges them is caught.
    void explicitClearImageRemovesEntry()
    {
        QImage src(2, 2, QImage::Format_ARGB32);
        src.fill(Qt::cyan);
        const QString id = QStringLiteral("explicit-clear");
        IconImageProvider::setImage(id, src);

        QSize sz;
        QVERIFY(!provider.requestImage(id, &sz, QSize()).isNull());

        IconImageProvider::clearImage(id);
        QSize after;
        QVERIFY(provider.requestImage(id, &after, QSize()).isNull());
        QCOMPARE(after, QSize(0, 0));

        // Clearing an already-cleared id is a no-op (idempotent).
        IconImageProvider::clearImage(id);
        QVERIFY(provider.requestImage(id, &after, QSize()).isNull());
    }

    // Passing a null QImage to setImage is documented to clear the
    // entry (equivalent to clearImage). Pin that contract so the SNI
    // publisher's "no-icon" branch stays correct.
    void setImageNullClearsEntry()
    {
        QImage src(2, 2, QImage::Format_ARGB32);
        src.fill(Qt::yellow);
        const QString id = QStringLiteral("null-clear");
        IconImageProvider::setImage(id, src);

        QSize sz;
        QVERIFY(!provider.requestImage(id, &sz, QSize()).isNull());

        IconImageProvider::setImage(id, QImage());
        QSize after;
        QVERIFY(provider.requestImage(id, &after, QSize()).isNull());
    }

    // Missing id returns a null QImage and a zero-size out parameter
    // (rather than a stale value or a crash).
    void unknownIdReturnsNullAndZeroSize()
    {
        QSize sz(99, 99);
        const QImage out = provider.requestImage(QStringLiteral("not-registered"), &sz, QSize());
        QVERIFY(out.isNull());
        QCOMPARE(sz, QSize(0, 0));
    }

    // decodePixmaps handles an empty list cleanly (the SNI item path
    // hits this when an app advertises no IconPixmap and only an
    // IconName).
    void decodePixmapsEmptyListReturnsNullImage()
    {
        const QImage out = IconThemeResolver::decodePixmaps({}, 24);
        QVERIFY(out.isNull());
    }

    // decodePixmaps selects the pixmap closest to the requested size
    // and converts network-order ARGB to host-order so the resulting
    // QImage renders correctly. Constructing a 2x2 ARGB blob is the
    // minimum to exercise the byte-swap path.
    void decodePixmapsPicksClosestSizeAndDecodes()
    {
        QByteArray bytes(2 * 2 * 4, '\0');
        // Network-order ARGB = 0xFFAABBCC for every pixel.
        for (int i = 0; i < 4; ++i) {
            const int base = i * 4;
            // 0xAARRGGBB on the wire is big-endian; serialize manually.
            bytes[base + 0] = static_cast<char>(0xFFu); // A
            bytes[base + 1] = static_cast<char>(0xAAu); // R
            bytes[base + 2] = static_cast<char>(0xBBu); // G
            bytes[base + 3] = static_cast<char>(0xCCu); // B
        }
        QList<QPair<QSize, QByteArray>> pixmaps;
        pixmaps.append({QSize(2, 2), bytes});

        const QImage out = IconThemeResolver::decodePixmaps(pixmaps, 24);
        QVERIFY(!out.isNull());
        QCOMPARE(out.size(), QSize(2, 2));
        QCOMPARE(out.pixelColor(0, 0), QColor(0xAA, 0xBB, 0xCC, 0xFF));
    }

    // decodePixmaps rejects pixmaps whose declared dimensions exceed
    // the security cap (kMaxIconDim = 4096 in the impl). Without the
    // guard a hostile app could cause an enormous allocation.
    void decodePixmapsRejectsOversizedDeclaredSize()
    {
        QList<QPair<QSize, QByteArray>> pixmaps;
        // Bytes deliberately small; the cap should reject before
        // reading anything.
        pixmaps.append({QSize(99999, 99999), QByteArray(16, '\0')});
        const QImage out = IconThemeResolver::decodePixmaps(pixmaps, 32);
        QVERIFY(out.isNull());
    }

    // decodePixmaps drops a pixmap whose declared dimensions don't
    // match its byte-count: declared 4x4 ARGB = 64 bytes, but only 8
    // supplied. The function must not over-read.
    void decodePixmapsRejectsShortBuffer()
    {
        QList<QPair<QSize, QByteArray>> pixmaps;
        pixmaps.append({QSize(4, 4), QByteArray(8, '\0')});
        const QImage out = IconThemeResolver::decodePixmaps(pixmaps, 16);
        QVERIFY(out.isNull());
    }

    // setImage rejects ids containing '?'; the lookup side strips at the
    // first '?' for the cache-bust query string, so a published id with
    // '?' would be silently unreachable. Verify the rejection is
    // targeted and a plain id still works.
    void setImageRejectsIdsWithQuestionMark()
    {
        QImage src(4, 4, QImage::Format_ARGB32);
        src.fill(Qt::green);
        const QString badId = QStringLiteral("a?b");
        IconImageProvider::setImage(badId, src);

        QSize sz;
        const QImage lookup = provider.requestImage(badId, &sz, QSize());
        QVERIFY2(lookup.isNull(), "lookup must miss a rejected id");

        // Verify a plain id still publishes and resolves.
        const QString goodId = QStringLiteral("plain-id-after-reject");
        IconImageProvider::setImage(goodId, src);
        const QImage hit = provider.requestImage(goodId, &sz, QSize());
        QCOMPARE(hit.size(), src.size());
        IconImageProvider::clearImage(goodId);
    }
};

QTEST_MAIN(TestImageProvider)
#include "test_imageprovider.moc"
