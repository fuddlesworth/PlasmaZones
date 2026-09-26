// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceChainCompose.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

#include <limits>

using namespace PhosphorSurfaceShaders;

namespace {

/// Minimal valid single-pass pack. isValid() requires a non-empty
/// fragmentShaderPath, which composeStageMap's param translation gates on.
SurfaceShaderEffect basePack()
{
    SurfaceShaderEffect e;
    e.id = QStringLiteral("border");
    e.name = QStringLiteral("Border");
    e.fragmentShaderPath = QStringLiteral("/packs/border/effect.frag");
    return e;
}

SurfaceShaderEffect::ParameterInfo floatParam(const QString& id, double defaultValue)
{
    SurfaceShaderEffect::ParameterInfo p;
    p.id = id;
    p.name = id;
    p.type = QStringLiteral("float");
    p.defaultValue = defaultValue;
    return p;
}

/// Write @p contents to @p path, creating parent directories.
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

/// Author `<root>/<id>/metadata.json` plus the stub effect.frag the registry's
/// on-disk check needs.
///
/// chainRoundBottomCorners resolves against DECLARED parameters, so these go
/// through a real registry rather than hand-built effects: the declared-default
/// arm is only reachable when a pack actually declares the control, and that is
/// the loader's job.
///
/// @p roundBottomDefault selects one of THREE shapes, and the branch order
/// matters because a default-constructed QVariant is both invalid AND null:
///   - invalid  -> declares `opacity` but NOT roundBottomCorners. That is the
///     shape of a pack that draws no outline; every such bundled pack
///     (fireflies, focus-fade, opacity-tint, phosphor-motes) declares other
///     parameters, so an EMPTY list would not exercise the find_if scan at all.
///   - null     -> declares roundBottomCorners with an explicit JSON null
///     default, which is how the "declares the control but states no default"
///     case is authored. NOTE this is deliberately SCHEMA-INVALID metadata: the
///     surface schema requires `default` and types it number/string/boolean. The
///     registry does not validate at load ("the catalog, not the gate"), which is
///     exactly why the resolver has to guard the value itself, so the guard is not
///     dead code even though no validator-clean pack can reach the state.
///   - a bool   -> declares it with that default.
bool writeSilhouettePack(const QString& root, const QString& id, const QVariant& roundBottomDefault)
{
    QJsonObject meta;
    meta.insert(QLatin1String("id"), id);
    meta.insert(QLatin1String("name"), id);
    meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
    QJsonArray params;
    // Always declared, so a pack that does not carry roundBottomCorners still has
    // a NON-EMPTY parameter list and the resolver's find_if really scans.
    QJsonObject filler;
    filler.insert(QLatin1String("id"), QStringLiteral("opacity"));
    filler.insert(QLatin1String("name"), QStringLiteral("Opacity"));
    filler.insert(QLatin1String("type"), QStringLiteral("float"));
    filler.insert(QLatin1String("default"), 1.0);
    params.append(filler);
    if (roundBottomDefault.isValid()) {
        QJsonObject param;
        param.insert(QLatin1String("id"), QStringLiteral("roundBottomCorners"));
        param.insert(QLatin1String("name"), QStringLiteral("Round bottom corners"));
        param.insert(QLatin1String("type"), QStringLiteral("bool"));
        // A null QVariant authors `"default": null`; a bool authors the bool.
        if (roundBottomDefault.isNull()) {
            param.insert(QLatin1String("default"), QJsonValue());
        } else {
            param.insert(QLatin1String("default"), roundBottomDefault.toBool());
        }
        params.append(param);
    }
    meta.insert(QLatin1String("parameters"), params);
    const QString packDir = root + QLatin1Char('/') + id;
    if (!writeFile(packDir + QStringLiteral("/metadata.json"), QJsonDocument(meta).toJson()))
        return false;
    return writeFile(packDir + QStringLiteral("/effect.frag"), QByteArrayLiteral("// stub\n"));
}

/// One pack's entry in the post-flatten effectiveParameters() map.
QVariantMap storedValue(bool roundBottomCorners)
{
    return QVariantMap{{QStringLiteral("roundBottomCorners"), roundBottomCorners}};
}

} // namespace

/// Covers the four exported helpers the daemon overlay host, the kwin-effect
/// compositor path, the settings decoration preview and the shell chrome share.
/// The padding resolution had been copy-pasted into two of those and had already
/// drifted in type; these pin the behaviour all four now depend on. The chain's
/// bottom-corner resolution is pinned here for the same reason: three of those
/// four hosts inject its answer, so a disagreement between them is a visual
/// defect rather than a preference.
class TestSurfaceChainCompose : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ── paddingRequest ───────────────────────────────────────────────

    /// Pins the OUTCOME: no paddingParam means no canvas request. The `isEmpty()` fast
    /// path at the top of paddingRequest is not isolated by this assertion — with it
    /// deleted, basePack() falls into the find_if, which fails, and the function
    /// answers 0.0 anyway.
    void paddingRequest_is_zero_without_a_paddingParam()
    {
        const SurfaceShaderEffect e = basePack();
        QCOMPARE(paddingRequest(e, {}), 0.0);
    }

    void paddingRequest_falls_back_to_the_declared_default()
    {
        SurfaceShaderEffect e = basePack();
        e.paddingParam = QStringLiteral("glowSize");
        e.parameters.append(floatParam(QStringLiteral("glowSize"), 12.0));
        QCOMPARE(paddingRequest(e, {}), 12.0);
    }

    void paddingRequest_prefers_the_per_surface_override()
    {
        SurfaceShaderEffect e = basePack();
        e.paddingParam = QStringLiteral("glowSize");
        e.parameters.append(floatParam(QStringLiteral("glowSize"), 12.0));
        const QVariantMap overrides{{QStringLiteral("glowSize"), 30.0}};
        QCOMPARE(paddingRequest(e, overrides), 30.0);
    }

    /// A paddingParam naming a parameter the pack never declares is pack-author
    /// error. It must degrade to the margin-less 1:1 geometry, not to an
    /// unbounded or garbage canvas request.
    void paddingRequest_is_zero_when_paddingParam_names_an_undeclared_param()
    {
        SurfaceShaderEffect e = basePack();
        e.paddingParam = QStringLiteral("nosuchparam");
        e.parameters.append(floatParam(QStringLiteral("glowSize"), 12.0));
        QCOMPARE(paddingRequest(e, {}), 0.0);
    }

    /// The override is consulted by NAME, so an override for some other
    /// parameter must not be mistaken for the padding request.
    void paddingRequest_ignores_an_override_for_a_different_param()
    {
        SurfaceShaderEffect e = basePack();
        e.paddingParam = QStringLiteral("glowSize");
        e.parameters.append(floatParam(QStringLiteral("glowSize"), 12.0));
        const QVariantMap overrides{{QStringLiteral("borderWidth"), 99.0}};
        QCOMPARE(paddingRequest(e, overrides), 12.0);
    }

    /// The case the two above do not cover between them: paddingParam names an
    /// UNDECLARED parameter AND an override carries that name. The declaration
    /// check has to run first, or a stored override for a parameter the pack
    /// does not have is honoured as a canvas request.
    ///
    /// Reachable rather than theoretical. resolveParams copies per-surface
    /// deltas verbatim and clampToBounds skips ids with no declared bound, so
    /// an undeclared id survives the flatten and arrives here.
    void paddingRequest_ignores_an_override_under_an_undeclared_paddingParam()
    {
        SurfaceShaderEffect e = basePack();
        e.paddingParam = QStringLiteral("nosuchparam");
        e.parameters.append(floatParam(QStringLiteral("glowSize"), 12.0));
        const QVariantMap overrides{{QStringLiteral("nosuchparam"), 240.0}};
        QCOMPARE(paddingRequest(e, overrides), 0.0);
    }

    /// A padding override of the wrong TYPE must not suppress the declared
    /// default. QVariant::toDouble() answers 0.0 for anything it cannot
    /// convert, so gating on presence alone silently collapsed a pack's
    /// margin to zero whenever a stored profile held a non-numeric value.
    void paddingRequest_ignores_a_non_numeric_override()
    {
        SurfaceShaderEffect e = basePack();
        e.paddingParam = QStringLiteral("glowSize");
        e.parameters.append(floatParam(QStringLiteral("glowSize"), 16.0));

        QCOMPARE(paddingRequest(e, {{QStringLiteral("glowSize"), QStringLiteral("not a number")}}), 16.0);
        QCOMPARE(paddingRequest(e, {{QStringLiteral("glowSize"), QVariant()}}), 16.0);
        // A numeric string is a legitimate override and still wins.
        QCOMPARE(paddingRequest(e, {{QStringLiteral("glowSize"), QStringLiteral("24")}}), 24.0);
    }

    /// A non-finite padding must not escape into a caller's clamp.
    ///
    /// NaN and infinity convert cleanly, so the type check above lets them
    /// through; only the explicit isfinite() test stops them. This matters
    /// past the aesthetics: the compositor bounds the result and then narrows
    /// it to an int for the capture canvas, and narrowing a NaN or an
    /// infinity to an integer is undefined behaviour. A qBound() around the
    /// value does not save it either, since every comparison against NaN is
    /// false.
    void paddingRequest_rejects_a_non_finite_override()
    {
        SurfaceShaderEffect e = basePack();
        e.paddingParam = QStringLiteral("glowSize");
        e.parameters.append(floatParam(QStringLiteral("glowSize"), 16.0));

        QCOMPARE(paddingRequest(e, {{QStringLiteral("glowSize"), qQNaN()}}), 16.0);
        QCOMPARE(paddingRequest(e, {{QStringLiteral("glowSize"), qInf()}}), 16.0);
        QCOMPARE(paddingRequest(e, {{QStringLiteral("glowSize"), -qInf()}}), 16.0);
    }

    /// The same guard on the DECLARED side, which has its own conversion.
    ///
    /// A pack whose own default is non-finite has nothing to fall back to, so
    /// the request has to come out as no padding at all rather than as a
    /// value no caller can clamp.
    void paddingRequest_rejects_a_non_finite_declared_default()
    {
        SurfaceShaderEffect e = basePack();
        e.paddingParam = QStringLiteral("glowSize");
        e.parameters.append(floatParam(QStringLiteral("glowSize"), qQNaN()));

        QCOMPARE(paddingRequest(e, {}), 0.0);
    }
    // ── composeStageMap ──────────────────────────────────────────────

    void composeStageMap_emits_the_host_contract_keys()
    {
        const SurfaceShaderEffect e = basePack();
        const QVariantMap stage = composeStageMap(e, {});
        // The keys composeStageMap emits for EVERY stage, multipass or not.
        // Not the whole set SurfaceDecoration.qml reads: it reads sixteen off
        // stage.stageData, and the other ten are the buffer keys emitted only
        // under multipass, which the two cases below pin.
        QVERIFY(stage.contains(QStringLiteral("source")));
        QVERIFY(stage.contains(QStringLiteral("vertexSource")));
        QVERIFY(stage.contains(QStringLiteral("preamble")));
        QVERIFY(stage.contains(QStringLiteral("params")));
        QVERIFY(stage.contains(QStringLiteral("animated")));
        QVERIFY(stage.contains(QStringLiteral("multipass")));
        QCOMPARE(stage.value(QStringLiteral("source")).toUrl(),
                 QUrl::fromLocalFile(QStringLiteral("/packs/border/effect.frag")));
    }

    /// An undeclared vertex stage must arrive as an EMPTY url, which is what the host
    /// falls through to its shared surface vert on.
    ///
    /// This pins the HOST CONTRACT, not a conversion: QUrl::fromLocalFile() already
    /// answers an empty, invalid url for an empty path, so the ternary in
    /// composeStageMap is documentation-by-code rather than a guard, and deleting it
    /// would leave this assertion green. Do not read the assertion as covering it.
    void composeStageMap_leaves_an_undeclared_vertex_stage_empty()
    {
        const SurfaceShaderEffect e = basePack();
        const QVariantMap stage = composeStageMap(e, {});
        QVERIFY(stage.value(QStringLiteral("vertexSource")).toUrl().isEmpty());
    }

    void composeStageMap_forwards_a_declared_vertex_stage()
    {
        SurfaceShaderEffect e = basePack();
        e.vertexShaderPath = QStringLiteral("/packs/border/effect.vert");
        const QVariantMap stage = composeStageMap(e, {});
        QCOMPARE(stage.value(QStringLiteral("vertexSource")).toUrl(),
                 QUrl::fromLocalFile(QStringLiteral("/packs/border/effect.vert")));
    }

    /// A single-pass pack carries multipass:false and NONE of the buffer keys,
    /// so the shader item keeps its own single-pass defaults.
    void composeStageMap_omits_every_buffer_key_for_a_single_pass_pack()
    {
        const SurfaceShaderEffect e = basePack();
        const QVariantMap stage = composeStageMap(e, {});
        QCOMPARE(stage.value(QStringLiteral("multipass")).toBool(), false);
        // All TEN keys composeStageMap inserts under stageMultipass, not the
        // four this used to check. A key left unpinned here is a key that can
        // start leaking into a single-pass stage without failing anything.
        for (const char* key : {"bufferShaderPaths", "bufferFeedback", "bufferScale", "bufferScales", "bufferWrap",
                                "bufferWraps", "bufferFilter", "bufferFilters", "useDepthBuffer", "halfFloatBuffers"}) {
            QVERIFY2(!stage.contains(QLatin1String(key)), key);
        }
    }

    void composeStageMap_forwards_the_whole_buffer_set_for_a_multipass_pack()
    {
        SurfaceShaderEffect e = basePack();
        e.isMultipass = true;
        e.bufferShaderPaths =
            QStringList{QStringLiteral("/packs/blur/gaussian_h.frag"), QStringLiteral("/packs/blur/gaussian_v.frag")};
        e.bufferFeedback = true;
        e.bufferScale = 0.25;
        e.bufferScales = QList<qreal>{0.5, 0.125};
        e.bufferWrap = QStringLiteral("clamp");
        e.bufferWraps = QStringList{QStringLiteral("clamp"), QString()};
        e.bufferFilter = QStringLiteral("linear");
        e.bufferFilters = QStringList{QString(), QStringLiteral("nearest")};
        e.useDepthBuffer = true;
        e.halfFloatBuffers = false;

        const QVariantMap stage = composeStageMap(e, {});
        QCOMPARE(stage.value(QStringLiteral("multipass")).toBool(), true);
        QCOMPARE(stage.value(QStringLiteral("bufferShaderPaths")).toStringList(), e.bufferShaderPaths);
        QCOMPARE(stage.value(QStringLiteral("bufferFeedback")).toBool(), true);
        QCOMPARE(stage.value(QStringLiteral("bufferScale")).toDouble(), 0.25);
        // Per-pass scales ride along as a QVariantList the QML side can
        // Array.from(); the shader item diverges its slots from them.
        const QVariantList scales = stage.value(QStringLiteral("bufferScales")).toList();
        QCOMPARE(scales.size(), 2);
        QCOMPARE(scales.at(0).toDouble(), 0.5);
        QCOMPARE(scales.at(1).toDouble(), 0.125);
        QCOMPARE(stage.value(QStringLiteral("bufferWrap")).toString(), QStringLiteral("clamp"));
        QCOMPARE(stage.value(QStringLiteral("bufferWraps")).toStringList(), e.bufferWraps);
        QCOMPARE(stage.value(QStringLiteral("bufferFilter")).toString(), QStringLiteral("linear"));
        QCOMPARE(stage.value(QStringLiteral("bufferFilters")).toStringList(), e.bufferFilters);
        QCOMPARE(stage.value(QStringLiteral("useDepthBuffer")).toBool(), true);
        // Opted OUT here, so the assertion fails both if the key is dropped
        // (absent reads as false only by accident) and if it is hardcoded to
        // the true default.
        QCOMPARE(stage.value(QStringLiteral("halfFloatBuffers")).toBool(), false);
        QVERIFY(stage.contains(QStringLiteral("halfFloatBuffers")));
    }

    /// The blur-quality tier multiplies every declared buffer scale.
    ///
    /// Pinned because this composer is the DAEMON-side counterpart of the
    /// compositor's clampedBufferScale, and for a while only the compositor
    /// applied the setting at all, so the same pack rendered at two densities
    /// depending on whether it decorated a window or an OSD.
    void composeStageMap_folds_the_blur_scale_multiplier_into_every_scale()
    {
        SurfaceShaderEffect e = basePack();
        e.isMultipass = true;
        e.bufferShaderPaths = QStringList{QStringLiteral("/packs/blur/a.frag"), QStringLiteral("/packs/blur/b.frag")};
        e.bufferScale = 0.5;
        e.bufferScales = QList<qreal>{0.5, 0.25};

        const QVariantMap halved = composeStageMap(e, {}, 0.5);
        QCOMPARE(halved.value(QStringLiteral("bufferScale")).toDouble(), 0.25);
        const QVariantList scales = halved.value(QStringLiteral("bufferScales")).toList();
        QCOMPARE(scales.size(), 2);
        QCOMPARE(scales.at(0).toDouble(), 0.25);
        QCOMPARE(scales.at(1).toDouble(), 0.125);

        // Defaulted, so every existing caller keeps the declared density.
        QCOMPARE(composeStageMap(e, {}).value(QStringLiteral("bufferScale")).toDouble(), 0.5);
    }

    /// The product is bounded into the allocator band, and an unusable multiplier
    /// is the identity rather than a floor.
    ///
    /// The second half matters more than it looks: the value reaches the shell over
    /// D-Bus, where an older daemon answers an unknown key with a valid EMPTY reply
    /// and QVariant("").toReal() is 0.0. Treating that as a real multiplier would
    /// collapse every pass to kMinBufferScale.
    void composeStageMap_bounds_the_product_and_ignores_an_unusable_multiplier()
    {
        SurfaceShaderEffect e = basePack();
        e.isMultipass = true;
        e.bufferShaderPaths = QStringList{QStringLiteral("/packs/blur/a.frag")};
        e.bufferScale = 1.0;
        e.bufferScales = QList<qreal>{1.0};

        // A HALF-density pack throughout, deliberately. With a pack at 1.0 the
        // identity answer and the clamped answer are both kMaxBufferScale, so an
        // assertion holds whether or not the multiplier is applied at all. That
        // vacuity was real twice over: it left the `huge` case passing with the
        // multiplier dropped entirely, and the infinity row passing with
        // std::isfinite deleted.
        // The bounds pinned NUMERICALLY once, because every other assertion here
        // compares against the same two symbols the code under test uses, so both
        // sides move together and nothing would catch the band itself changing.
        // CHANGELOG entries quote the floor as "a hundred and twenty-eighth".
        QCOMPARE(SurfaceShaderEffect::kMinBufferScale, 1.0 / 128.0);
        QCOMPARE(SurfaceShaderEffect::kMaxBufferScale, 1.0);

        SurfaceShaderEffect half = e;
        half.bufferScale = 0.5;
        half.bufferScales = QList<qreal>{0.5};

        const QVariantMap huge = composeStageMap(half, {}, 1000.0);
        QCOMPARE(huge.value(QStringLiteral("bufferScale")).toDouble(), SurfaceShaderEffect::kMaxBufferScale);
        QCOMPARE(huge.value(QStringLiteral("bufferScales")).toList().at(0).toDouble(),
                 SurfaceShaderEffect::kMaxBufferScale);

        const QVariantMap tiny = composeStageMap(half, {}, 1.0e-9);
        QCOMPARE(tiny.value(QStringLiteral("bufferScale")).toDouble(), SurfaceShaderEffect::kMinBufferScale);
        QCOMPARE(tiny.value(QStringLiteral("bufferScales")).toList().at(0).toDouble(),
                 SurfaceShaderEffect::kMinBufferScale);
        const qreal unusable[] = {0.0, -1.0, std::numeric_limits<qreal>::quiet_NaN(),
                                  std::numeric_limits<qreal>::infinity()};
        for (const qreal m : unusable) {
            const QVariantMap stage = composeStageMap(half, {}, m);
            QCOMPARE(stage.value(QStringLiteral("bufferScale")).toDouble(), 0.5);
            QCOMPARE(stage.value(QStringLiteral("bufferScales")).toList().at(0).toDouble(), 0.5);
        }
    }

    /// The buffer format is a per-pack contract, and its default is the
    /// EXPENSIVE one.
    ///
    /// A pack that says nothing keeps RGBA16F, because a buffer holding HDR
    /// radiance or a feedback accumulator degrades visibly at 8 bits. Pinned
    /// separately from the opted-out case above so a default flipped the other
    /// way is caught rather than quietly halving every pack's precision.
    void composeStageMap_defaults_a_multipass_pack_to_half_float_buffers()
    {
        SurfaceShaderEffect e = basePack();
        e.isMultipass = true;
        e.bufferShaderPaths = QStringList{QStringLiteral("/packs/blur/gaussian_h.frag")};

        const QVariantMap stage = composeStageMap(e, {});
        QCOMPARE(stage.value(QStringLiteral("halfFloatBuffers")).toBool(), true);
    }

    /// The registry clears bufferShaderPaths fail-closed when a declared
    /// builtin: token cannot be located, leaving isMultipass set but the list
    /// empty. That pack must stay single-pass rather than reach the item with
    /// an empty pass list — this is the emptiness half of the gate.
    void composeStageMap_keeps_a_multipass_pack_with_no_resolved_buffers_single_pass()
    {
        SurfaceShaderEffect e = basePack();
        e.isMultipass = true;
        e.bufferShaderPaths.clear();

        const QVariantMap stage = composeStageMap(e, {});
        QCOMPARE(stage.value(QStringLiteral("multipass")).toBool(), false);
        QVERIFY(!stage.contains(QStringLiteral("bufferShaderPaths")));
    }

    /// Buffer paths present but the pack never opted in: also single-pass.
    void composeStageMap_keeps_buffers_off_when_the_pack_did_not_opt_in()
    {
        SurfaceShaderEffect e = basePack();
        e.isMultipass = false;
        e.bufferShaderPaths = QStringList{QStringLiteral("/packs/blur/gaussian_h.frag")};

        const QVariantMap stage = composeStageMap(e, {});
        QCOMPARE(stage.value(QStringLiteral("multipass")).toBool(), false);
        QVERIFY(!stage.contains(QStringLiteral("bufferShaderPaths")));
    }

    void composeStageMap_forwards_the_animated_flag()
    {
        SurfaceShaderEffect e = basePack();
        QCOMPARE(composeStageMap(e, {}).value(QStringLiteral("animated")).toBool(), false);
        e.animated = true;
        QCOMPARE(composeStageMap(e, {}).value(QStringLiteral("animated")).toBool(), true);
    }

    /// The stage's params are the TRANSLATED slot map, not the friendly one:
    /// the host uploads to customParams / customColor lanes, so a friendly key
    /// reaching the item unchanged would silently never bind.
    void composeStageMap_translates_params_into_slot_keys()
    {
        SurfaceShaderEffect e = basePack();
        e.parameters.append(floatParam(QStringLiteral("borderWidth"), 2.0));
        const QVariantMap stage = composeStageMap(e, {{QStringLiteral("borderWidth"), 5.0}});
        const QVariantMap params = stage.value(QStringLiteral("params")).toMap();
        QVERIFY2(!params.contains(QStringLiteral("borderWidth")),
                 "friendly param id must not survive into the stage's uploaded params");
        QCOMPARE(params.value(QStringLiteral("customParams1_x")).toDouble(), 5.0);
    }

    /// An unusable pack composes to nothing at all.
    ///
    /// A pack whose fragment shader was cleared — which is what the
    /// path-traversal guard does when it rejects a declared path — is not
    /// merely a stage that draws nothing: without the validity gate it is a
    /// stage carrying a preamble and an `animated` flag over an empty source
    /// url, which a host would still append to its chain. Emptiness is the
    /// signal callers skip on.
    void composeStageMap_is_empty_for_an_unusable_pack()
    {
        SurfaceShaderEffect e = basePack();
        e.fragmentShaderPath.clear();
        QVERIFY(!e.isValid());
        QVERIFY2(composeStageMap(e, {}).isEmpty(), "a pack with no fragment shader must compose to no stage");

        SurfaceShaderEffect noId = basePack();
        noId.id.clear();
        QVERIFY(!noId.isValid());
        QVERIFY(composeStageMap(noId, {}).isEmpty());
    }

    // ── chainRoundBottomCorners ──────────────────────────────────────
    //
    // The pane's silhouette is one shape for the whole chain. These pin the
    // resolution order the three injecting hosts read, because a disagreement
    // between a backdrop pack and the border tracing it is always a visual
    // defect and never a preference. (The fourth host, the settings decoration
    // preview, composes one pack at a time and so injects nothing.)

    /// A chain of packs that draw no outline has nothing to agree about, and an
    /// invalid answer is what tells the host to inject nothing. Injecting a
    /// fabricated `true` instead would be indistinguishable downstream from a
    /// pack that really declared it.
    void chainRoundBottomCorners_is_invalid_when_no_pack_declares_it()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("fireflies"), {}));
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("opacity-tint"), {}));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        // Assert the packs really LOADED. Without this the slot passes just as well
        // when the fixture writes metadata the loader rejects and the registry holds
        // nothing at all, which is a different arm entirely (and one :640 covers).
        QVERIFY(registry.hasEffect(QStringLiteral("fireflies")));
        QVERIFY(registry.hasEffect(QStringLiteral("opacity-tint")));

        const QStringList chain{QStringLiteral("fireflies"), QStringLiteral("opacity-tint")};
        QVERIFY(!chainRoundBottomCorners(registry, chain, {}).isValid());
    }

    /// Nothing stored anywhere: the first pack in CHAIN ORDER that declares the
    /// control settles it. Bundled packs all declare the same default, so this
    /// arm only bites for a third-party pack shipping a different one, and it
    /// still has to produce one answer rather than let each pack keep its own.
    void chainRoundBottomCorners_falls_back_to_the_first_declaring_packs_default()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("backdrop"), false));
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("border"), true));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const QStringList chain{QStringLiteral("backdrop"), QStringLiteral("border")};
        const QVariant answer = chainRoundBottomCorners(registry, chain, {});
        QVERIFY(answer.isValid());
        QCOMPARE(answer.toBool(), false);

        // Reversing the chain reverses the answer: it is chain order that
        // decides, not pack id or discovery order.
        const QStringList reversed{QStringLiteral("border"), QStringLiteral("backdrop")};
        QCOMPARE(chainRoundBottomCorners(registry, reversed, {}).toBool(), true);
    }

    /// A value the user actually set outranks ANY declared default, including
    /// one belonging to an earlier pack in the chain. effectiveParameters()
    /// carries only what a user or preset set, so presence in the map is the
    /// signal that this is a choice rather than a default racing a default.
    void chainRoundBottomCorners_prefers_a_stored_value_over_an_earlier_declared_default()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("backdrop"), true));
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("border"), true));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        // Only the SECOND pack carries a stored value. The first pack's
        // declared true must not win just for coming first.
        const QVariantMap allParams{{QStringLiteral("border"), storedValue(false)}};
        const QStringList chain{QStringLiteral("backdrop"), QStringLiteral("border")};
        QCOMPARE(chainRoundBottomCorners(registry, chain, allParams).toBool(), false);
    }

    /// Two stored values disagree, which is a genuine conflict with no right
    /// answer. It resolves by chain order and stays deterministic rather than
    /// depending on QVariantMap iteration.
    void chainRoundBottomCorners_takes_the_first_stored_value_in_chain_order()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("backdrop"), true));
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("border"), true));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        const QVariantMap allParams{{QStringLiteral("backdrop"), storedValue(true)},
                                    {QStringLiteral("border"), storedValue(false)}};
        QCOMPARE(chainRoundBottomCorners(registry, {QStringLiteral("backdrop"), QStringLiteral("border")}, allParams)
                     .toBool(),
                 true);
        QCOMPARE(chainRoundBottomCorners(registry, {QStringLiteral("border"), QStringLiteral("backdrop")}, allParams)
                     .toBool(),
                 false);
    }

    /// A profile naming a pack the user uninstalled keeps its stored values.
    /// Letting that vote would hand the chain a silhouette from a pack that
    /// draws nothing, which is the same class of bug as paddingRequest honouring
    /// an override under an undeclared paddingParam.
    void chainRoundBottomCorners_ignores_a_pack_the_registry_does_not_know()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("border"), true));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(!registry.hasEffect(QStringLiteral("uninstalled")));

        const QVariantMap allParams{{QStringLiteral("uninstalled"), storedValue(false)}};
        const QStringList chain{QStringLiteral("uninstalled"), QStringLiteral("border")};
        // The surviving pack's declared true, not the ghost's stored false.
        QCOMPARE(chainRoundBottomCorners(registry, chain, allParams).toBool(), true);
    }

    /// A stored value for a pack that never declared the control is stale config
    /// (the user switched packs, or hand-edited the profile). resolveParams
    /// copies unrecognised ids through verbatim, so this reaches the resolver and
    /// must not be read as that pack's answer.
    void chainRoundBottomCorners_ignores_a_stored_value_on_a_pack_that_does_not_declare_it()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("opacity-tint"), {}));
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("border"), true));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        // Positive assertion for the same reason as above: "declares other params but
        // not this one" and "not in the registry at all" both yield true here, and it
        // is the FORMER this slot exists to pin.
        QVERIFY(registry.hasEffect(QStringLiteral("opacity-tint")));

        const QVariantMap allParams{{QStringLiteral("opacity-tint"), storedValue(false)}};
        const QStringList chain{QStringLiteral("opacity-tint"), QStringLiteral("border")};
        QCOMPARE(chainRoundBottomCorners(registry, chain, allParams).toBool(), true);
    }

    /// A pack that DECLARES the control with no default states no opinion, so it must
    /// abstain and let a later pack's declared default decide. Before the validity
    /// guard, defaultValue.toBool() answered false for an absent default and — because
    /// QVariant(false) is itself valid — latched into the fallback and blocked every
    /// later pack, so one silent pack squared the whole chain.
    void chainRoundBottomCorners_a_declarer_without_a_default_abstains()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // Valid-but-null selects the `"default": null` shape; see writeSilhouettePack.
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("backdrop"), QVariant::fromValue(nullptr)));
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("border"), true));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(registry.hasEffect(QStringLiteral("backdrop")));
        QVERIFY(registry.hasEffect(QStringLiteral("border")));

        const QStringList chain{QStringLiteral("backdrop"), QStringLiteral("border")};
        const QVariant answer = chainRoundBottomCorners(registry, chain, {});
        QVERIFY2(answer.isValid(), "the later pack states a default, so the chain has an answer");
        QCOMPARE(answer.toBool(), true);
    }

    /// A stored value that is present but null is not a choice either. It falls through
    /// to THAT pack's own declared default rather than terminating the scan with a
    /// fabricated false. Reachable from a hand-edited or imported profile.
    void chainRoundBottomCorners_ignores_a_null_stored_value()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("border"), true));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(registry.hasEffect(QStringLiteral("border")));

        const QVariantMap allParams{
            {QStringLiteral("border"),
             QVariantMap{{QStringLiteral("roundBottomCorners"), QVariant::fromValue(nullptr)}}}};
        const QStringList chain{QStringLiteral("border")};
        QCOMPARE(chainRoundBottomCorners(registry, chain, allParams).toBool(), true);
    }

    /// An unusable stored value falls through to THAT pack's declared default, and the
    /// scan then CONTINUES — so a genuine stored value on a later pack still outranks the
    /// fallback. This is the outcome most likely to be misread: "falls through to the
    /// declared default" does not mean "returns it", and turning the fall-through into a
    /// return would break exactly this case.
    void chainRoundBottomCorners_a_later_stored_value_outranks_a_fallback_default()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("backdrop"), true));
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("border"), true));
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("late"), false));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(registry.hasEffect(QStringLiteral("backdrop")));
        QVERIFY(registry.hasEffect(QStringLiteral("border")));
        QVERIFY(registry.hasEffect(QStringLiteral("late")));

        // backdrop's stored value is null, so it contributes only its declared true as a
        // fallback; border's stored false is a real choice and must win. Turning the
        // fall-through into an early RETURN would answer backdrop's true and fail here.
        const QVariantMap allParams{{QStringLiteral("backdrop"),
                                     QVariantMap{{QStringLiteral("roundBottomCorners"), QVariant::fromValue(nullptr)}}},
                                    {QStringLiteral("border"), storedValue(false)}};
        const QStringList chain{QStringLiteral("backdrop"), QStringLiteral("border")};
        QCOMPARE(chainRoundBottomCorners(registry, chain, allParams).toBool(), false);

        // The mirror case, needed because the assertion above passes against the
        // PRE-FIX code too (which read the null as a usable false and returned it, the
        // same answer border's stored false gives). Here no later pack stores anything,
        // so the fallback backdrop contributed IS the answer, and it must be backdrop's
        // true rather than `late`'s declared false. Three mutations fail this:
        // treating the null as usable, skipping the pack outright instead of falling
        // through, and letting a later declarer overwrite an already-held fallback.
        const QVariantMap fallbackOnly{
            {QStringLiteral("backdrop"),
             QVariantMap{{QStringLiteral("roundBottomCorners"), QVariant::fromValue(nullptr)}}}};
        const QStringList fallbackChain{QStringLiteral("backdrop"), QStringLiteral("late")};
        QCOMPARE(chainRoundBottomCorners(registry, fallbackChain, fallbackOnly).toBool(), true);
    }

    /// A pack the registry KNOWS but cannot use gets no vote either. The loader keeps a
    /// pack whose fragmentShader escapes its own directory but clears the path, so
    /// hasEffect() is true while isValid() is false — the one shape that distinguishes
    /// the isValid() guard from the registry lookup.
    void chainRoundBottomCorners_ignores_a_registered_but_invalid_pack()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // Declares FALSE, the opposite of the surviving pack, so the slot fails if the
        // invalid pack is allowed to vote.
        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("broken"));
        meta.insert(QLatin1String("name"), QStringLiteral("broken"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("../outside.frag"));
        QJsonObject param;
        param.insert(QLatin1String("id"), QStringLiteral("roundBottomCorners"));
        param.insert(QLatin1String("name"), QStringLiteral("Round bottom corners"));
        param.insert(QLatin1String("type"), QStringLiteral("bool"));
        param.insert(QLatin1String("default"), false);
        QJsonArray params;
        params.append(param);
        meta.insert(QLatin1String("parameters"), params);
        QVERIFY(writeFile(tmp.path() + QStringLiteral("/broken/metadata.json"), QJsonDocument(meta).toJson()));
        QVERIFY(writeSilhouettePack(tmp.path(), QStringLiteral("border"), true));

        SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY2(registry.hasEffect(QStringLiteral("broken")), "the registry is the catalog, not the gate");
        QVERIFY2(!registry.effect(QStringLiteral("broken")).isValid(), "the escaping shader path is cleared");

        const QStringList chain{QStringLiteral("broken"), QStringLiteral("border")};
        QCOMPARE(chainRoundBottomCorners(registry, chain, {}).toBool(), true);
    }

    /// The id the hosts inject under has to be the id the resolver reads, or the
    /// chain answer lands in a key no pack declares and is silently dropped.
    void roundBottomCornersParamId_is_the_id_packs_declare()
    {
        QCOMPARE(roundBottomCornersParamId(), QStringLiteral("roundBottomCorners"));
    }
};

QTEST_MAIN(TestSurfaceChainCompose)
#include "test_surfacechaincompose.moc"
