// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// buildCustomOrder() bridges the persisted priority order to the LayoutPreview
// id namespace used by sortPreviews(). Snapping layouts and scrolling templates
// are keyed by the unprefixed braced-UUID string in both, but tiling algorithms
// are stored unprefixed ("bsp") while their previews are keyed "autotile:bsp" —
// so the tiling ids must be prefixed or the priority order silently no-ops for
// the tiling view. ("Unprefixed", not "bare": bare/braced is this repo's
// WithoutBraces axis, and these UUIDs keep their braces.)

#include <QTest>

#include "core/utils/unifiedlayoutlist.h"

#include "helpers/StubSettings.h"

using PhosphorZones::LayoutUtils::buildCustomOrder;

class TestUnifiedLayoutOrder : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tilingOrder_prefixedToPreviewNamespace()
    {
        PlasmaZones::StubSettings s;
        // The snapping order is populated so includeManual=false is proven to
        // GATE (deleting its guard would leak the UUID into the result), not
        // merely to skip an empty list.
        s.setSnappingLayoutOrder({QStringLiteral("{11111111-1111-1111-1111-111111111111}")});
        s.setTilingAlgorithmOrder({QStringLiteral("bsp"), QStringLiteral("theater")});

        const QStringList order = buildCustomOrder(&s, /*includeManual=*/false, /*includeAutotile=*/true);
        QCOMPARE(order, (QStringList{QStringLiteral("autotile:bsp"), QStringLiteral("autotile:theater")}));
    }

    void snappingOrder_keptUnprefixed()
    {
        PlasmaZones::StubSettings s;
        s.setSnappingLayoutOrder({QStringLiteral("{11111111-1111-1111-1111-111111111111}"),
                                  QStringLiteral("{22222222-2222-2222-2222-222222222222}")});
        // Same gating proof, mirrored: includeAutotile=false must actually
        // exclude a populated tiling order.
        s.setTilingAlgorithmOrder({QStringLiteral("bsp")});

        const QStringList order = buildCustomOrder(&s, /*includeManual=*/true, /*includeAutotile=*/false);
        QCOMPARE(order,
                 (QStringList{QStringLiteral("{11111111-1111-1111-1111-111111111111}"),
                              QStringLiteral("{22222222-2222-2222-2222-222222222222}")}));
    }

    void combined_snappingThenPrefixedTiling()
    {
        PlasmaZones::StubSettings s;
        s.setSnappingLayoutOrder({QStringLiteral("{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}")});
        s.setTilingAlgorithmOrder({QStringLiteral("bsp")});

        const QStringList order = buildCustomOrder(&s, /*includeManual=*/true, /*includeAutotile=*/true);
        QCOMPARE(
            order,
            (QStringList{QStringLiteral("{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}"), QStringLiteral("autotile:bsp")}));
    }

    void scrollingOrder_keptUnprefixed()
    {
        PlasmaZones::StubSettings s;
        s.setScrollingTemplateOrder({QStringLiteral("{33333333-3333-3333-3333-333333333333}"),
                                     QStringLiteral("{44444444-4444-4444-4444-444444444444}")});

        const QStringList order = buildCustomOrder(&s, /*includeManual=*/false, /*includeAutotile=*/false,
                                                   /*includeScrollingTemplates=*/true);
        QCOMPARE(order,
                 (QStringList{QStringLiteral("{33333333-3333-3333-3333-333333333333}"),
                              QStringLiteral("{44444444-4444-4444-4444-444444444444}")}));
    }

    void scrollingOrder_excludedWithoutFlag()
    {
        PlasmaZones::StubSettings s;
        s.setSnappingLayoutOrder({QStringLiteral("{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}")});
        s.setTilingAlgorithmOrder({QStringLiteral("bsp")});
        s.setScrollingTemplateOrder({QStringLiteral("{33333333-3333-3333-3333-333333333333}")});

        // A list that carries no template rows must not receive template ids —
        // the flag defaults to false so pre-existing callers stay unchanged.
        // All three orders are populated so this asserts "the template id is
        // absent from a non-empty result", not merely "nothing came back":
        // a bare isEmpty() would keep passing if buildCustomOrder returned
        // nothing at all.
        QCOMPARE(
            buildCustomOrder(&s, /*includeManual=*/true, /*includeAutotile=*/true),
            (QStringList{QStringLiteral("{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}"), QStringLiteral("autotile:bsp")}));
    }

    void allThreeFamilies_concatenatedInFamilyOrder()
    {
        PlasmaZones::StubSettings s;
        s.setSnappingLayoutOrder({QStringLiteral("{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}")});
        s.setTilingAlgorithmOrder({QStringLiteral("bsp")});
        s.setScrollingTemplateOrder({QStringLiteral("{33333333-3333-3333-3333-333333333333}")});

        // The one production caller that enables all three families
        // (LayoutAdaptor::getLayoutList) depends on templates concatenating
        // LAST: template ids landing before the manual/autotile ids would
        // displace those families' indices in sortPreviews' order map.
        QCOMPARE(buildCustomOrder(&s, /*includeManual=*/true, /*includeAutotile=*/true,
                                  /*includeScrollingTemplates=*/true),
                 (QStringList{QStringLiteral("{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}"), QStringLiteral("autotile:bsp"),
                              QStringLiteral("{33333333-3333-3333-3333-333333333333}")}));
    }

    void scrollingOrder_skipsNoneSentinel()
    {
        PlasmaZones::StubSettings s;
        // The reserved "none" word can only enter the key through a writer no
        // validator saw (a hand-edited config.json) — buildCustomOrder must
        // drop it, or the None row gains a finite sort index and floats out
        // of its pinned-last place.
        s.setScrollingTemplateOrder({QStringLiteral("{33333333-3333-3333-3333-333333333333}"), QStringLiteral("none"),
                                     QStringLiteral("{44444444-4444-4444-4444-444444444444}")});

        QCOMPARE(buildCustomOrder(&s, /*includeManual=*/false, /*includeAutotile=*/false,
                                  /*includeScrollingTemplates=*/true),
                 (QStringList{QStringLiteral("{33333333-3333-3333-3333-333333333333}"),
                              QStringLiteral("{44444444-4444-4444-4444-444444444444}")}));
    }

    void nullSettings_returnsEmpty()
    {
        QVERIFY(buildCustomOrder(nullptr, true, true).isEmpty());
    }
};

QTEST_MAIN(TestUnifiedLayoutOrder)
#include "test_unified_layout_order.moc"
