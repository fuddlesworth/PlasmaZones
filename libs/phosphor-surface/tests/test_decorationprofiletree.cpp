// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QtTest/QtTest>

using namespace PhosphorSurfaceShaders;

namespace {

/// A profile carrying an explicit chain plus per-pack parameters, for
/// round-trip and inheritance assertions.
DecorationProfile makeProfile(const QStringList& chain, double borderWidth, const QString& borderColor)
{
    DecorationProfile p;
    p.chain = chain;
    QVariantMap borderParams;
    borderParams.insert(QStringLiteral("width"), borderWidth);
    borderParams.insert(QStringLiteral("color"), borderColor);
    QVariantMap params;
    params.insert(QStringLiteral("border"), borderParams);
    p.parameters = params;
    return p;
}

} // namespace

class TestDecorationProfileTree : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ── Serialization ────────────────────────────────────────────────────

    void roundTrip_preserves_baseline_overrides_and_order()
    {
        DecorationProfileTree tree;
        tree.setBaseline(makeProfile(QStringList{QStringLiteral("border")}, 2.0, QStringLiteral("#112233")));
        // Insertion order chosen NOT to be alphabetical so a re-sort would
        // be visible.
        tree.setOverride(QStringLiteral("window.floating"),
                         makeProfile(QStringList{QStringLiteral("glow")}, 4.0, QStringLiteral("#445566")));
        tree.setOverride(
            QStringLiteral("osd"),
            makeProfile(QStringList{QStringLiteral("border"), QStringLiteral("glow")}, 1.0, QStringLiteral("#778899")));
        tree.setOverride(QStringLiteral("window.tiled"), makeProfile(QStringList{}, 3.0, QStringLiteral("#aabbcc")));

        const QJsonObject json = tree.toJson();
        const DecorationProfileTree restored = DecorationProfileTree::fromJson(json);

        QCOMPARE(restored, tree);
        // Insertion order survives the JSON overrides array verbatim.
        const QStringList expectedOrder{QStringLiteral("window.floating"), QStringLiteral("osd"),
                                        QStringLiteral("window.tiled")};
        QCOMPARE(restored.overriddenPaths(), expectedOrder);
        // The engaged-but-empty chain on window.tiled survives as engaged.
        QVERIFY(restored.directOverride(QStringLiteral("window.tiled")).chain.has_value());
        QVERIFY(restored.directOverride(QStringLiteral("window.tiled")).chain->isEmpty());
    }

    void disabledPacks_roundTrip_filter_and_inheritance()
    {
        // Round-trip: an engaged disabled set survives JSON; an absent field
        // restores to nullopt (pre-toggle configs load with every pack on).
        DecorationProfile p =
            makeProfile(QStringList{QStringLiteral("border"), QStringLiteral("glow")}, 2.0, QStringLiteral("#112233"));
        p.disabledPacks = QStringList{QStringLiteral("glow")};
        const DecorationProfile restored = DecorationProfile::fromJson(p.toJson());
        QCOMPARE(restored, p);
        QVERIFY(restored.disabledPacks.has_value());

        const DecorationProfile legacy = DecorationProfile::fromJson(
            makeProfile(QStringList{QStringLiteral("border")}, 2.0, QStringLiteral("#112233")).toJson());
        QVERIFY(!legacy.disabledPacks.has_value());
        QCOMPARE(legacy.enabledChain(), QStringList{QStringLiteral("border")});

        // enabledChain filters the disabled pack; effectiveChain keeps it so
        // the editor still lists it.
        QCOMPARE(p.enabledChain(), QStringList{QStringLiteral("border")});
        QCOMPARE(p.effectiveChain(), (QStringList{QStringLiteral("border"), QStringLiteral("glow")}));

        // Engaged-but-empty means "explicitly nothing disabled" and blocks an
        // ancestor's disabled set through overlay, same as chain's semantics.
        DecorationProfileTree tree;
        tree.setBaseline(p);
        DecorationProfile child;
        child.disabledPacks = QStringList();
        tree.setOverride(QStringLiteral("window"), child);
        QCOMPARE(tree.resolve(QStringLiteral("window")).enabledChain(),
                 (QStringList{QStringLiteral("border"), QStringLiteral("glow")}));

        // A child that leaves the field unset inherits the baseline's set.
        DecorationProfile inheriting;
        tree.setOverride(QStringLiteral("window"), inheriting);
        QCOMPARE(tree.resolve(QStringLiteral("window")).enabledChain(), QStringList{QStringLiteral("border")});
        QCOMPARE(tree.resolve(QStringLiteral("window")).effectiveDisabledPacks(), QStringList{QStringLiteral("glow")});
    }

    void empty_tree_serializes_to_nonempty_object()
    {
        // Load-bearing: Settings::decorationProfileTree's isEmpty() guard
        // relies on an all-default tree still producing a populated object
        // ({"baseline":…, "overrides":[]}) rather than {}.
        const DecorationProfileTree tree;
        const QJsonObject json = tree.toJson();

        QVERIFY(!json.isEmpty());
        QVERIFY(json.contains(QLatin1String("baseline")));
        QVERIFY(json.contains(QLatin1String("overrides")));
        QVERIFY(json.value(QLatin1String("baseline")).isObject());
        QVERIFY(json.value(QLatin1String("overrides")).isArray());
        QCOMPARE(json.value(QLatin1String("overrides")).toArray().size(), 0);
    }

    void fromJson_drops_malformed_and_unsupported_entries_keeps_valid_siblings()
    {
        QJsonObject baseline;
        {
            QJsonArray chain;
            chain.append(QStringLiteral("border"));
            baseline.insert(QLatin1String("chain"), chain);
        }

        QJsonArray overrides;
        // Non-object array entry → toObject() empty → empty path → dropped.
        overrides.append(QJsonValue(42));
        overrides.append(QJsonValue(QStringLiteral("not-an-object")));
        // Supported-but-empty path entry → dropped by the isEmpty() guard.
        {
            QJsonObject e;
            e.insert(QLatin1String("path"), QString());
            e.insert(QLatin1String("profile"), QJsonObject());
            overrides.append(e);
        }
        // Path that names no decorable surface → dropped by
        // decorationSurfaceSupported().
        {
            QJsonObject e;
            e.insert(QLatin1String("path"), QStringLiteral("bogus.surface"));
            e.insert(QLatin1String("profile"), QJsonObject());
            overrides.append(e);
        }
        // Traversal-shaped path → not a supported surface → dropped.
        {
            QJsonObject e;
            e.insert(QLatin1String("path"), QStringLiteral("window.tiled/../../etc/passwd"));
            e.insert(QLatin1String("profile"), QJsonObject());
            overrides.append(e);
        }
        // Deeper-than-leaf path → not supported → dropped.
        {
            QJsonObject e;
            e.insert(QLatin1String("path"), QStringLiteral("window.tiled.extra"));
            e.insert(QLatin1String("profile"), QJsonObject());
            overrides.append(e);
        }
        // Valid sibling → kept.
        {
            QJsonObject profile;
            QJsonArray chain;
            chain.append(QStringLiteral("glow"));
            profile.insert(QLatin1String("chain"), chain);
            QJsonObject e;
            e.insert(QLatin1String("path"), QStringLiteral("window.tiled"));
            e.insert(QLatin1String("profile"), profile);
            overrides.append(e);
        }

        QJsonObject root;
        root.insert(QLatin1String("baseline"), baseline);
        root.insert(QLatin1String("overrides"), overrides);

        const DecorationProfileTree tree = DecorationProfileTree::fromJson(root);

        QCOMPARE(tree.overriddenPaths(), QStringList{QStringLiteral("window.tiled")});
        QVERIFY(tree.hasOverride(QStringLiteral("window.tiled")));
        QVERIFY(!tree.hasOverride(QStringLiteral("bogus.surface")));
        QCOMPARE(tree.directOverride(QStringLiteral("window.tiled")).effectiveChain(),
                 QStringList{QStringLiteral("glow")});
        // Baseline still parsed even amid the malformed override list.
        QCOMPARE(tree.baseline().effectiveChain(), QStringList{QStringLiteral("border")});
    }

    void fromJson_duplicate_path_last_value_wins_first_position_kept()
    {
        QJsonArray overrides;
        for (const QString& color : {QStringLiteral("#111111"), QStringLiteral("#222222")}) {
            QJsonObject profile;
            QJsonObject params;
            QJsonObject borderParams;
            borderParams.insert(QLatin1String("color"), color);
            params.insert(QLatin1String("border"), borderParams);
            profile.insert(QLatin1String("parameters"), params);
            QJsonObject e;
            e.insert(QLatin1String("path"), QStringLiteral("osd"));
            e.insert(QLatin1String("profile"), profile);
            overrides.append(e);
        }
        QJsonObject root;
        root.insert(QLatin1String("overrides"), overrides);

        const DecorationProfileTree tree = DecorationProfileTree::fromJson(root);

        // De-dup collapses to a single insertion-order entry.
        QCOMPARE(tree.overriddenPaths(), QStringList{QStringLiteral("osd")});
        // Last value wins.
        const QVariantMap params = tree.directOverride(QStringLiteral("osd")).effectiveParameters();
        const QVariantMap borderParams = params.value(QStringLiteral("border")).toMap();
        QCOMPARE(borderParams.value(QStringLiteral("color")).toString(), QStringLiteral("#222222"));
    }

    // ── resolve() walk-up inheritance ────────────────────────────────────

    void resolve_leaf_wins_over_category_over_baseline()
    {
        DecorationProfileTree tree;
        tree.setBaseline(makeProfile(QStringList{QStringLiteral("base")}, 1.0, QStringLiteral("#000000")));

        DecorationProfile category;
        category.chain = QStringList{QStringLiteral("cat")};
        tree.setOverride(QStringLiteral("window"), category);

        DecorationProfile leaf;
        leaf.chain = QStringList{QStringLiteral("leaf")};
        tree.setOverride(QStringLiteral("window.tiled"), leaf);

        QCOMPARE(tree.resolve(QStringLiteral("window.tiled")).effectiveChain(), QStringList{QStringLiteral("leaf")});
        QCOMPARE(tree.resolve(QStringLiteral("window")).effectiveChain(), QStringList{QStringLiteral("cat")});
        // A sibling with no override of its own inherits the category.
        QCOMPARE(tree.resolve(QStringLiteral("window.snapped")).effectiveChain(), QStringList{QStringLiteral("cat")});
        // A surface off the overridden branch falls through to the baseline.
        QCOMPARE(tree.resolve(QStringLiteral("osd")).effectiveChain(), QStringList{QStringLiteral("base")});
    }

    void resolve_unset_field_inherits_while_set_field_overrides()
    {
        DecorationProfileTree tree;
        tree.setBaseline(makeProfile(QStringList{QStringLiteral("base")}, 1.0, QStringLiteral("#000000")));

        DecorationProfile category;
        category.chain = QStringList{QStringLiteral("cat")};
        tree.setOverride(QStringLiteral("window"), category);

        // Leaf sets ONLY parameters; its chain stays nullopt → inherited.
        DecorationProfile leaf;
        QVariantMap leafParams;
        leafParams.insert(QStringLiteral("glow"), QVariantMap{{QStringLiteral("radius"), 5.0}});
        leaf.parameters = leafParams;
        QVERIFY(!leaf.chain.has_value());
        tree.setOverride(QStringLiteral("window.tiled"), leaf);

        const DecorationProfile resolved = tree.resolve(QStringLiteral("window.tiled"));
        // Chain inherits the category (leaf left it unset).
        QCOMPARE(resolved.effectiveChain(), QStringList{QStringLiteral("cat")});
        // Parameters come from the leaf's explicit set.
        QCOMPARE(resolved.effectiveParameters(), leafParams);
    }

    void resolve_empty_path_returns_baseline_with_defaults()
    {
        DecorationProfileTree tree;
        tree.setBaseline(makeProfile(QStringList{QStringLiteral("base")}, 1.0, QStringLiteral("#000000")));

        const DecorationProfile resolved = tree.resolve(QString());
        QCOMPARE(resolved.effectiveChain(), QStringList{QStringLiteral("base")});
        // withDefaults() engages both optionals even on the empty-path walk.
        QVERIFY(resolved.chain.has_value());
        QVERIFY(resolved.parameters.has_value());
    }

    void resolve_default_tree_returns_engaged_empties()
    {
        const DecorationProfileTree tree;
        const DecorationProfile resolved = tree.resolve(QStringLiteral("window.tiled"));
        QVERIFY(resolved.chain.has_value());
        QVERIFY(resolved.chain->isEmpty());
        QVERIFY(resolved.parameters.has_value());
        QVERIFY(resolved.parameters->isEmpty());
        // disabledPacks reaches the same engaged-empty parity as chain /
        // parameters (withDefaults engages all three), so pin the third leg.
        QVERIFY(resolved.disabledPacks.has_value());
        QVERIFY(resolved.disabledPacks->isEmpty());
    }

    // ── Mutation / lookup API ────────────────────────────────────────────

    void setOverride_empty_path_is_noop()
    {
        DecorationProfileTree tree;
        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("x")};
        tree.setOverride(QString(), p);
        QVERIFY(tree.overriddenPaths().isEmpty());
        QVERIFY(!tree.hasOverride(QString()));
    }

    void directOverride_absent_path_returns_all_unset()
    {
        const DecorationProfileTree tree;
        const DecorationProfile p = tree.directOverride(QStringLiteral("window.tiled"));
        // Indistinguishable from a real all-unset override — both optionals
        // are disengaged (documented directOverride contract).
        QVERIFY(!p.chain.has_value());
        QVERIFY(!p.parameters.has_value());
    }

    void clearOverride_reports_removal_and_prunes_order()
    {
        DecorationProfileTree tree;
        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("x")};
        tree.setOverride(QStringLiteral("window.tiled"), p);
        tree.setOverride(QStringLiteral("osd"), p);

        QVERIFY(!tree.clearOverride(QStringLiteral("popup.snapAssist"))); // never set
        QVERIFY(tree.clearOverride(QStringLiteral("window.tiled")));
        QVERIFY(!tree.hasOverride(QStringLiteral("window.tiled")));
        QCOMPARE(tree.overriddenPaths(), QStringList{QStringLiteral("osd")});
    }

    void clearAllOverrides_empties_but_keeps_baseline()
    {
        DecorationProfileTree tree;
        tree.setBaseline(makeProfile(QStringList{QStringLiteral("base")}, 1.0, QStringLiteral("#000000")));
        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("x")};
        tree.setOverride(QStringLiteral("osd"), p);

        tree.clearAllOverrides();
        QVERIFY(tree.overriddenPaths().isEmpty());
        QCOMPARE(tree.baseline().effectiveChain(), QStringList{QStringLiteral("base")});
    }

    void setOverride_same_path_replaces_value_keeps_single_entry()
    {
        DecorationProfileTree tree;
        DecorationProfile first;
        first.chain = QStringList{QStringLiteral("a")};
        DecorationProfile second;
        second.chain = QStringList{QStringLiteral("b")};
        tree.setOverride(QStringLiteral("osd"), first);
        tree.setOverride(QStringLiteral("osd"), second);

        QCOMPARE(tree.overriddenPaths(), QStringList{QStringLiteral("osd")});
        QCOMPARE(tree.directOverride(QStringLiteral("osd")).effectiveChain(), QStringList{QStringLiteral("b")});
    }

    // ── Equality ─────────────────────────────────────────────────────────

    void equality_considers_baseline_overrides_and_order()
    {
        DecorationProfileTree a;
        a.setBaseline(makeProfile(QStringList{QStringLiteral("base")}, 1.0, QStringLiteral("#000000")));
        DecorationProfile p;
        p.chain = QStringList{QStringLiteral("x")};
        a.setOverride(QStringLiteral("osd"), p);
        a.setOverride(QStringLiteral("window.tiled"), p);

        DecorationProfileTree b = a;
        QVERIFY(a == b);
        QVERIFY(!(a != b));

        // Different baseline → not equal.
        DecorationProfileTree c = a;
        c.setBaseline(makeProfile(QStringList{QStringLiteral("other")}, 1.0, QStringLiteral("#000000")));
        QVERIFY(a != c);

        // Same members, different insertion order → not equal.
        DecorationProfileTree d;
        d.setBaseline(a.baseline());
        d.setOverride(QStringLiteral("window.tiled"), p);
        d.setOverride(QStringLiteral("osd"), p);
        QVERIFY(a != d);
    }
};

QTEST_MAIN(TestDecorationProfileTree)
#include "test_decorationprofiletree.moc"
