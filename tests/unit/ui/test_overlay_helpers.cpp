// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQmlProperty>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>

#include <LayerShellQt/Window>

#include "core/logging.h"
#include "core/enums.h"
#include "../helpers/TestHelpers.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Extracted inline functions from daemon/overlayservice/internal.h
//
// internal.h cannot be included directly in test TUs due to its relative
// include of ../config/configdefaults.h (only resolves in unity builds).
// We replicate the exact function bodies here for testing. These must be
// kept in sync with internal.h — any functional change there should update
// these copies and this comment.
// ═══════════════════════════════════════════════════════════════════════════════

namespace PlasmaZones {

inline void writeQmlProperty(QObject* object, const QString& name, const QVariant& value)
{
    if (!object) {
        return;
    }

    QQmlProperty prop(object, name);
    if (prop.isValid()) {
        prop.write(value);
    } else {
        object->setProperty(name.toUtf8().constData(), value);
    }
}

inline QVariantList patchZonesWithHighlight(const QVariantList& zones, QQuickWindow* window)
{
    if (!window) {
        return zones;
    }
    const QString hid = window->property("highlightedZoneId").toString();
    const QVariantList hids = window->property("highlightedZoneIds").toList();

    QVariantList out;
    for (const QVariant& z : zones) {
        QVariantMap m = z.toMap();
        const QString id = m.value(QLatin1String("id")).toString();
        bool hi = (!id.isEmpty() && id == hid);
        if (!hi) {
            for (const QVariant& v : hids) {
                if (v.toString() == id) {
                    hi = true;
                    break;
                }
            }
        }
        m[QLatin1String("isHighlighted")] = hi;
        out.append(m);
    }
    return out;
}

inline QVariantList parseZonesJson(const QString& json, const char* context)
{
    QVariantList zones;
    if (json.isEmpty()) {
        return zones;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcOverlay) << context << "invalid zones JSON:" << parseError.errorString();
        return zones;
    }
    if (!doc.isArray()) {
        qCWarning(lcOverlay) << context << "zones JSON is not an array";
        return zones;
    }
    for (const QJsonValue& v : doc.array()) {
        if (v.isObject()) {
            QVariantMap m;
            const QJsonObject o = v.toObject();
            for (auto it = o.begin(); it != o.end(); ++it) {
                m.insert(it.key(), it.value().toVariant());
            }
            zones.append(m);
        }
    }
    return zones;
}

inline void ensureShaderTimerStarted(QElapsedTimer& timer, QMutex& mutex, std::atomic<qint64>& lastFrame,
                                     std::atomic<int>& frameCount)
{
    QMutexLocker locker(&mutex);
    if (!timer.isValid()) {
        timer.start();
        lastFrame.store(0);
        frameCount.store(0);
    }
}

inline LayerShellQt::Window::Anchors getAnchorsForPosition(ZoneSelectorPosition pos)
{
    switch (pos) {
    case ZoneSelectorPosition::TopLeft:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft);
    case ZoneSelectorPosition::Top:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft
                                             | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::TopRight:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::Left:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorTop
                                             | LayerShellQt::Window::AnchorBottom);
    case ZoneSelectorPosition::Center:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                             | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::Right:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorRight | LayerShellQt::Window::AnchorTop
                                             | LayerShellQt::Window::AnchorBottom);
    case ZoneSelectorPosition::BottomLeft:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft);
    case ZoneSelectorPosition::Bottom:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft
                                             | LayerShellQt::Window::AnchorRight);
    case ZoneSelectorPosition::BottomRight:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorRight);
    default:
        return LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft
                                             | LayerShellQt::Window::AnchorRight);
    }
}

} // namespace PlasmaZones

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
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorTop), "TopLeft must have AnchorTop");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorLeft), "TopLeft must have AnchorLeft");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorBottom), "TopLeft must not have AnchorBottom");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorRight), "TopLeft must not have AnchorRight");
    }

    void testGetAnchorsForPosition_top()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::Top);
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorTop), "Top must have AnchorTop");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorLeft), "Top must have AnchorLeft (horizontal stretch)");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorRight), "Top must have AnchorRight (horizontal stretch)");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorBottom), "Top must not have AnchorBottom");
    }

    void testGetAnchorsForPosition_topRight()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::TopRight);
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorTop), "TopRight must have AnchorTop");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorRight), "TopRight must have AnchorRight");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorLeft), "TopRight must not have AnchorLeft");
    }

    void testGetAnchorsForPosition_left()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::Left);
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorLeft), "Left must have AnchorLeft");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorTop), "Left must have AnchorTop (vertical stretch)");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorBottom),
                 "Left must have AnchorBottom (vertical stretch)");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorRight), "Left must not have AnchorRight");
    }

    void testGetAnchorsForPosition_right()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::Right);
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorRight), "Right must have AnchorRight");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorTop), "Right must have AnchorTop (vertical stretch)");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorBottom),
                 "Right must have AnchorBottom (vertical stretch)");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorLeft), "Right must not have AnchorLeft");
    }

    void testGetAnchorsForPosition_bottomLeft()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::BottomLeft);
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorBottom), "BottomLeft must have AnchorBottom");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorLeft), "BottomLeft must have AnchorLeft");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorTop), "BottomLeft must not have AnchorTop");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorRight), "BottomLeft must not have AnchorRight");
    }

    void testGetAnchorsForPosition_bottom()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::Bottom);
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorBottom), "Bottom must have AnchorBottom");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorLeft),
                 "Bottom must have AnchorLeft (horizontal stretch)");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorRight),
                 "Bottom must have AnchorRight (horizontal stretch)");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorTop), "Bottom must not have AnchorTop");
    }

    void testGetAnchorsForPosition_bottomRight()
    {
        auto anchors = getAnchorsForPosition(ZoneSelectorPosition::BottomRight);
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorBottom), "BottomRight must have AnchorBottom");
        QVERIFY2(anchors.testFlag(LayerShellQt::Window::AnchorRight), "BottomRight must have AnchorRight");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorLeft), "BottomRight must not have AnchorLeft");
        QVERIFY2(!anchors.testFlag(LayerShellQt::Window::AnchorTop), "BottomRight must not have AnchorTop");
    }

    void testGetAnchorsForPosition_defaultFallback()
    {
        // Invalid enum value should fall through to default (Top anchors)
        auto def = getAnchorsForPosition(static_cast<ZoneSelectorPosition>(99));
        QVERIFY(def.testFlag(LayerShellQt::Window::AnchorTop));
        QVERIFY(def.testFlag(LayerShellQt::Window::AnchorLeft));
        QVERIFY(def.testFlag(LayerShellQt::Window::AnchorRight));
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
