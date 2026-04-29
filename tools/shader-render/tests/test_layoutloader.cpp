// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../layoutloader.h"

#include <QDir>
#include <QFile>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

namespace {

QString writeLayoutJson(QTemporaryDir& dir, const QString& body)
{
    const QString path = QDir(dir.path()).filePath(QStringLiteral("layout.json"));
    QFile f(path);
    Q_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(body.toUtf8());
    return path;
}

} // namespace

class TestLayoutLoader : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalizedRectsArePassedThrough()
    {
        QTemporaryDir dir;
        const QString path = writeLayoutJson(dir, QStringLiteral(R"({
            "zones": [
                {"zoneNumber": 1, "relativeGeometry": {"x": 0.0, "y": 0.0, "width": 0.5, "height": 1.0}},
                {"zoneNumber": 2, "relativeGeometry": {"x": 0.5, "y": 0.0, "width": 0.5, "height": 1.0}}
            ]
        })"));

        QVector<PlasmaZones::ShaderRender::Zone> zones;
        // Resolution is intentionally large to catch any accidental multiply-by-resolution.
        QVERIFY(PlasmaZones::ShaderRender::loadLayoutZones(path, QSize(1920, 1080), zones));
        QCOMPARE(zones.size(), 2);
        QCOMPARE(zones[0].rect, QRectF(0.0, 0.0, 0.5, 1.0));
        QCOMPARE(zones[1].rect, QRectF(0.5, 0.0, 0.5, 1.0));
    }

    void zoneNumberFallsBackToIndex()
    {
        QTemporaryDir dir;
        const QString path = writeLayoutJson(dir, QStringLiteral(R"({
            "zones": [
                {"relativeGeometry": {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0}},
                {"relativeGeometry": {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0}}
            ]
        })"));

        QVector<PlasmaZones::ShaderRender::Zone> zones;
        QVERIFY(PlasmaZones::ShaderRender::loadLayoutZones(path, QSize(1, 1), zones));
        QCOMPARE(zones[0].zoneNumber, 1);
        QCOMPARE(zones[1].zoneNumber, 2);
    }

    void perZoneColorsApplyWhenSet()
    {
        QTemporaryDir dir;
        const QString path = writeLayoutJson(dir, QStringLiteral(R"({
            "zones": [
                {
                    "zoneNumber": 1,
                    "relativeGeometry": {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0},
                    "highlightColor": "#ff0000",
                    "borderColor": "#00ff00",
                    "borderWidth": 4.0,
                    "borderRadius": 16.0
                }
            ]
        })"));

        QVector<PlasmaZones::ShaderRender::Zone> zones;
        QVERIFY(PlasmaZones::ShaderRender::loadLayoutZones(path, QSize(100, 100), zones));
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0].fillColor, QColor(QStringLiteral("#ff0000")));
        QCOMPARE(zones[0].borderColor, QColor(QStringLiteral("#00ff00")));
        QCOMPARE(zones[0].borderWidth, 4.0);
        QCOMPARE(zones[0].borderRadius, 16.0);
    }

    void isHighlightedDefaultsToFalse()
    {
        // The renderer cycles isHighlighted per slice; the loader must NOT
        // pre-set every zone to true (would defeat the cycling demo).
        QTemporaryDir dir;
        const QString path = writeLayoutJson(dir, QStringLiteral(R"({
            "zones": [
                {"zoneNumber": 1, "relativeGeometry": {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0}},
                {"zoneNumber": 2, "relativeGeometry": {"x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0}}
            ]
        })"));

        QVector<PlasmaZones::ShaderRender::Zone> zones;
        QVERIFY(PlasmaZones::ShaderRender::loadLayoutZones(path, QSize(1, 1), zones));
        for (const auto& z : zones) {
            QVERIFY(!z.isHighlighted);
        }
    }

    void emptyZonesArrayReturnsFalse()
    {
        QTemporaryDir dir;
        const QString path = writeLayoutJson(dir, QStringLiteral(R"({"zones": []})"));

        QVector<PlasmaZones::ShaderRender::Zone> zones;
        QVERIFY(!PlasmaZones::ShaderRender::loadLayoutZones(path, QSize(1, 1), zones));
        QVERIFY(zones.isEmpty());
    }

    void malformedJsonReturnsFalse()
    {
        QTemporaryDir dir;
        const QString path = writeLayoutJson(dir, QStringLiteral("not json"));

        QVector<PlasmaZones::ShaderRender::Zone> zones;
        QVERIFY(!PlasmaZones::ShaderRender::loadLayoutZones(path, QSize(1, 1), zones));
    }

    void missingFileReturnsFalse()
    {
        QVector<PlasmaZones::ShaderRender::Zone> zones;
        QVERIFY(
            !PlasmaZones::ShaderRender::loadLayoutZones(QStringLiteral("/nonexistent/path.json"), QSize(1, 1), zones));
    }

    void outOfRangeRectsAreClampedTo01()
    {
        // Contract: rect components are normalized [0, 1]. common.glsl's
        // helpers multiply by iResolution, so a pixel-space rect would land
        // off-screen and the shader would render black. Validate at the
        // boundary instead of pushing garbage into the UBO.
        QTemporaryDir dir;
        const QString path = writeLayoutJson(dir, QStringLiteral(R"({
            "zones": [
                {"zoneNumber": 1, "relativeGeometry": {"x": -0.25, "y": 0.0, "width": 1.5, "height": 1.0}},
                {"zoneNumber": 2, "relativeGeometry": {"x": 1920, "y": 1080, "width": 0.5, "height": 0.5}}
            ]
        })"));

        QVector<PlasmaZones::ShaderRender::Zone> zones;
        QVERIFY(PlasmaZones::ShaderRender::loadLayoutZones(path, QSize(1920, 1080), zones));
        QCOMPARE(zones.size(), 2);
        // Negative x clamped to 0; oversized width clamped to 1.
        QCOMPARE(zones[0].rect, QRectF(0.0, 0.0, 1.0, 1.0));
        // Pixel-space coordinates clamped to 1; in-range entries pass through.
        QCOMPARE(zones[1].rect, QRectF(1.0, 1.0, 0.5, 0.5));
    }

    void missingRelativeGeometryDefaultsToZeroRect()
    {
        // A zone object with no relativeGeometry pins the current behaviour:
        // the entry is still appended with a (0,0,0,0) rect. Catches
        // accidental future drift toward "skip silently" or "reject the
        // whole file" without forcing either decision now.
        QTemporaryDir dir;
        const QString path = writeLayoutJson(dir, QStringLiteral(R"({
            "zones": [
                {"zoneNumber": 1}
            ]
        })"));

        QVector<PlasmaZones::ShaderRender::Zone> zones;
        QVERIFY(PlasmaZones::ShaderRender::loadLayoutZones(path, QSize(1, 1), zones));
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0].rect, QRectF(0.0, 0.0, 0.0, 0.0));
        QCOMPARE(zones[0].zoneNumber, 1);
    }
};

QTEST_GUILESS_MAIN(TestLayoutLoader)
#include "test_layoutloader.moc"
