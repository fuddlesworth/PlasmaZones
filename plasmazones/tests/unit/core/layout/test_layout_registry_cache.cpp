// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_registry_cache.cpp
 * @brief Hot-path memoization guard for LayoutRegistry::resolveAssignmentEntry
 *
 * `assignmentEntryForScreen` is called per cursor-move on the overlay/OSD path
 * and per snap-geometry calculation; with N rules each call walks the
 * priority-sorted list and evaluates every rule's match expression. This test
 * pins the windowless-query cache behavior so:
 *
 *   1. Repeated identical queries grow the cache by exactly one entry — the
 *      walk runs once per (screenId, desktop, activity) tuple per rule-set
 *      revision, even though the connector / virtual-screen fallback chain in
 *      `assignmentEntryForScreen` drives the same tuple into multiple calls
 *      per public invocation.
 *   2. A rule-set mutation invalidates the cache. The revision bump is the
 *      one signal — no explicit clear is needed at signal time.
 *   3. Distinct context tuples populate distinct cache entries (no key
 *      collisions across the three independent dimensions).
 *   4. `nullopt` (genuine cascade miss) is cacheable too — a missed lookup
 *      pays the linear walk exactly once per revision, not three times per
 *      cursor frame.
 */

#include <QElapsedTimer>
#include <QScopedPointer>
#include <QString>
#include <QTest>
#include <QUuid>

#include <memory>
#include <vector>

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutRegistryCache : public QObject
{
    Q_OBJECT

private:
    PhosphorZones::Layout* createTestLayout(const QString& name, QObject* parent = nullptr)
    {
        auto* layout = new PhosphorZones::Layout(name, parent);
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        layout->addZone(zone);
        return layout;
    }

    PhosphorZones::LayoutRegistry* createRegistry()
    {
        m_guards.emplace_back(std::make_unique<IsolatedConfigGuard>());
        auto* mgr = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        return mgr;
    }

    /// Seed the registry with N rules pinning distinct (screen, desktop) tuples
    /// so resolveAssignmentEntry for the target screen actually has work to do
    /// (the priority walk must inspect every rule).
    void seedRuleSet(PhosphorZones::LayoutRegistry* mgr, int n, PhosphorZones::Layout* layout)
    {
        for (int i = 0; i < n; ++i) {
            mgr->assignLayout(QStringLiteral("DP-%1").arg(i), 0, QString(), layout);
        }
    }

    std::vector<std::unique_ptr<IsolatedConfigGuard>> m_guards;

private Q_SLOTS:

    void cleanup()
    {
        m_guards.clear();
    }

    // ─────────────────────────────────────────────────────────────────────
    // Repeat-call: identical queries grow the cache by exactly one entry.
    // ─────────────────────────────────────────────────────────────────────
    void testRepeatedQueryHitsCache()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createRegistry());
        auto* layout = createTestLayout(QStringLiteral("L"));
        mgr->addLayout(layout);
        seedRuleSet(mgr.data(), 50, layout);

        const int sizeBefore = mgr->contextResolveCacheSize();

        // The public resolver chains up to three internal resolveAssignmentEntry
        // calls (connector / virtual-screen fallback). For a stored, regular
        // screen the first call hits, but the cache must still see exactly
        // one new key for the (screen, desktop, activity) tuple regardless of
        // how many times the public method is invoked.
        for (int i = 0; i < 100; ++i) {
            (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-3"), 0, QString());
        }
        // The one new entry — plus any entries the seed batch's signal
        // emission warmed via assignmentIdForScreen() in upsertAssignmentRule
        // (it doesn't, but the seed itself doesn't mutate the cache from a
        // post-warm baseline, only invalidates between iterations).
        QCOMPARE(mgr->contextResolveCacheSize() - sizeBefore, 1);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Distinct tuples populate distinct entries — no key collisions across
    // the three independent context dimensions.
    // ─────────────────────────────────────────────────────────────────────
    void testDistinctQueriesPopulateDistinctEntries()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createRegistry());
        auto* layout = createTestLayout(QStringLiteral("L"));
        mgr->addLayout(layout);
        seedRuleSet(mgr.data(), 10, layout);

        const int sizeBefore = mgr->contextResolveCacheSize();

        // 5 distinct screens × 3 distinct desktops × 2 distinct activities
        // — every combination is a unique cache key.
        for (int s = 0; s < 5; ++s) {
            for (int d = 1; d <= 3; ++d) {
                for (int a = 0; a < 2; ++a) {
                    (void)mgr->assignmentEntryForScreen(QStringLiteral("HDMI-%1").arg(s), d,
                                                        a == 0 ? QString() : QStringLiteral("activity-%1").arg(a));
                }
            }
        }
        // Each tuple's first call inserts one entry. For an unstored screen
        // the fallback chain may itself drive several distinct keys (a
        // virtual-screen id retries to the physical id), but pure connector
        // names like "HDMI-N" don't recurse — so exactly 30 new entries.
        QCOMPARE(mgr->contextResolveCacheSize() - sizeBefore, 30);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Cache miss (no pinned context rule) is itself cached — a genuine
    // miss must not re-walk three times per cursor frame.
    // ─────────────────────────────────────────────────────────────────────
    void testMissIsCached()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createRegistry());
        // A registry with NO rules — every query is a miss. The default
        // provider still kicks in at the public layer (assignmentEntryForScreen
        // synthesizes from `resolveDefaultAssignmentEntry()` on miss), but the
        // private cache records the nullopt all the same.
        auto* layout = createTestLayout(QStringLiteral("L"));
        mgr->addLayout(layout);

        const int sizeBefore = mgr->contextResolveCacheSize();

        // 50 calls, same tuple — exactly one nullopt cached.
        for (int i = 0; i < 50; ++i) {
            (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-unknown"), 7, QStringLiteral("missing-activity"));
        }
        QCOMPARE(mgr->contextResolveCacheSize() - sizeBefore, 1);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Mutation invalidates the cache via the rule-set revision bump.
    // ─────────────────────────────────────────────────────────────────────
    void testMutationInvalidatesCache()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createRegistry());
        auto* layout = createTestLayout(QStringLiteral("L"));
        mgr->addLayout(layout);
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);

        // Warm cache for two distinct tuples.
        (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString());
        (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-2"), 0, QString());
        const int populated = mgr->contextResolveCacheSize();
        QVERIFY(populated >= 2);

        // Mutating the rule set bumps RuleSet::revision(); the next
        // resolveAssignmentEntry call clears the stale cache and reseeds.
        auto* other = createTestLayout(QStringLiteral("Other"));
        mgr->addLayout(other);
        mgr->assignLayout(QStringLiteral("DP-99"), 0, QString(), other);

        // First call post-mutation clears + inserts one — net cache size is 1.
        (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString());
        QCOMPARE(mgr->contextResolveCacheSize(), 1);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Microbench — informational. The assertion ceiling is generous (the
    // exact ns/call varies with CPU + load). The goal is to fail loudly if
    // the cache regresses to linear-walk territory (10–50 µs at N=50).
    // ─────────────────────────────────────────────────────────────────────
    void benchCachedHit()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createRegistry());
        auto* layout = createTestLayout(QStringLiteral("L"));
        mgr->addLayout(layout);
        seedRuleSet(mgr.data(), 50, layout);

        // Warm the cache.
        for (int i = 0; i < 10; ++i) {
            (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-25"), 0, QString());
        }

        const int n = 100000;
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < n; ++i) {
            (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-25"), 0, QString());
        }
        const double cachedNs = double(t.nsecsElapsed()) / n;

        // The SAME work with the cache defeated, measured in the same run on
        // the same machine: a fresh screen id every iteration is a miss every
        // iteration, so this is the linear walk through 50 context rules with
        // match.evaluate() that the cache exists to avoid.
        //
        // A RATIO, not an absolute ns ceiling. The claim being pinned is "the
        // cache drops the cost by an order of magnitude", and the old 5000
        // ns/call constant was only a proxy for it — a proxy that measured the
        // machine as much as the code, and flapped when ctest ran the suite in
        // parallel and the benchmark lost the CPU (6209 ns/call at -j12, well
        // under a microsecond alone). Contention slows both legs together, so
        // the ratio survives what the constant did not.
        const int missN = 2000;
        QElapsedTimer missTimer;
        missTimer.start();
        for (int i = 0; i < missN; ++i) {
            (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-miss-%1").arg(i), 0, QString());
        }
        const double uncachedNs = double(missTimer.nsecsElapsed()) / missN;

        qDebug("@ N=50 rules: cached %.0f ns/call, uncached %.0f ns/call (%.1fx)", cachedNs, uncachedNs,
               uncachedNs / cachedNs);

        // 5x rather than the full order of magnitude the comment describes:
        // the point is to catch the cache not working at all (a ratio near
        // 1.0), and a threshold that sits just under the real number is how a
        // benchmark starts flapping again.
        QVERIFY2(uncachedNs > cachedNs * 5.0,
                 qPrintable(QStringLiteral("cache is not buying anything: cached %1 ns/call vs uncached %2 ns/call")
                                .arg(cachedNs)
                                .arg(uncachedNs)));
    }

    // ─────────────────────────────────────────────────────────────────────
    // Cached value tracks the source of truth across an in-place edit.
    // After a mutation, the returned entry must reflect the NEW rule set,
    // not a stale cached entry from the previous revision.
    // ─────────────────────────────────────────────────────────────────────
    void testCacheReturnsFreshValueAfterMutation()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createRegistry());
        auto* a = createTestLayout(QStringLiteral("A"));
        auto* b = createTestLayout(QStringLiteral("B"));
        mgr->addLayout(a);
        mgr->addLayout(b);

        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), a);
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 0)->name(), QStringLiteral("A"));

        // Repeat the same call — value is cached but must remain correct.
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 0)->name(), QStringLiteral("A"));
        }

        // Re-assign in place — same key, different layout. The cache must
        // discard the stale entry once it sees the revision bump.
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), b);
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 0)->name(), QStringLiteral("B"));

        // Stable after the bump — repeated reads keep returning B from the
        // re-seeded cache.
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 0)->name(), QStringLiteral("B"));
        }
    }
};

QTEST_MAIN(TestLayoutRegistryCache)
#include "test_layout_registry_cache.moc"
