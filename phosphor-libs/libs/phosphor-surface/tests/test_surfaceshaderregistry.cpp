// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
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

    void multipass_with_present_buffer_files_preserves_buffer_overrides()
    {
        // Positive counterpart to the fail-closed and orphan-clear tests: a
        // VALID multipass pack (every declared buffer shader on disk) must
        // keep isMultipass AND its per-buffer wrap/filter overrides through
        // the registry scan — pinning that the single-pass coherence clear is
        // gated on !isMultipass, not unconditional.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("bloom"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("multipass"), true);
        QJsonArray buffers;
        buffers.append(QStringLiteral("buffer_a.frag"));
        meta.insert(QLatin1String("bufferShaders"), buffers);
        QJsonArray wraps;
        wraps.append(QStringLiteral("repeat"));
        meta.insert(QLatin1String("bufferWraps"), wraps);
        QJsonArray filters;
        filters.append(QStringLiteral("nearest"));
        meta.insert(QLatin1String("bufferFilters"), filters);
        QVERIFY(writePack(tmp.path(), QStringLiteral("bloom"), meta,
                          {QStringLiteral("effect.frag"), QStringLiteral("buffer_a.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const SurfaceShaderEffect e = registry.effect(QStringLiteral("bloom"));
        QVERIFY(e.isValid());
        QVERIFY(e.isMultipass);
        QCOMPARE(e.bufferShaderPaths.size(), 1);
        QCOMPARE(e.bufferWraps, QStringList{QStringLiteral("repeat")});
        QCOMPARE(e.bufferFilters, QStringList{QStringLiteral("nearest")});
    }

    void animated_flag_parses_and_roundtrips()
    {
        // The "animated" metadata flag drives the daemon hosts' per-frame
        // tick gate (SurfaceShaderItem playing). Pin the parse, the false
        // default, and the toJson round-trip.
        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("pulse"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("animated"), true);

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(meta);
        QVERIFY(e.animated);
        QVERIFY(SurfaceShaderEffect::fromJson(e.toJson()).animated);

        QJsonObject metaStatic;
        metaStatic.insert(QLatin1String("id"), QStringLiteral("still"));
        metaStatic.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        const SurfaceShaderEffect s = SurfaceShaderEffect::fromJson(metaStatic);
        QVERIFY(!s.animated);
        QVERIFY(!s.toJson().contains(QLatin1String("animated")));
    }

    void needsBackdrop_flag_parses_and_roundtrips()
    {
        // "needsBackdrop" marks a pack that samples the scene behind the
        // window (frost / glass). Pin the parse, the false default, and the
        // toJson round-trip; the compositor keys its backdrop capture and
        // composite routing on this flag.
        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("frost"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("needsBackdrop"), true);

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(meta);
        QVERIFY(e.needsBackdrop);
        QVERIFY(SurfaceShaderEffect::fromJson(e.toJson()).needsBackdrop);

        QJsonObject metaPlain;
        metaPlain.insert(QLatin1String("id"), QStringLiteral("border"));
        metaPlain.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        const SurfaceShaderEffect s = SurfaceShaderEffect::fromJson(metaPlain);
        QVERIFY(!s.needsBackdrop);
        QVERIFY(!s.toJson().contains(QLatin1String("needsBackdrop")));
    }

    void interiorOpaque_flag_parses_and_roundtrips()
    {
        // "interiorOpaque" is the never-thins-the-interior promise, which a
        // margin-only halo pack (glow, shadow) and a pack compositing over the
        // capture (fireflies, phosphor-motes) both satisfy: the
        // pack never thins a texel inside the natural frame rect, so a chain
        // of such packs keeps the client's opaque region truthful and the
        // compositor can skip setTranslucent(). Pin the parse, the FALSE
        // default (the safe direction: an undeclared pack is assumed to
        // thin the interior), and the toJson round-trip.
        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("shadow"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("interiorOpaque"), true);

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(meta);
        QVERIFY(e.interiorOpaque);
        QVERIFY(SurfaceShaderEffect::fromJson(e.toJson()).interiorOpaque);

        QJsonObject metaPlain;
        metaPlain.insert(QLatin1String("id"), QStringLiteral("border"));
        metaPlain.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        const SurfaceShaderEffect s = SurfaceShaderEffect::fromJson(metaPlain);
        QVERIFY(!s.interiorOpaque);
        QVERIFY(!s.toJson().contains(QLatin1String("interiorOpaque")));

        // The flag participates in operator== — that equality is what the
        // registry reconcile and the decoration profile diff use to decide a
        // pack changed, so dropping it from the comparison would leave the
        // kwin effect's chainInteriorOpaque sweep never re-evaluated after a
        // metadata-only flip.
        SurfaceShaderEffect flipped = e;
        flipped.interiorOpaque = false;
        QVERIFY(!(flipped == e));
    }

    void providesBorder_flag_parses_and_roundtrips()
    {
        // "providesBorder" marks a decoration pack that renders the window
        // border itself, so the plain border layer is suppressed and the
        // pack's shared border params are seeded from the border setting.
        // Pin the parse, the false default, and the toJson round-trip.
        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("fancy-border"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("providesBorder"), true);

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(meta);
        QVERIFY(e.providesBorder);
        QVERIFY(SurfaceShaderEffect::fromJson(e.toJson()).providesBorder);

        QJsonObject metaPlain;
        metaPlain.insert(QLatin1String("id"), QStringLiteral("plain"));
        metaPlain.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        const SurfaceShaderEffect s = SurfaceShaderEffect::fromJson(metaPlain);
        QVERIFY(!s.providesBorder);
        QVERIFY(!s.toJson().contains(QLatin1String("providesBorder")));
    }

    void providesOpacityTint_flag_parses_and_roundtrips()
    {
        // "providesOpacityTint" marks the reserved opacity-tint layer pack,
        // whose params are seeded from the plain opacity/tint setting and the
        // SetOpacity / SetTint* rule slots. Pin the parse, the false default,
        // and the toJson round-trip.
        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("opacity-tint"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("providesOpacityTint"), true);

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(meta);
        QVERIFY(e.providesOpacityTint);
        QVERIFY(SurfaceShaderEffect::fromJson(e.toJson()).providesOpacityTint);

        QJsonObject metaPlain;
        metaPlain.insert(QLatin1String("id"), QStringLiteral("plain"));
        metaPlain.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        const SurfaceShaderEffect s = SurfaceShaderEffect::fromJson(metaPlain);
        QVERIFY(!s.providesOpacityTint);
        QVERIFY(!s.toJson().contains(QLatin1String("providesOpacityTint")));
    }

    void paddingParam_parses_and_roundtrips()
    {
        // "paddingParam" names the parameter whose resolved value is the
        // outer margin the compositor pads the capture canvas by. Pin the
        // parse, the empty default, and the toJson round-trip.
        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("halo"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("paddingParam"), QStringLiteral("glowSize"));

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(meta);
        QCOMPARE(e.paddingParam, QStringLiteral("glowSize"));
        QCOMPARE(SurfaceShaderEffect::fromJson(e.toJson()).paddingParam, QStringLiteral("glowSize"));

        QJsonObject metaPlain;
        metaPlain.insert(QLatin1String("id"), QStringLiteral("flat"));
        metaPlain.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        const SurfaceShaderEffect plain = SurfaceShaderEffect::fromJson(metaPlain);
        QVERIFY(plain.paddingParam.isEmpty());
        QVERIFY(!plain.toJson().contains(QLatin1String("paddingParam")));
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

    /// `bufferScales` is positionally aligned with bufferShaders like the wrap
    /// and filter lists: every entry is kept in place, each is clamped like the
    /// single-value scale, a non-number falls back to that scale rather than
    /// being dropped (which would shift every later pass), and the list is
    /// capped at the pass budget. It round-trips through toJson.
    void fromJson_keeps_per_pass_buffer_scales_aligned_and_clamped()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("s"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        obj.insert(QLatin1String("bufferScale"), 0.25);
        QJsonArray scales;
        scales.append(0.5);
        scales.append(QStringLiteral("oops"));
        scales.append(5.0);
        scales.append(0.001);
        obj.insert(QLatin1String("bufferScales"), scales);
        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(obj);
        QCOMPARE(e.bufferScales.size(), 4);
        QCOMPARE(e.bufferScales.at(0), 0.5);
        QCOMPARE(e.bufferScales.at(1), 0.25); // the non-number slot follows bufferScale
        QCOMPARE(e.bufferScales.at(2), SurfaceShaderEffect::kMaxBufferScale);
        QCOMPARE(e.bufferScales.at(3), SurfaceShaderEffect::kMinBufferScale);

        const SurfaceShaderEffect again = SurfaceShaderEffect::fromJson(e.toJson());
        QVERIFY(again == e);
        QCOMPARE(again.bufferScales, e.bufferScales);

        QJsonArray surplus;
        for (int i = 0; i < SurfaceShaderEffect::kMaxBufferPasses + 2; ++i) {
            surplus.append(0.5);
        }
        obj.insert(QLatin1String("bufferScales"), surplus);
        QCOMPARE(SurfaceShaderEffect::fromJson(obj).bufferScales.size(), SurfaceShaderEffect::kMaxBufferPasses);
    }

    /// The PATHS cap, which the per-pass scale case above does not cover. The
    /// two arrays are capped by separate code and the scales one was the only
    /// side with a test, so the cap that decides how many passes actually RUN
    /// was unpinned.
    ///
    /// One past the budget rather than an arbitrary surplus: a cap that is off
    /// by one is the plausible regression, and a ten-entry array would pass a
    /// broken cap of nine.
    void fromJson_caps_bufferShaders_at_the_pass_budget()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("s"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        obj.insert(QLatin1String("multipass"), true);
        QJsonArray buffers;
        for (int i = 0; i < SurfaceShaderEffect::kMaxBufferPasses + 1; ++i) {
            buffers.append(QStringLiteral("pass%1.frag").arg(i));
        }
        obj.insert(QLatin1String("bufferShaders"), buffers);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("declares .* buffer passes")));
        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(obj);
        QCOMPARE(e.bufferShaderPaths.size(), SurfaceShaderEffect::kMaxBufferPasses);
        // The surplus is dropped from the END, so the kept entries are the
        // first N in declaration order. That matters: the builtin chain is
        // positional, so dropping from the front would reorder it.
        QCOMPARE(e.bufferShaderPaths.first(), QStringLiteral("pass0.frag"));
        QCOMPARE(e.bufferShaderPaths.last(),
                 QStringLiteral("pass%1.frag").arg(SurfaceShaderEffect::kMaxBufferPasses - 1));

        // Exactly at the budget is accepted whole and warns about nothing.
        QJsonArray exact;
        for (int i = 0; i < SurfaceShaderEffect::kMaxBufferPasses; ++i) {
            exact.append(QStringLiteral("pass%1.frag").arg(i));
        }
        obj.insert(QLatin1String("bufferShaders"), exact);
        QCOMPARE(SurfaceShaderEffect::fromJson(obj).bufferShaderPaths.size(), SurfaceShaderEffect::kMaxBufferPasses);
    }

    // ── parseEffect scan + builtin-buffer helpers ────────────────────────

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
        // bufferScales is the third member of the positionally-aligned set and
        // was the one the coherence block missed.
        QJsonArray scales;
        scales.append(0.5);
        meta.insert(QLatin1String("bufferScales"), scales);
        QVERIFY(writePack(tmp.path(), QStringLiteral("orphan"), meta, {QStringLiteral("effect.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const SurfaceShaderEffect e = registry.effect(QStringLiteral("orphan"));
        QVERIFY(e.isValid());
        QVERIFY(!e.isMultipass);
        QVERIFY2(e.bufferWraps.isEmpty(), "single-pass pack must not carry orphan bufferWraps");
        QVERIFY2(e.bufferFilters.isEmpty(), "single-pass pack must not carry orphan bufferFilters");
        QVERIFY2(e.bufferScales.isEmpty(), "single-pass pack must not carry orphan bufferScales");
    }

    void parseEffect_clears_bufferShaderPaths_when_multipass_flag_absent()
    {
        // A pack that declares `bufferShaders` but omits `multipass: true` never
        // enters the resolve-to-absolute branch, so without the coherence clear
        // it would carry RAW RELATIVE buffer names out of the scan — they
        // survive toJson / operator== and feed the file watcher CWD-relative
        // (bogus) paths. The single-pass coherence block must clear them.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("buffers-no-multipass"));
        meta.insert(QLatin1String("name"), QStringLiteral("Buffers No Multipass"));
        meta.insert(QLatin1String("description"), QStringLiteral("Declares bufferShaders but not multipass."));
        meta.insert(QLatin1String("category"), QStringLiteral("Decoration"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QJsonArray buffers;
        buffers.append(QStringLiteral("buffer0.frag"));
        meta.insert(QLatin1String("bufferShaders"), buffers);
        // Deliberately NO "multipass": true.
        QVERIFY(writePack(tmp.path(), QStringLiteral("buffers-no-multipass"), meta,
                          {QStringLiteral("effect.frag"), QStringLiteral("buffer0.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const SurfaceShaderEffect e = registry.effect(QStringLiteral("buffers-no-multipass"));
        QVERIFY(e.isValid());
        QVERIFY(!e.isMultipass);
        QVERIFY2(e.bufferShaderPaths.isEmpty(),
                 "single-pass pack must not carry orphan (unresolved relative) bufferShaderPaths");
    }

    void parseEffect_resolves_builtin_buffer_tokens_against_shared_dir()
    {
        // `builtin:gaussian-h` / `builtin:gaussian-v` resolve to the standard
        // passes in the search root's shared/ dir (sibling of the pack dir),
        // bypassing the pack-dir confinement guard via a fixed whitelist.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeFile(tmp.path() + QStringLiteral("/shared/gaussian_h.frag"), QByteArrayLiteral("// stub\n")));
        QVERIFY(writeFile(tmp.path() + QStringLiteral("/shared/gaussian_v.frag"), QByteArrayLiteral("// stub\n")));

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("builtin-blur"));
        meta.insert(QLatin1String("name"), QStringLiteral("Builtin Blur"));
        meta.insert(QLatin1String("description"), QStringLiteral("Uses the standard gaussian buffer passes."));
        meta.insert(QLatin1String("category"), QStringLiteral("Decoration"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("multipass"), true);
        QJsonArray buffers;
        buffers.append(QStringLiteral("builtin:gaussian-h"));
        buffers.append(QStringLiteral("builtin:gaussian-v"));
        meta.insert(QLatin1String("bufferShaders"), buffers);
        QVERIFY(writePack(tmp.path(), QStringLiteral("builtin-blur"), meta, {QStringLiteral("effect.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const SurfaceShaderEffect e = registry.effect(QStringLiteral("builtin-blur"));
        QVERIFY(e.isValid());
        QVERIFY(e.isMultipass);
        QCOMPARE(e.bufferShaderPaths.size(), 2);
        QCOMPARE(QFileInfo(e.bufferShaderPaths.at(0)).fileName(), QStringLiteral("gaussian_h.frag"));
        QCOMPARE(QFileInfo(e.bufferShaderPaths.at(1)).fileName(), QStringLiteral("gaussian_v.frag"));
        for (const QString& p : e.bufferShaderPaths) {
            QVERIFY2(QFileInfo(p).isAbsolute(), "builtin buffer paths must resolve to absolute files");
            QVERIFY(QFile::exists(p));
            // The assertion this test is NAMED for. Without it the case cannot
            // fail for its stated reason: if the sibling probe regressed, the
            // QStandardPaths fallback would find the INSTALLED copy under
            // /usr/share on any machine with the package on it and every
            // assertion above would still pass. That is the dev-passes /
            // CI-fails asymmetry, in the direction that hides a regression.
            QVERIFY2(QFileInfo(p).canonicalFilePath().startsWith(QFileInfo(tmp.path()).canonicalFilePath()),
                     qPrintable(QStringLiteral("resolved outside the temporary pack tree (%1), so the sibling probe "
                                               "did not serve it: %2")
                                    .arg(tmp.path(), p)));
        }
    }

    void parseEffect_fails_closed_on_unknown_builtin_buffer_token()
    {
        // An unknown `builtin:` token funnels into the same fail-closed
        // single-pass fallback as a missing pack-local buffer file.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("bad-builtin"));
        meta.insert(QLatin1String("name"), QStringLiteral("Bad Builtin"));
        meta.insert(QLatin1String("description"), QStringLiteral("Declares an unknown builtin buffer token."));
        meta.insert(QLatin1String("category"), QStringLiteral("Decoration"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("multipass"), true);
        QJsonArray buffers;
        buffers.append(QStringLiteral("builtin:no-such-pass"));
        meta.insert(QLatin1String("bufferShaders"), buffers);
        QVERIFY(writePack(tmp.path(), QStringLiteral("bad-builtin"), meta, {QStringLiteral("effect.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const SurfaceShaderEffect e = registry.effect(QStringLiteral("bad-builtin"));
        QVERIFY(e.isValid());
        QVERIFY2(!e.isMultipass, "unknown builtin token must fail-close to single-pass");
        QVERIFY(e.bufferShaderPaths.isEmpty());
    }

    void resolveBuiltinBufferShader_falls_back_to_standard_paths_for_user_packs()
    {
        // A user pack (~/.local/share/plasmazones/surface/<pack>) has no
        // sibling shared/ dir, so resolution falls back to
        // QStandardPaths::locate. Exercise that branch under
        // QStandardPaths test mode with the shared pass installed into the
        // test-mode data location. Assertions run only after test mode is
        // switched off again so a failure cannot leak global state into
        // sibling tests.
        QTemporaryDir packRoot; // deliberately WITHOUT a shared/ sibling
        const bool packRootValid = packRoot.isValid();

        QStandardPaths::setTestModeEnabled(true);
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const QString installed = dataDir + QStringLiteral("/plasmazones/surface/shared/gaussian_h.frag");
        const bool wrote = writeFile(installed, QByteArrayLiteral("// stub\n"));
        const QString resolved = SurfaceShaderRegistry::resolveBuiltinBufferShader(
            QStringLiteral("builtin:gaussian-h"), packRoot.path() + QStringLiteral("/user-pack"));
        QFile::remove(installed);
        QStandardPaths::setTestModeEnabled(false);

        QVERIFY(packRootValid);
        QVERIFY(wrote);
        QVERIFY2(!resolved.isEmpty(), "user pack without a sibling shared/ dir must resolve via QStandardPaths");
        QCOMPARE(QFileInfo(resolved).fileName(), QStringLiteral("gaussian_h.frag"));
        QVERIFY(QFileInfo(resolved).isAbsolute());
    }

    void builtinBuffer_helpers_reject_non_builtin_tokens()
    {
        // Pin the header contract directly: a plain pack-local buffer name is
        // NOT a builtin token, and the resolver returns empty for it (and for an
        // unknown builtin token) rather than fabricating a path. This keeps
        // parseEffect's "resolve, else treat as pack-local" branch correct.
        QVERIFY(!SurfaceShaderRegistry::isBuiltinBufferShader(QStringLiteral("buffer0.frag")));
        QVERIFY(!SurfaceShaderRegistry::isBuiltinBufferShader(QString()));
        QVERIFY(SurfaceShaderRegistry::isBuiltinBufferShader(QStringLiteral("builtin:gaussian-h")));

        const QString packDir = QStringLiteral("/tmp/some-pack");
        QVERIFY(SurfaceShaderRegistry::resolveBuiltinBufferShader(QStringLiteral("buffer0.frag"), packDir).isEmpty());
        QVERIFY(SurfaceShaderRegistry::resolveBuiltinBufferShader(QStringLiteral("builtin:does-not-exist"), packDir)
                    .isEmpty());
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

    /// A parameter id declared twice keeps the FIRST entry and drops the rest.
    ///
    /// Not a tidiness rule. buildParamPreamble emits one `#define p_<id> …` per
    /// parameter, so two entries sharing an id redefine the same macro with a
    /// different replacement list, which is a GLSL compile error that takes the
    /// whole pack down to a black surface. Losing the second editor row is the
    /// far smaller loss, and it is the first entry that must survive because
    /// lanes are assigned in declaration order.
    void duplicate_parameter_ids_are_dropped()
    {
        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("dupes"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));

        const auto floatParam = [](const QString& id, double def) {
            QJsonObject p;
            p.insert(QLatin1String("id"), id);
            p.insert(QLatin1String("name"), id);
            p.insert(QLatin1String("type"), QStringLiteral("float"));
            p.insert(QLatin1String("default"), def);
            return p;
        };

        QJsonArray params;
        params.append(floatParam(QStringLiteral("radius"), 1.0));
        params.append(floatParam(QStringLiteral("radius"), 2.0)); // duplicate
        params.append(floatParam(QStringLiteral("spread"), 3.0));
        meta.insert(QLatin1String("parameters"), params);

        const SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(meta);
        QCOMPARE(e.parameters.size(), 2);
        // The FIRST radius survives, with its own default.
        QCOMPARE(e.parameters.at(0).id, QStringLiteral("radius"));
        QCOMPARE(e.parameters.at(0).defaultValue.toDouble(), 1.0);
        // And the parameter after the duplicate keeps its position, so the
        // lane it is assigned does not shift.
        QCOMPARE(e.parameters.at(1).id, QStringLiteral("spread"));
        QCOMPARE(e.parameters.at(1).defaultValue.toDouble(), 3.0);
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

    void parseEffect_resolves_every_builtin_kawase_token()
    {
        // The gaussian pair had a slot and the SEVEN Kawase tokens had none,
        // although they are the chain every blur-family pack now ships. The
        // whitelist is a fixed table, so a token missing from it resolves to
        // nothing and fails the pack closed to single-pass with no compile
        // error anywhere. Naming all seven is what makes a one-entry typo or
        // omission fail here rather than in a user's session.
        //
        // The ORDER is asserted too, because these passes are positional: each
        // reads the level above it by channel index, so a chain that resolves
        // the right seven files in the wrong order blurs wrongly while looking
        // entirely well-formed.
        const QStringList kTokens = {QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
                                     QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                                     QStringLiteral("builtin:kawase-up-0"),   QStringLiteral("builtin:kawase-up-1"),
                                     QStringLiteral("builtin:kawase-up-2")};
        const QStringList kFiles = {QStringLiteral("kawase_down_0.frag"), QStringLiteral("kawase_down_1.frag"),
                                    QStringLiteral("kawase_down_2.frag"), QStringLiteral("kawase_down_3.frag"),
                                    QStringLiteral("kawase_up_0.frag"),   QStringLiteral("kawase_up_1.frag"),
                                    QStringLiteral("kawase_up_2.frag")};

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        for (const QString& f : kFiles) {
            QVERIFY(writeFile(tmp.path() + QStringLiteral("/shared/") + f, QByteArrayLiteral("// stub\n")));
        }

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("kawase-chain"));
        meta.insert(QLatin1String("name"), QStringLiteral("Kawase Chain"));
        meta.insert(QLatin1String("description"), QStringLiteral("Declares the full dual Kawase pyramid."));
        meta.insert(QLatin1String("category"), QStringLiteral("Decoration"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("multipass"), true);
        QJsonArray buffers;
        for (const QString& t : kTokens) {
            buffers.append(t);
        }
        meta.insert(QLatin1String("bufferShaders"), buffers);
        QVERIFY(writePack(tmp.path(), QStringLiteral("kawase-chain"), meta, {QStringLiteral("effect.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const SurfaceShaderEffect e = registry.effect(QStringLiteral("kawase-chain"));
        QVERIFY(e.isValid());
        QVERIFY(e.isMultipass);
        QCOMPARE(e.bufferShaderPaths.size(), kFiles.size());
        for (qsizetype i = 0; i < kFiles.size(); ++i) {
            QCOMPARE(QFileInfo(e.bufferShaderPaths.at(i)).fileName(), kFiles.at(i));
            // Served by the SIBLING probe, not by an installed copy under
            // /usr/share. Same reason the gaussian slot above asserts it: without
            // this the case passes on any machine with the package installed
            // even if the probe regressed.
            QVERIFY2(QFileInfo(e.bufferShaderPaths.at(i))
                         .canonicalFilePath()
                         .startsWith(QFileInfo(tmp.path()).canonicalFilePath()),
                     qPrintable(e.bufferShaderPaths.at(i)));
        }
    }

    void parseEffect_carries_per_pass_bufferScales_through_a_multipass_scan()
    {
        // bufferScales is the per-pass resolution list the pyramid needs, and
        // nothing asserted it survives a scan at all. It has to arrive in ORDER
        // and it has to arrive COMPLETE: the entries are positional, so a list
        // that lost one silently re-points every later pass at a neighbour's
        // resolution, which renders as a blur of the wrong width rather than as
        // any kind of error.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeFile(tmp.path() + QStringLiteral("/shared/kawase_down_0.frag"), QByteArrayLiteral("// stub\n")));
        QVERIFY(writeFile(tmp.path() + QStringLiteral("/shared/kawase_up_2.frag"), QByteArrayLiteral("// stub\n")));

        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("scaled-chain"));
        meta.insert(QLatin1String("name"), QStringLiteral("Scaled Chain"));
        meta.insert(QLatin1String("description"), QStringLiteral("Declares per-pass buffer scales."));
        meta.insert(QLatin1String("category"), QStringLiteral("Decoration"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("multipass"), true);
        meta.insert(QLatin1String("bufferShaders"),
                    QJsonArray{QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-up-2")});
        meta.insert(QLatin1String("bufferScales"), QJsonArray{0.25, 0.0625});
        QVERIFY(writePack(tmp.path(), QStringLiteral("scaled-chain"), meta, {QStringLiteral("effect.frag")}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const SurfaceShaderEffect e = registry.effect(QStringLiteral("scaled-chain"));
        QVERIFY(e.isValid());
        QVERIFY(e.isMultipass);
        QCOMPARE(e.bufferScales.size(), 2);
        QCOMPARE(e.bufferScales.at(0), 0.25);
        QCOMPARE(e.bufferScales.at(1), 0.0625);
    }
};

QTEST_MAIN(TestSurfaceShaderRegistry)
#include "test_surfaceshaderregistry.moc"
