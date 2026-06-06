// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceIconTheme/IconThemeResolver.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using PhosphorServiceIconTheme::IconThemeResolver;

class TestIconThemeResolver : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir fixtureRoot;
    QString themeRoot;

    // Build a minimal fixture theme tree on disk so the resolver has
    // something deterministic to walk. Real-system icon themes are
    // both unpredictable AND huge, the fixture keeps the test
    // hermetic and < 1ms.
    void writeFixture()
    {
        QVERIFY(fixtureRoot.isValid());
        themeRoot = fixtureRoot.path() + QStringLiteral("/icons");
        QDir().mkpath(themeRoot + QStringLiteral("/testtheme/16x16/apps"));
        QDir().mkpath(themeRoot + QStringLiteral("/testtheme/32x32/apps"));
        QDir().mkpath(themeRoot + QStringLiteral("/testtheme/scalable/apps"));
        QDir().mkpath(themeRoot + QStringLiteral("/hicolor/22x22/apps"));

        // index.theme, declares the three directories and inherits hicolor.
        QFile index(themeRoot + QStringLiteral("/testtheme/index.theme"));
        QVERIFY(index.open(QIODevice::WriteOnly));
        index.write(R"([Icon Theme]
Name=TestTheme
Inherits=hicolor
Directories=16x16/apps,32x32/apps,scalable/apps

[16x16/apps]
Size=16
Type=Fixed
Context=Applications

[32x32/apps]
Size=32
Type=Fixed
Context=Applications

[scalable/apps]
Size=32
MinSize=8
MaxSize=512
Type=Scalable
Context=Applications
)");
        index.close();

        // Minimal hicolor index for the inheritance test. Real-world
        // hicolor always exists; we recreate just enough of it that
        // the resolver finds our `inherited-only` test icon.
        QFile hicolorIndex(themeRoot + QStringLiteral("/hicolor/index.theme"));
        QVERIFY(hicolorIndex.open(QIODevice::WriteOnly));
        hicolorIndex.write(R"([Icon Theme]
Name=Hicolor
Directories=22x22/apps

[22x22/apps]
Size=22
Type=Fixed
Context=Applications
)");
        hicolorIndex.close();

        // Synthesise a 1×1 PNG for each "icon". We don't need to
        // render, just need QImage::isNull() to be false after
        // resolution.
        QImage red(1, 1, QImage::Format_ARGB32);
        red.fill(Qt::red);
        QVERIFY(red.save(themeRoot + QStringLiteral("/testtheme/16x16/apps/test-app.png")));
        QVERIFY(red.save(themeRoot + QStringLiteral("/testtheme/32x32/apps/test-app.png")));
        QVERIFY(red.save(themeRoot + QStringLiteral("/hicolor/22x22/apps/inherited-only.png")));
    }

    // Point Qt's standard-paths machinery at our fixture so the
    // resolver picks it up via GenericDataLocation.
    void redirectXdgDataDirs()
    {
        qputenv("XDG_DATA_HOME", fixtureRoot.path().toUtf8());
        qputenv("XDG_DATA_DIRS", fixtureRoot.path().toUtf8());
    }

private Q_SLOTS:
    void initTestCase()
    {
        writeFixture();
        redirectXdgDataDirs();
    }

    void resolverIsSingleton()
    {
        auto* r = IconThemeResolver::instance();
        QVERIFY(r);
        QCOMPARE(r, IconThemeResolver::instance());
    }

    void setThemeOverrideTakesEffect()
    {
        auto* r = IconThemeResolver::instance();
        r->setThemeName(QStringLiteral("testtheme"));
        QCOMPARE(r->themeName(), QStringLiteral("testtheme"));
    }

    void resolvesExactSizeMatchInTheme()
    {
        auto* r = IconThemeResolver::instance();
        r->setThemeName(QStringLiteral("testtheme"));

        const auto img = r->iconForName(QStringLiteral("test-app"), 16);
        QVERIFY(!img.isNull());
    }

    void resolvesViaInheritedHicolor()
    {
        auto* r = IconThemeResolver::instance();
        r->setThemeName(QStringLiteral("testtheme"));

        // inherited-only exists ONLY in hicolor; testtheme inherits it.
        const auto img = r->iconForName(QStringLiteral("inherited-only"), 22);
        QVERIFY(!img.isNull());
    }

    void returnsEmptyForUnknownIcon()
    {
        auto* r = IconThemeResolver::instance();
        r->setThemeName(QStringLiteral("testtheme"));

        const auto img = r->iconForName(QStringLiteral("does-not-exist-anywhere"), 24);
        QVERIFY(img.isNull());
    }

    void emptyNameReturnsEmpty()
    {
        auto* r = IconThemeResolver::instance();
        QVERIFY(r->iconForName(QString(), 16).isNull());
    }

    void zeroSizeReturnsEmpty()
    {
        auto* r = IconThemeResolver::instance();
        QVERIFY(r->iconForName(QStringLiteral("test-app"), 0).isNull());
    }

    void nonPositiveScaleReturnsEmpty()
    {
        // Pins the `scale <= 0` guard added in the Phase-2.0 PR. Both
        // zero and negative scale should short-circuit to an empty
        // QImage so the cache key isn't polluted with sentinel scales
        // and the distance math at the search layer doesn't underflow.
        auto* r = IconThemeResolver::instance();
        r->setThemeName(QStringLiteral("testtheme"));
        QVERIFY(r->iconForName(QStringLiteral("test-app"), 16, 0).isNull());
        QVERIFY(r->iconForName(QStringLiteral("test-app"), 16, -1).isNull());
    }

    // setThemeName rejects path-traversal patterns at the API boundary.
    // The accepted name is concatenated into filesystem paths
    // (root + "/" + themeName + "/index.theme") so a value like
    // "../etc" would let the resolver probe arbitrary files.
    void setThemeNameRejectsUnsafeNames()
    {
        auto* r = IconThemeResolver::instance();
        // First, establish a known-good theme as the baseline.
        r->setThemeName(QStringLiteral("testtheme"));
        const QString before = r->themeName();
        QCOMPARE(before, QStringLiteral("testtheme"));

        // Each of these should be rejected and leave themeName()
        // unchanged. Empty is allowed (the "fall back to detected
        // theme" path), so it's not part of the unsafe set.
        r->setThemeName(QStringLiteral("foo/bar"));
        QCOMPARE(r->themeName(), before);

        r->setThemeName(QStringLiteral("../etc"));
        QCOMPARE(r->themeName(), before);

        r->setThemeName(QStringLiteral("hicolor\\..\\evil"));
        QCOMPARE(r->themeName(), before);

        QString withNul = QStringLiteral("name");
        withNul.append(QChar(QChar::Null));
        withNul.append(QStringLiteral("evil"));
        r->setThemeName(withNul);
        QCOMPARE(r->themeName(), before);
    }
};

QTEST_MAIN(TestIconThemeResolver)
#include "test_iconthemeresolver.moc"
