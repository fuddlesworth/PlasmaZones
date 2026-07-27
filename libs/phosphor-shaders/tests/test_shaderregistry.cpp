// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorShaders/ShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorShaders;

/// In-lib coverage for ShaderRegistry::parsePackMetadata — the offline parse
/// entry point the pack validator uses, exercising the same parser the live
/// scan runs. Pins the pack-path containment guard, the auto-slot assignment
/// contract, the duplicate-id dedup, and the schema/size gates, so the lib
/// keeps its advertised standalone-build test coverage for its largest TU.
class TestShaderRegistry : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString makePack(const QJsonObject& meta, const QString& name = QStringLiteral("pack"))
    {
        const QString packDir = m_dir.filePath(name);
        QDir().mkpath(packDir);
        QFile f(packDir + QStringLiteral("/metadata.json"));
        if (!f.open(QIODevice::WriteOnly)) {
            return {};
        }
        f.write(QJsonDocument(meta).toJson());
        f.close();
        QFile frag(packDir + QStringLiteral("/effect.frag"));
        frag.open(QIODevice::WriteOnly);
        frag.write("void main() {}\n");
        frag.close();
        return packDir;
    }

    static QJsonObject baseMeta()
    {
        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("test-pack"));
        meta.insert(QLatin1String("name"), QStringLiteral("Test Pack"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("parameters"), QJsonArray());
        return meta;
    }

    static QJsonObject param(const QString& id, const QString& type, int slot = -1)
    {
        QJsonObject p;
        p.insert(QLatin1String("id"), id);
        p.insert(QLatin1String("name"), id);
        p.insert(QLatin1String("group"), QStringLiteral("g"));
        p.insert(QLatin1String("type"), type);
        p.insert(QLatin1String("default"), 0.0);
        if (slot >= 0) {
            p.insert(QLatin1String("slot"), slot);
        }
        return p;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    void testParsesAMinimalPack()
    {
        const QString packDir = makePack(baseMeta(), QStringLiteral("minimal"));
        QString error;
        const auto info = ShaderRegistry::parsePackMetadata(packDir, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(info.name, QStringLiteral("Test Pack"));
        QCOMPARE(info.packDir, QDir(packDir).absolutePath());
        QVERIFY(info.sourcePath.endsWith(QLatin1String("/effect.frag")));
    }

    void testRefusesATraversalFragmentPath()
    {
        QJsonObject meta = baseMeta();
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("../../outside.frag"));
        const QString packDir = makePack(meta, QStringLiteral("traversal"));
        const auto info = ShaderRegistry::parsePackMetadata(packDir);
        // The guard clears the escaping path rather than resolving it.
        QVERIFY(info.sourcePath.isEmpty());
    }

    void testRefusesAnAbsoluteFragmentPath()
    {
        QJsonObject meta = baseMeta();
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("/etc/hostname"));
        const QString packDir = makePack(meta, QStringLiteral("absolute"));
        const auto info = ShaderRegistry::parsePackMetadata(packDir);
        QVERIFY(info.sourcePath.isEmpty());
    }

    void testAutoSlotsFillDeclarationOrderAroundExplicitSlots()
    {
        QJsonObject meta = baseMeta();
        QJsonArray params;
        params.append(param(QStringLiteral("a"), QStringLiteral("float"))); // auto
        params.append(param(QStringLiteral("b"), QStringLiteral("float"), 0)); // explicit 0
        params.append(param(QStringLiteral("c"), QStringLiteral("float"))); // auto
        params.append(param(QStringLiteral("col"), QStringLiteral("color"))); // color pool numbers independently
        meta.insert(QLatin1String("parameters"), params);
        const QString packDir = makePack(meta, QStringLiteral("slots"));
        const auto info = ShaderRegistry::parsePackMetadata(packDir);
        QCOMPARE(info.parameters.size(), 4);
        // "a" skips the reserved lane 0 (claimed by "b"), landing on 1; "c"
        // continues to 2; the color pool starts fresh at 0.
        QCOMPARE(info.parameters[0].slot, 1);
        QCOMPARE(info.parameters[1].slot, 0);
        QCOMPARE(info.parameters[2].slot, 2);
        QCOMPARE(info.parameters[3].slot, 0);
    }

    void testDuplicateParameterIdFirstDeclarationWins()
    {
        QJsonObject meta = baseMeta();
        QJsonArray params;
        params.append(param(QStringLiteral("speed"), QStringLiteral("float"), 3));
        params.append(param(QStringLiteral("speed"), QStringLiteral("float"), 7));
        meta.insert(QLatin1String("parameters"), params);
        const QString packDir = makePack(meta, QStringLiteral("dup"));
        const auto info = ShaderRegistry::parsePackMetadata(packDir);
        QCOMPARE(info.parameters.size(), 1);
        QCOMPARE(info.parameters[0].slot, 3);
        // And the preamble emits exactly one define for it.
        const QString preamble = ShaderRegistry::paramPreamble(info);
        QCOMPARE(preamble.count(QStringLiteral("#define p_speed ")), 1);
    }

    void testSchemaGateRefusesAPackMissingRequiredFields()
    {
        QJsonObject meta; // no id / name / fragmentShader / parameters
        meta.insert(QLatin1String("description"), QStringLiteral("not a pack"));
        const QString packDir = makePack(meta, QStringLiteral("schemafail"));
        QString error;
        const auto info = ShaderRegistry::parsePackMetadata(packDir, &error);
        QVERIFY(!info.isValid());
        QVERIFY2(error.contains(QLatin1String("schema")), qPrintable(error));
    }

    void testSinglePassPackCarriesNoBufferState()
    {
        // No multipass key and no buffer.frag on disk: the parser must not
        // publish any buffer fields (the phantom implicit buffer.frag bug).
        const QString packDir = makePack(baseMeta(), QStringLiteral("singlepass"));
        const auto info = ShaderRegistry::parsePackMetadata(packDir);
        QVERIFY(!info.isMultipass);
        QVERIFY(info.bufferShaderPaths.isEmpty());
        QVERIFY(info.bufferWraps.isEmpty());
        QVERIFY(info.bufferFilters.isEmpty());
        QVERIFY(!info.bufferFeedback);
        QVERIFY(!info.useDepthBuffer);
    }
};

QTEST_MAIN(TestShaderRegistry)
#include "test_shaderregistry.moc"
