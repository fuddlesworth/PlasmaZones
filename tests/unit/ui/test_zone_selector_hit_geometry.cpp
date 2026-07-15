// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QHash>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRectF>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <QtPlugin>

Q_IMPORT_PLUGIN(org_plasmazones_commonPlugin)

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

    /// Mirror of internal.h::collectQmlItemsByName — the traversal selector.cpp
    /// performs against a laid-out card.
    static void collectByName(QQuickItem* item, const QString& name, QVector<QQuickItem*>& out)
    {
        if (!item) {
            return;
        }
        if (item->objectName() == name) {
            out.append(item);
        }
        const auto children = item->childItems();
        for (auto* child : children) {
            collectByName(child, name, out);
        }
    }

    /// Build a LayoutCard for a layout tagged `aspectRatioClass`, force a layout
    /// pass, and return each zone's rendered rect keyed by its model index —
    /// exactly what updateSelectorPosition reads.
    QHash<int, QRectF> renderedZoneRects(const QString& aspectRatioClass, QQuickItem** cardOut = nullptr)
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
        card->setWidth(kPreviewWidth + 36);
        card->setHeight(kPreviewHeight + 46);
        // Anchored/centred children only resolve once the item has been
        // polished; without this every rect maps back to the origin.
        card->polish();
        QQuickItem* item = card;
        while (item) {
            item->polish();
            item = item->parentItem();
        }

        QVector<QQuickItem*> zoneItems;
        collectByName(card, zoneObjectName(), zoneItems);
        for (QQuickItem* zoneItem : std::as_const(zoneItems)) {
            bool ok = false;
            const int index = zoneItem->property("index").toInt(&ok);
            if (!ok) {
                continue;
            }
            rects.insert(index, zoneItem->mapRectToItem(card, QRectF(0, 0, zoneItem->width(), zoneItem->height())));
        }

        if (cardOut) {
            *cardOut = card;
        } else {
            delete card;
        }
        return rects;
    }

private Q_SLOTS:
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
