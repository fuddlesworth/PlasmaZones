// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceChainCompose.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QUrl>
#include <QtTest/QtTest>

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

} // namespace

/// Covers the two helpers the daemon overlay host, the kwin-effect
/// compositor path, and the settings decoration preview share. The padding
/// resolution had been copy-pasted into two of those and had already drifted
/// in type; these pin the behaviour all three now depend on.
class TestSurfaceChainCompose : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ── paddingRequest ───────────────────────────────────────────────

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

    // ── composeStageMap ──────────────────────────────────────────────

    void composeStageMap_emits_the_host_contract_keys()
    {
        const SurfaceShaderEffect e = basePack();
        const QVariantMap stage = composeStageMap(e, {});
        // The keys SurfaceDecoration.qml reads off every stage.
        QVERIFY(stage.contains(QStringLiteral("source")));
        QVERIFY(stage.contains(QStringLiteral("vertexSource")));
        QVERIFY(stage.contains(QStringLiteral("preamble")));
        QVERIFY(stage.contains(QStringLiteral("params")));
        QVERIFY(stage.contains(QStringLiteral("animated")));
        QVERIFY(stage.contains(QStringLiteral("multipass")));
        QCOMPARE(stage.value(QStringLiteral("source")).toUrl(),
                 QUrl::fromLocalFile(QStringLiteral("/packs/border/effect.frag")));
    }

    /// An empty vertexShaderPath must stay an EMPTY url, not file:// of "".
    /// The host falls through to its shared surface vert on the empty case.
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
        QVERIFY(!stage.contains(QStringLiteral("bufferShaderPaths")));
        QVERIFY(!stage.contains(QStringLiteral("bufferScale")));
        QVERIFY(!stage.contains(QStringLiteral("bufferFeedback")));
        QVERIFY(!stage.contains(QStringLiteral("useDepthBuffer")));
    }

    void composeStageMap_forwards_the_whole_buffer_set_for_a_multipass_pack()
    {
        SurfaceShaderEffect e = basePack();
        e.isMultipass = true;
        e.bufferShaderPaths =
            QStringList{QStringLiteral("/packs/blur/gaussian_h.frag"), QStringLiteral("/packs/blur/gaussian_v.frag")};
        e.bufferFeedback = true;
        e.bufferScale = 0.25;
        e.bufferWrap = QStringLiteral("clamp");
        e.bufferWraps = QStringList{QStringLiteral("clamp"), QString()};
        e.bufferFilter = QStringLiteral("linear");
        e.bufferFilters = QStringList{QString(), QStringLiteral("nearest")};
        e.useDepthBuffer = true;

        const QVariantMap stage = composeStageMap(e, {});
        QCOMPARE(stage.value(QStringLiteral("multipass")).toBool(), true);
        QCOMPARE(stage.value(QStringLiteral("bufferShaderPaths")).toStringList(), e.bufferShaderPaths);
        QCOMPARE(stage.value(QStringLiteral("bufferFeedback")).toBool(), true);
        QCOMPARE(stage.value(QStringLiteral("bufferScale")).toDouble(), 0.25);
        QCOMPARE(stage.value(QStringLiteral("bufferWrap")).toString(), QStringLiteral("clamp"));
        QCOMPARE(stage.value(QStringLiteral("bufferWraps")).toStringList(), e.bufferWraps);
        QCOMPARE(stage.value(QStringLiteral("bufferFilter")).toString(), QStringLiteral("linear"));
        QCOMPARE(stage.value(QStringLiteral("bufferFilters")).toStringList(), e.bufferFilters);
        QCOMPARE(stage.value(QStringLiteral("useDepthBuffer")).toBool(), true);
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
};

QTEST_MAIN(TestSurfaceChainCompose)
#include "test_surfacechaincompose.moc"
