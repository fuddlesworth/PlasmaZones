// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <QTest>

using PhosphorEngine::WindowRegistry;

/**
 * Pins WindowContext's effective-context policy — the root-cause fix for the
 * silent float-state flips: per-window mode resolution must read the WINDOW's
 * own desktop/activity, not the screen's current ones, so a per-output desktop
 * switch (or activity switch) crossing a mode boundary cannot flip the
 * effective float answer without a broadcast.
 */
class TestWindowContext : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void effectiveDesktop_data();
    void effectiveDesktop();
    void effectiveActivity_data();
    void effectiveActivity();
    void windowContextLookup();
    void spanReachesEveryListedActivity();
};

void TestWindowContext::effectiveDesktop_data()
{
    QTest::addColumn<int>("virtualDesktop");
    QTest::addColumn<QList<int>>("virtualDesktops");
    QTest::addColumn<int>("screenCurrent");
    QTest::addColumn<int>("expected");

    // The core fix: the window's own desktop wins over the screen's current
    // one. Reverting to screen-current resolution fails this row.
    QTest::newRow("own-desktop-beats-screen-current") << 3 << QList<int>{} << 1 << 3;
    QTest::newRow("own-desktop-equals-screen-current") << 2 << QList<int>{} << 2 << 2;
    // Sticky / unknown (0) falls back to the screen's current desktop.
    QTest::newRow("sticky-falls-back-to-screen-current") << 0 << QList<int>{} << 4 << 4;
    // Multi-desktop span: when the screen currently shows one of the spanned
    // desktops, that one is the live context — preferred over the first entry.
    QTest::newRow("span-prefers-visible-spanned-desktop") << 2 << QList<int>{2, 3} << 3 << 3;
    QTest::newRow("span-first-entry-when-visible") << 2 << QList<int>{2, 3} << 2 << 2;
    // Screen shows a desktop OUTSIDE the span: the window's first desktop wins.
    QTest::newRow("span-offscreen-uses-first-entry") << 2 << QList<int>{2, 3} << 5 << 2;
}

void TestWindowContext::effectiveDesktop()
{
    QFETCH(int, virtualDesktop);
    QFETCH(QList<int>, virtualDesktops);
    QFETCH(int, screenCurrent);
    QFETCH(int, expected);

    WindowRegistry::WindowContext ctx;
    ctx.virtualDesktop = virtualDesktop;
    ctx.virtualDesktops = virtualDesktops;
    QCOMPARE(ctx.effectiveDesktop(screenCurrent), expected);
}

void TestWindowContext::effectiveActivity_data()
{
    QTest::addColumn<QString>("activity");
    QTest::addColumn<QStringList>("activities");
    QTest::addColumn<QString>("currentActivity");
    QTest::addColumn<QString>("expected");

    QTest::newRow("own-activity-beats-current")
        << QStringLiteral("{aaaa}") << QStringList() << QStringLiteral("{bbbb}") << QStringLiteral("{aaaa}");
    QTest::newRow("all-activities-falls-back-to-current")
        << QString() << QStringList() << QStringLiteral("{bbbb}") << QStringLiteral("{bbbb}");
    // A window on several activities resolves to the one in view when it is
    // listed, like a desktop span (F426), and to its first otherwise.
    QTest::newRow("several-activities-prefer-the-current-one")
        << QStringLiteral("{aaaa}") << QStringList{QStringLiteral("{aaaa}"), QStringLiteral("{bbbb}")}
        << QStringLiteral("{bbbb}") << QStringLiteral("{bbbb}");
    QTest::newRow("several-activities-current-not-listed")
        << QStringLiteral("{aaaa}") << QStringList{QStringLiteral("{aaaa}"), QStringLiteral("{bbbb}")}
        << QStringLiteral("{cccc}") << QStringLiteral("{aaaa}");
}

void TestWindowContext::effectiveActivity()
{
    QFETCH(QString, activity);
    QFETCH(QStringList, activities);
    QFETCH(QString, currentActivity);
    QFETCH(QString, expected);

    WindowRegistry::WindowContext ctx;
    ctx.activity = activity;
    ctx.activities = activities;
    QCOMPARE(ctx.effectiveActivity(currentActivity), expected);
}

void TestWindowContext::windowContextLookup()
{
    // The registry hands the resolver exactly the upserted per-window context,
    // and an unknown instance yields nullopt (resolver falls back to the
    // screen's current context).
    WindowRegistry registry;
    PhosphorEngine::WindowMetadata meta;
    meta.appId = QStringLiteral("org.example.app");
    meta.virtualDesktop = 2;
    meta.virtualDesktops = QList<int>{2, 3};
    meta.activity = QStringLiteral("{aaaa}");
    meta.activities = QStringList{QStringLiteral("{aaaa}"), QStringLiteral("{bbbb}")};
    registry.upsert(QStringLiteral("instance-1"), meta);

    const auto ctx = registry.windowContext(QStringLiteral("instance-1"));
    QVERIFY(ctx.has_value());
    QCOMPARE(ctx->virtualDesktop, 2);
    QCOMPARE(ctx->virtualDesktops, (QList<int>{2, 3}));
    QCOMPARE(ctx->activity, QStringLiteral("{aaaa}"));
    QCOMPARE(ctx->effectiveDesktop(3), 3);
    QCOMPARE(ctx->activities, meta.activities);
    QCOMPARE(ctx->effectiveActivity(QStringLiteral("{bbbb}")), QStringLiteral("{bbbb}"));
    const auto desktopContext = registry.desktopContext(QStringLiteral("org.example.app|instance-1"));
    QVERIFY(desktopContext.has_value());
    QCOMPARE(desktopContext->activities, meta.activities);

    QVERIFY(!registry.windowContext(QStringLiteral("unknown")).has_value());
}

void TestWindowContext::spanReachesEveryListedActivity()
{
    // A window on several activities is a member of each (F426): a span that
    // listed only its first marked every other activity's key stale.
    PhosphorEngine::DesktopSpan span;
    span.known = true;
    span.desktops = {1};
    span.activity = QStringLiteral("{aaaa}");
    span.activities = QStringList{QStringLiteral("{aaaa}"), QStringLiteral("{bbbb}")};
    QVERIFY(span.coversActivity(QStringLiteral("{aaaa}")));
    QVERIFY(span.coversActivity(QStringLiteral("{bbbb}")));
    QVERIFY(!span.coversActivity(QStringLiteral("{cccc}")));
    QVERIFY(span.coversActivity(QString()));
    QVERIFY(span.coversKey(PhosphorEngine::PlacementStateKey{QStringLiteral("DP-1"), 1, QStringLiteral("{bbbb}")}));

    // One activity, or all of them, as before.
    span.activities.clear();
    QVERIFY(!span.coversActivity(QStringLiteral("{bbbb}")));
    span.activity.clear();
    QVERIFY(span.coversActivity(QStringLiteral("{bbbb}")));
}

QTEST_APPLESS_MAIN(TestWindowContext)
#include "test_windowcontext.moc"
