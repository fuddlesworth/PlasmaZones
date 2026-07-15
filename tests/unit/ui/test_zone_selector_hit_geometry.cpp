// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QHash>
#include <QList>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRectF>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <algorithm>
#include <utility>

#include <QtPlugin>

// The functions under test. overlay_helpers.h is the overlayservice header with
// no config / registry dependency, precisely so test TUs can include it.
#include "daemon/overlayservice/overlay_helpers.h"

Q_IMPORT_PLUGIN(org_plasmazones_commonPlugin)

using PlasmaZones::collectQmlItemsByName;
using PlasmaZones::mapVisibleRectToItem;

/**
 * @brief The C++/QML contract the zone selector's cursor hit test relies on.
 *
 * OverlayService::updateSelectorPosition finds a LayoutCard's zone delegates by
 * objectName and reads their rendered geometry, rather than recomputing where
 * QML put them. It reads geometry because recomputing drifted: LayoutCard fits
 * the preview to the layout's aspectRatioClass and letterboxes it inside the
 * indicator box, then insets it by smallSpacing. The old arithmetic modelled
 * neither, so the cursor mapped to the wrong zone (or to none) near card edges.
 *
 * Both halves of that contract fail silently at runtime — a renamed objectName
 * or a dropped `index` just stops the highlight from tracking the cursor — so
 * they are pinned here.
 */
class TestZoneSelectorHitGeometry : public QObject
{
    Q_OBJECT

private:
    QQmlEngine m_engine;

    // The zone selector's card metrics (ZoneSelectorContent / zoneselectorlayout.h).
    static constexpr qreal kPreviewWidth = 180;
    static constexpr qreal kPreviewHeight = 101;
    /// Card chrome around the preview, mirroring ZoneSelectorContent's cell:
    /// width = indicatorWidth + cardSidePadding * 2, height = indicatorHeight +
    /// labelSpace + cardPadding.
    static constexpr qreal kCardSidePadding = 18;
    static constexpr qreal kCardLabelSpace = 28;
    static constexpr qreal kCardPadding = 18;

    /// The objectName selector.cpp searches for. Hardcoded rather than shared
    /// with the C++ side on purpose: this test's job is to fail if either end
    /// renames it unilaterally.
    static QString zoneObjectName()
    {
        return QStringLiteral("zonePreviewZone");
    }

    static QVariantList twoColumnZones()
    {
        QVariantList zones;
        for (int i = 0; i < 2; ++i) {
            QVariantMap zone;
            zone[QLatin1String("x")] = i * 0.5;
            zone[QLatin1String("y")] = 0.0;
            zone[QLatin1String("width")] = 0.5;
            zone[QLatin1String("height")] = 1.0;
            zone[QLatin1String("zoneNumber")] = i + 1;
            zones.append(zone);
        }
        return zones;
    }

    /// Build a LayoutCard for a layout tagged `aspectRatioClass`, force a layout
    /// pass, and return each zone's rendered rect keyed by its model index —
    /// exactly what updateSelectorPosition reads.
    QHash<int, QRectF> renderedZoneRects(const QString& aspectRatioClass)
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import org.plasmazones.common as QFZCommon\n"
            "QFZCommon.LayoutCard { showCardBackground: true }\n",
            QUrl(QStringLiteral("qrc:/test_zone_selector_hit_geometry.qml")));

        QHash<int, QRectF> rects;
        if (!component.isReady()) {
            qWarning() << "component not ready:" << component.errorString();
            return rects;
        }

        QVariantMap layoutData;
        layoutData[QLatin1String("zones")] = twoColumnZones();
        layoutData[QLatin1String("displayName")] = QStringLiteral("Test");
        layoutData[QLatin1String("aspectRatioClass")] = aspectRatioClass;

        QVariantMap initial;
        initial[QStringLiteral("layoutData")] = layoutData;
        initial[QStringLiteral("previewWidth")] = kPreviewWidth;
        initial[QStringLiteral("previewHeight")] = kPreviewHeight;

        auto* card = qobject_cast<QQuickItem*>(component.createWithInitialProperties(initial));
        if (!card) {
            qWarning() << "create failed:" << component.errorString();
            return rects;
        }
        card->setWidth(kPreviewWidth + kCardSidePadding * 2);
        card->setHeight(kPreviewHeight + kCardLabelSpace + kCardPadding);

        QVector<QQuickItem*> zoneItems;
        collectQmlItemsByName(card, zoneObjectName(), zoneItems);
        for (QQuickItem* zoneItem : std::as_const(zoneItems)) {
            bool ok = false;
            const int index = zoneItem->property("index").toInt(&ok);
            if (!ok) {
                continue;
            }
            rects.insert(index, zoneItem->mapRectToItem(card, QRectF(0, 0, zoneItem->width(), zoneItem->height())));
        }

        delete card;
        return rects;
    }

private Q_SLOTS:
    /// updateSelectorPosition identifies each layout card by reading `index` off
    /// the GridLayout's children, and relies on the Repeater itself dropping out
    /// because it carries no such property. If reading `index` off a delegate
    /// ever stops working, the selector finds zero cards and nothing highlights
    /// at all, so the mechanism is pinned here on a mirror of
    /// ZoneSelectorContent's card delegate.
    void testCardDelegatesExposeModelIndexAndRepeaterDoesNot()
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "import QtQuick.Layouts\n"
            "GridLayout {\n"
            "    columns: 2\n"
            "    Repeater {\n"
            "        model: 3\n"
            "        delegate: Item {\n"
            "            required property int index\n"
            "            implicitWidth: 40\n"
            "            implicitHeight: 20\n"
            "        }\n"
            "    }\n"
            "}\n",
            QUrl(QStringLiteral("qrc:/test_card_index.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        auto* grid = qobject_cast<QQuickItem*>(component.create());
        QVERIFY(grid);

        QList<int> indices;
        int childrenWithoutIndex = 0;
        const auto kids = grid->childItems();
        for (QQuickItem* kid : kids) {
            bool ok = false;
            const int index = kid->property("index").toInt(&ok);
            if (ok) {
                indices.append(index);
            } else {
                ++childrenWithoutIndex;
            }
        }
        std::sort(indices.begin(), indices.end());

        QCOMPARE(indices, QList<int>({0, 1, 2}));
        QVERIFY2(childrenWithoutIndex >= 1, "expected the Repeater itself to expose no model index");
        delete grid;
    }

    /// The zone selector's ScrollView clips once the layout list overflows. A
    /// card scrolled out of the viewport keeps a scene-graph position, so a bare
    /// mapRectToItem hands back coordinates where it paints nothing — and the
    /// selector's slot is a fullscreen transparent Item, so the cursor really
    /// can sit there mid-drag. mapVisibleRectToItem must clip it away.
    void testClippedItemReportsNoVisibleRect()
    {
        QQmlComponent component(&m_engine);
        component.setData(
            "import QtQuick\n"
            "Item {\n"
            "    width: 200; height: 200\n"
            "    Item {\n"
            "        objectName: \"viewport\"\n"
            "        x: 0; y: 100; width: 200; height: 50\n"
            "        clip: true\n"
            "        Item { objectName: \"inside\";  x: 0; y: 10;   width: 40; height: 20 }\n"
            "        Item { objectName: \"scrolled\"; x: 0; y: -80; width: 40; height: 20 }\n"
            "    }\n"
            "}\n",
            QUrl(QStringLiteral("qrc:/test_clip.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        auto* root = qobject_cast<QQuickItem*>(component.create());
        QVERIFY(root);

        QVector<QQuickItem*> inside;
        QVector<QQuickItem*> scrolled;
        collectQmlItemsByName(root, QStringLiteral("inside"), inside);
        collectQmlItemsByName(root, QStringLiteral("scrolled"), scrolled);
        QCOMPARE(inside.size(), 1);
        QCOMPARE(scrolled.size(), 1);

        // Visible child: clipping leaves it alone.
        const QRectF visible = mapVisibleRectToItem(inside.at(0), root);
        QCOMPARE(visible, QRectF(0, 110, 40, 20));

        // Scrolled-out child: a bare map still reports a rect inside the root,
        // which is exactly what would mis-hit. The clipped map rejects it.
        const QRectF bare =
            scrolled.at(0)->mapRectToItem(root, QRectF(0, 0, scrolled.at(0)->width(), scrolled.at(0)->height()));
        QCOMPARE(bare, QRectF(0, 20, 40, 20));
        QVERIFY2(bare.contains(QPointF(20, 30)), "precondition: the unclipped rect would swallow this cursor");

        const QRectF clipped = mapVisibleRectToItem(scrolled.at(0), root);
        QVERIFY2(clipped.isEmpty(), "a fully clipped item must report no visible rect");
        QVERIFY2(!clipped.contains(QPointF(20, 30)), "clipped item must not hit-test");
        delete root;
    }

    /// The delegates must be findable by objectName and expose their model
    /// index. Without both, selector.cpp's traversal silently finds nothing and
    /// no zone ever highlights.
    void testZoneDelegatesAreDiscoverableByObjectName()
    {
        const QHash<int, QRectF> rects = renderedZoneRects(QStringLiteral("any"));

        QCOMPARE(rects.size(), 2);
        QVERIFY(rects.contains(0));
        QVERIFY(rects.contains(1));
        for (const QRectF& rect : rects) {
            QVERIFY2(!rect.isEmpty(), "zone rect is empty — layout pass did not resolve");
        }
    }

    /// Model index must map to spatial order, not traversal order.
    void testZoneRectsFollowModelIndex()
    {
        const QHash<int, QRectF> rects = renderedZoneRects(QStringLiteral("any"));
        QCOMPARE(rects.size(), 2);
        QVERIFY2(rects.value(0).center().x() < rects.value(1).center().x(), "zone 0 must render left of zone 1");
    }

    /// The bug: a layout whose aspect class is wider than the preview box gets
    /// letterboxed, so its zones occupy a shorter band centred in that box. The
    /// old hit test mapped zones onto the full box, so cursor positions in the
    /// letterbox void resolved to a zone that is not painted there.
    void testLetterboxedLayoutShrinksZoneBand()
    {
        const QHash<int, QRectF> square = renderedZoneRects(QStringLiteral("any"));
        const QHash<int, QRectF> wide = renderedZoneRects(QStringLiteral("ultrawide"));

        QCOMPARE(square.size(), 2);
        QCOMPARE(wide.size(), 2);

        const QRectF squareZone = square.value(0);
        const QRectF wideZone = wide.value(0);

        // 21:9 fitted into a 180x101 box is 180x77, so the band loses ~24px of
        // height and is pushed down by ~12. Assert the direction and rough
        // magnitude rather than exact pixels, which depend on Kirigami units.
        QVERIFY2(wideZone.height() < squareZone.height() - 15,
                 qPrintable(QStringLiteral("expected letterboxed zone to be shorter: wide=%1 square=%2")
                                .arg(wideZone.height())
                                .arg(squareZone.height())));
        QVERIFY2(wideZone.top() > squareZone.top() + 5,
                 qPrintable(QStringLiteral("expected letterboxed zone to be pushed down: wide=%1 square=%2")
                                .arg(wideZone.top())
                                .arg(squareZone.top())));

        // The concrete failure: a point just inside the top of the un-letterboxed
        // band. The old math treats it as a hit on zone 0; nothing is painted
        // there, so the rendered rect must reject it.
        const QPointF inLetterboxVoid(wideZone.center().x(), squareZone.top() + 1);
        QVERIFY2(!wideZone.contains(inLetterboxVoid), "point in the letterbox void must not hit a zone");
    }
};

QTEST_MAIN(TestZoneSelectorHitGeometry)
#include "test_zone_selector_hit_geometry.moc"
