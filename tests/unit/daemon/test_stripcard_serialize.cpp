// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// stripColumnsToVariantList — the QML wire shape behind the strip-mode zone
// selector's stripColumns property. Pins the key set (explicitly, via the
// exact key list — a value compare against a coercion default cannot tell a
// present key from an absent one), the active-column stamp, and the position
// contract (no filtering, no reordering).

#include "common/stripcardserialize.h"
#include "core/types/zoneselectorlayout.h"

#include <PhosphorScrollEngine/ScrollEngineTypes.h>

#include <QtTest>

using namespace PhosphorScrollEngine;

class TestStripCardSerialize : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emitsCanonicalShape()
    {
        ScrollStripSnapshot snap;
        snap.valid = true;
        snap.activeColumnIndex = 1;

        ScrollStripSnapshotColumn normal;
        // Non-default fraction so a dropped insert cannot pass as the
        // absent-key coercion value.
        normal.widthFraction = 0.4;
        // Non-zero origin and non-unit width so x, y and width all have
        // non-default expectations (a dropped insert cannot pass as the
        // absent-key coercion value).
        normal.tiles.append({.windowId = QStringLiteral("a"),
                             .relRect = QRectF(0.25, 0.5, 0.75, 0.5),
                             .minimized = false,
                             .hidden = false,
                             .activeTab = true});
        normal.tiles.append({.windowId = QStringLiteral("b"),
                             .relRect = QRectF(),
                             .minimized = false,
                             .hidden = false,
                             .activeTab = false});

        ScrollStripSnapshotColumn tabbedColumn;
        tabbedColumn.tabbed = true;
        tabbedColumn.tiles.append({.windowId = QStringLiteral("c"),
                                   .relRect = QRectF(0, 0, 1, 1),
                                   .minimized = false,
                                   .hidden = false,
                                   .activeTab = true});
        tabbedColumn.tiles.append({.windowId = QStringLiteral("d"),
                                   .relRect = QRectF(),
                                   .minimized = false,
                                   .hidden = true,
                                   .activeTab = false});

        snap.columns.append(normal);
        snap.columns.append(tabbedColumn);

        const QVariantList list = PlasmaZones::stripColumnsToVariantList(snap);
        QCOMPARE(list.size(), 2);

        // The exact key sets, so a dropped or renamed insert fails here
        // instead of surviving behind a coercion default.
        const QStringList columnKeys{QStringLiteral("active"), QStringLiteral("tabbed"), QStringLiteral("tiles"),
                                     QStringLiteral("widthFraction")};
        const QStringList tileKeys{QStringLiteral("activeTab"), QStringLiteral("height"), QStringLiteral("width"),
                                   QStringLiteral("x"), QStringLiteral("y")};

        const QVariantMap first = list.at(0).toMap();
        QCOMPARE(first.keys(), columnKeys);
        QCOMPARE(first.value(QStringLiteral("tabbed")).toBool(), false);
        QCOMPARE(first.value(QStringLiteral("active")).toBool(), false);
        QCOMPARE(first.value(QStringLiteral("widthFraction")).toReal(), 0.4);
        const QVariantList firstTiles = first.value(QStringLiteral("tiles")).toList();
        QCOMPARE(firstTiles.size(), 2);
        const QVariantMap tileA = firstTiles.at(0).toMap();
        QCOMPARE(tileA.keys(), tileKeys);
        QCOMPARE(tileA.value(QStringLiteral("x")).toReal(), 0.25);
        QCOMPARE(tileA.value(QStringLiteral("y")).toReal(), 0.5);
        QCOMPARE(tileA.value(QStringLiteral("width")).toReal(), 0.75);
        QCOMPARE(tileA.value(QStringLiteral("height")).toReal(), 0.5);
        QCOMPARE(tileA.value(QStringLiteral("activeTab")).toBool(), true);
        const QVariantMap tileB = firstTiles.at(1).toMap();
        // The exact key set on the RECT-LESS tile too: a serializer that
        // skipped the x/y/width/height inserts for a null relRect would
        // still satisfy the value compares below (0.0 is the coercion
        // default), so only the key list can catch that omission.
        QCOMPARE(tileB.keys(), tileKeys);
        QCOMPARE(tileB.value(QStringLiteral("width")).toReal(), 0.0);
        QCOMPARE(tileB.value(QStringLiteral("activeTab")).toBool(), false);

        const QVariantMap second = list.at(1).toMap();
        QCOMPARE(second.keys(), columnKeys);
        QCOMPARE(second.value(QStringLiteral("tabbed")).toBool(), true);
        QCOMPARE(second.value(QStringLiteral("active")).toBool(), true);
        const QVariantList secondTiles = second.value(QStringLiteral("tiles")).toList();
        QCOMPARE(secondTiles.size(), 2);
        QCOMPARE(secondTiles.at(0).toMap().value(QStringLiteral("activeTab")).toBool(), true);
        // The hidden tab is the other rect-less shape; pin its key set too.
        QCOMPARE(secondTiles.at(1).toMap().keys(), tileKeys);
        QCOMPARE(secondTiles.at(1).toMap().value(QStringLiteral("activeTab")).toBool(), false);
    }

    /// stripFractionsFromColumns is the serializer's round-trip partner and
    /// the input to the bar-width math: order and count must survive, and
    /// malformed entries must degrade to 0.0 (which the width formulas turn
    /// into the full-width fallback) rather than crashing or skewing later
    /// positions.
    void fractionsRoundTripAndDegrade()
    {
        ScrollStripSnapshot snap;
        snap.valid = true;
        for (const qreal f : {0.4, 0.25, 1.0}) {
            ScrollStripSnapshotColumn column;
            column.widthFraction = f;
            column.tiles.append({.windowId = QStringLiteral("w"),
                                 .relRect = QRectF(0, 0, 1, 1),
                                 .minimized = false,
                                 .hidden = false,
                                 .activeTab = true});
            snap.columns.append(column);
        }
        const QList<qreal> fractions =
            PlasmaZones::stripFractionsFromColumns(PlasmaZones::stripColumnsToVariantList(snap));
        QCOMPARE(fractions, (QList<qreal>{0.4, 0.25, 1.0}));

        // Malformed input: an empty list, a column map with no widthFraction
        // key, and a non-numeric widthFraction each yield 0.0 per entry with
        // count and order preserved.
        QCOMPARE(PlasmaZones::stripFractionsFromColumns({}).size(), 0);
        QVariantList malformed;
        malformed.append(QVariantMap{}); // no widthFraction key
        malformed.append(QVariantMap{{QStringLiteral("widthFraction"), QStringLiteral("garbage")}});
        malformed.append(QVariantMap{{QStringLiteral("widthFraction"), 0.5}});
        malformed.append(QStringLiteral("not a map"));
        QCOMPARE(PlasmaZones::stripFractionsFromColumns(malformed), (QList<qreal>{0.0, 0.0, 0.5, 0.0}));
    }

    /// stripCardPreviewWidth and the stripFractions arm of
    /// computeZoneSelectorLayout are the C++ half of the three-way
    /// card-width parity chokepoint (ZoneSelectorStripCard.qml and
    /// ZoneSelectorContent.qml mirror the formula). Pin the fraction
    /// fallback, the sliver floor and the summed content width so the QML
    /// mirrors have an executable anchor to be checked against.
    void stripCardWidthParityAnchor()
    {
        using PlasmaZones::stripCardPreviewWidth;
        QCOMPARE(stripCardPreviewWidth(180, 0.5), 90);
        QCOMPARE(stripCardPreviewWidth(180, 1.0), 180); // upper bound is INCLUSIVE
        QCOMPARE(stripCardPreviewWidth(180, 0.0), 180); // non-positive falls back to full width
        QCOMPARE(stripCardPreviewWidth(180, -0.4), 180);
        QCOMPARE(stripCardPreviewWidth(180, 1.5), 180); // oversized falls back to full width
        QCOMPARE(stripCardPreviewWidth(180, 0.01), 8); // sliver floor

        PlasmaZones::ZoneSelectorConfig config;
        config.sizeMode = 1; // Manual, so indicatorWidth is previewWidth verbatim
        config.previewWidth = 180;
        config.previewLockAspect = false;
        config.previewHeight = 101;
        config.layoutMode = 1; // Horizontal (what the strip resolver stamps)
        const QList<qreal> fractions{0.5, 0.25, 1.0};
        const PlasmaZones::ZoneSelectorLayout layout =
            PlasmaZones::computeZoneSelectorLayout(config, QRect(0, 0, 2560, 1440), 3, fractions);
        // Hand-computed: per-card previews 90 + 45 + 180, plus
        // cardSidePadding * 2 chrome per card, plus 2 spacings between the
        // three cards.
        QCOMPARE(layout.scrollContentWidth,
                 90 + 45 + 180 + 3 * layout.cardSidePadding * 2 + 2 * layout.indicatorSpacing);
        // The fraction list is the authority on card count in the strip arm.
        QCOMPARE(layout.columns, 3);
        QCOMPARE(layout.rows, 1);
    }

    /// The VERTICAL arm of the same chokepoint, which had zero unit coverage:
    /// the cards stack DOWN the popup, the per-card extent is measured
    /// against indicatorHeight, and each card adds labelSpace + cardPadding
    /// chrome where the horizontal arm adds side padding. The formula is
    /// rebuilt from stripCardPreviewWidth (pinned independently above) so
    /// this does not use the arm's own answer as its expectation.
    void stripCardWidthParityAnchorVertical()
    {
        using PlasmaZones::stripCardPreviewWidth;
        PlasmaZones::ZoneSelectorConfig config;
        config.sizeMode = 1; // Manual
        config.previewWidth = 180;
        config.previewLockAspect = false;
        config.previewHeight = 101;
        config.layoutMode = 1;
        const QList<qreal> fractions{0.5, 0.25, 1.0};
        const PlasmaZones::ZoneSelectorLayout layout = PlasmaZones::computeZoneSelectorLayout(
            config, QRect(0, 0, 1440, 2560), 3, fractions, /*stripVerticalAxis=*/true);
        QCOMPARE(layout.rows, 3);
        QCOMPARE(layout.columns, 1);
        QCOMPARE(layout.totalRows, 3);
        const int perCardChrome = layout.labelSpace + layout.cardPadding;
        QCOMPARE(
            layout.scrollContentHeight,
            stripCardPreviewWidth(layout.indicatorHeight, 0.5) + stripCardPreviewWidth(layout.indicatorHeight, 0.25)
                + stripCardPreviewWidth(layout.indicatorHeight, 1.0) + 3 * perCardChrome + 2 * layout.indicatorSpacing);
        QCOMPARE(layout.scrollContentWidth, layout.indicatorWidth + layout.cardSidePadding * 2);
    }

    void emptySnapshotSerializesEmpty()
    {
        QCOMPARE(PlasmaZones::stripColumnsToVariantList(ScrollStripSnapshot{}).size(), 0);
    }

    /// An invalid snapshot serializes exactly like its columns say — the
    /// valid flag has no wire representation (the popup renders "no strip
    /// data" and "empty strip" identically on purpose; this pins that the
    /// serializer neither drops columns nor invents an error shape for it).
    void invalidSnapshotStillSerializesColumns()
    {
        ScrollStripSnapshot snap;
        snap.valid = false;
        ScrollStripSnapshotColumn column;
        column.tiles.append({.windowId = QStringLiteral("a"),
                             .relRect = QRectF(0, 0, 1, 1),
                             .minimized = false,
                             .hidden = false,
                             .activeTab = true});
        snap.columns.append(column);
        QCOMPARE(PlasmaZones::stripColumnsToVariantList(snap).size(), 1);
    }

    /// An out-of-range activeColumnIndex stamps no column active.
    void outOfRangeActiveIndexStampsNoColumn()
    {
        ScrollStripSnapshot snap;
        snap.valid = true;
        snap.activeColumnIndex = 5;
        for (int i = 0; i < 2; ++i) {
            ScrollStripSnapshotColumn column;
            column.tiles.append({.windowId = QStringLiteral("w"),
                                 .relRect = QRectF(0, 0, 1, 1),
                                 .minimized = false,
                                 .hidden = false,
                                 .activeTab = true});
            snap.columns.append(column);
        }
        const QVariantList list = PlasmaZones::stripColumnsToVariantList(snap);
        QCOMPARE(list.size(), 2);
        for (const QVariant& entry : list) {
            QCOMPARE(entry.toMap().value(QStringLiteral("active")).toBool(), false);
        }
    }
};

QTEST_APPLESS_MAIN(TestStripCardSerialize)
#include "test_stripcard_serialize.moc"
