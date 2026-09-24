// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The four shader-pack metadata schemas hard-code numbers that BELONG to the
// C++ contracts: how many buffer passes a family allows, how far a buffer may
// be downscaled, how many user textures a pack may declare. Nothing connected
// the two, so every one of those numbers was a hand edit that had to be
// remembered across four files.
//
// It has already been paid for once. Raising kMaxBufferPasses from 4 to 8 meant
// editing maxItems in three places in the surface schema, and lowering
// kMinBufferScale from 0.125 to 0.03125 meant editing minimum in three more
// across two schemas. Both were done correctly, by hand, with nothing checking.
// scripts/validate-json-schemas.py cannot help: it validates DATA against a
// schema and never reads a C++ constant.
//
// So this file is the missing direction. It reads the committed schemas out of
// the source tree and compares the bound each one publishes to the constant it
// is supposed to track. A future constant change now fails a test naming the
// schema and the key, instead of shipping a schema that silently rejects a
// legal pack or accepts an illegal one.

#include <QtTest>

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorPointer/PointerShaderContract.h>
#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorSurface/SurfaceShaderContract.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

QJsonObject loadSchema(const QString& name)
{
    QFile file(QStringLiteral(P_SOURCE_DIR "/data/schemas/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        return {};
    }
    return doc.object();
}

/// A schema property, by name, from the top-level `properties` map.
QJsonObject prop(const QJsonObject& schema, const char* key)
{
    return schema.value(QLatin1String("properties")).toObject().value(QLatin1String(key)).toObject();
}

} // namespace

class TestShaderSchemaBounds : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Every schema must still parse and carry a properties map, so a later
    /// case that reads a missing key fails loudly rather than comparing a
    /// default-constructed 0 against a real constant and passing by accident.
    void schemasLoad_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("surface") << QStringLiteral("surface-metadata.schema.json");
        QTest::newRow("pointer") << QStringLiteral("pointer-metadata.schema.json");
        QTest::newRow("animation") << QStringLiteral("animation-metadata.schema.json");
        QTest::newRow("overlay") << QStringLiteral("shader-metadata.schema.json");
    }

    void schemasLoad()
    {
        QFETCH(QString, name);
        const QJsonObject schema = loadSchema(name);
        QVERIFY2(!schema.isEmpty(), qPrintable(name));
        QVERIFY2(schema.contains(QLatin1String("properties")), qPrintable(name));
    }

    /// The surface family's three per-pass arrays are all one-entry-per-pass, so
    /// all three track the SAME constant. They drifted together last time by
    /// being remembered together; here they are pinned together.
    void surfacePerPassArraysTrackTheBufferPassBudget()
    {
        const QJsonObject schema = loadSchema(QStringLiteral("surface-metadata.schema.json"));
        for (const char* key : {"bufferShaders", "bufferScales", "bufferWraps", "bufferFilters"}) {
            const QJsonValue maxItems = prop(schema, key).value(QLatin1String("maxItems"));
            QVERIFY2(maxItems.isDouble(), key);
            // QVERIFY2 rather than QCOMPARE: the whole point is to name which of
            // the four arrays was left behind, and QCOMPARE's message cannot.
            QVERIFY2(maxItems.toInt() == PhosphorShaders::kMaxBufferPasses,
                     qPrintable(QStringLiteral("surface %1: maxItems %2, kMaxBufferPasses %3")
                                    .arg(QLatin1String(key))
                                    .arg(maxItems.toInt())
                                    .arg(PhosphorShaders::kMaxBufferPasses)));
        }
    }

    /// The pointer family caps its chain lower than the shared budget, on
    /// purpose, so it tracks its OWN constant. Comparing it to the shared one
    /// would be the bug this test exists to prevent.
    void pointerBufferShadersTracksThePointerBudget()
    {
        const QJsonObject schema = loadSchema(QStringLiteral("pointer-metadata.schema.json"));
        const QJsonValue maxItems = prop(schema, "bufferShaders").value(QLatin1String("maxItems"));
        QVERIFY(maxItems.isDouble());
        QCOMPARE(maxItems.toInt(), PhosphorPointerShaders::PointerShaderContract::kMaxBufferPasses);
        QVERIFY(PhosphorPointerShaders::PointerShaderContract::kMaxBufferPasses < PhosphorShaders::kMaxBufferPasses);
    }

    void animationBufferShadersTracksTheAnimationBudget()
    {
        const QJsonObject schema = loadSchema(QStringLiteral("animation-metadata.schema.json"));
        const QJsonValue maxItems = prop(schema, "bufferShaders").value(QLatin1String("maxItems"));
        QVERIFY(maxItems.isDouble());
        QCOMPARE(maxItems.toInt(), PhosphorAnimationShaders::AnimationShaderContract::kMaxBufferPasses);
    }

    /// Every scale bound in every family, against the one pair of constants.
    /// The per-item bounds inside the surface array are reached through `items`,
    /// which is where a hand edit is most likely to miss one.
    void everyBufferScaleBoundTracksTheScaleConstants()
    {
        const QJsonObject surface = loadSchema(QStringLiteral("surface-metadata.schema.json"));
        const QJsonObject pointer = loadSchema(QStringLiteral("pointer-metadata.schema.json"));
        const QJsonObject animation = loadSchema(QStringLiteral("animation-metadata.schema.json"));
        const QJsonObject overlay = loadSchema(QStringLiteral("shader-metadata.schema.json"));

        const QList<QPair<QString, QJsonObject>> bounded = {
            {QStringLiteral("surface bufferScale"), prop(surface, "bufferScale")},
            {QStringLiteral("surface bufferScales items"),
             prop(surface, "bufferScales").value(QLatin1String("items")).toObject()},
            {QStringLiteral("pointer bufferScale"), prop(pointer, "bufferScale")},
            {QStringLiteral("animation bufferScale"), prop(animation, "bufferScale")},
            {QStringLiteral("overlay bufferScale"), prop(overlay, "bufferScale")},
        };

        for (const auto& [where, node] : bounded) {
            const QJsonValue minimum = node.value(QLatin1String("minimum"));
            const QJsonValue maximum = node.value(QLatin1String("maximum"));
            QVERIFY2(minimum.isDouble(), qPrintable(where + QStringLiteral(": no minimum")));
            QVERIFY2(maximum.isDouble(), qPrintable(where + QStringLiteral(": no maximum")));
            QVERIFY2(qFuzzyCompare(minimum.toDouble(), PhosphorShaders::kMinBufferScale),
                     qPrintable(QStringLiteral("%1: minimum %2, kMinBufferScale %3")
                                    .arg(where)
                                    .arg(minimum.toDouble())
                                    .arg(PhosphorShaders::kMinBufferScale)));
            QVERIFY2(qFuzzyCompare(maximum.toDouble(), PhosphorShaders::kMaxBufferScale),
                     qPrintable(QStringLiteral("%1: maximum %2, kMaxBufferScale %3")
                                    .arg(where)
                                    .arg(maximum.toDouble())
                                    .arg(PhosphorShaders::kMaxBufferScale)));
        }
    }

    /// The user-texture budget. Surface and pointer declare it separately and
    /// the contracts say the two are deliberately equal, so a change to either
    /// has to reach its own schema.
    void textureArraysTrackTheUserTextureSlotBudgets()
    {
        const QJsonObject surface = loadSchema(QStringLiteral("surface-metadata.schema.json"));
        const QJsonValue surfaceMax = prop(surface, "textures").value(QLatin1String("maxItems"));
        QVERIFY(surfaceMax.isDouble());
        QCOMPARE(surfaceMax.toInt(), PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots);

        const QJsonObject pointer = loadSchema(QStringLiteral("pointer-metadata.schema.json"));
        const QJsonValue pointerMax = prop(pointer, "textures").value(QLatin1String("maxItems"));
        QVERIFY(pointerMax.isDouble());
        QCOMPARE(pointerMax.toInt(), PhosphorPointerShaders::PointerShaderContract::kMaxUserTextureSlots);
    }
};

QTEST_MAIN(TestShaderSchemaBounds)
#include "test_shader_schema_bounds.moc"
