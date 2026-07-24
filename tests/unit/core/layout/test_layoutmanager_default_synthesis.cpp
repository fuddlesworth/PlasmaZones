// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layoutmanager_default_synthesis.cpp
 * @brief Level-1 default synthesis + suppression cascade tests for PhosphorZones::LayoutRegistry
 */

#include <QTest>
#include <QScopedPointer>
#include <QUuid>
#include <memory>

#include "LayoutManagerAssignmentFixture.h"

using namespace PlasmaZones;

class TestLayoutManagerDefaultSynthesis : public LayoutManagerAssignmentFixture
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // Level-1 cascade default — symmetric snap + autotile providers (issue #368)
    //
    // The level-1 (global) tier of the assignment hierarchy is two pass-through
    // providers: setDefaultLayoutIdProvider (snap UUID) and
    // setDefaultAutotileAlgorithmProvider (autotile algorithm). Composition
    // roots gate each on its own enabled flag; the library decides precedence
    // (snap > autotile) so the daemon stays free of mode-priority logic. Pin
    // the four enabled-flag combinations and the cascade-takes-precedence rule.
    // ═══════════════════════════════════════════════════════════════════════════

    void testLevel1Default_autotileOnly_synthesizesAutotile()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // Snap disabled (provider returns empty), autotile provider returns
        // an algorithm — the cascade should resolve autotile.
        mgr->setDefaultLayoutIdProvider([]() {
            return QString();
        });
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry.tilingAlgorithm, QStringLiteral("bsp"));
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:bsp"));
    }

    void testLevel1Default_snapOnly_synthesizesSnap()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QString();
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layoutId);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), layoutId);
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 4), layout);
    }

    void testLevel1Default_bothEmpty_returnsNoEntry()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // Both providers return empty — neither snap nor autotile is the
        // user's active default. Cascade should treat as "no assignment".
        mgr->setDefaultLayoutIdProvider([]() {
            return QString();
        });
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QString();
        });

        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QVERIFY(!entry.isValid());
    }

    void testLevel1Default_bothSet_snapWinsByLibraryPrecedence()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        // Both providers return non-empty — library precedence picks snap.
        // Composition roots that wire both provider lambdas without
        // gating on enabled flags will see snap consistently.
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layoutId);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), layoutId);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Suppress default layout assignment — the global gate + per-context override.
    //
    // The global setDefaultAssignmentSuppressedProvider gate makes an unassigned
    // context resolve to NO active layout (the same empty-entry state as having no
    // providers). A per-context DefaultLayoutAssignment rule overrides the global
    // baseline either way: false suppresses one context even when global allows,
    // true forces the default through even when global suppresses. Explicit
    // assignments are never affected — suppression only gates the synthesized
    // default.
    // ═══════════════════════════════════════════════════════════════════════════

    void testSuppressDefault_globalOn_suppressesSynthesizedDefault()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // Global suppress on — the snap default must NOT be synthesized.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QVERIFY(!entry.isValid());
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
    }

    void testSuppressDefault_globalOff_synthesizesAsBefore()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // Provider wired but reports off — behaviour is unchanged from today.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return false;
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.snappingLayout, layoutId);
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), layoutId);
    }

    void testSuppressDefault_perContextSuppress_overridesGlobalAllow()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // Global allows the default (off). A per-context rule suppresses DP-1.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return false;
        });
        addDefaultAssignmentRule(mgr.data(), QStringLiteral("DP-1"), 0, QString(), /*allow=*/false);

        // DP-1 is suppressed by its rule.
        QVERIFY(!mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4).isValid());
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        // DP-2 (no rule) still gets the global default.
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-2"), 4), layoutId);
    }

    void testSuppressDefault_perContextAllow_overridesGlobalSuppress()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);

        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // Global suppresses everywhere. A per-context rule re-enables DP-1.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        addDefaultAssignmentRule(mgr.data(), QStringLiteral("DP-1"), 0, QString(), /*allow=*/true);

        // DP-1 forces the default through.
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), layoutId);
        // DP-2 (no rule) stays suppressed by the global setting.
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-2"), 4).isEmpty());
    }

    void testSuppressDefault_explicitAssignmentUnaffected()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);

        // Global suppress on, but the user has an explicit assignment on DP-1.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);

        // The explicit assignment wins — suppression only gates the synthesized
        // default, never a stored assignment.
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QCOMPARE(entry.snappingLayout, layout->id().toString());
        QCOMPARE(entry.activeLayoutId(), layout->id().toString());
    }

    // isContextActiveLayoutSuppressed — the daemon's "no active layout because of
    // suppression" gate. Must fire ONLY when suppression is the cause, never for
    // other empty-assignment states (e.g. snapping enabled with no default id),
    // so the daemon's defaultLayout() fallbacks are preserved there.

    void testIsContextSuppressed_globalOn_unassigned_true()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);
        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        QVERIFY(mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 4));
    }

    void testIsContextSuppressed_globalOff_noDefault_false()
    {
        // Regression guard: no providers wired (no default configured) is NOT
        // suppression — the daemon must keep its defaultLayout() fallback here.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return false;
        });
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        QVERIFY(!mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 4));
    }

    void testIsContextSuppressed_explicitAssignment_false()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Manual"));
        mgr->addLayout(layout);
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), layout);
        QVERIFY(!mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 0));
    }

    void testIsContextSuppressed_perContextOverrides()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);
        const QString layoutId = layout->id().toString();
        mgr->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });
        // Global off, but a per-context suppress rule on DP-1.
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return false;
        });
        addDefaultAssignmentRule(mgr.data(), QStringLiteral("DP-1"), 0, QString(), /*allow=*/false);
        QVERIFY(mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 4));
        // DP-2 (no rule) follows the global off → not suppressed.
        QVERIFY(!mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-2"), 4));
    }

    void testIsContextSuppressed_pinnedModeOnlyRule_overridesGlobalSuppress()
    {
        // A rule that sets only the engine mode (no layout) on a monitor
        // must override the global suppress setting — the context stays active so
        // the overlay / zone selector show and the layout falls back to default.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Snap"));
        mgr->addLayout(layout);
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        addEngineModeRule(mgr.data(), QStringLiteral("DP-1"), 2, QString(), QStringLiteral("snapping"));
        // DP-1 has a pinned mode rule → not suppressed despite global suppress.
        QVERIFY(!mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 2));
        // DP-2 (no rule) → still suppressed by the global setting.
        QVERIFY(mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-2"), 2));
    }

    void testDefaultSuppressedForContext_bareAutotileModeRule_stillSuppressed()
    {
        // The autotile gate's primitive. A mode-only autotile rule sets the mode
        // but draws its algorithm from the suppressed global default. So the
        // context is NOT "active-layout-suppressed" (the mode rule covers it),
        // yet its DEFAULT *is* suppressed — which is what the autotile activation
        // gate checks to refuse tiling a bare "autotile:" context.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });
        addEngineModeRule(mgr.data(), QStringLiteral("DP-1"), 2, QString(), QStringLiteral("autotile"));
        // The mode rule covers the context → not active-layout-suppressed.
        QVERIFY(!mgr->isContextActiveLayoutSuppressed(QStringLiteral("DP-1"), 2));
        // But the synthesized default IS suppressed → bare autotile must not tile.
        QVERIFY(mgr->isDefaultAssignmentSuppressedForContext(QStringLiteral("DP-1"), 2));
        // A per-context allow override flips it back on.
        addDefaultAssignmentRule(mgr.data(), QStringLiteral("DP-1"), 2, QString(), /*allow=*/true);
        QVERIFY(!mgr->isDefaultAssignmentSuppressedForContext(QStringLiteral("DP-1"), 2));
    }

    void testLevel1Default_storedEntryTakesPrecedence()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("PerDesktop"));
        mgr->addLayout(layout);

        // Autotile is the global default, but desktop 1 has an explicit
        // snap assignment — explicit cascade entries always win.
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });
        mgr->assignLayout(QStringLiteral("DP-1"), 1, QString(), layout);

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 1);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, layout->id().toString());

        // Other desktops still get the synthesized autotile default.
        const auto desk2 = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2);
        QCOMPARE(desk2.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(desk2.tilingAlgorithm, QStringLiteral("bsp"));
    }

    void testLevel1Default_noProviders_preservesPre368Behaviour()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // No providers set at all — cascade miss returns default-constructed
        // entry, matching the historical behaviour callers rely on.
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QVERIFY(!entry.isValid());
    }

    void testLevel1Default_snapWithUnknownUuid_layoutForScreenFallsThrough()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* registered = createTestLayout(QStringLiteral("Registered"));
        mgr->addLayout(registered);

        // Snap provider returns a UUID that's NOT in the registry — a stale
        // defaultLayoutId from settings. layoutForScreen must still resolve
        // to a real Layout* (defaultLayout falls back to first by defaultOrder).
        const QString bogusId = QUuid::createUuid().toString();
        mgr->setDefaultLayoutIdProvider([bogusId]() {
            return bogusId;
        });

        // assignmentIdForScreen surfaces the raw stored string — KCM/UI
        // can warn or clear it.
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), bogusId);
        QCOMPARE(mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4).snappingLayout, bogusId);
        // layoutForScreen must not return nullptr.
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 4), registered);
    }

    void testLevel1Default_autotileWithEmptyAlgorithm_treatedAsNoAutotile()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        // Provider returns empty algorithm — composition root would only
        // do this if autotile is disabled, so the cascade should treat as
        // "no level-1 autotile default" and fall through to no entry.
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QString();
        });

        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
        QVERIFY(!mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4).isValid());
    }

    void testLevel1Default_cascadeHitSkipsProviders()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Stored"));
        mgr->addLayout(layout);
        mgr->assignLayout(QStringLiteral("DP-1"), 1, QString(), layout);

        auto snapCalls = std::make_shared<int>(0);
        auto autoCalls = std::make_shared<int>(0);
        mgr->setDefaultLayoutIdProvider([snapCalls]() {
            ++(*snapCalls);
            return QString(); // empty so autotile would be consulted
        });
        mgr->setDefaultAutotileAlgorithmProvider([autoCalls]() {
            ++(*autoCalls);
            return QStringLiteral("bsp");
        });

        // Exact-key cascade hit — neither provider should be invoked.
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 1);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(*snapCalls, 0);
        QCOMPARE(*autoCalls, 0);

        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 1), layout->id().toString());
        QCOMPARE(*snapCalls, 0);
        QCOMPARE(*autoCalls, 0);

        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 1), layout);
        QCOMPARE(*snapCalls, 0);
        QCOMPARE(*autoCalls, 0);

        // Cascade miss on a different desktop — both providers consulted in
        // priority order: snap first (returns empty), then autotile (wins).
        (void)mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 2);
        QCOMPARE(*snapCalls, 1);
        QCOMPARE(*autoCalls, 1);
    }

    void testLevel1Default_replaceProviderReplacesBehaviour()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:bsp"));

        // Replace — second value wins, not stacks.
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("dwindle");
        });
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:dwindle"));

        // Clearing restores pre-368 cascade behaviour.
        mgr->setDefaultAutotileAlgorithmProvider({});
        QVERIFY(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4).isEmpty());
    }

    // The daemon's snap provider returns m_settings->defaultLayoutId() directly
    // when snappingEnabled is true. If defaultLayoutId() is empty (e.g. a
    // user has snap on but never picked a default layout), the snap provider
    // returns empty and the cascade falls through to the autotile branch. The
    // user explicitly enabled snap, but they get autotile on unconfigured
    // contexts. This pins that behaviour so anyone changing it has to
    // explicitly decide whether the silent mode-swap is desirable; a future
    // tightening (snap mode with empty layout treated as a sentinel "snap, no
    // zones") would update this expectation.
    void testLevel1Default_snapEnabledEmptyId_autotileEnabled_autotileWins()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultLayoutIdProvider([]() {
            return QString();
        });
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });

        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 4);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(entry.tilingAlgorithm, QStringLiteral("bsp"));
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 4), QStringLiteral("autotile:bsp"));
    }

    // Cascade level-2 (per-screen base entry, key = (screen, 0, "")) must
    // hide level-1 providers. Without this, a per-screen base assignment
    // would be silently shadowed by the user's global default whenever the
    // queried desktop didn't have its own explicit entry. Companion to
    // testLevel1Default_cascadeHitSkipsProviders, which covers the level-3
    // exact-key case; this one covers the more interesting level-2 case.
    void testLevel1Default_perScreenBaseEntryHidesProviders()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* baseLayout = createTestLayout(QStringLiteral("PerScreenBase"));
        mgr->addLayout(baseLayout);

        // Per-screen base entry on DP-1 (no per-desktop entries).
        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), baseLayout);

        // Both providers wired with non-empty values that would otherwise
        // win at level-1 if the cascade missed.
        auto snapCalls = std::make_shared<int>(0);
        auto autoCalls = std::make_shared<int>(0);
        mgr->setDefaultLayoutIdProvider([snapCalls]() {
            ++(*snapCalls);
            return QStringLiteral("{ffffffff-ffff-ffff-ffff-ffffffffffff}");
        });
        mgr->setDefaultAutotileAlgorithmProvider([autoCalls]() {
            ++(*autoCalls);
            return QStringLiteral("bsp");
        });

        // Querying any desktop on DP-1 should resolve to the per-screen
        // base entry — the cascade hits at level-2 and providers are not
        // consulted. Pinning the call counter at 0 catches future
        // refactors that would re-order resolution.
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 5);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, baseLayout->id().toString());
        QCOMPARE(*snapCalls, 0);
        QCOMPARE(*autoCalls, 0);

        // Same query via the id-only and Layout* paths, same expectation.
        QCOMPARE(mgr->assignmentIdForScreen(QStringLiteral("DP-1"), 5), baseLayout->id().toString());
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 5), baseLayout);
        QCOMPARE(*snapCalls, 0);
        QCOMPARE(*autoCalls, 0);

        // A different screen with no entry DOES fall through to providers.
        (void)mgr->assignmentEntryForScreen(QStringLiteral("HDMI-2"), 5);
        QCOMPARE(*snapCalls, 1);
        // Snap provider returned non-empty (bogus uuid), so autotile not consulted.
        QCOMPARE(*autoCalls, 0);
    }

    // hasExplicitAssignment must distinguish "stored" from "synthesized
    // fallback". This is the building block consumers like the D-Bus
    // getAllScreenAssignments JSON readback rely on to avoid round-tripping
    // the synthesized default back into stored state (which would shadow
    // future global-default changes). The KCM JSON gate added in
    // src/dbus/layoutadaptor/assignment.cpp is correct only as long as
    // this invariant holds — pin it here so a future refactor that
    // accidentally makes hasExplicitAssignment provider-aware would fail
    // this test.
    void testLevel1Default_hasExplicitAssignmentIgnoresSynth()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        mgr->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });

        // Cascade miss returns a synthesized entry…
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("DP-1"), 0);
        QVERIFY(entry.isValid());
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);

        // …but hasExplicitAssignment correctly reports "no stored entry".
        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 0, QString()));
        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 5, QString()));

        // After an explicit assignment, hasExplicitAssignment flips true
        // for that exact key only.
        auto* layout = createTestLayout(QStringLiteral("Stored"));
        mgr->addLayout(layout);
        mgr->assignLayout(QStringLiteral("DP-1"), 1, QString(), layout);

        QVERIFY(mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 1, QString()));
        // Sibling desktop is still synth-only, not explicit.
        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("DP-1"), 2, QString()));
        // Different screen is also still synth-only.
        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("HDMI-2"), 1, QString()));
    }
};

QTEST_MAIN(TestLayoutManagerDefaultSynthesis)
#include "test_layoutmanager_default_synthesis.moc"
