// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../metadataloader.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

QString writeMetadataJson(QTemporaryDir& dir, const QString& body)
{
    const QString path = QDir(dir.path()).filePath(QStringLiteral("metadata.json"));
    QFile f(path);
    Q_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(body.toUtf8());
    return path;
}

void writeFile(QTemporaryDir& dir, const QString& name)
{
    const QString path = QDir(dir.path()).filePath(name);
    QFile f(path);
    Q_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("// stub\n");
}

} // namespace

class TestMetadataLoader : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unsetSlotsKeepSentinels()
    {
        // The PR's customParams sentinel fix: an unset slot must remain at
        // the (-1.0,-1.0,-1.0,-1.0) sentinel so the GLSL fallback path runs.
        // Initializing to {} would silently set every unset slot to zero.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": []
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        for (const auto& v : md.customParams) {
            QCOMPARE(v.x(), -1.0f);
            QCOMPARE(v.y(), -1.0f);
            QCOMPARE(v.z(), -1.0f);
            QCOMPARE(v.w(), -1.0f);
        }
        for (const auto& c : md.customColors) {
            QCOMPARE(c.alphaF(), 0.0); // transparent black sentinel
        }
    }

    void slotFanOutAcrossVec4Channels()
    {
        // Slot S → customParams[S/4].(x|y|z|w)[S%4]. Verify slots 0..7 land
        // in slots[0]/slots[1] correctly, and a slot in the second half (e.g.
        // slot 9 → slots[2].y) doesn't get clobbered into slots[9] (which
        // would be out of range under the broken indexing the PR fixed).
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "p0", "type": "float", "slot": 0,  "default": 0.5},
                {"id": "p1", "type": "float", "slot": 1,  "default": 1.5},
                {"id": "p2", "type": "float", "slot": 2,  "default": 2.5},
                {"id": "p3", "type": "float", "slot": 3,  "default": 3.5},
                {"id": "p4", "type": "float", "slot": 4,  "default": 4.5},
                {"id": "p9", "type": "float", "slot": 9,  "default": 9.5},
                {"id": "p31","type": "float", "slot": 31, "default": 31.5}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QCOMPARE(md.customParams[0].x(), 0.5f);
        QCOMPARE(md.customParams[0].y(), 1.5f);
        QCOMPARE(md.customParams[0].z(), 2.5f);
        QCOMPARE(md.customParams[0].w(), 3.5f);
        QCOMPARE(md.customParams[1].x(), 4.5f);
        QCOMPARE(md.customParams[2].y(), 9.5f); // slot 9 → [2].y
        QCOMPARE(md.customParams[7].w(), 31.5f); // slot 31 → [7].w
    }

    void boolMapsToOneOrZero()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "on",  "type": "bool", "slot": 0, "default": true},
                {"id": "off", "type": "bool", "slot": 1, "default": false}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QCOMPARE(md.customParams[0].x(), 1.0f);
        QCOMPARE(md.customParams[0].y(), 0.0f);
    }

    void colorSlotPopulatesIndependentArray()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "tint", "type": "color", "slot": 5, "default": "#80ff00ff"}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QCOMPARE(md.customColors[5], QColor(QStringLiteral("#80ff00ff")));
    }

    void outOfRangeSlotsAreDropped()
    {
        // The seedParam guard rejects slot >= 32. The seedColor guard rejects
        // slot >= 16. Image type rejects slot >= 4. Out-of-range writes must
        // not corrupt valid neighbours.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "valid",   "type": "float", "slot": 0,  "default": 7.0},
                {"id": "tooBig",  "type": "float", "slot": 32, "default": 99.0},
                {"id": "color99", "type": "color", "slot": 99, "default": "#ff0000"},
                {"id": "img4",    "type": "image", "slot": 4,  "default": "tex.png"}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QCOMPARE(md.customParams[0].x(), 7.0f);
        // Sentinel preserved on the unrelated slots
        QCOMPARE(md.customParams[7].w(), -1.0f);
        // No image slot 4 (image array is 4-element).
        for (const auto& path : md.userTextures) {
            QVERIFY(path.isEmpty() || !path.endsWith(QStringLiteral("tex.png")));
        }
    }

    void unknownTypeIsIgnoredCleanly()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "weird", "type": "matrix3", "slot": 0, "default": 1.0}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        // Slot stays at sentinel because no recognised type wrote it.
        QCOMPARE(md.customParams[0].x(), -1.0f);
    }

    void missingFragmentShaderFails()
    {
        QTemporaryDir dir;
        // No effect.frag written.
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": ""
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(!PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
    }

    void missingFragmentShaderKeyFailsWhenDefaultAbsent()
    {
        // When fragmentShader is absent the loader falls back to the daemon's
        // default name (effect.frag). If that file isn't on disk either, the
        // loader must reject at the boundary rather than silently propagate a
        // path that surfaces later as an opaque shader-compile error.
        QTemporaryDir dir;
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({"id": "test"})"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(!PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
    }

    void missingFragmentShaderKeySucceedsWhenDefaultExists()
    {
        // Symmetric guard: with the default file present, an omitted key
        // resolves cleanly to <dir>/effect.frag and the loader succeeds.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({"id": "test"})"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QVERIFY(md.fragmentShader.endsWith(QStringLiteral("effect.frag")));
    }

    void negativeSlotIsDropped()
    {
        // An explicit negative slot is a metadata error and is dropped
        // (with a warning, not asserted on here). Adjacent valid slots
        // must remain untouched.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "good", "type": "float", "slot": 0,  "default": 4.0},
                {"id": "bad",  "type": "float", "slot": -3, "default": 9.0}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QCOMPARE(md.customParams[0].x(), 4.0f);
        // The negative slot wrote nothing — sentinel preserved on neighbours.
        QCOMPARE(md.customParams[0].y(), -1.0f);
        QCOMPARE(md.customParams[7].w(), -1.0f);
    }

    void missingSlotFieldIsSilentlySkipped()
    {
        // A parameter without a slot field is treated as UI-only metadata
        // (no shader push), distinct from an explicit negative which is a
        // metadata error. Verifies no slot is touched.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "uiOnly", "type": "float", "default": 5.0}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        for (const auto& v : md.customParams) {
            QCOMPARE(v.x(), -1.0f);
            QCOMPARE(v.y(), -1.0f);
            QCOMPARE(v.z(), -1.0f);
            QCOMPARE(v.w(), -1.0f);
        }
    }
};

QTEST_GUILESS_MAIN(TestMetadataLoader)
#include "test_metadataloader.moc"
