// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QColor>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

#include <QtPlugin>

// Force-link the static QML module's auto-generated init symbol so the test
// binary registers `org.plasmazones.common` types into the QmlEngine. Without
// this the linker drops the init code as dead and the import fails to resolve.
// The symbol name is the qmldir's `classname` field.
Q_IMPORT_PLUGIN(org_plasmazones_commonPlugin)

/**
 * @brief Per-zone highlight semantics of the shared ZonePreview.
 *
 * The zone selector picks one zone out of a layout card while a window is
 * dragged: OverlayService::updateSelectorPosition hit-tests the cursor and
 * writes `selectedZoneIndex` down to the card's ZonePreview. That index is only
 * observable if the singled-out zone renders differently from its siblings.
 *
 * ZonePreview is shared with consumers that pass NO per-zone selection (layout
 * picker, layout OSD, settings thumbnails) and rely on the card-level
 * `isActive` / `isHovered` states lighting every zone at once. Those two
 * behaviours live in one expression, so this pins both halves: card-level state
 * lights everything only while no specific zone is selected.
 *
 * The same component also draws the scrolling strip previews' tab indicators,
 * and the second half of this file pins those: which zones get a band at all,
 * one pill per tab, the edge and length the engine resolved, which pill is lit,
 * and that a column with more tabs than the band has pixels stays inside its
 * own tile. They live here because they are the same shared component, and a
 * layout host sprouting indicators for windows that do not exist is a
 * regression in the highlight surface above as much as in the strip.
 */
class TestZonePreviewHighlight : public QObject
{
    Q_OBJECT

private:
    /// The wire values of PhosphorScrollEngine::TabIndicatorPosition, which
    /// reaches the preview as a bare int (PhosphorProtocol StripPreviewKey).
    /// Spelled here for readability only — nothing in this TU sees the C++
    /// enum, and the QML carries its own copy of the numbering, so this does
    /// NOT catch a renumbering upstream. The static_asserts in
    /// settingsschema_scrolling.cpp catch an enum-only renumbering, and
    /// test_stripzones_tabkeys covers a coordinated one.
    enum TabPosition {
        Left = 0,
        Right = 1,
        Top = 2,
        Bottom = 3
    };

    /// Half a pixel. Wide enough to absorb the fractional gap arithmetic the
    /// delegates apply, far narrower than the tens of pixels separating any
    /// two of the four edges in these fixtures, so a band on the wrong edge
    /// cannot slip through it.
    static constexpr qreal kEdgeEpsilon = 0.51;

    QQmlEngine m_engine;

    /// Four side-by-side zones in the flat x/y/width/height wire shape that
    /// layoutpreviewserialize.cpp emits for the selector.
    static QVariantList fourZones()
    {
        QVariantList zones;
        for (int i = 0; i < 4; ++i) {
            QVariantMap zone;
            zone[QLatin1String("x")] = i * 0.25;
            zone[QLatin1String("y")] = 0.0;
            zone[QLatin1String("width")] = 0.25;
            zone[QLatin1String("height")] = 1.0;
            zone[QLatin1String("zoneNumber")] = i + 1;
            // Stable per-zone id for the highlightedZoneIds path; unused by
            // the index-based tests.
            zone[QLatin1String("zoneId")] = QStringLiteral("zone-%1").arg(i + 1);
            zones.append(zone);
        }
        return zones;
    }

    /// Instantiate a ZonePreview with `properties` applied, and return the
    /// per-zone highlight state in zone order.
    QList<bool> highlightStates(const QVariantMap& properties)
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.ZonePreview { width: 180; height: 101 }\n",
            QUrl(QStringLiteral("qrc:/test_zone_preview_highlight.qml")));
        QList<bool> states;
        if (!component.isReady()) {
            qWarning() << "component not ready:" << component.errorString();
            return states;
        }

        // `zones` is a required property, so it has to be supplied at creation
        // rather than assigned afterwards.
        QVariantMap initial = properties;
        initial[QStringLiteral("zones")] = fourZones();

        auto* preview = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        if (!preview) {
            qWarning() << "create failed:" << component.errorString();
            return states;
        }

        // The Repeater parents each zone delegate to the ZonePreview itself, in
        // model order. Delegates are the only children carrying the property.
        const auto kids = preview->childItems();
        for (QQuickItem* kid : kids) {
            const QVariant highlighted = kid->property("isZoneHighlighted");
            if (highlighted.isValid()) {
                states.append(highlighted.toBool());
            }
        }
        delete preview;
        return states;
    }

    /// Instantiate a ZonePreview over fourZones() with the zone at
    /// @p tabbedIndex carrying strip tab data, and return the preview. It is
    /// parented to the QML engine so its items outlive the call; the whole
    /// tree dies with the fixture. Returns nullptr if creation failed, which
    /// every caller must check — an unchecked null here turns a QML regression
    /// into a crash instead of a failure.
    QQuickItem* tabPreview(int tabCount, int tabbedIndex, int position, qreal lengthProportion, int activeTab = 0)
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.ZonePreview { width: 180; height: 101 }\n",
            QUrl(QStringLiteral("qrc:/test_zone_preview_tabs.qml")));
        if (!component.isReady()) {
            qWarning() << "component not ready:" << component.errorString();
            return nullptr;
        }

        QVariantList zones = fourZones();
        QVariantMap tabbed = zones.at(tabbedIndex).toMap();
        tabbed[QLatin1String("tabCount")] = tabCount;
        tabbed[QLatin1String("activeTab")] = activeTab;
        tabbed[QLatin1String("tabPosition")] = position;
        tabbed[QLatin1String("tabLength")] = lengthProportion;
        zones[tabbedIndex] = tabbed;

        QVariantMap initial;
        initial[QStringLiteral("zones")] = zones;
        auto* preview =
            qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial, m_engine.rootContext()));
        if (!preview) {
            qWarning() << "create failed:" << component.errorString();
            return nullptr;
        }
        preview->setParent(&m_engine);
        return preview;
    }

    /// The tab-indicator bands of a preview. Only a zone carrying tab data has
    /// one, so this is also the count of zones the preview thinks are tabbed.
    static QList<QQuickItem*> bandsOf(QQuickItem* preview)
    {
        QList<QQuickItem*> bands;
        for (QQuickItem* kid : preview->childItems()) {
            if (kid->objectName() == QLatin1String("zonePreviewTabIndicator")) {
                bands.append(kid);
            }
        }
        return bands;
    }

    /// The zone delegates of a preview, in model order. The band's expected
    /// geometry is read off these rather than recomputed from the fixture's
    /// fractions, so the assertions cannot drift with the gap and minimum-size
    /// handling the delegates apply.
    static QList<QQuickItem*> zonesOf(QQuickItem* preview)
    {
        QList<QQuickItem*> zones;
        for (QQuickItem* kid : preview->childItems()) {
            if (kid->objectName() == QLatin1String("zonePreviewZone")) {
                zones.append(kid);
            }
        }
        return zones;
    }

    /// The pills of one tab band: its Rectangle children, told apart from the
    /// band's own Repeater by the `radius` only a Rectangle carries.
    static QList<QQuickItem*> pillsOf(QQuickItem* band)
    {
        QList<QQuickItem*> pills;
        for (QQuickItem* kid : band->childItems()) {
            if (kid->property("radius").isValid()) {
                pills.append(kid);
            }
        }
        return pills;
    }

    /// A bare ZonePreview at @p width by @p height with @p axisHint, no zones.
    /// The ticks are what is under test, so an empty strip is the honest
    /// fixture: they must draw without any zone to hang off.
    QQuickItem* axisPreview(const QString& axisHint, qreal width, qreal height)
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.ZonePreview { }\n",
            QUrl(QStringLiteral("qrc:/test_zone_preview_axis.qml")));
        if (!component.isReady()) {
            qWarning() << "component not ready:" << component.errorString();
            return nullptr;
        }
        QVariantMap initial;
        initial[QStringLiteral("zones")] = QVariantList();
        initial[QStringLiteral("stripAxisHint")] = axisHint;
        initial[QStringLiteral("width")] = width;
        initial[QStringLiteral("height")] = height;
        initial[QStringLiteral("edgeGap")] = 3.0;
        auto* preview = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        if (!preview) {
            qWarning() << "create failed:" << component.errorString();
            return nullptr;
        }
        preview->setParent(&m_engine);
        return preview;
    }

    /// One axis tick by object name, or nullptr.
    static QQuickItem* tickOf(QQuickItem* preview, const char* name)
    {
        for (QQuickItem* kid : preview->childItems()) {
            if (kid->objectName() == QLatin1String(name)) {
                return kid;
            }
        }
        return nullptr;
    }

private Q_SLOTS:
    /// A layout host must draw NO ticks. ZonePreview is shared with the
    /// picker, the OSD and every settings thumbnail, and a chevron on a snap
    /// layout would claim it scrolls.
    void testNoAxisTicksWithoutAHint()
    {
        QQuickItem* preview = axisPreview(QStringLiteral("none"), 200, 120);
        QVERIFY(preview);
        QVERIFY(!tickOf(preview, "zonePreviewAxisTickStart")->isVisible());
        QVERIFY(!tickOf(preview, "zonePreviewAxisTickEnd")->isVisible());
    }

    /// The horizontal ticks sit on the LEFT and RIGHT edges, vertically
    /// centred. The chevron is built pointing left and rotated into the other
    /// three directions about its own centre, so a box that is not square
    /// silently offsets the rotated legs — these two cases are what pins the
    /// square-box choice in ZonePreview.
    void testHorizontalAxisTicksSitOnTheSideEdges()
    {
        QQuickItem* preview = axisPreview(QStringLiteral("horizontal"), 200, 120);
        QVERIFY(preview);
        QQuickItem* start = tickOf(preview, "zonePreviewAxisTickStart");
        QQuickItem* end = tickOf(preview, "zonePreviewAxisTickEnd");
        QVERIFY(start && end);
        QVERIFY(start->isVisible());
        QVERIFY(end->isVisible());

        // Square, so `rotation` about the centre cannot move the on-screen box.
        QCOMPARE(start->width(), start->height());

        // Inset from each side edge by the edge gap, and the pair is
        // symmetric about the box's vertical centre line.
        QVERIFY(qAbs(start->x() - 3.0) < kEdgeEpsilon);
        QVERIFY(qAbs((preview->width() - end->x() - end->width()) - 3.0) < kEdgeEpsilon);
        QVERIFY(qAbs(start->y() + start->height() / 2 - preview->height() / 2) < kEdgeEpsilon);
        QVERIFY(qAbs(end->y() + end->height() / 2 - preview->height() / 2) < kEdgeEpsilon);

        // Pointing outward, away from each other: 0 is left, 180 is right.
        QCOMPARE(start->rotation(), 0.0);
        QCOMPARE(end->rotation(), 180.0);
    }

    /// The vertical strip's ticks transpose to the TOP and BOTTOM edges.
    /// Drawing the horizontal pair on a vertical strip would name a direction
    /// that screen never takes, which is the one thing the old sketch got
    /// right and this must not lose.
    void testVerticalAxisTicksSitOnTheTopAndBottomEdges()
    {
        QQuickItem* preview = axisPreview(QStringLiteral("vertical"), 120, 200);
        QVERIFY(preview);
        QQuickItem* start = tickOf(preview, "zonePreviewAxisTickStart");
        QQuickItem* end = tickOf(preview, "zonePreviewAxisTickEnd");
        QVERIFY(start && end);
        QVERIFY(start->isVisible());
        QVERIFY(end->isVisible());

        // Asserted on BOTH ticks and before anything else, because it is the
        // precondition that makes every coordinate below mean what it says.
        // These legs are rotated 90 and 270 degrees about their own centres,
        // so on a non-square box the on-screen extent swaps and x/y stop
        // describing where the chevron actually lands. Without this the rest
        // of the test passes on a visibly misplaced tick.
        QCOMPARE(start->width(), start->height());
        QCOMPARE(end->width(), end->height());

        QVERIFY(qAbs(start->y() - 3.0) < kEdgeEpsilon);
        QVERIFY(qAbs((preview->height() - end->y() - end->height()) - 3.0) < kEdgeEpsilon);
        QVERIFY(qAbs(start->x() + start->width() / 2 - preview->width() / 2) < kEdgeEpsilon);
        QVERIFY(qAbs(end->x() + end->width() / 2 - preview->width() / 2) < kEdgeEpsilon);

        QCOMPARE(start->rotation(), 90.0);
        QCOMPARE(end->rotation(), 270.0);
    }

    /// Each chevron is two strokes meeting at a shared tip. They pivot on
    /// Item.Left at the SAME origin — splaying them from their own centres
    /// instead opens the shape into a Z, which still looks like a mark and so
    /// would not fail any of the placement assertions above.
    void testAxisTickStrokesShareOneTip()
    {
        QQuickItem* preview = axisPreview(QStringLiteral("horizontal"), 200, 120);
        QVERIFY(preview);
        QQuickItem* start = tickOf(preview, "zonePreviewAxisTickStart");
        QVERIFY(start);

        // Told apart from the chevron's own Repeater by the `radius` only a
        // Rectangle carries, the same discriminator pillsOf uses.
        QList<QQuickItem*> strokes;
        for (QQuickItem* kid : start->childItems()) {
            if (kid->property("radius").isValid()) {
                strokes.append(kid);
            }
        }
        QCOMPARE(strokes.size(), 2);
        QCOMPARE(strokes.at(0)->x(), strokes.at(1)->x());
        QCOMPARE(strokes.at(0)->y(), strokes.at(1)->y());
        // Splayed either side of the axis, not stacked.
        QCOMPARE(strokes.at(0)->rotation(), -45.0);
        QCOMPARE(strokes.at(1)->rotation(), 45.0);
    }

    /// The ticks must survive the hop through LayoutCard, which is how every
    /// real host reaches ZonePreview (the settings thumbnail, the picker and
    /// the selector all go through it; only the OSD instantiates the preview
    /// directly).
    ///
    /// This exists because the analogous hop was MISSED on the shell side:
    /// LayoutOsdContent grew the property and the C++ pushed it, but the
    /// osdSlot in between never declared it, so setProperty quietly created a
    /// dead dynamic property and no tick ever drew. A property that is only
    /// tested at its two ends passes while the middle is unwired.
    void testLayoutCardForwardsTheAxisHintToItsPreview()
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.LayoutCard { previewWidth: 200; previewHeight: 120 }\n",
            QUrl(QStringLiteral("qrc:/test_layout_card_axis.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QVariantMap initial;
        QVariantMap layoutData;
        layoutData[QStringLiteral("zones")] = fourZones();
        initial[QStringLiteral("layoutData")] = layoutData;
        initial[QStringLiteral("stripAxisHint")] = QStringLiteral("vertical");
        auto* card = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        QVERIFY2(card, qPrintable(component.errorString()));
        card->setParent(&m_engine);

        // Identified by carrying BOTH `zones` and `stripAxisHint`, which is
        // ZonePreview's signature and nothing else in the card's tree has.
        // Not by the delegates' objectName: those are QObject-parented to
        // their Repeater, so walking up from one is a detour through an
        // ownership chain this test has no reason to depend on.
        QQuickItem* preview = nullptr;
        for (QQuickItem* kid : card->findChildren<QQuickItem*>()) {
            if (kid->property("zones").isValid() && kid->property("stripAxisHint").isValid()) {
                preview = kid;
                break;
            }
        }
        QVERIFY(preview);
        QCOMPARE(preview->property("stripAxisHint").toString(), QStringLiteral("vertical"));
        QVERIFY(tickOf(preview, "zonePreviewAxisTickStart")->isVisible());
    }

    /// An empty-strip caption swaps the well's contents: the zone preview goes
    /// away (its own ticks would double up with the arrow) and the empty state
    /// takes over. Same missed-hop risk as above.
    void testLayoutCardEmptyCaptionReplacesThePreview()
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.LayoutCard { previewWidth: 200; previewHeight: 120 }\n",
            QUrl(QStringLiteral("qrc:/test_layout_card_empty.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        QVariantMap initial;
        QVariantMap layoutData;
        layoutData[QStringLiteral("zones")] = QVariantList();
        initial[QStringLiteral("layoutData")] = layoutData;
        initial[QStringLiteral("stripAxisHint")] = QStringLiteral("horizontal");
        initial[QStringLiteral("stripEmptyCaption")] = QStringLiteral("No windows on the strip yet");
        auto* card = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        QVERIFY2(card, qPrintable(component.errorString()));
        card->setParent(&m_engine);

        // The empty state carries the caption; the preview is hidden.
        bool sawCaption = false;
        for (QQuickItem* kid : card->findChildren<QQuickItem*>()) {
            const QVariant caption = kid->property("caption");
            if (caption.isValid() && caption.toString() == QLatin1String("No windows on the strip yet")) {
                sawCaption = true;
                QVERIFY(kid->isVisible());
                QCOMPARE(kid->property("verticalAxis").toBool(), false);
            }
            const QVariant hint = kid->property("stripAxisHint");
            if (hint.isValid() && kid->property("zones").isValid()) {
                QVERIFY(!kid->isVisible());
            }
        }
        QVERIFY(sawCaption);
    }

    /// The zone selector's active layout card. Before the fix `isActive` lit
    /// every zone, so the hit-tested zone was indistinguishable and picking a
    /// specific zone to snap into had no visible feedback.
    void testActiveCardHighlightsOnlySelectedZone()
    {
        QVariantMap props;
        props[QStringLiteral("isActive")] = true;
        props[QStringLiteral("selectedZoneIndex")] = 2;

        const QList<bool> states = highlightStates(props);
        QCOMPARE(states, QList<bool>({false, false, true, false}));
    }

    /// The card under the cursor: LayoutCard maps its `isSelected` onto
    /// ZonePreview's `isHovered`, so the whole card reads as hovered.
    void testHoveredCardHighlightsOnlySelectedZone()
    {
        QVariantMap props;
        props[QStringLiteral("isHovered")] = true;
        props[QStringLiteral("selectedZoneIndex")] = 1;

        const QList<bool> states = highlightStates(props);
        QCOMPARE(states, QList<bool>({false, true, false, false}));
    }

    /// Consumers with no per-zone selection keep the whole-card highlight.
    void testCardStateLightsEveryZoneWithoutSelection()
    {
        QVariantMap active;
        active[QStringLiteral("isActive")] = true;
        active[QStringLiteral("selectedZoneIndex")] = -1;
        QCOMPARE(highlightStates(active), QList<bool>({true, true, true, true}));

        QVariantMap hovered;
        hovered[QStringLiteral("isHovered")] = true;
        hovered[QStringLiteral("selectedZoneIndex")] = -1;
        QCOMPARE(highlightStates(hovered), QList<bool>({true, true, true, true}));
    }

    /// The ID-based half of `hasZoneSelection`: a highlightedZoneIds list
    /// singles out the matching zone and suppresses the card-level
    /// `isActive` lighting of the other zones.
    void testHighlightedZoneIdsLightOnlyMatchedZone()
    {
        QVariantMap props;
        props[QStringLiteral("isActive")] = true;
        props[QStringLiteral("highlightedZoneIds")] = QVariantList{QStringLiteral("zone-3")};

        const QList<bool> states = highlightStates(props);
        QCOMPARE(states, QList<bool>({false, false, true, false}));
    }

    /// Nothing selected and no card-level state: every zone stays dim.
    void testNeutralCardHighlightsNothing()
    {
        const QList<bool> states = highlightStates(QVariantMap());
        QCOMPARE(states, QList<bool>({false, false, false, false}));
    }

    /// Carrying tab data is the whole gate. A LAYOUT zone has no tab keys, so
    /// the layout picker, the OSD and the algorithm previews must draw no band
    /// at all — not an inert one — or they sprout indicators for windows that
    /// do not exist and pay for an overlay item per zone to do it.
    void testTabIndicatorDrawsOnlyOnZonesCarryingTabData()
    {
        QQuickItem* preview = tabPreview(2, 1, Bottom, 0.5);
        QVERIFY(preview);
        const QList<QQuickItem*> bands = bandsOf(preview);
        QCOMPARE(bands.size(), 1);
        // The other three zones are still drawn; it is only the band that is
        // absent, so this is not passing because the whole preview is empty.
        QCOMPARE(zonesOf(preview).size(), 4);
    }

    /// One pill per tab, hidden tabs included. The strip walk emits only a
    /// tabbed column's SHOWN tab, so the pill count is the whole of what tells
    /// the viewer how many windows the tile stands for.
    void testTabIndicatorDrawsOnePillPerTab()
    {
        QQuickItem* preview = tabPreview(5, 1, Bottom, 0.5);
        QVERIFY(preview);
        const QList<QQuickItem*> bands = bandsOf(preview);
        QCOMPARE(bands.size(), 1);
        // Counted by the property the pills carry: the band's Repeater is a
        // child item of its own, and a raw child count would fold it in.
        QCOMPARE(pillsOf(bands.at(0)).size(), 5);
    }

    /// The band runs along the edge the engine resolved, at the length it
    /// resolved, for every one of the four positions. Expected geometry is
    /// read off the tabbed ZONE delegate, so a band that ignored the position
    /// key cannot land inside the tolerance of another edge.
    void testTabIndicatorFollowsTheResolvedEdge_data()
    {
        QTest::addColumn<int>("position");
        QTest::addColumn<bool>("vertical");

        QTest::newRow("left") << static_cast<int>(Left) << true;
        QTest::newRow("right") << static_cast<int>(Right) << true;
        QTest::newRow("top") << static_cast<int>(Top) << false;
        QTest::newRow("bottom") << static_cast<int>(Bottom) << false;
    }

    void testTabIndicatorFollowsTheResolvedEdge()
    {
        QFETCH(int, position);
        QFETCH(bool, vertical);

        constexpr qreal lengthProportion = 0.5;

        QQuickItem* preview = tabPreview(2, 1, position, lengthProportion);
        QVERIFY(preview);
        const QList<QQuickItem*> bands = bandsOf(preview);
        QCOMPARE(bands.size(), 1);
        QQuickItem* band = bands.at(0);
        QQuickItem* tile = zonesOf(preview).at(1);

        // Long axis: the resolved proportion of the tile's extent, centred,
        // exactly as indicatorRectFor centres the real bar on its column.
        const qreal tileLength = vertical ? tile->height() : tile->width();
        const qreal bandLength = vertical ? band->height() : band->width();
        const qreal bandThickness = vertical ? band->width() : band->height();
        QVERIFY(bandThickness < bandLength);
        QVERIFY(qAbs(bandLength - tileLength * lengthProportion) < kEdgeEpsilon);

        const qreal crossOrigin = vertical ? band->y() : band->x();
        const qreal tileCrossOrigin = vertical ? tile->y() : tile->x();
        QVERIFY(qAbs(crossOrigin - (tileCrossOrigin + (tileLength - bandLength) / 2)) < kEdgeEpsilon);

        // Short axis: flush INSIDE the named edge. Outside it would land on
        // the neighbouring tile at this scale and read as that tile's bar.
        const qreal nearEdge = vertical ? tile->x() : tile->y();
        const qreal farEdge = nearEdge + (vertical ? tile->width() : tile->height());
        const qreal bandNear = vertical ? band->x() : band->y();
        if (position == Left || position == Top) {
            QVERIFY(qAbs(bandNear - nearEdge) < kEdgeEpsilon);
        } else {
            QVERIFY(qAbs((bandNear + bandThickness) - farEdge) < kEdgeEpsilon);
        }
    }

    /// Exactly the tab on show is lit. Without this the band reports how many
    /// windows the column holds but not which one the viewer is looking at,
    /// and a preview that lit all of them or none would look plausible.
    void testTabIndicatorLightsOnlyTheShownTab()
    {
        constexpr int shownTab = 2;
        QQuickItem* preview = tabPreview(4, 1, Bottom, 0.8, shownTab);
        QVERIFY(preview);
        const QList<QQuickItem*> bands = bandsOf(preview);
        QCOMPARE(bands.size(), 1);
        const QList<QQuickItem*> pills = pillsOf(bands.at(0));
        QCOMPARE(pills.size(), 4);

        const QColor lit = pills.at(shownTab)->property("color").value<QColor>();
        int litCount = 0;
        for (const QQuickItem* pill : pills) {
            if (pill->property("color").value<QColor>() == lit) {
                ++litCount;
            }
        }
        QCOMPARE(litCount, 1);
    }

    /// A column with more tabs than its band has pixels. Both pill extents are
    /// floored, so the row laid out is longer than the band — the pills must
    /// stay inside the tile rather than running onto its neighbour, which is
    /// the whole reason the band draws inside the edge in the first place.
    void testTabIndicatorPillsStayInsideTheBandWhenCrowded()
    {
        QQuickItem* preview = tabPreview(40, 1, Bottom, 0.2);
        QVERIFY(preview);
        const QList<QQuickItem*> bands = bandsOf(preview);
        QCOMPARE(bands.size(), 1);
        QQuickItem* band = bands.at(0);
        const QList<QQuickItem*> pills = pillsOf(band);
        QCOMPARE(pills.size(), 40);

        // The laid-out row really does overrun, so the clip is load-bearing
        // rather than decorative — assert the premise before the guard.
        const QQuickItem* last = pills.constLast();
        QVERIFY(last->x() + last->width() > band->width());
        QVERIFY(band->clip());

        // The band itself is within the tile. Pill containment rides on the
        // clip asserted above rather than being measured here: the pills
        // deliberately DO overrun, so their own rects prove nothing.
        QQuickItem* tile = zonesOf(preview).at(1);
        QVERIFY(band->x() >= tile->x() - kEdgeEpsilon);
        QVERIFY(band->x() + band->width() <= tile->x() + tile->width() + kEdgeEpsilon);
    }
};

QTEST_MAIN(TestZonePreviewHighlight)
#include "test_zone_preview_highlight.moc"
