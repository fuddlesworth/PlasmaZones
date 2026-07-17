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
    // Don't put the f.open() call inside Q_ASSERT — Q_ASSERT compiles
    // out to `static_cast<void>(false && (cond))` in release builds,
    // which short-circuits the open() and leaves the file unwritten.
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qFatal("writeMetadataJson: failed to open %s: %s", qPrintable(path), qPrintable(f.errorString()));
    }
    f.write(body.toUtf8());
    return path;
}

void writeFile(QTemporaryDir& dir, const QString& name)
{
    const QString path = QDir(dir.path()).filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qFatal("writeFile: failed to open %s: %s", qPrintable(path), qPrintable(f.errorString()));
    }
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
        for (const auto& texPath : md.userTextures) {
            QVERIFY(texPath.isEmpty() || !texPath.endsWith(QStringLiteral("tex.png")));
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

    void unknownTypeStillClaimsLane()
    {
        // An unknown-type param claims a scalar lane (the registry pools
        // unknown types as scalar) even though its value is never seeded, so
        // a float declared after it lands on the NEXT lane — the same
        // numbering the p_<id> preamble uses.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "weird", "type": "matrix3", "default": 1.0},
                {"id": "speed", "type": "float", "default": 5.0}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QCOMPARE(md.customParams[0].x(), -1.0f); // weird claimed lane 0, unseeded
        QCOMPARE(md.customParams[0].y(), 5.0f); // speed packs into lane 1
    }

    void malformedJsonFails()
    {
        // A file that isn't parseable JSON must be rejected at the boundary
        // (QJsonParseError path), not surface later as an empty metadata
        // struct with a missing fragment shader.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        const QString path = writeMetadataJson(dir, QStringLiteral("{ this is not json"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(!PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
    }

    void nonObjectRootFails()
    {
        // Valid JSON whose root is not an object (array here) must be
        // rejected the same way — the loader reads keys off the root object.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        const QString path = writeMetadataJson(dir, QStringLiteral("[1, 2, 3]"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(!PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
    }

    void multipassBufferChainIsParsed()
    {
        // The multipass fields ride through the loader: bufferShaders resolve
        // absolute against the pack dir (traversal-guarded like every other
        // path), bufferWraps ride along verbatim, and the single-bufferShader
        // form populates the scalar field.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("bufA.frag"));
        writeFile(dir, QStringLiteral("bufB.frag"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "multipass": true,
            "bufferShaders": ["bufA.frag", "bufB.frag"],
            "bufferWraps": ["repeat", "clamp"],
            "bufferFilters": ["nearest", "linear"]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QVERIFY(md.multipass);
        QCOMPARE(md.bufferShaders.size(), 2);
        QVERIFY(md.bufferShaders[0].endsWith(QStringLiteral("bufA.frag")));
        QVERIFY(md.bufferShaders[1].endsWith(QStringLiteral("bufB.frag")));
        const QStringList expectedWraps{QStringLiteral("repeat"), QStringLiteral("clamp")};
        QCOMPARE(md.bufferWraps, expectedWraps);
        const QStringList expectedFilters{QStringLiteral("nearest"), QStringLiteral("linear")};
        QCOMPARE(md.bufferFilters, expectedFilters);

        QTemporaryDir dir2;
        writeFile(dir2, QStringLiteral("effect.frag"));
        writeFile(dir2, QStringLiteral("buffer.frag"));
        const QString singlePath = writeMetadataJson(dir2, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "multipass": true,
            "bufferShader": "buffer.frag"
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata mdSingle;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(singlePath, mdSingle));
        QVERIFY(mdSingle.bufferShader.endsWith(QStringLiteral("buffer.frag")));
        QVERIFY(mdSingle.bufferShaders.isEmpty());
    }

    void traversalEscapeOnBufferShadersIsDropped()
    {
        // A bufferShaders entry that escapes the metadata directory is
        // rejected by the traversal guard and must be skipped, not appended
        // as an empty path the renderer would try to load. Entries that
        // resolve inside the directory survive.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("bufA.frag"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "multipass": true,
            "bufferShaders": ["bufA.frag", "../../../etc/passwd"]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QCOMPARE(md.bufferShaders.size(), 1);
        QVERIFY(md.bufferShaders[0].endsWith(QStringLiteral("bufA.frag")));
        for (const auto& bufPath : md.bufferShaders) {
            QVERIFY(!bufPath.isEmpty());
            QVERIFY(!bufPath.contains(QStringLiteral("/etc/")));
        }
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

    void negativeSlotAutoAssigns()
    {
        // An explicit negative slot is treated exactly like a missing one
        // (registry T1.1 parity): the param auto-assigns the next free lane
        // of its pool after explicit slots are reserved. Here slot 0 is
        // taken, so "bad" packs into lane 1.
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
        QCOMPARE(md.customParams[0].y(), 9.0f); // auto-assigned lane 1
        QCOMPARE(md.customParams[7].w(), -1.0f);
    }

    void wallpaperFlagIsParsed()
    {
        // The renderer treats metadata.wallpaper=true as "warn loud + force
        // useWallpaper=false on the effect" — see seedShaderEffect in
        // renderer.cpp. The loader's job is just to surface the field
        // truthfully so the renderer can decide. This test pins the parse so
        // a future schema rename (or accidental swap to .toString) doesn't
        // silently drop the warning path the renderer relies on.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        const QString withTrue = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "wallpaper": true
        })"));
        PlasmaZones::ShaderRender::ShaderMetadata mdTrue;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(withTrue, mdTrue));
        QVERIFY(mdTrue.wallpaper);

        // Default false when omitted — guards against the inverse footgun
        // (a default change here would silently turn every shader into a
        // wallpaper-divergence warning).
        QTemporaryDir dir2;
        writeFile(dir2, QStringLiteral("effect.frag"));
        const QString withoutKey = writeMetadataJson(dir2, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag"
        })"));
        PlasmaZones::ShaderRender::ShaderMetadata mdDefault;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(withoutKey, mdDefault));
        QVERIFY(!mdDefault.wallpaper);
    }

    void traversalEscapeFallsBack()
    {
        // A relative path containing `..` that escapes the metadata directory
        // must be rejected at the boundary. The loader resolves the implicit
        // default fragment filename (effect.frag) when fragmentShader is set to
        // an escape attempt — an empty string falls back, but the file isn't
        // present so loadShaderMetadata returns false at the existence check.
        QTemporaryDir dir;
        // Don't write effect.frag — we want the loader to fail when traversal
        // is rejected and the default-fallback path doesn't exist.
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "../../../etc/passwd"
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        // Either fragmentShader resolves to empty (rejected traversal) and the
        // existence check fails, or it stays as the rejected sentinel — either
        // way, loadShaderMetadata must return false. Critical: it must not
        // resolve to the literal /etc/passwd-style escape.
        QVERIFY(!PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QVERIFY(!md.fragmentShader.contains(QStringLiteral("/etc/")));
    }

    void traversalEscapeOnImageSlotDropsPath()
    {
        // Image-slot paths that escape are silently dropped (loader still
        // succeeds because the fragment shader is fine; just the image isn't
        // populated). Pin that the escape attempt does NOT appear in
        // userTextures.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "img", "type": "image", "slot": 0, "default": "../../../etc/passwd"}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        for (const auto& texPath : md.userTextures) {
            QVERIFY(!texPath.contains(QStringLiteral("/etc/")));
        }
    }

    void missingSlotFieldAutoAssigns()
    {
        // A parameter without a slot field auto-assigns the next free lane
        // of its pool in declaration order (registry T1.1 parity), mixing
        // freely with explicit slots — explicit lanes are reserved first,
        // then the auto params fill the gaps. Pools number independently
        // (the color claims color lane 0 regardless of the scalar traffic).
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "auto0",    "type": "float", "default": 5.0},
                {"id": "pinned1",  "type": "float", "slot": 1, "default": 6.0},
                {"id": "auto2",    "type": "float", "default": 7.0},
                {"id": "tint",     "type": "color", "default": "#22d3ee"}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QCOMPARE(md.customParams[0].x(), 5.0f); // auto0 → lane 0
        QCOMPARE(md.customParams[0].y(), 6.0f); // pinned1 → lane 1 (explicit)
        QCOMPARE(md.customParams[0].z(), 7.0f); // auto2 → lane 2 (skips reserved 1)
        QCOMPARE(md.customParams[0].w(), -1.0f);
        QCOMPARE(md.customColors[0], QColor(QStringLiteral("#22d3ee")));
    }

    void invalidGlslIdClaimsNoLane()
    {
        // A param whose id is not a valid GLSL identifier body gets no
        // p_<id> define from the registry's preamble, so it must claim no
        // lane here either (registry T1.1 parity) — even mid-list: the
        // valid params before and after it take consecutive lanes as if
        // the invalid one were absent, and its default is never seeded.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "first",  "type": "float", "default": 1.5},
                {"id": "bad-id", "type": "float", "default": 9.0},
                {"id": "second", "type": "float", "default": 2.5}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QCOMPARE(md.customParams[0].x(), 1.5f); // first → lane 0
        QCOMPARE(md.customParams[0].y(), 2.5f); // second → lane 1 (bad-id claimed nothing)
        QCOMPARE(md.customParams[0].z(), -1.0f);
    }

    void imagePoolAutoAssigns()
    {
        // The image pool auto-numbers independently of scalars and colors:
        // two slotless image params take image lanes 0 and 1 regardless of
        // scalar traffic, and their wrap fields ride along.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("effect.frag"));
        writeFile(dir, QStringLiteral("zone.vert"));
        writeFile(dir, QStringLiteral("texA.png"));
        writeFile(dir, QStringLiteral("texB.png"));
        const QString path = writeMetadataJson(dir, QStringLiteral(R"({
            "id": "test",
            "fragmentShader": "effect.frag",
            "vertexShader": "zone.vert",
            "parameters": [
                {"id": "speed", "type": "float", "default": 3.0},
                {"id": "texA",  "type": "image", "default": "texA.png"},
                {"id": "texB",  "type": "image", "default": "texB.png", "wrap": "repeat"}
            ]
        })"));

        PlasmaZones::ShaderRender::ShaderMetadata md;
        QVERIFY(PlasmaZones::ShaderRender::loadShaderMetadata(path, md));
        QCOMPARE(md.customParams[0].x(), 3.0f); // scalar pool unaffected
        QVERIFY(md.userTextures[0].endsWith(QStringLiteral("texA.png")));
        QVERIFY(md.userTextures[1].endsWith(QStringLiteral("texB.png")));
        QCOMPARE(md.userTextureWraps[0], QStringLiteral("clamp"));
        QCOMPARE(md.userTextureWraps[1], QStringLiteral("repeat"));
    }
};

QTEST_GUILESS_MAIN(TestMetadataLoader)
#include "test_metadataloader.moc"
