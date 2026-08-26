// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_decorationpreviewcontroller.cpp
 * @brief Data-surface tests for DecorationPreviewController.
 *
 * The controller feeds the settings app's live decoration preview. Its whole
 * value is that the preview describes a chain stage exactly as the daemon
 * does, so what is pinned here is the SHAPE the QML host consumes and the
 * degraded paths a browsing user can actually reach: an unknown pack, a pack
 * with no padding request, a null registry.
 *
 * Rendering is out of scope (it needs a GPU and a scene graph, which is what
 * the GPU-gated test_surface_decoration_orientation covers). What is in scope
 * is that previewChain produces a one-stage chain in SurfaceDecoration.qml's
 * expected shape, and that previewOuterPadding — the extended-FBO request that
 * gives glow / shadow / motes their transparent room — resolves and clamps.
 *
 * Packs are authored into a QTemporaryDir and discovered through the real
 * SurfaceShaderRegistry, so the metadata → registry → controller path is
 * exercised end to end rather than against a hand-built effect struct.
 */

#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include "settings/pages/decorationpreviewcontroller.h"

using namespace PlasmaZones;

namespace {

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

/// Author a minimal surface pack. @p extra is merged over the base metadata so
/// a test can add paddingParam / parameters without restating the whole object.
bool writePack(const QString& root, const QString& id, const QJsonObject& extra)
{
    QJsonObject meta{{QStringLiteral("id"), id},
                     {QStringLiteral("name"), id},
                     {QStringLiteral("version"), QStringLiteral("1.0")},
                     {QStringLiteral("fragmentShader"), QStringLiteral("effect.frag")}};
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        meta.insert(it.key(), it.value());

    const QString dir = root + QLatin1Char('/') + id;
    if (!writeFile(dir + QStringLiteral("/effect.frag"), QByteArrayLiteral("void main() {}")))
        return false;
    return writeFile(dir + QStringLiteral("/metadata.json"), QJsonDocument(meta).toJson());
}

QJsonObject floatParam(const QString& id, double def, double min, double max)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("name"), id},
                       {QStringLiteral("type"), QStringLiteral("float")},
                       {QStringLiteral("default"), def},
                       {QStringLiteral("min"), min},
                       {QStringLiteral("max"), max}};
}

} // namespace

class TestDecorationPreviewController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString root = m_dir.path();
        // Plain single-pass pack with one parameter.
        QVERIFY(writePack(root, QStringLiteral("border"),
                          QJsonObject{{QStringLiteral("parameters"),
                                       QJsonArray{floatParam(QStringLiteral("borderWidth"), 2.0, 0.0, 20.0)}}}));
        // Outer-effect pack: declares a padding parameter, the extended-FBO
        // request the preview has to honour or a glow gets clipped.
        QVERIFY(writePack(root, QStringLiteral("glow"),
                          QJsonObject{{QStringLiteral("paddingParam"), QStringLiteral("glowSize")},
                                      {QStringLiteral("parameters"),
                                       QJsonArray{floatParam(QStringLiteral("glowSize"), 16.0, 0.0, 64.0)}}}));

        m_registry.addSearchPath(root, PhosphorFsLoader::LiveReload::Off);
        QVERIFY2(m_registry.hasEffect(QStringLiteral("border")), "fixture packs must be discoverable");
        QVERIFY(m_registry.hasEffect(QStringLiteral("glow")));
    }

    // ── previewChain ─────────────────────────────────────────────────

    /// One pack in, exactly one stage out, carrying the keys
    /// SurfaceDecoration.qml binds against.
    void previewChain_is_a_single_stage_in_the_host_shape()
    {
        DecorationPreviewController c(&m_registry, nullptr);
        const QVariantList chain = c.previewChain(QStringLiteral("border"), {});
        QCOMPARE(chain.size(), 1);
        const QVariantMap stage = chain.first().toMap();
        QVERIFY(stage.contains(QStringLiteral("source")));
        QVERIFY(stage.contains(QStringLiteral("preamble")));
        QVERIFY(stage.contains(QStringLiteral("params")));
        QVERIFY(stage.contains(QStringLiteral("animated")));
        QVERIFY(stage.contains(QStringLiteral("multipass")));
        QVERIFY(stage.value(QStringLiteral("source")).toUrl().isLocalFile());
    }

    /// An edited parameter has to reach the stage, or the preview would not
    /// respond to the editor at all.
    void previewChain_carries_edited_params_into_the_stage()
    {
        DecorationPreviewController c(&m_registry, nullptr);
        const QVariantList chain = c.previewChain(QStringLiteral("border"), {{QStringLiteral("borderWidth"), 7.0}});
        QCOMPARE(chain.size(), 1);
        const QVariantMap params = chain.first().toMap().value(QStringLiteral("params")).toMap();
        QCOMPARE(params.value(QStringLiteral("customParams1_x")).toDouble(), 7.0);
    }

    /// A pack id that is not installed must leave the host inert (empty chain
    /// = undecorated card), not produce a half-built stage.
    void previewChain_is_empty_for_an_unknown_pack()
    {
        DecorationPreviewController c(&m_registry, nullptr);
        QVERIFY(c.previewChain(QStringLiteral("nosuchpack"), {}).isEmpty());
        QVERIFY(c.previewChain(QString(), {}).isEmpty());
    }

    /// The degraded construction path documented on the class: no registry,
    /// empty results rather than a crash.
    void previewChain_is_empty_without_a_registry()
    {
        DecorationPreviewController c(nullptr, nullptr);
        QVERIFY(c.previewChain(QStringLiteral("border"), {}).isEmpty());
        QCOMPARE(c.previewOuterPadding(QStringLiteral("border"), {}), 0.0);
        QVERIFY(c.packInfo(QStringLiteral("border")).isEmpty());
    }

    // ── previewOuterPadding ──────────────────────────────────────────

    /// A pack that requests no outer margin keeps the 1:1 geometry.
    void previewOuterPadding_is_zero_for_a_pack_without_one()
    {
        DecorationPreviewController c(&m_registry, nullptr);
        QCOMPARE(c.previewOuterPadding(QStringLiteral("border"), {}), 0.0);
    }

    void previewOuterPadding_uses_the_declared_default()
    {
        DecorationPreviewController c(&m_registry, nullptr);
        QCOMPARE(c.previewOuterPadding(QStringLiteral("glow"), {}), 16.0);
    }

    /// Editing the padding parameter must move the preview's transparent room
    /// live — this is what lets a user drag glow size and watch the halo grow
    /// instead of seeing it clipped at the old canvas.
    void previewOuterPadding_follows_an_edited_padding_param()
    {
        DecorationPreviewController c(&m_registry, nullptr);
        QCOMPARE(c.previewOuterPadding(QStringLiteral("glow"), {{QStringLiteral("glowSize"), 40.0}}), 40.0);
    }

    /// Clamped like the daemon and compositor: a hostile or typo'd value must
    /// not be able to demand an absurd preview canvas.
    void previewOuterPadding_is_clamped_to_the_shared_maximum()
    {
        DecorationPreviewController c(&m_registry, nullptr);
        const double huge = c.previewOuterPadding(QStringLiteral("glow"), {{QStringLiteral("glowSize"), 100000.0}});
        QCOMPARE(huge, static_cast<double>(PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx));
        const double negative = c.previewOuterPadding(QStringLiteral("glow"), {{QStringLiteral("glowSize"), -50.0}});
        QCOMPARE(negative, 0.0);
    }

    // ── packInfo ─────────────────────────────────────────────────────

    void packInfo_exposes_identity_parameters_and_the_pane_flags()
    {
        DecorationPreviewController c(&m_registry, nullptr);
        const QVariantMap info = c.packInfo(QStringLiteral("border"));
        QCOMPARE(info.value(QStringLiteral("id")).toString(), QStringLiteral("border"));
        // The pane keys its two explanatory notices on these.
        QVERIFY(info.contains(QStringLiteral("audio")));
        QVERIFY(info.contains(QStringLiteral("needsBackdrop")));
        const QVariantList params = info.value(QStringLiteral("parameters")).toList();
        QCOMPARE(params.size(), 1);
        QCOMPARE(params.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("borderWidth"));
        QCOMPARE(params.first().toMap().value(QStringLiteral("default")).toDouble(), 2.0);
    }

    /// Audio capture must never start without the user's visualizer setting,
    /// so a plain border preview cannot spawn a CAVA process. With null
    /// settings the getter is false, and start is a no-op.
    void audio_capture_stays_off_without_the_visualizer_setting()
    {
        DecorationPreviewController c(&m_registry, nullptr);
        QVERIFY(!c.audioVisualizerEnabled());
        c.startAudioCapture();
        QVERIFY(c.audioSpectrumVariant().value<QVector<float>>().isEmpty());
        c.stopAudioCapture(); // must be safe with no provider ever created
    }

private:
    QTemporaryDir m_dir;
    PhosphorSurfaceShaders::SurfaceShaderRegistry m_registry;
};

QTEST_MAIN(TestDecorationPreviewController)
#include "test_decorationpreviewcontroller.moc"
