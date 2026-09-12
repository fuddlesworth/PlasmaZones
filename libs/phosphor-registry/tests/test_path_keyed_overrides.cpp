// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// PathKeyedOverrides — the baseline-plus-keyed-overrides container the four
// shader-assignment trees share.
//
// It had no direct test. Every assertion about it was indirect, through one of
// the trees, and one of those trees (OverlayShaderTree) has no unit test of its
// own at all — so for that payload the empty-key refusal and clearOverride's
// return value were unpinned entirely. The container is the piece that, when it
// was written out four times by hand, drifted in ways nobody intended; it is
// exactly the piece that should be pinned where it lives.
//
// The payload here is a trivial struct rather than one of the real profiles, on
// purpose: these slots are about the CONTAINER's contract (ordering, refusal,
// the three equality predicates), and borrowing a real payload would make the
// test depend on that payload's own equality policy.

#include <PhosphorRegistry/PathKeyedOverrides.h>

#include <QStringList>
#include <QtTest>

namespace {

/// A payload with nothing but an int, so equality is unambiguous.
struct Value
{
    int n = 0;

    bool operator==(const Value& other) const
    {
        return n == other.n;
    }
    bool operator!=(const Value& other) const
    {
        return !(*this == other);
    }
};

using Store = PhosphorRegistry::PathKeyedOverrides<Value>;

} // namespace

class TestPathKeyedOverrides : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void anEmptyKeyIsRefused()
    {
        // The empty string is how every tree spells "the baseline", so accepting it
        // as an override key would create an entry shadowing the baseline it is
        // supposed to BE. Each tree used to guard this separately; the guard lives
        // here now, so this is the only place it can be pinned.
        Store store;
        store.setOverride(QString(), Value{7});
        QVERIFY(store.hasNoOverrides());
        QVERIFY(!store.hasOverride(QString()));
        QVERIFY(store.keys().isEmpty());

        // And on a NON-empty store, which is what makes this resistant: exercising the
        // guard only on a fresh one leaves `if (key.isEmpty() && m_overrides.isEmpty())`
        // passing, and that mutation refuses the empty key exactly once.
        store.setOverride(QStringLiteral("real"), Value{1});
        store.setOverride(QString(), Value{7});
        QCOMPARE(store.keys().size(), 1);
        QCOMPARE(store.keys().constFirst(), QStringLiteral("real"));
        QVERIFY(!store.hasOverride(QString()));
    }

    void keysAreInInsertionOrderNotSortedOrder()
    {
        // Non-alphabetical, so an implementation that sorted would fail rather than
        // coincide. Two of the four trees serialise their overrides as a JSON ARRAY,
        // where this order is user-visible and round-trips.
        Store store;
        store.setOverride(QStringLiteral("zeta"), Value{1});
        store.setOverride(QStringLiteral("alpha"), Value{2});
        store.setOverride(QStringLiteral("mid"), Value{3});

        const QStringList expected{QStringLiteral("zeta"), QStringLiteral("alpha"), QStringLiteral("mid")};
        QCOMPARE(store.keys(), expected);
    }

    void reSettingAnExistingKeyKeepsItsPosition()
    {
        // setOverride appends only on a miss. Appending unconditionally would both
        // duplicate the key and move it to the end, which is why `clearOverride` can
        // use removeAll and why that equivalence is worth pinning.
        Store store;
        store.setOverride(QStringLiteral("a"), Value{1});
        store.setOverride(QStringLiteral("b"), Value{2});
        store.setOverride(QStringLiteral("a"), Value{9});

        const QStringList expected{QStringLiteral("a"), QStringLiteral("b")};
        QCOMPARE(store.keys(), expected);
        QCOMPARE(store.directOverride(QStringLiteral("a")).n, 9);
    }

    void clearThenResetAppendsAtTheEnd()
    {
        // Clearing forgets the position. That is the behaviour, not an accident of
        // it: a key the user removed and re-added is a new entry in the list they
        // see, and the two array-form trees persist that order.
        Store store;
        store.setOverride(QStringLiteral("a"), Value{1});
        store.setOverride(QStringLiteral("b"), Value{2});
        QVERIFY(store.clearOverride(QStringLiteral("a")));
        store.setOverride(QStringLiteral("a"), Value{3});

        const QStringList expected{QStringLiteral("b"), QStringLiteral("a")};
        QCOMPARE(store.keys(), expected);
    }

    void clearOverrideReportsWhetherItRemovedAnything()
    {
        // Callers forward this bool up as "did anything change", which gates a
        // change signal in every tree. An unconditional true would make a no-op
        // clear emit, at which point the tree's own no-op gate is the only thing
        // left standing between a stray click and the daemon.
        Store store;
        store.setOverride(QStringLiteral("a"), Value{1});
        QVERIFY(store.clearOverride(QStringLiteral("a")));
        QVERIFY(!store.clearOverride(QStringLiteral("a")));
        QVERIFY(!store.clearOverride(QStringLiteral("never-set")));
        QVERIFY(store.hasNoOverrides());
        QVERIFY(store.keys().isEmpty());
    }

    void findOverrideAnswersNullptrOnAMiss()
    {
        // The walk-up loops use this instead of hasOverride + directOverride, so a
        // miss returning a pointer to a default-constructed payload (rather than
        // nullptr) would make every ancestor step overlay an empty profile.
        Store store;
        store.setOverride(QStringLiteral("a"), Value{5});
        const Value* hit = store.findOverride(QStringLiteral("a"));
        QVERIFY(hit != nullptr);
        QCOMPARE(hit->n, 5);
        QVERIFY(store.findOverride(QStringLiteral("b")) == nullptr);
        // And it agrees with the by-value accessor, which is what lets a caller pick
        // either without thinking about it.
        QCOMPARE(store.directOverride(QStringLiteral("a")).n, hit->n);
    }

    void directOverrideOnAMissAnswersADefaultPayload()
    {
        // Returning the default rather than asserting is what lets a caller ask
        // about a key it has not checked, which several tree accessors rely on.
        Store store;
        QCOMPARE(store.directOverride(QStringLiteral("nope")).n, Value{}.n);
    }

    void forEachInOrderVisitsEveryOverrideInInsertionOrder()
    {
        // The shared half of serialisation. An implementation that walked the
        // underlying hash would pass every other assertion in this file and still
        // make both array-form trees order-unstable on disk.
        Store store;
        store.setOverride(QStringLiteral("zeta"), Value{1});
        store.setOverride(QStringLiteral("alpha"), Value{2});
        store.setOverride(QStringLiteral("mid"), Value{3});

        QStringList seenKeys;
        QList<int> seenValues;
        store.forEachInOrder([&](const QString& key, const Value& value) {
            seenKeys.append(key);
            seenValues.append(value.n);
        });

        const QStringList expectedKeys{QStringLiteral("zeta"), QStringLiteral("alpha"), QStringLiteral("mid")};
        QCOMPARE(seenKeys, expectedKeys);
        QCOMPARE(seenValues, QList<int>({1, 2, 3}));
    }

    void sameOverridesIsOrderFreeAndSameKeyOrderIsNot()
    {
        // The reason the container hands out three predicates instead of one
        // operator==: the overlay tree compares order-INSENSITIVELY, because it
        // serialises its overrides as a JSON object whose keys are sorted either
        // way, while the other three compare order too. An operator== here would
        // have had to pick one policy for all four.
        Store a;
        a.setOverride(QStringLiteral("x"), Value{1});
        a.setOverride(QStringLiteral("y"), Value{2});

        Store b;
        b.setOverride(QStringLiteral("y"), Value{2});
        b.setOverride(QStringLiteral("x"), Value{1});

        QVERIFY(a.sameOverrides(b));
        QVERIFY(!a.sameKeyOrder(b));

        // A differing VALUE is unequal under both.
        Store c;
        c.setOverride(QStringLiteral("x"), Value{1});
        c.setOverride(QStringLiteral("y"), Value{99});
        QVERIFY(!a.sameOverrides(c));
        QVERIFY(a.sameKeyOrder(c));

        // And a differing SIZE, which a naive per-key loop over one side alone
        // would miss.
        Store d;
        d.setOverride(QStringLiteral("x"), Value{1});
        QVERIFY(!a.sameOverrides(d));
    }

    void sameBaselineIsIndependentOfTheOverrides()
    {
        Store a;
        a.setBaseline(Value{4});
        Store b;
        b.setBaseline(Value{4});
        b.setOverride(QStringLiteral("x"), Value{1});

        QVERIFY(a.sameBaseline(b));
        QVERIFY(!a.sameOverrides(b));

        b.setBaseline(Value{5});
        QVERIFY(!a.sameBaseline(b));
    }

    void clearAllOverridesLeavesTheBaselineAlone()
    {
        Store store;
        store.setBaseline(Value{4});
        store.setOverride(QStringLiteral("a"), Value{1});
        store.setOverride(QStringLiteral("b"), Value{2});

        store.clearAllOverrides();
        QVERIFY(store.hasNoOverrides());
        QVERIFY(store.keys().isEmpty());
        // The baseline is a separate statement and clearing the overrides is not a
        // reset of the whole tree.
        QCOMPARE(store.baseline().n, 4);
    }
};

QTEST_MAIN(TestPathKeyedOverrides)
#include "test_path_keyed_overrides.moc"
