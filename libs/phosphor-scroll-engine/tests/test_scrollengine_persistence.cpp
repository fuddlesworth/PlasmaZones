// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Strip-structure persistence: the mode-round-trip stash's focus/anchor
// carry, and the serialize/restore blob a login restore rides — including
// the appId claim that survives cross-session window-uuid drift. Split from
// test_scrollengine_smoke.cpp (file-size ceiling); same headless setup.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace PhosphorScrollEngine;

class TestScrollEnginePersistence : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void modeRoundTripRestoresFocusAndAnchor();
    void serializedStripRestoreSurvivesIdDrift();
    void pruneSweepsStashedTilesForClosedWindows();
    void pruneSpareStashStagedFromPersistence();
    void unclaimedStashTilesExpireAfterThreeSessions();
    void coTenantClaimDoesNotRenewSiblingLease();

private:
    /// Smoke-suite twin: geometry providers wired so the apply path
    /// resolves real rects against a 1200x800 work area.
    static ScrollEngine* makeProviderEngine(QObject* parent, const QSet<QString>& screens)
    {
        auto* engine = new ScrollEngine(nullptr, nullptr, parent);
        const auto geometry = [](const QString&) {
            return QRect(0, 0, 1200, 800);
        };
        engine->setScreenGeometryProviders(geometry, geometry);
        engine->setActiveScreens(screens);
        return engine;
    }
};

void TestScrollEnginePersistence::modeRoundTripRestoresFocusAndAnchor()
{
    // The structural stash alone rebuilt stacks and widths but was
    // focus-blind: the first arrival won the focus on the empty strip, so
    // every mode round trip re-anchored the strip on an arbitrary window.
    // The stash now carries the focused window and the view anchor.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->consumeWindowIntoColumn(QStringLiteral("S1")); // b joins a's stack
    engine->toggleColumnTabbed(QStringLiteral("S1"));
    engine->windowFocused(QStringLiteral("app|c"), QStringLiteral("S1"));
    engine->centerColumn(QStringLiteral("S1"));

    auto* before = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S1")));
    QVERIFY(before);
    QCOMPARE(before->strip().activeWindowId(), QStringLiteral("app|c"));
    const int anchorBefore = before->strip().viewAnchor();

    // Mode reassignment of the SAME context: teardown stashes the strip.
    engine->setActiveScreens({});
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|c")));

    // Cycle back; the FOCUSED window arrives last so its focus/anchor
    // restore lands on the fully rebuilt strip (exact-anchor assertion).
    engine->setActiveScreens({QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);

    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|a")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|b")), 0);
    QCOMPARE(engine->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|c")), 1);
    auto* after = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S1")));
    QVERIFY(after);
    QCOMPARE(after->strip().columns().at(0).display, ColumnDisplay::Tabbed);
    QCOMPARE(after->strip().activeWindowId(), QStringLiteral("app|c"));
    QCOMPARE(after->strip().viewAnchor(), anchorBefore);
}

void TestScrollEnginePersistence::serializedStripRestoreSurvivesIdDrift()
{
    // Login restore: serialize a live strip, feed it to a FRESH engine, and
    // re-announce the windows with DIFFERENT uuids (same appId prefix) —
    // the drift a real restart produces. The tabbed two-window column, the
    // focus, and the structure must rebuild via the appId claim.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("other|u3"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->consumeWindowIntoColumn(QStringLiteral("S1")); // u2 joins u1's stack
    engine1->toggleColumnTabbed(QStringLiteral("S1"));
    engine1->windowFocused(QStringLiteral("other|u3"), QStringLiteral("S1"));
    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);
    // New-session uuids; the stashed tiles are claimed one-to-one in
    // arrival order per app: n1 claims u1's slot, n2 claims u2's.
    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("app|n2"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("other|n3"), QStringLiteral("S1"), 0, 0);

    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|n1")), 0);
    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|n2")), 0);
    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("other|n3")), 1);
    auto* state = static_cast<ScrollState*>(engine2->stateForScreen(QStringLiteral("S1")));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().at(0).display, ColumnDisplay::Tabbed);
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 2);
    // The stashed focus (other|u3) followed its claimed successor.
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("other|n3"));
}

void TestScrollEnginePersistence::pruneSweepsStashedTilesForClosedWindows()
{
    // A stash is keyed by CONTEXT, and the existing sweeps only fire when the
    // context itself dies (desktop, activity, output). A window that closes
    // while the screen is in ANOTHER mode leaves its tile in a stash whose
    // context is still perfectly live, so nothing ever reached it: the entry
    // could never satisfy the all-consumed drop condition, survived into
    // serializeStripState, and its appId claim could later hand an unrelated
    // same-app window the dead tile's slot.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);

    // Leave Scrolling: the teardown stashes all three tiles.
    engine->setActiveScreens({});
    const QByteArray stashed = QJsonDocument(engine->serializeStripState()).toJson();
    QVERIFY2(stashed.contains("app|b"), "precondition: the stash holds all three tiles");

    // One window closes while the screen is in another mode. The context is
    // untouched, so the context-keyed sweeps cannot reach this stash and only
    // pruneStaleWindows can.
    engine->pruneStaleWindows({QStringLiteral("app|a"), QStringLiteral("app|c")});

    // Asserted while the screen is STILL out of Scrolling, deliberately: once
    // the context goes live again its strip wins the serialize key and would
    // mask a surviving ghost in the stash underneath.
    const QByteArray swept = QJsonDocument(engine->serializeStripState()).toJson();
    QVERIFY2(!swept.contains("app|b"), "the closed window must not survive in the stash");
    QVERIFY2(swept.contains("app|a") && swept.contains("app|c"), "the surviving tiles must be left alone by the sweep");

    // And the pruned stash still restores the survivors on the way back.
    engine->setActiveScreens({QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|a")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|c")));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|b")));
}

void TestScrollEnginePersistence::pruneSpareStashStagedFromPersistence()
{
    // The aliveness sweep above and the cross-session restore pull in exactly
    // opposite directions, and the daemon runs them back to back at every
    // login: init stages the persisted blob into the stash, then the effect's
    // bringup fires pruneStaleWindows with the live window set.
    //
    // NO staged id can be in that set. The whole premise of the appId claim is
    // that the instance half of a window id is regenerated each launch, which
    // is why the restore matches on the app prefix. So a sweep that reads
    // "absent from the alive set" as "closed" erases the entire snapshot on
    // the first prune, and the tabbed columns, widths, focus and view anchor
    // are gone. Only pruneSweepsStashedTilesForClosedWindows exercised the
    // sweep, and it uses in-session ids, so it cannot see this at all.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("other|u3"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->consumeWindowIntoColumn(QStringLiteral("S1"));
    engine1->toggleColumnTabbed(QStringLiteral("S1"));
    const QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    ScrollEngine* engine2 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine2->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine2->restoreStripState(blob);

    // The login-order prune, with THIS session's ids. None of them matches a
    // staged tile, which is the normal and expected case.
    engine2->pruneStaleWindows({QStringLiteral("app|n1"), QStringLiteral("app|n2"), QStringLiteral("other|n3")});

    engine2->windowOpened(QStringLiteral("app|n1"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("app|n2"), QStringLiteral("S1"), 0, 0);
    engine2->windowOpened(QStringLiteral("other|n3"), QStringLiteral("S1"), 0, 0);

    auto* state = static_cast<ScrollState*>(engine2->stateForScreen(QStringLiteral("S1")));
    QVERIFY(state);
    QVERIFY2(state->strip().columns().size() == 2,
             "the persisted structure must survive the bringup prune, not collapse to one column per window");
    QVERIFY2(state->strip().columns().at(0).display == ColumnDisplay::Tabbed,
             "the stashed tabbed column must still rebuild after the prune");
    QCOMPARE(state->strip().columns().at(0).tiles.size(), 2);
    QCOMPARE(engine2->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("other|n3")), 1);

    // The exemption is not permanent: once a tile has been claimed the entry
    // is anchored in this session's id space, so a later prune sweeps it
    // normally. Without the flag clear, a stash tile for a window that closes
    // mid-session would become immortal again.
    //
    // Asserted BEHAVIOURALLY, on the strip, with the screen still active.
    // Reading serializeStripState here would prove nothing: taking the screen
    // out of scrolling to expose the stash runs stashStripStructure, which
    // REPLACES the entry wholesale from the live strip, so the staged tile is
    // gone regardless of whether the flag was ever cleared.
    ScrollEngine* engine3 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine3->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine3->restoreStripState(blob);
    // app|m1 claims app|u1's tile in the stashed tabbed column, which clears
    // the persistence exemption for the whole entry.
    engine3->windowOpened(QStringLiteral("app|m1"), QStringLiteral("S1"), 0, 0);
    // The prune now APPLIES: app|u2's tile names a window that is not alive,
    // so it is swept out of the entry.
    engine3->pruneStaleWindows({QStringLiteral("app|m1")});
    // With the sweep applied, app|n2 has no stashed slot left to claim and
    // opens in its own column. With the exemption stuck, it claims app|u2's
    // surviving slot and stacks into app|m1's column instead.
    engine3->windowOpened(QStringLiteral("app|n2"), QStringLiteral("S1"), 0, 0);
    const int m1Col = engine3->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|m1"));
    const int n2Col = engine3->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|n2"));
    QVERIFY2(m1Col != n2Col,
             "after a claim the sweep must apply again, so the dead stashed tile cannot capture a later arrival");

    // Positive control for the arm above: identical sequence WITHOUT the
    // post-claim prune. The second arrival claims app|u2's surviving slot and
    // stacks into the first claimant's column, proving the separate-columns
    // assertion really measures the sweep and not a broken claim path.
    ScrollEngine* engine4 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine4->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine4->restoreStripState(blob);
    engine4->windowOpened(QStringLiteral("app|m1"), QStringLiteral("S1"), 0, 0);
    engine4->windowOpened(QStringLiteral("app|n2"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine4->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|n2")),
             engine4->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|m1")));
}

void TestScrollEnginePersistence::unclaimedStashTilesExpireAfterThreeSessions()
{
    // The per-tile unclaimedSessions lease: a staged tile ages by one at each
    // serialize it sits through unclaimed, and restoreStripState drops it at
    // kMaxUnclaimedSessions (3). Without the drop, the prune exemption makes
    // the tile immortal — pruneStaleWindows fires exactly once per session,
    // at bringup, while the staged entry is still sweep-exempt — and a
    // long-dead slot eventually captures an unrelated same-app window.
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->consumeWindowIntoColumn(QStringLiteral("S1")); // u2 joins u1's stack
    engine1->toggleColumnTabbed(QStringLiteral("S1"));
    QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    // Three sessions restore the blob and shut down without ANY claim. Each
    // pass ages the tiles by one: 1, 2, 3.
    QJsonObject penultimate;
    for (int session = 0; session < 3; ++session) {
        penultimate = blob;
        ScrollEngine* e = makeProviderEngine(&owner, {QStringLiteral("S1")});
        e->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
        e->restoreStripState(blob);
        blob = e->serializeStripState();
    }

    // Control first, with the SECOND-to-last blob (ages 2): the lease is not
    // yet expired, so the claim still fires and the pair rebuilds tabbed in
    // one column. This is what makes the expiry assertion below non-vacuous.
    ScrollEngine* control = makeProviderEngine(&owner, {QStringLiteral("S1")});
    control->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    control->restoreStripState(penultimate);
    control->windowOpened(QStringLiteral("app|c1"), QStringLiteral("S1"), 0, 0);
    control->windowOpened(QStringLiteral("app|c2"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(control->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|c1")),
             control->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|c2")));

    // The last blob carries age 3: restore drops both tiles, so the arrivals
    // find no staged slots and open plainly, one column each.
    ScrollEngine* engine5 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine5->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine5->restoreStripState(blob);
    engine5->windowOpened(QStringLiteral("app|x1"), QStringLiteral("S1"), 0, 0);
    engine5->windowOpened(QStringLiteral("app|x2"), QStringLiteral("S1"), 0, 0);
    const int x1Col = engine5->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|x1"));
    const int x2Col = engine5->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|x2"));
    QVERIFY2(x1Col != x2Col, "an expired stash tile must not capture a later same-app arrival");
}

void TestScrollEnginePersistence::coTenantClaimDoesNotRenewSiblingLease()
{
    // The lease is PER TILE, not per entry. A same-app co-tenant that comes
    // back every session claims ITS tile (fresh lease) but must not renew the
    // dead sibling's: with an entry-level counter the claim reset the whole
    // entry's age and the sibling slot lived forever. The claiming window is
    // closed before each serialize so the stash entry — not a live strip —
    // is what the shutdown snapshot writes for the key (the claimed-then-
    // closed tile legitimately writes a fresh lease: its app demonstrably
    // comes back).
    QObject owner;
    ScrollEngine* engine1 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine1->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine1->windowOpened(QStringLiteral("app|u1"), QStringLiteral("S1"), 0, 0);
    engine1->windowOpened(QStringLiteral("app|u2"), QStringLiteral("S1"), 0, 0);
    engine1->windowFocused(QStringLiteral("app|u1"), QStringLiteral("S1"));
    engine1->consumeWindowIntoColumn(QStringLiteral("S1")); // u2 joins u1's stack
    engine1->toggleColumnTabbed(QStringLiteral("S1"));
    QJsonObject blob = engine1->serializeStripState();
    QVERIFY(!blob.isEmpty());

    // Three sessions each claim ONE tile (the first slot, by appId match, in
    // arrival order) and close it again before shutdown. The claimed lineage
    // re-leases every time; the sibling ages 1, 2, 3 untouched.
    for (int session = 0; session < 3; ++session) {
        ScrollEngine* e = makeProviderEngine(&owner, {QStringLiteral("S1")});
        e->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
        e->restoreStripState(blob);
        const QString claimant = QStringLiteral("app|m%1").arg(session);
        e->windowOpened(claimant, QStringLiteral("S1"), 0, 0);
        e->windowClosed(claimant);
        blob = e->serializeStripState();
    }

    // The sibling's lease expired on the final restore; only the claimed
    // lineage's slot survives. The first arrival claims it, the second finds
    // no staged slot left and opens its own column. With an entry-level
    // counter both would claim and stack tabbed into one column.
    ScrollEngine* engine5 = makeProviderEngine(&owner, {QStringLiteral("S1")});
    engine5->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine5->restoreStripState(blob);
    engine5->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine5->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    const int aCol = engine5->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|a"));
    const int bCol = engine5->columnIndexForWindow(QStringLiteral("S1"), QStringLiteral("app|b"));
    QVERIFY2(aCol != bCol, "a returning co-tenant's claim must not renew the dead sibling tile's lease");
}

QTEST_GUILESS_MAIN(TestScrollEnginePersistence)
#include "test_scrollengine_persistence.moc"
