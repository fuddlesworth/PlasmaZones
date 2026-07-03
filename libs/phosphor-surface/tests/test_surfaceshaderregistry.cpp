// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace PhosphorSurfaceShaders;

namespace {

/// Write @p contents to @p path, creating parent directories. Fails the
/// current test on any I/O error.
bool writeFile(const QString& path, const QByteArray& contents)
{
    const QFileInfo fi(path);
    if (!QDir().mkpath(fi.absolutePath()))
        return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(contents) == contents.size();
}

/// Author a pack directory `<root>/<subdir>/metadata.json` plus its
/// referenced shader/asset files. @p extraFiles are relative names (to
/// the pack dir) written as empty stubs so on-disk existence checks pass.
bool writePack(const QString& root, const QString& subdir, const QJsonObject& metadata, const QStringList& extraFiles)
{
    const QString packDir = root + QLatin1Char('/') + subdir;
    const QJsonDocument doc(metadata);
    if (!writeFile(packDir + QStringLiteral("/metadata.json"), doc.toJson()))
        return false;
    for (const QString& rel : extraFiles) {
        if (!writeFile(packDir + QLatin1Char('/') + rel, QByteArrayLiteral("// stub\n")))
            return false;
    }
    return true;
}

/// A minimal in-memory effect that passes isValid() (id + fragment path).
SurfaceShaderEffect makeInMemoryEffect(const QString& id)
{
    SurfaceShaderEffect e;
    e.id = id;
    e.fragmentShaderPath = QStringLiteral("effect.frag");
    return e;
}

SurfaceShaderEffect::ParameterInfo makeParam(const QString& id, const QString& type, const QVariant& defaultValue)
{
    SurfaceShaderEffect::ParameterInfo p;
    p.id = id;
    p.type = type;
    p.defaultValue = defaultValue;
    return p;
}

} // namespace

class TestSurfaceShaderRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ── Discovery ────────────────────────────────────────────────────────

    void discovers_valid_pack_and_reports_unknown_absent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("border"));
        meta.insert(QLatin1String("name"), QStringLiteral("Border"));
        meta.insert(QLatin1String("description"), QStringLiteral("A window border."));
        meta.insert(QLatin1String("category"), QStringLiteral("Border"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QVERIFY(writePack(tmp.path(), QStringLiteral("border"), meta, {QStringLiteral("effect.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        QVERIFY(registry.hasEffect(QStringLiteral("border")));
        const SurfaceShaderEffect e = registry.effect(QStringLiteral("border"));
        QVERIFY(e.isValid());
        QCOMPARE(e.id, QStringLiteral("border"));
        QCOMPARE(e.name, QStringLiteral("Border"));
        QCOMPARE(e.category, QStringLiteral("Border"));
        // Fragment path is resolved to an absolute path under the pack dir.
        QVERIFY(QFileInfo(e.fragmentShaderPath).isAbsolute());
        QVERIFY(e.fragmentShaderPath.endsWith(QStringLiteral("effect.frag")));
        QCOMPARE(registry.effectIds(), QStringList{QStringLiteral("border")});

        // Unknown id → absent, and effect() yields a default/empty struct.
        QVERIFY(!registry.hasEffect(QStringLiteral("nope")));
        const SurfaceShaderEffect missing = registry.effect(QStringLiteral("nope"));
        QVERIFY(!missing.isValid());
        QVERIFY(missing.id.isEmpty());
    }

    void multipass_with_missing_buffer_file_fails_closed_to_single_pass()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("blur"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("multipass"), true);
        QJsonArray buffers;
        buffers.append(QStringLiteral("buffer_a.frag")); // declared but NOT written to disk
        meta.insert(QLatin1String("bufferShaders"), buffers);
        QJsonArray wraps;
        wraps.append(QStringLiteral("clamp"));
        meta.insert(QLatin1String("bufferWraps"), wraps);
        // effect.frag exists; buffer_a.frag intentionally missing.
        QVERIFY(writePack(tmp.path(), QStringLiteral("blur"), meta, {QStringLiteral("effect.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const SurfaceShaderEffect e = registry.effect(QStringLiteral("blur"));
        QVERIFY(e.isValid());
        // A missing buffer disables multipass entirely (fail-closed) rather
        // than silently compacting and shifting per-buffer overrides.
        QVERIFY(!e.isMultipass);
        QVERIFY(e.bufferShaderPaths.isEmpty());
        // Orphaned per-buffer overrides are cleared in lockstep.
        QVERIFY(e.bufferWraps.isEmpty());
    }

    // ── SurfaceShaderEffect::fromJson validation ─────────────────────────

    void fromJson_resets_unknown_texture_wrap_to_empty()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("tex"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QJsonArray textures;
        QJsonObject t;
        t.insert(QLatin1String("path"), QStringLiteral("noise.png"));
        t.insert(QLatin1String("wrap"), QStringLiteral("bogus"));
        textures.append(t);
        obj.insert(QLatin1String("textures"), textures);

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(obj);
        QCOMPARE(e.textures.size(), 1);
        QCOMPARE(e.textures[0].path, QStringLiteral("noise.png"));
        QVERIFY(e.textures[0].wrap.isEmpty()); // unknown token reset to runtime default
    }

    void fromJson_resets_unknown_buffer_wrap_and_filter_tokens_in_place()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("buf"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        obj.insert(QLatin1String("bufferWrap"), QStringLiteral("weird"));
        obj.insert(QLatin1String("bufferFilter"), QStringLiteral("weird"));
        // All entries non-empty so positional alignment is unambiguous: the
        // invalid middle token is replaced IN PLACE, not dropped.
        QJsonArray wraps;
        wraps.append(QStringLiteral("mirror"));
        wraps.append(QStringLiteral("bogus"));
        wraps.append(QStringLiteral("clamp"));
        obj.insert(QLatin1String("bufferWraps"), wraps);
        QJsonArray filters;
        filters.append(QStringLiteral("linear"));
        filters.append(QStringLiteral("bogus"));
        obj.insert(QLatin1String("bufferFilters"), filters);

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(obj);
        QVERIFY(e.bufferWrap.isEmpty());
        QVERIFY(e.bufferFilter.isEmpty());
        // Positional alignment preserved: only the invalid slot cleared.
        QCOMPARE(e.bufferWraps, (QStringList{QStringLiteral("mirror"), QString(), QStringLiteral("clamp")}));
        QCOMPARE(e.bufferFilters, (QStringList{QStringLiteral("linear"), QString()}));

        // Alignment must also survive a full toJson→fromJson round trip: the
        // cleared middle slot serializes as "" and MUST be kept in place on
        // the next load (dropping it would shift "clamp" from buffer[2] to
        // buffer[1] the first time a saved pack is re-read).
        const SurfaceShaderEffect r = SurfaceShaderEffect::fromJson(e.toJson());
        QCOMPARE(r.bufferWraps, e.bufferWraps);
        QCOMPARE(r.bufferFilters, e.bufferFilters);
    }

    void fromJson_keeps_explicit_empty_buffer_override_slots_in_place()
    {
        // An author writing ["clamp", "", "repeat"] means "buffer 1 uses the
        // default". The empty middle entry is a positional placeholder and
        // must not be dropped.
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("buf2"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QJsonArray wraps;
        wraps.append(QStringLiteral("clamp"));
        wraps.append(QString());
        wraps.append(QStringLiteral("repeat"));
        obj.insert(QLatin1String("bufferWraps"), wraps);

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(obj);
        QCOMPARE(e.bufferWraps, (QStringList{QStringLiteral("clamp"), QString(), QStringLiteral("repeat")}));
    }

    void fromJson_clamps_buffer_scale_to_contract_range()
    {
        const auto scaleFor = [](double raw) {
            QJsonObject obj;
            obj.insert(QLatin1String("id"), QStringLiteral("s"));
            obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
            obj.insert(QLatin1String("bufferScale"), raw);
            return SurfaceShaderEffect::fromJson(obj).bufferScale;
        };
        QCOMPARE(scaleFor(5.0), SurfaceShaderEffect::kMaxBufferScale);
        QCOMPARE(scaleFor(0.001), SurfaceShaderEffect::kMinBufferScale);
        QCOMPARE(scaleFor(0.5), 0.5);
    }

    void parseEffect_clears_orphan_buffer_overrides_on_single_pass_pack()
    {
        // A pack that declares per-buffer wrap/filter overrides WITHOUT
        // declaring multipass/bufferShaders must not carry the orphan arrays
        // out of the registry scan: they claim positional alignment with an
        // empty bufferShaderPaths, survive toJson, and skew operator==.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("orphan"));
        meta.insert(QLatin1String("name"), QStringLiteral("Orphan"));
        meta.insert(QLatin1String("description"), QStringLiteral("A pack with stray buffer overrides."));
        meta.insert(QLatin1String("category"), QStringLiteral("Decoration"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QJsonArray wraps;
        wraps.append(QStringLiteral("clamp"));
        meta.insert(QLatin1String("bufferWraps"), wraps);
        QJsonArray filters;
        filters.append(QStringLiteral("nearest"));
        meta.insert(QLatin1String("bufferFilters"), filters);
        QVERIFY(writePack(tmp.path(), QStringLiteral("orphan"), meta, {QStringLiteral("effect.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const SurfaceShaderEffect e = registry.effect(QStringLiteral("orphan"));
        QVERIFY(e.isValid());
        QVERIFY(!e.isMultipass);
        QVERIFY2(e.bufferWraps.isEmpty(), "single-pass pack must not carry orphan bufferWraps");
        QVERIFY2(e.bufferFilters.isEmpty(), "single-pass pack must not carry orphan bufferFilters");
    }

    void fromJson_leaves_multipass_flag_raw_without_normalizing()
    {
        // fromJson itself does NOT fail closed on multipass-with-no-buffers —
        // that normalization is the registry's parseEffect scan step. Pin the
        // raw fromJson behavior so the two layers don't drift.
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("mp"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        obj.insert(QLatin1String("multipass"), true);
        // No bufferShaders array declared at all.

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(obj);
        QVERIFY(e.isMultipass);
        QVERIFY(e.bufferShaderPaths.isEmpty());
    }

    // ── translateSurfaceParams ───────────────────────────────────────────

    void translate_routes_scalars_and_colors_to_independent_slot_pools()
    {
        SurfaceShaderEffect e = makeInMemoryEffect(QStringLiteral("mixed"));
        e.parameters = {
            makeParam(QStringLiteral("glow"), QStringLiteral("float"), 0.1),
            makeParam(QStringLiteral("count"), QStringLiteral("int"), 3),
            makeParam(QStringLiteral("on"), QStringLiteral("bool"), true),
            makeParam(QStringLiteral("tint"), QStringLiteral("color"), QStringLiteral("#ff8800")),
        };

        // Override only glow; the rest fall back to declared defaults.
        QVariantMap friendly;
        friendly.insert(QStringLiteral("glow"), 0.4);
        const QVariantMap out = SurfaceShaderRegistry::translateSurfaceParams(e, friendly);

        // Scalar pool advances float/int/bool in declaration order; the color
        // param does NOT consume a customParams sub-slot.
        QCOMPARE(out.value(SurfaceShaderContract::slotKey(0)).toDouble(), 0.4); // glow override
        QCOMPARE(out.value(SurfaceShaderContract::slotKey(1)).toInt(), 3); // count default
        QCOMPARE(out.value(SurfaceShaderContract::slotKey(2)).toFloat(), 1.0f); // bool true → 1.0
        // Color pool is separate and 1-based.
        QVERIFY(out.contains(SurfaceShaderContract::colorKey(0)));
        QCOMPARE(out.value(SurfaceShaderContract::colorKey(0)).value<QColor>(), QColor(0xff, 0x88, 0x00));
        // The color did not land in a scalar slot.
        QVERIFY(!out.contains(SurfaceShaderContract::slotKey(3)));
    }

    void translate_coerces_color_from_string_and_qcolor_overrides()
    {
        SurfaceShaderEffect e = makeInMemoryEffect(QStringLiteral("tinted"));
        e.parameters = {makeParam(QStringLiteral("tint"), QStringLiteral("color"), QStringLiteral("#ff8800"))};

        // String override.
        QVariantMap fromString;
        fromString.insert(QStringLiteral("tint"), QStringLiteral("#00ff00"));
        QCOMPARE(SurfaceShaderRegistry::translateSurfaceParams(e, fromString)
                     .value(SurfaceShaderContract::colorKey(0))
                     .value<QColor>(),
                 QColor(0x00, 0xff, 0x00));

        // QColor override.
        QVariantMap fromColor;
        fromColor.insert(QStringLiteral("tint"), QColor(0x11, 0x22, 0x33));
        QCOMPARE(SurfaceShaderRegistry::translateSurfaceParams(e, fromColor)
                     .value(SurfaceShaderContract::colorKey(0))
                     .value<QColor>(),
                 QColor(0x11, 0x22, 0x33));
    }

    void translate_ignores_unknown_override_ids()
    {
        SurfaceShaderEffect e = makeInMemoryEffect(QStringLiteral("solo"));
        e.parameters = {makeParam(QStringLiteral("glow"), QStringLiteral("float"), 0.1)};

        QVariantMap friendly;
        friendly.insert(QStringLiteral("does-not-exist"), 9.0);
        const QVariantMap out = SurfaceShaderRegistry::translateSurfaceParams(e, friendly);

        // Unknown id ignored; the declared param keeps its default.
        QCOMPARE(out.value(SurfaceShaderContract::slotKey(0)).toDouble(), 0.1);
        QCOMPARE(out.size(), 1);
    }

    void translate_returns_empty_for_invalid_effect()
    {
        SurfaceShaderEffect e; // no id / fragment → isValid() false
        QVERIFY(SurfaceShaderRegistry::translateSurfaceParams(e, QVariantMap{}).isEmpty());
    }

    // ── Path traversal guard ─────────────────────────────────────────────

    void traversal_texture_path_is_rejected_valid_slot_survives()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("textured"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QJsonArray textures;
        // Slot 0: relative `..`-traversal escaping the pack dir → rejected.
        {
            QJsonObject t;
            t.insert(QLatin1String("path"), QStringLiteral("../../escape.png"));
            textures.append(t);
        }
        // Slot 1: legitimate in-dir texture → survives, keeps its position.
        {
            QJsonObject t;
            t.insert(QLatin1String("path"), QStringLiteral("noise.png"));
            textures.append(t);
        }
        meta.insert(QLatin1String("textures"), textures);
        QVERIFY(writePack(tmp.path(), QStringLiteral("textured"), meta,
                          {QStringLiteral("effect.frag"), QStringLiteral("noise.png")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        const SurfaceShaderEffect e = registry.effect(QStringLiteral("textured"));
        QVERIFY(e.isValid());

        const QVariantMap out = registry.translateSurfaceParams(QStringLiteral("textured"), QVariantMap{});
        // Rejected slot 0 (uTexture1) is excluded; the surviving slot 1 keeps
        // its position and emits as uTexture2.
        QVERIFY(!out.contains(QStringLiteral("uTexture1")));
        QVERIFY(out.contains(QStringLiteral("uTexture2")));
        const QString survivorPath = out.value(QStringLiteral("uTexture2")).toString();
        QVERIFY(QFileInfo(survivorPath).isAbsolute());
        QVERIFY(survivorPath.endsWith(QStringLiteral("noise.png")));
    }

    void runtime_override_traversal_path_is_rejected_on_disk_anchored_pack()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("anchored"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QVERIFY(writePack(tmp.path(), QStringLiteral("anchored"), meta, {QStringLiteral("effect.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        // A friendlyParams override that tries to escape the sourceDir is
        // rejected against the same guard the scan-time resolver uses.
        QVariantMap friendly;
        friendly.insert(QStringLiteral("uTexture1"), QStringLiteral("../../../etc/passwd"));
        const QVariantMap out = registry.translateSurfaceParams(QStringLiteral("anchored"), friendly);
        QVERIFY(!out.contains(QStringLiteral("uTexture1")));
    }
};

QTEST_MAIN(TestSurfaceShaderRegistry)
#include "test_surfaceshaderregistry.moc"
