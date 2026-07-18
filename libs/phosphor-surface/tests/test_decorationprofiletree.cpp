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

    // ── Seed defaults ────────────────────────────────────────────────────

    /// An empty user tree takes the whole seed layer verbatim: every seed
    /// override lands, in seed insertion order, field-for-field.
    void withSeedDefaults_emptyTree_takesSeedsVerbatim()
    {
        DecorationProfileTree seeds;
        seeds.setOverride(QStringLiteral("osd"),
                          makeProfile(QStringList{QStringLiteral("border"), QStringLiteral("shadow")}, 1.0,
                                      QStringLiteral("#112233")));
        seeds.setOverride(QStringLiteral("popup.layoutPicker"),
                          makeProfile(QStringList{QStringLiteral("border")}, 2.0, QStringLiteral("#445566")));

        const DecorationProfileTree merged = DecorationProfileTree{}.withSeedDefaults(seeds);
        QCOMPARE(merged, seeds);
    }

    /// A user override that engages `chain` at a seeded path wins outright —
    /// including the engaged-but-EMPTY chain, which must keep the surface
    /// explicitly undecorated rather than resurrect the seed. No seed field
    /// (not even parameters) leaks under a user-built chain.
    void withSeedDefaults_engagedChainBlocksInjection()
    {
        DecorationProfileTree seeds;
        seeds.setOverride(QStringLiteral("osd"),
                          makeProfile(QStringList{QStringLiteral("border"), QStringLiteral("shadow")}, 1.0,
                                      QStringLiteral("#112233")));

        // Engaged empty chain = explicitly undecorated.
        DecorationProfileTree cleared;
        DecorationProfile none;
        none.chain = QStringList{};
        cleared.setOverride(QStringLiteral("osd"), none);
        const DecorationProfileTree mergedCleared = cleared.withSeedDefaults(seeds);
        QVERIFY(mergedCleared.resolve(QStringLiteral("osd")).enabledChain().isEmpty());
        QVERIFY2(!mergedCleared.directOverride(QStringLiteral("osd")).parameters.has_value(),
                 "seed parameters must not leak under a user-engaged chain");

        // Engaged non-empty chain: the user look stands untouched.
        DecorationProfileTree custom;
        DecorationProfile glow;
        glow.chain = QStringList{QStringLiteral("glow")};
        custom.setOverride(QStringLiteral("osd"), glow);
        const DecorationProfileTree mergedCustom = custom.withSeedDefaults(seeds);
        QCOMPARE(mergedCustom.resolve(QStringLiteral("osd")).enabledChain(), QStringList{QStringLiteral("glow")});
        QVERIFY(!mergedCustom.directOverride(QStringLiteral("osd")).parameters.has_value());
    }

    /// A parameters-only override at a seeded path is a RETUNE: the seed
    /// chain injects into the unengaged chain slot while the user's engaged
    /// parameters map wins wholesale.
    void withSeedDefaults_parametersOnlyOverride_keepsSeedChain()
    {
        DecorationProfileTree seeds;
        seeds.setOverride(QStringLiteral("osd"),
                          makeProfile(QStringList{QStringLiteral("border"), QStringLiteral("shadow")}, 1.0,
                                      QStringLiteral("#112233")));

        DecorationProfileTree user;
        DecorationProfile retune;
        QVariantMap shadowParams;
        shadowParams.insert(QStringLiteral("shadowStrength"), 0.9);
        QVariantMap params;
        params.insert(QStringLiteral("shadow"), shadowParams);
        retune.parameters = params;
        user.setOverride(QStringLiteral("osd"), retune);

        const DecorationProfile resolved = user.withSeedDefaults(seeds).resolve(QStringLiteral("osd"));
        QCOMPARE(resolved.enabledChain(), (QStringList{QStringLiteral("border"), QStringLiteral("shadow")}));
        QCOMPARE(resolved.effectiveParameters()
                     .value(QStringLiteral("shadow"))
                     .toMap()
                     .value(QStringLiteral("shadowStrength"))
                     .toDouble(),
                 0.9);
    }

    /// A seed BASELINE fills unengaged user-baseline slots, and an engaged
    /// user baseline chain blocks the whole baseline injection.
    void withSeedDefaults_seedBaselineFillsUnengagedSlots()
    {
        DecorationProfileTree seeds;
        seeds.setBaseline(makeProfile(QStringList{QStringLiteral("glow")}, 3.0, QStringLiteral("#112233")));

        // Empty user tree: the seed baseline lands verbatim.
        const DecorationProfileTree merged = DecorationProfileTree{}.withSeedDefaults(seeds);
        QCOMPARE(merged.baseline().effectiveChain(), QStringList{QStringLiteral("glow")});
        QVERIFY(merged.baseline().parameters.has_value());

        // Engaged user baseline chain: nothing injects, not even parameters.
        DecorationProfileTree custom;
        DecorationProfile own;
        own.chain = QStringList{QStringLiteral("border")};
        custom.setBaseline(own);
        const DecorationProfileTree mergedCustom = custom.withSeedDefaults(seeds);
        QCOMPARE(mergedCustom.baseline().effectiveChain(), QStringList{QStringLiteral("border")});
        QVERIFY2(!mergedCustom.baseline().parameters.has_value(),
                 "seed baseline parameters must not leak under a user-engaged baseline chain");
    }

    /// disabledPacks injects like the other fields: it lands in an unengaged
    /// slot and an engaged (even empty) user slot blocks it.
    void withSeedDefaults_disabledPacksInjection()
    {
        DecorationProfileTree seeds;
        DecorationProfile seed = makeProfile(QStringList{QStringLiteral("border"), QStringLiteral("shadow")}, 1.0,
                                             QStringLiteral("#112233"));
        seed.disabledPacks = QStringList{QStringLiteral("shadow")};
        seeds.setOverride(QStringLiteral("osd"), seed);

        // Unengaged slot: the seed's disable set lands (shadow filtered out).
        const DecorationProfileTree merged = DecorationProfileTree{}.withSeedDefaults(seeds);
        QCOMPARE(merged.resolve(QStringLiteral("osd")).enabledChain(), QStringList{QStringLiteral("border")});

        // Engaged-but-empty user slot: explicitly nothing disabled, seed set
        // blocked, both packs render.
        DecorationProfileTree user;
        DecorationProfile allOn;
        allOn.disabledPacks = QStringList{};
        user.setOverride(QStringLiteral("osd"), allOn);
        const DecorationProfileTree mergedAllOn = user.withSeedDefaults(seeds);
        QCOMPARE(mergedAllOn.resolve(QStringLiteral("osd")).enabledChain(),
                 (QStringList{QStringLiteral("border"), QStringLiteral("shadow")}));
    }

    /// Parameters engaged at an ANCESTOR (with no chain anywhere) block the
    /// seed's leaf parameters: resolve() overlays deepest-last, so an injected
    /// leaf map would silently shadow the user's category-level tuning. The
    /// seed chain still injects (its own walk is clear).
    void withSeedDefaults_ancestorParamsBlockSeedLeafParams()
    {
        DecorationProfileTree seeds;
        seeds.setOverride(QStringLiteral("popup.layoutPicker"),
                          makeProfile(QStringList{QStringLiteral("border"), QStringLiteral("shadow")}, 1.0,
                                      QStringLiteral("#112233")));

        DecorationProfileTree user;
        DecorationProfile tune;
        QVariantMap borderParams;
        borderParams.insert(QStringLiteral("width"), 7.0);
        QVariantMap params;
        params.insert(QStringLiteral("border"), borderParams);
        tune.parameters = params;
        user.setOverride(QStringLiteral("popup"), tune);

        const DecorationProfile resolved = user.withSeedDefaults(seeds).resolve(QStringLiteral("popup.layoutPicker"));
        QCOMPARE(resolved.enabledChain(), (QStringList{QStringLiteral("border"), QStringLiteral("shadow")}));
        QCOMPARE(resolved.effectiveParameters()
                     .value(QStringLiteral("border"))
                     .toMap()
                     .value(QStringLiteral("width"))
                     .toDouble(),
                 7.0);
    }

    /// A chain engaged at an ANCESTOR (category override or the baseline)
    /// gates leaf injection too: injecting the seed at the leaf would shadow
    /// the user's broader look in the walk-up.
    void withSeedDefaults_ancestorChainBlocksLeafInjection()
    {
        DecorationProfileTree seeds;
        seeds.setOverride(QStringLiteral("popup.layoutPicker"),
                          makeProfile(QStringList{QStringLiteral("border"), QStringLiteral("shadow")}, 1.0,
                                      QStringLiteral("#112233")));

        // Category override engages a chain for every popup.
        DecorationProfileTree viaCategory;
        DecorationProfile glow;
        glow.chain = QStringList{QStringLiteral("glow")};
        viaCategory.setOverride(QStringLiteral("popup"), glow);
        QCOMPARE(viaCategory.withSeedDefaults(seeds).resolve(QStringLiteral("popup.layoutPicker")).enabledChain(),
                 QStringList{QStringLiteral("glow")});

        // Baseline chain gates everything.
        DecorationProfileTree viaBaseline;
        viaBaseline.setBaseline(glow);
        const DecorationProfileTree merged = viaBaseline.withSeedDefaults(seeds);
        QVERIFY(!merged.hasOverride(QStringLiteral("popup.layoutPicker")));
        QCOMPARE(merged.resolve(QStringLiteral("popup.layoutPicker")).enabledChain(),
                 QStringList{QStringLiteral("glow")});
    }
};

QTEST_MAIN(TestDecorationProfileTree)
#include "test_decorationprofiletree.moc"
