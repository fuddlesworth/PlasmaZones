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
};

QTEST_MAIN(TestSurfaceChainCompose)
#include "test_surfacechaincompose.moc"
