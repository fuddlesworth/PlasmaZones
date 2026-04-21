// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QJSEngine>
#include <QJSValue>

#include <PhosphorTiles/ScriptedAlgorithmHelpers.h>
#include <PhosphorTiles/AutotileConstants.h>

using namespace PhosphorTiles;
using namespace PhosphorTiles::ScriptedHelpers;

class TestMetadataParsing : public QObject
{
    Q_OBJECT

private:
    QJSEngine m_engine;

    QJSValue evaluate(const QString& js)
    {
        QJSValue result = m_engine.evaluate(js);
        if (result.isError()) {
            qWarning() << "JS eval error:" << result.toString();
        }
        return result;
    }

    QJSValue makeMetadata(const QString& jsObjectLiteral)
    {
        return evaluate(QStringLiteral("(") + jsObjectLiteral + QStringLiteral(")"));
    }

    QJSValue makeCustomParams(const QString& jsArrayLiteral)
    {
        return evaluate(QStringLiteral("(") + jsArrayLiteral + QStringLiteral(")"));
    }

private Q_SLOTS:

    // =========================================================================
    // parseMetadataFromJs — basic fields
    // =========================================================================

    void testParseMetadata_nameAndDescription()
    {
        auto js = makeMetadata(QStringLiteral(R"({ name: "My Algo", description: "A description" })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.name, QStringLiteral("My Algo"));
        QCOMPARE(meta.description, QStringLiteral("A description"));
    }

    void testParseMetadata_emptyObject()
    {
        auto js = makeMetadata(QStringLiteral("{}"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QVERIFY(meta.name.isEmpty());
        QVERIFY(meta.description.isEmpty());
        QVERIFY(!meta.supportsMasterCount);
        QVERIFY(!meta.supportsSplitRatio);
        QCOMPARE(meta.defaultSplitRatio, 0.0);
        QCOMPARE(meta.defaultMaxWindows, 0);
    }

    void testParseMetadata_notAnObject()
    {
        auto js = evaluate(QStringLiteral("42"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QVERIFY(meta.name.isEmpty());
        QCOMPARE(meta.defaultSplitRatio, 0.0);
    }

    // =========================================================================
    // parseMetadataFromJs — boolean fields
    // =========================================================================

    void testParseMetadata_boolFields()
    {
        auto js = makeMetadata(QStringLiteral(
            R"({ supportsMasterCount: true, supportsSplitRatio: false, producesOverlappingZones: true, supportsMemory: true, centerLayout: true, supportsMinSizes: false })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QVERIFY(meta.supportsMasterCount);
        QVERIFY(!meta.supportsSplitRatio);
        QVERIFY(meta.producesOverlappingZones);
        QVERIFY(meta.supportsMemory);
        QVERIFY(meta.centerLayout);
        QVERIFY(!meta.supportsMinSizes);
    }

    void testParseMetadata_boolFromNumber()
    {
        auto js = makeMetadata(QStringLiteral(R"({ supportsMasterCount: 1, supportsSplitRatio: 0 })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QVERIFY(meta.supportsMasterCount);
        QVERIFY(!meta.supportsSplitRatio);
    }

    void testParseMetadata_boolFromString_fallsBack()
    {
        auto js = makeMetadata(QStringLiteral(R"({ supportsMasterCount: "true" })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QVERIFY(!meta.supportsMasterCount);
    }

    // =========================================================================
    // parseMetadataFromJs — numeric fields
    // =========================================================================

    void testParseMetadata_splitRatioClamped()
    {
        auto js = makeMetadata(QStringLiteral(R"({ defaultSplitRatio: -1.0 })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.defaultSplitRatio, AutotileDefaults::MinSplitRatio);

        js = makeMetadata(QStringLiteral(R"({ defaultSplitRatio: 99.0 })"));
        meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.defaultSplitRatio, AutotileDefaults::MaxSplitRatio);
    }

    void testParseMetadata_splitRatioValid()
    {
        auto js = makeMetadata(QStringLiteral(R"({ defaultSplitRatio: 0.65 })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.defaultSplitRatio, 0.65);
    }

    void testParseMetadata_splitRatioInfinity()
    {
        auto js = makeMetadata(QStringLiteral(R"({ defaultSplitRatio: Infinity })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.defaultSplitRatio, 0.0);
    }

    void testParseMetadata_splitRatioNaN()
    {
        auto js = makeMetadata(QStringLiteral(R"({ defaultSplitRatio: NaN })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.defaultSplitRatio, 0.0);
    }

    void testParseMetadata_windowsClamped()
    {
        auto js = makeMetadata(QStringLiteral(R"({ defaultMaxWindows: 9999, minimumWindows: -5 })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.defaultMaxWindows, AutotileDefaults::MaxMetadataWindows);
        QCOMPARE(meta.minimumWindows, AutotileDefaults::MinMetadataWindows);
    }

    void testParseMetadata_minimumWindowsClampedToMax()
    {
        auto js = makeMetadata(QStringLiteral(R"({ defaultMaxWindows: 2, minimumWindows: 5 })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QVERIFY(meta.minimumWindows <= meta.defaultMaxWindows);
        QCOMPARE(meta.minimumWindows, 2);
    }

    void testParseMetadata_masterZoneIndexClamped()
    {
        auto js = makeMetadata(QStringLiteral(R"({ masterZoneIndex: 999 })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.masterZoneIndex, AutotileDefaults::MaxZones - 1);

        js = makeMetadata(QStringLiteral(R"({ masterZoneIndex: -5 })"));
        meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.masterZoneIndex, -1);
    }

    // =========================================================================
    // parseMetadataFromJs — zoneNumberDisplay
    // =========================================================================

    void testParseMetadata_zoneNumberDisplay()
    {
        auto js = makeMetadata(QStringLiteral(R"({ zoneNumberDisplay: "all" })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.zoneNumberDisplay, PhosphorLayout::ZoneNumberDisplay::All);

        js = makeMetadata(QStringLiteral(R"({ zoneNumberDisplay: "last" })"));
        meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.zoneNumberDisplay, PhosphorLayout::ZoneNumberDisplay::Last);
    }

    void testParseMetadata_zoneNumberDisplayInvalid()
    {
        auto js = makeMetadata(QStringLiteral(R"({ zoneNumberDisplay: "bogus" })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.zoneNumberDisplay, PhosphorLayout::ZoneNumberDisplay::RendererDecides);
    }

    // =========================================================================
    // parseMetadataFromJs — id validation
    // =========================================================================

    void testParseMetadata_idValid()
    {
        auto js = makeMetadata(QStringLiteral(R"({ id: "my-custom-algo" })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.id, QStringLiteral("my-custom-algo"));
    }

    void testParseMetadata_idScriptPrefix()
    {
        auto js = makeMetadata(QStringLiteral(R"({ id: "script:bad" })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QVERIFY(meta.id.isEmpty());
    }

    void testParseMetadata_idUppercase()
    {
        auto js = makeMetadata(QStringLiteral(R"({ id: "UPPERCASE" })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QVERIFY(meta.id.isEmpty());
    }

    void testParseMetadata_idStartsWithDigit()
    {
        auto js = makeMetadata(QStringLiteral(R"({ id: "9lives" })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QVERIFY(meta.id.isEmpty());
    }

    // =========================================================================
    // parseMetadataFromJs — customParams nested in metadata
    // =========================================================================

    void testParseMetadata_customParamsNested()
    {
        auto js = makeMetadata(QStringLiteral(
            R"({ name: "Test", customParams: [{ name: "gap", type: "number", default: 8, min: 0, max: 50, description: "Gap" }] })"));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.customParams.size(), 1);
        QCOMPARE(meta.customParams[0].name, QStringLiteral("gap"));
        QCOMPARE(meta.customParams[0].defaultValue.toDouble(), 8.0);
    }

    // =========================================================================
    // parseCustomParamsFromJs — number type
    // =========================================================================

    void testCustomParams_numberBasic()
    {
        auto js = makeCustomParams(QStringLiteral(
            R"([{ name: "ratio", type: "number", default: 0.5, min: 0.0, max: 1.0, description: "Split ratio" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].name, QStringLiteral("ratio"));
        QCOMPARE(params[0].type, QStringLiteral("number"));
        QCOMPARE(params[0].defaultValue.toDouble(), 0.5);
        QCOMPARE(params[0].minValue, 0.0);
        QCOMPARE(params[0].maxValue, 1.0);
        QCOMPARE(params[0].description, QStringLiteral("Split ratio"));
    }

    void testCustomParams_numberMinGreaterThanMax()
    {
        auto js = makeCustomParams(
            QStringLiteral(R"([{ name: "x", type: "number", default: 5, min: 10, max: 2, description: "Swapped" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QVERIFY(params[0].minValue <= params[0].maxValue);
        QCOMPARE(params[0].minValue, 2.0);
        QCOMPARE(params[0].maxValue, 10.0);
    }

    void testCustomParams_numberDefaultClampedToRange()
    {
        auto js = makeCustomParams(QStringLiteral(
            R"([{ name: "x", type: "number", default: 100, min: 0, max: 10, description: "Clamped" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].defaultValue.toDouble(), 10.0);
    }

    void testCustomParams_numberInfinityDefault()
    {
        auto js = makeCustomParams(QStringLiteral(
            R"([{ name: "x", type: "number", default: Infinity, min: 0, max: 10, description: "Inf" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].defaultValue.toDouble(), params[0].minValue);
    }

    void testCustomParams_numberNaNDefault()
    {
        auto js = makeCustomParams(
            QStringLiteral(R"([{ name: "x", type: "number", default: NaN, min: 0, max: 10, description: "NaN" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].defaultValue.toDouble(), params[0].minValue);
    }

    void testCustomParams_numberMissingMinMax()
    {
        auto js = makeCustomParams(
            QStringLiteral(R"([{ name: "x", type: "number", default: 0.5, description: "No min/max" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].minValue, 0.0);
        QCOMPARE(params[0].maxValue, 1.0);
        QCOMPARE(params[0].defaultValue.toDouble(), 0.5);
    }

    void testCustomParams_numberInfinityMinMax()
    {
        auto js = makeCustomParams(QStringLiteral(
            R"([{ name: "x", type: "number", default: 5, min: -Infinity, max: Infinity, description: "InfRange" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].minValue, 0.0);
        QCOMPARE(params[0].maxValue, 1.0);
    }

    // =========================================================================
    // parseCustomParamsFromJs — bool type
    // =========================================================================

    void testCustomParams_boolTrue()
    {
        auto js =
            makeCustomParams(QStringLiteral(R"([{ name: "wrap", type: "bool", default: true, description: "Wrap" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].type, QStringLiteral("bool"));
        QCOMPARE(params[0].defaultValue.toBool(), true);
    }

    void testCustomParams_boolFalse()
    {
        auto js = makeCustomParams(
            QStringLiteral(R"([{ name: "wrap", type: "bool", default: false, description: "No wrap" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].defaultValue.toBool(), false);
    }

    // =========================================================================
    // parseCustomParamsFromJs — enum type
    // =========================================================================

    void testCustomParams_enumBasic()
    {
        auto js = makeCustomParams(QStringLiteral(
            R"([{ name: "mode", type: "enum", default: "auto", options: ["auto", "manual"], description: "Mode" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].type, QStringLiteral("enum"));
        QCOMPARE(params[0].defaultValue.toString(), QStringLiteral("auto"));
        QCOMPARE(params[0].enumOptions.size(), 2);
        QCOMPARE(params[0].enumOptions[0], QStringLiteral("auto"));
        QCOMPARE(params[0].enumOptions[1], QStringLiteral("manual"));
    }

    void testCustomParams_enumInvalidDefault()
    {
        auto js = makeCustomParams(QStringLiteral(
            R"([{ name: "mode", type: "enum", default: "bogus", options: ["a", "b"], description: "Mode" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].defaultValue.toString(), QStringLiteral("a"));
    }

    void testCustomParams_enumEmptyOptions()
    {
        auto js = makeCustomParams(
            QStringLiteral(R"([{ name: "mode", type: "enum", default: "x", options: [], description: "Empty" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 0);
    }

    void testCustomParams_enumNoOptionsProperty()
    {
        auto js = makeCustomParams(
            QStringLiteral(R"([{ name: "mode", type: "enum", default: "x", description: "No opts" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 0);
    }

    // =========================================================================
    // parseCustomParamsFromJs — validation / edge cases
    // =========================================================================

    void testCustomParams_missingName()
    {
        auto js = makeCustomParams(
            QStringLiteral(R"([{ type: "number", default: 1, min: 0, max: 10, description: "No name" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 0);
    }

    void testCustomParams_missingType()
    {
        auto js = makeCustomParams(QStringLiteral(R"([{ name: "x", default: 1, description: "No type" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 0);
    }

    void testCustomParams_unknownType()
    {
        auto js = makeCustomParams(
            QStringLiteral(R"([{ name: "x", type: "string", default: "hi", description: "Bad type" }])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 0);
    }

    void testCustomParams_maxEntries()
    {
        QString arrayStr = QStringLiteral("[");
        for (int i = 0; i < 70; ++i) {
            if (i > 0)
                arrayStr += QStringLiteral(",");
            arrayStr += QStringLiteral(R"({ name: "p%1", type: "bool", default: true, description: "d" })").arg(i);
        }
        arrayStr += QStringLiteral("]");
        auto js = makeCustomParams(arrayStr);
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 64);
    }

    void testCustomParams_notAnArray()
    {
        auto js = evaluate(QStringLiteral("({})"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 0);
    }

    void testCustomParams_nonObjectEntry()
    {
        auto js = makeCustomParams(QStringLiteral(R"([42, "hello", null])"));
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 0);
    }

    void testCustomParams_nameTruncated()
    {
        QString longName = QString(100, QLatin1Char('a'));
        QString arrayStr =
            QStringLiteral(R"([{ name: "%1", type: "bool", default: true, description: "d" }])").arg(longName);
        auto js = makeCustomParams(arrayStr);
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].name.size(), 64);
    }

    void testCustomParams_descriptionTruncated()
    {
        QString longDesc = QString(300, QLatin1Char('x'));
        QString arrayStr =
            QStringLiteral(R"([{ name: "p", type: "bool", default: true, description: "%1" }])").arg(longDesc);
        auto js = makeCustomParams(arrayStr);
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].description.size(), 200);
    }

    void testCustomParams_enumOptionsTruncated()
    {
        QString longOpt = QString(100, QLatin1Char('z'));
        QString arrayStr =
            QStringLiteral(R"([{ name: "m", type: "enum", default: "%1", options: ["%1"], description: "d" }])")
                .arg(longOpt);
        auto js = makeCustomParams(arrayStr);
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].enumOptions[0].size(), 64);
    }

    void testCustomParams_enumOptionsCapped()
    {
        QString arrayStr = QStringLiteral("[{ name: \"m\", type: \"enum\", default: \"o0\", options: [");
        for (int i = 0; i < 300; ++i) {
            if (i > 0)
                arrayStr += QStringLiteral(",");
            arrayStr += QStringLiteral("\"o%1\"").arg(i);
        }
        arrayStr += QStringLiteral("], description: \"d\" }]");
        auto js = makeCustomParams(arrayStr);
        auto params = parseCustomParamsFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(params.size(), 1);
        QCOMPARE(params[0].enumOptions.size(), 256);
    }

    // =========================================================================
    // parseMetadataFromJs — string length truncation
    // =========================================================================

    void testParseMetadata_nameTruncated()
    {
        QString longName = QString(200, QLatin1Char('n'));
        auto js = makeMetadata(QStringLiteral(R"({ name: "%1" })").arg(longName));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.name.size(), 100);
    }

    void testParseMetadata_descriptionTruncated()
    {
        QString longDesc = QString(700, QLatin1Char('d'));
        auto js = makeMetadata(QStringLiteral(R"({ description: "%1" })").arg(longDesc));
        auto meta = parseMetadataFromJs(js, QStringLiteral("test.js"));
        QCOMPARE(meta.description.size(), 500);
    }
};

QTEST_MAIN(TestMetadataParsing)
#include "test_metadata_parsing.moc"
