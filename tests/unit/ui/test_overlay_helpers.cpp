// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QQuickWindow>

// Shared overlay helpers — single source of truth for both production and tests.
// Previously these were duplicated here; now they live in overlay_helpers.h.
#include "daemon/overlayservice/overlay_helpers.h"
#include "../helpers/TestHelpers.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for overlay service helper functions
 *
 * Tests cover:
 * - parseZonesJson: valid array, invalid JSON, non-array JSON, empty input
 * - patchZonesWithHighlight: single/multi highlight, empty ids, null window
 * - writeQmlProperty: valid property, null object safety
 * - ensureShaderTimerStarted: idempotency
 * - getAnchorsForPosition: all positions
 *
 * These are pure/inline functions from internal.h. They are replicated here
 * because internal.h has include dependencies that only resolve in unity builds.
 * The function bodies are exact copies and must stay in sync.
 */
class TestOverlayHelpers : public QObject
{
    Q_OBJECT

private:
    // Helper: build a JSON array string from zone maps
    static QString zonesJsonString(const QVariantList& zones)
    {
        QJsonArray arr;
        for (const QVariant& z : zones) {
            arr.append(QJsonObject::fromVariantMap(z.toMap()));
        }
        return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }

    // Delegate to shared TestHelpers::makeZone (uses JsonKeys constants)
    static QVariantMap makeZone(const QString& id, float x, float y, float w, float h, int zoneNumber = 0,
                                bool highlighted = false)
    {
        return TestHelpers::makeZone(id, x, y, w, h, zoneNumber, highlighted);
    }

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // P0: parseZonesJson
    // ═══════════════════════════════════════════════════════════════════════════

    void testParseZonesJson_validArray()
    {
        QVariantList input;
        input.append(makeZone(QStringLiteral("z1"), 0, 0, 960, 1080, 1));
        input.append(makeZone(QStringLiteral("z2"), 960, 0, 960, 1080, 2));

        QString json = zonesJsonString(input);
        QVariantList result = parseZonesJson(json, "test:");

        QCOMPARE(result.size(), 2);

        // Verify first zone fields are preserved
        QVariantMap z1 = result[0].toMap();
        QCOMPARE(z1.value(QStringLiteral("id")).toString(), QStringLiteral("z1"));
        QCOMPARE(z1.value(QStringLiteral("zoneNumber")).toInt(), 1);

        // Verify second zone with exact coordinate comparison
        QVariantMap z2 = result[1].toMap();
        QCOMPARE(z2.value(QStringLiteral("id")).toString(), QStringLiteral("z2"));
        QVERIFY(qFuzzyCompare(z2.value(QStringLiteral("x")).toFloat(), 960.0f));
    }

    void testParseZonesJson_invalidJson_returnsEmpty()
    {
        // Malformed JSON should return empty list without crashing
        QVariantList result = parseZonesJson(QStringLiteral("{invalid json!!!"), "test:");
        QVERIFY(result.isEmpty());
    }

    void testParseZonesJson_notArray_returnsEmpty()
    {
        // JSON object (not array) should return empty list
        QJsonObject obj;
        obj.insert(QStringLiteral("key"), QStringLiteral("value"));
        QString json = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));

        QVariantList result = parseZonesJson(json, "test:");
        QVERIFY(result.isEmpty());
    }

    void testParseZonesJson_emptyString_returnsEmpty()
    {
        QVariantList result = parseZonesJson(QString(), "test:");
        QVERIFY(result.isEmpty());

        QVariantList result2 = parseZonesJson(QStringLiteral(""), "test:");
        QVERIFY(result2.isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P0: patchZonesWithHighlight
    // ═══════════════════════════════════════════════════════════════════════════

    void testPatchZonesWithHighlight_singleZoneHighlighted()
    {
        QQuickWindow window;
        window.setProperty("highlightedZoneId", QStringLiteral("z2"));
        window.setProperty("highlightedZoneIds", QVariantList());

        QVariantList zones;
        zones.append(makeZone(QStringLiteral("z1"), 0, 0, 960, 1080));
        zones.append(makeZone(QStringLiteral("z2"), 960, 0, 960, 1080));
        zones.append(makeZone(QStringLiteral("z3"), 0, 540, 960, 540));

        QVariantList result = patchZonesWithHighlight(zones, &window);

        QCOMPARE(result.size(), 3);
        QVERIFY(!result[0].toMap().value(QLatin1String("isHighlighted")).toBool());
        QVERIFY(result[1].toMap().value(QLatin1String("isHighlighted")).toBool());
        QVERIFY(!result[2].toMap().value(QLatin1String("isHighlighted")).toBool());
    }

    void testPatchZonesWithHighlight_multipleZoneIds()
    {
        QQuickWindow window;
        window.setProperty("highlightedZoneId", QString()); // empty single ID
        QVariantList highlightIds;
        highlightIds.append(QStringLiteral("z1"));
        highlightIds.append(QStringLiteral("z3"));
        window.setProperty("highlightedZoneIds", highlightIds);

        QVariantList zones;
        zones.append(makeZone(QStringLiteral("z1"), 0, 0, 960, 540));
        zones.append(makeZone(QStringLiteral("z2"), 960, 0, 960, 540));
        zones.append(makeZone(QStringLiteral("z3"), 0, 540, 960, 540));
        zones.append(makeZone(QStringLiteral("z4"), 960, 540, 960, 540));

        QVariantList result = patchZonesWithHighlight(zones, &window);

        QCOMPARE(result.size(), 4);
        QVERIFY(result[0].toMap().value(QLatin1String("isHighlighted")).toBool());
        QVERIFY(!result[1].toMap().value(QLatin1String("isHighlighted")).toBool());
        QVERIFY(result[2].toMap().value(QLatin1String("isHighlighted")).toBool());
        QVERIFY(!result[3].toMap().value(QLatin1String("isHighlighted")).toBool());
    }

    void testPatchZonesWithHighlight_noHighlight()
    {
        QQuickWindow window;
        window.setProperty("highlightedZoneId", QString());
        window.setProperty("highlightedZoneIds", QVariantList());

        QVariantList zones;
        zones.append(makeZone(QStringLiteral("z1"), 0, 0, 960, 540, 1, true)); // was highlighted in input
        zones.append(makeZone(QStringLiteral("z2"), 960, 0, 960, 540, 2, false));

        QVariantList result = patchZonesWithHighlight(zones, &window);

        QCOMPARE(result.size(), 2);
        // patchZonesWithHighlight overrides isHighlighted based on window properties,
        // so even though input had isHighlighted=true, output should be false
        QVERIFY(!result[0].toMap().value(QLatin1String("isHighlighted")).toBool());
        QVERIFY(!result[1].toMap().value(QLatin1String("isHighlighted")).toBool());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: patchZonesWithHighlight edge cases
    // ═══════════════════════════════════════════════════════════════════════════

    void testPatchZonesWithHighlight_emptyZoneId_notHighlighted()
    {
        QQuickWindow window;
        window.setProperty("highlightedZoneId", QStringLiteral(""));
        window.setProperty("highlightedZoneIds", QVariantList());

        QVariantList zones;
        // Zone with empty id should never match empty highlightedZoneId
        QVariantMap z = makeZone(QString(), 0, 0, 960, 1080);
        zones.append(z);

        QVariantList result = patchZonesWithHighlight(zones, &window);

        QCOMPARE(result.size(), 1);
        // The code checks (!id.isEmpty() && id == hid), so empty ids never match
        QVERIFY(!result[0].toMap().value(QLatin1String("isHighlighted")).toBool());
    }

    void testPatchZonesWithHighlight_nullWindow_passthrough()
    {
        QVariantList zones;
        zones.append(makeZone(QStringLiteral("z1"), 0, 0, 960, 1080, 1, true));

        // Null window returns input unchanged
        QVariantList result = patchZonesWithHighlight(zones, nullptr);

        QCOMPARE(result.size(), 1);
        // isHighlighted preserved from input when window is null
        QVERIFY(result[0].toMap().value(QLatin1String("isHighlighted")).toBool());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: writeQmlProperty
    // ═══════════════════════════════════════════════════════════════════════════

    void testWriteQmlProperty_validProperty()
    {
        // Use a plain QObject with a dynamic property
        QObject obj;
        obj.setProperty("testProp", 42);

        writeQmlProperty(&obj, QStringLiteral("testProp"), 99);

        // writeQmlProperty falls back to setProperty for non-QML objects
        QCOMPARE(obj.property("testProp").toInt(), 99);
    }

    void testWriteQmlProperty_nullObject_nocrash()
    {
        // Must not crash when object is null
        writeQmlProperty(nullptr, QStringLiteral("anyProp"), QVariant(42));
        // If we get here, no crash occurred - test passes
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: ensureShaderTimerStarted
    // ═══════════════════════════════════════════════════════════════════════════

    void testEnsureShaderTimerStarted_onlyStartsOnce()
    {
        QElapsedTimer timer;
        QMutex mutex;
        std::atomic<qint64> lastFrame{999};
        std::atomic<int> frameCount{42};

        // Timer is not valid initially
        QVERIFY(!timer.isValid());

        // First call should start the timer and reset counters
        ensureShaderTimerStarted(timer, mutex, lastFrame, frameCount);
        QVERIFY(timer.isValid());
        QCOMPARE(lastFrame.load(), qint64(0));
        QCOMPARE(frameCount.load(), 0);

        // Second call should be a no-op (timer already valid)
        // Set sentinel values to detect if they get reset
        lastFrame.store(100);
        frameCount.store(50);

        ensureShaderTimerStarted(timer, mutex, lastFrame, frameCount);

        // Values should NOT have been reset since timer was already valid
        QCOMPARE(lastFrame.load(), qint64(100));
        QCOMPARE(frameCount.load(), 50);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: getAnchorsForPosition
    // ═══════════════════════════════════════════════════════════════════════════

    void testGetAnchorsForPosition_topLeft()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::TopLeft);
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorTop), "TopLeft must have AnchorTop");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorLeft), "TopLeft must have AnchorLeft");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorBottom), "TopLeft must not have AnchorBottom");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorRight), "TopLeft must not have AnchorRight");
    }

    void testGetAnchorsForPosition_top()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::Top);
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorTop), "Top must have AnchorTop");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorLeft),
                 "Top must have AnchorLeft (horizontal stretch)");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorRight),
                 "Top must have AnchorRight (horizontal stretch)");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorBottom), "Top must not have AnchorBottom");
    }

    void testGetAnchorsForPosition_topRight()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::TopRight);
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorTop), "TopRight must have AnchorTop");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorRight), "TopRight must have AnchorRight");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorLeft), "TopRight must not have AnchorLeft");
    }

    void testGetAnchorsForPosition_left()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::Left);
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorLeft), "Left must have AnchorLeft");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorTop), "Left must have AnchorTop (vertical stretch)");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorBottom),
                 "Left must have AnchorBottom (vertical stretch)");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorRight), "Left must not have AnchorRight");
    }

    void testGetAnchorsForPosition_right()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::Right);
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorRight), "Right must have AnchorRight");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorTop),
                 "Right must have AnchorTop (vertical stretch)");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorBottom),
                 "Right must have AnchorBottom (vertical stretch)");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorLeft), "Right must not have AnchorLeft");
    }

    void testGetAnchorsForPosition_bottomLeft()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::BottomLeft);
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorBottom), "BottomLeft must have AnchorBottom");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorLeft), "BottomLeft must have AnchorLeft");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorTop), "BottomLeft must not have AnchorTop");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorRight), "BottomLeft must not have AnchorRight");
    }

    void testGetAnchorsForPosition_bottom()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::Bottom);
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorBottom), "Bottom must have AnchorBottom");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorLeft),
                 "Bottom must have AnchorLeft (horizontal stretch)");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorRight),
                 "Bottom must have AnchorRight (horizontal stretch)");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorTop), "Bottom must not have AnchorTop");
    }

    void testGetAnchorsForPosition_bottomRight()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::BottomRight);
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorBottom), "BottomRight must have AnchorBottom");
        QVERIFY2(anchors.testFlag(PlasmaZones::LayerSurface::AnchorRight), "BottomRight must have AnchorRight");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorLeft), "BottomRight must not have AnchorLeft");
        QVERIFY2(!anchors.testFlag(PlasmaZones::LayerSurface::AnchorTop), "BottomRight must not have AnchorTop");
    }

    void testGetAnchorsForPosition_center()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::Center);
        QCOMPARE(anchors, PlasmaZones::LayerSurface::AnchorAll);
    }

    void testGetAnchorsForPosition_defaultFallback()
    {
        // Invalid enum value should fall through to default (Top anchors)
        auto def = getAnchorsForPosition(static_cast<ZoneSelectorPosition>(99));
        QVERIFY(def.testFlag(PlasmaZones::LayerSurface::AnchorTop));
        QVERIFY(def.testFlag(PlasmaZones::LayerSurface::AnchorLeft));
        QVERIFY(def.testFlag(PlasmaZones::LayerSurface::AnchorRight));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: parseZonesJson — additional edge cases
    // ═══════════════════════════════════════════════════════════════════════════

    void testParseZonesJson_mixedArray_skipsNonObjects()
    {
        QString json = QStringLiteral("[1, \"string\", null, {\"id\": \"z1\"}]");
        QVariantList result = parseZonesJson(json, "test:");
        QCOMPARE(result.size(), 1);
        QCOMPARE(result[0].toMap().value(QLatin1String("id")).toString(), QStringLiteral("z1"));
    }

    void testParseZonesJson_emptyArray_returnsEmpty()
    {
        QVariantList result = parseZonesJson(QStringLiteral("[]"), "test:");
        QVERIFY(result.isEmpty());
    }
};

// Custom main: QQuickWindow requires QGuiApplication, but QTEST_MAIN creates
// QCoreApplication. Provide QGuiApplication explicitly.
int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TestOverlayHelpers tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_overlay_helpers.moc"
