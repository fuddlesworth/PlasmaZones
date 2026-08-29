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

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include "helpers/StubSettings.h"
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

QJsonObject colorParam(const QString& id, const QString& def)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("name"), id},
                       {QStringLiteral("type"), QStringLiteral("color")},
                       {QStringLiteral("default"), def}};
}

QJsonObject boolParam(const QString& id, bool def)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("name"), id},
                       {QStringLiteral("type"), QStringLiteral("bool")},
                       {QStringLiteral("default"), def}};
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

        // Backdrop-sampling multipass pack, modelled on the shipping glass /
        // blur family. Without one of these the multipass and needsBackdrop
        // branches are unreachable and asserting on them only pins a constant.
        //
        // The builtin: tokens resolve against a `shared/` directory that is a
        // SIBLING of the pack dir (surfacebuiltinbuffers.cpp), and an
        // unresolvable token fails CLOSED — isMultipass is forced false and the
        // buffer list is cleared. Writing the stubs is therefore what makes the
        // multipass assertion mean anything; the QStandardPaths fallback is not
        // usable here because the test's XDG sandbox does not cover
        // XDG_DATA_DIRS, so it would find an installed pack on a developer box
        // and nothing in CI.
        QVERIFY(writeFile(root + QStringLiteral("/shared/gaussian_h.frag"), QByteArrayLiteral("void main() {}")));
        QVERIFY(writeFile(root + QStringLiteral("/shared/gaussian_v.frag"), QByteArrayLiteral("void main() {}")));
        QVERIFY(writePack(
            root, QStringLiteral("frost"),
            QJsonObject{{QStringLiteral("needsBackdrop"), true},
                        {QStringLiteral("multipass"), true},
                        {QStringLiteral("bufferShaders"),
                         QJsonArray{QStringLiteral("builtin:gaussian-h"), QStringLiteral("builtin:gaussian-v")}},
                        {QStringLiteral("parameters"),
                         QJsonArray{floatParam(QStringLiteral("blurRadius"), 12.0, 1.0, 64.0)}}}));

        // Padding-requesting pack with several parameters, the shape of the
        // glow / shadow family: one parameter doubles as the outer-margin
        // request, so the preview has to reserve room for a halo it also draws.
        QVERIFY(writePack(root, QStringLiteral("pixels"),
                          QJsonObject{{QStringLiteral("paddingParam"), QStringLiteral("haloSize")},
                                      {QStringLiteral("parameters"),
                                       QJsonArray{floatParam(QStringLiteral("haloSize"), 40.0, 0.0, 64.0),
                                                  floatParam(QStringLiteral("cornerRadius"), 8.0, 0.0, 64.0),
                                                  floatParam(QStringLiteral("strength"), 0.5, 0.0, 1.0)}}}));

        // Theme-reactive pack, the border family's shape: it declares the two
        // colour params the resolver writes and opts into the system accent.
        // Declaration order is slot order and the colour slot keys are 1-based,
        // so activeColor takes customColor1 and inactiveColor customColor2.
        QVERIFY(
            writePack(root, QStringLiteral("accented"),
                      QJsonObject{{QStringLiteral("parameters"),
                                   QJsonArray{colorParam(QStringLiteral("activeColor"), QStringLiteral("#ff0000")),
                                              colorParam(QStringLiteral("inactiveColor"), QStringLiteral("#00ff00")),
                                              boolParam(QStringLiteral("useSystemAccent"), true)}}}));

        m_registry.addSearchPath(root, PhosphorFsLoader::LiveReload::Off);
        QVERIFY2(m_registry.hasEffect(QStringLiteral("border")), "fixture packs must be discoverable");
        QVERIFY(m_registry.hasEffect(QStringLiteral("glow")));
        QVERIFY(m_registry.hasEffect(QStringLiteral("frost")));
        QVERIFY(m_registry.hasEffect(QStringLiteral("accented")));
        QVERIFY(m_registry.hasEffect(QStringLiteral("pixels")));
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

    /// The same identity check against the SHIPPING packs rather than the
    /// fixtures above, because the fixtures are all `float` and the real packs
    /// are not: mosaic's cellSize and the blur family's blurRadius are `int`,
    /// and they reach the shader by different routes (the generated p_<id>
    /// preamble versus a raw buffer-pass lane). A card that composes those
    /// differently from the dialog shows a different effect in the browser.
    void shipping_packs_compose_identically_from_an_empty_map()
    {
        PhosphorSurfaceShaders::SurfaceShaderRegistry registry;
        registry.addSearchPath(QStringLiteral(P_SOURCE_DIR "/data/surface"), PhosphorFsLoader::LiveReload::Off);
        QVERIFY2(registry.hasEffect(QStringLiteral("mosaic")), "shipping packs must be discoverable");
        DecorationPreviewController c(&registry, nullptr);

        for (const QString& packId : {QStringLiteral("mosaic"), QStringLiteral("frosted-glass"), QStringLiteral("blur"),
                                      QStringLiteral("glow")}) {
            QVariantMap declared;
            for (const QVariant& p : c.packInfo(packId).value(QStringLiteral("parameters")).toList()) {
                const QVariantMap info = p.toMap();
                if (info.value(QStringLiteral("default")).isValid())
                    declared.insert(info.value(QStringLiteral("id")).toString(), info.value(QStringLiteral("default")));
            }
            // Every pack in the list has to exist and produce a stage. Without
            // this a renamed pack yields two empty chains, and an
            // empty-versus-empty comparison passes while checking nothing.
            QVERIFY2(registry.hasEffect(packId), qPrintable(packId));
            const QVariantList cardChain = c.previewChain(packId, {});
            const QVariantList paneChain = c.previewChain(packId, declared);
            QVERIFY2(!cardChain.isEmpty(), qPrintable(packId));
            QVERIFY2(!paneChain.isEmpty(), qPrintable(packId));
            QCOMPARE(cardChain.size(), paneChain.size());

            const QVariantMap card = cardChain.value(0).toMap();
            const QVariantMap pane = paneChain.value(0).toMap();
            QVERIFY2(!card.isEmpty(), qPrintable(packId));
            QCOMPARE(card.value(QStringLiteral("preamble")).toString(),
                     pane.value(QStringLiteral("preamble")).toString());
            QCOMPARE(card.value(QStringLiteral("params")).toMap(), pane.value(QStringLiteral("params")).toMap());
            // Whole stage, not the two interesting keys: bufferScale and the
            // buffer-pass descriptors decide how a multi-pass pack is drawn,
            // and a card that differs there shows a different effect.
            QCOMPARE(card, pane);
        }
    }

    /// The browser card passes an EMPTY parameter map and the detail pane
    /// passes an explicit copy of every declared default (it seeds _liveParams
    /// from the pack's own metadata). Those two must compose to byte-identical
    /// stages, or the same pack renders differently in the two places it is
    /// shown even when both are handed identical geometry.
    void an_empty_map_and_the_declared_defaults_compose_identically()
    {
        DecorationPreviewController c(&m_registry, nullptr);

        for (const QString& packId : {QStringLiteral("pixels"), QStringLiteral("border"), QStringLiteral("accented"),
                                      QStringLiteral("glow"), QStringLiteral("frost")}) {
            // What the dialog does: every declared parameter, at its default.
            QVariantMap declared;
            const QVariantList params = c.packInfo(packId).value(QStringLiteral("parameters")).toList();
            QVERIFY2(!params.isEmpty(), qPrintable(packId));
            for (const QVariant& p : params) {
                const QVariantMap info = p.toMap();
                if (info.value(QStringLiteral("default")).isValid())
                    declared.insert(info.value(QStringLiteral("id")).toString(), info.value(QStringLiteral("default")));
            }

            const QVariantList fromCard = c.previewChain(packId, {});
            const QVariantList fromPane = c.previewChain(packId, declared);
            QCOMPARE(fromCard.size(), fromPane.size());
            for (qsizetype i = 0; i < fromCard.size(); ++i) {
                const QVariantMap cardStage = fromCard.at(i).toMap();
                const QVariantMap paneStage = fromPane.at(i).toMap();
                const QVariantMap cardParams = cardStage.value(QStringLiteral("params")).toMap();
                const QVariantMap paneParams = paneStage.value(QStringLiteral("params")).toMap();
                for (auto it = paneParams.constBegin(); it != paneParams.constEnd(); ++it) {
                    QVERIFY2(cardParams.contains(it.key()),
                             qPrintable(packId + QStringLiteral(": card stage is missing ") + it.key()));
                    QVERIFY2(cardParams.value(it.key()) == it.value(),
                             qPrintable(packId + QStringLiteral(": ") + it.key() + QStringLiteral(" card=")
                                        + cardParams.value(it.key()).toString() + QStringLiteral(" pane=")
                                        + it.value().toString()));
                }
                QCOMPARE(cardParams.keys(), paneParams.keys());
                QCOMPARE(cardStage.value(QStringLiteral("preamble")), paneStage.value(QStringLiteral("preamble")));
                // The per-key loop above exists only for a readable failure.
                // THIS is the assertion the docstring promises: every key
                // composeStageMap inserts, bufferScale and the buffer-pass
                // descriptors included, not just params and preamble.
                QCOMPARE(cardStage, paneStage);
            }
            QCOMPARE(c.previewOuterPadding(packId, {}), c.previewOuterPadding(packId, declared));
        }
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

        // With real settings whose visualizer is OFF, so the setting itself is
        // what refuses. The null-settings case above short-circuits on the
        // first operand and would still pass if the enableAudioVisualizer()
        // check were deleted outright, which is not what this slot claims.
        StubSettings off;
        off.setEnableAudioVisualizer(false);
        DecorationPreviewController gated(&m_registry, &off);
        QVERIFY(!gated.audioVisualizerEnabled());
        gated.startAudioCapture();
        QVERIFY(gated.audioSpectrumVariant().value<QVector<float>>().isEmpty());
        gated.stopAudioCapture();
    }

    /// Toggling the visualizer setting republishes, and does not start capture
    /// on its own.
    ///
    /// Two separate obligations in one handler, neither previously exercised.
    /// The republish is what drives the pane's "turn the visualizer on" notice,
    /// so it has to fire whether or not a preview is open. The start is gated
    /// on a standing capture request instead, because this controller outlives
    /// any one dialog and reacting to the setting alone would spawn an external
    /// CAVA process for a preview nobody is looking at.
    ///
    /// Neither leg needs CAVA to exist: nothing here ever requests capture, so
    /// the guard is what is being pinned, not the provider.
    void toggling_the_visualizer_setting_republishes_without_starting_capture()
    {
        StubSettings settings;
        settings.setEnableAudioVisualizer(false);
        DecorationPreviewController c(&m_registry, &settings);
        QVERIFY(!c.audioVisualizerEnabled());

        QSignalSpy spy(&c, &DecorationPreviewController::audioVisualizerEnabledChanged);
        QVERIFY(spy.isValid());

        settings.setEnableAudioVisualizer(true);
        QCOMPARE(spy.count(), 1);
        QVERIFY(c.audioVisualizerEnabled());
        // Nothing asked for a spectrum, so turning the setting on must not have
        // started one.
        QVERIFY(c.audioSpectrumVariant().value<QVector<float>>().isEmpty());

        settings.setEnableAudioVisualizer(false);
        QCOMPARE(spy.count(), 2);
        QVERIFY(!c.audioVisualizerEnabled());
        QVERIFY(c.audioSpectrumVariant().value<QVector<float>>().isEmpty());
    }

    /// A theme-reactive pack previews in the USER's colours, not the palette's.
    ///
    /// previewChain resolves useSystemAccent / useThemeNeutral against the
    /// highlight and inactive colours from settings, falling back to the
    /// QPalette only when there are none. Every other slot here passes a null
    /// ISettings, so the settings-driven direction — the one the class doc
    /// sells, and the reason the preview matches what the daemon will draw —
    /// was never exercised.
    void theme_colors_come_from_settings_when_a_pack_asks_for_them()
    {
        const QColor highlight(0x11, 0x22, 0xEE);
        const QColor inactive(0x44, 0x55, 0x66);
        StubSettings settings;
        settings.setHighlightColor(highlight);
        settings.setInactiveColor(inactive);

        DecorationPreviewController c(&m_registry, &settings);
        // Size first: QList::first() on an empty list is undefined behaviour,
        // so a regression that made previewChain return nothing would turn this
        // slot into a crash rather than a readable failure.
        const QVariantList chain = c.previewChain(QStringLiteral("accented"), {});
        QCOMPARE(chain.size(), 1);
        const QVariantMap stage = chain.first().toMap();
        const QVariantMap params = stage.value(QStringLiteral("params")).toMap();

        // The pack declares activeColor / inactiveColor, so the resolver's
        // writes reach real slot lanes. Colour slot keys are 1-based, and
        // declaration order assigns them, so activeColor is customColor1.
        // Alpha is forced opaque by the resolver.
        QColor active = params.value(QStringLiteral("customColor1")).value<QColor>();
        QColor idle = params.value(QStringLiteral("customColor2")).value<QColor>();
        QCOMPARE(active.rgb(), highlight.rgb());
        QCOMPARE(idle.rgb(), inactive.rgb());
    }

    /// The preview revision moves when a composed preview's unstated inputs do.
    ///
    /// previewChain resolves theme colours per call and looks the pack up in
    /// the registry, but the QML bindings that call it pass neither as an
    /// argument, so nothing would tell them to recompose. QML records no
    /// dependency on a Q_INVOKABLE call either, which is why this is a counter
    /// they can read rather than a bare signal — a signal alone would leave the
    /// preview stale while looking, in the diff, entirely fixed.
    void preview_revision_moves_when_the_theme_colours_change()
    {
        StubSettings settings;
        DecorationPreviewController c(&m_registry, &settings);
        QSignalSpy spy(&c, &DecorationPreviewController::previewRevisionChanged);
        QVERIFY(spy.isValid());

        const int before = c.previewRevision();
        settings.setHighlightColor(QColor(0x10, 0x20, 0x30));
        QCOMPARE(spy.count(), 1);
        QVERIFY(c.previewRevision() != before);

        settings.setInactiveColor(QColor(0x40, 0x50, 0x60));
        QCOMPARE(spy.count(), 2);

        // A no-op write must not churn the preview: ISettings only signals on a
        // real change, and this rides that.
        settings.setInactiveColor(QColor(0x40, 0x50, 0x60));
        QCOMPARE(spy.count(), 2);
    }

    /// The wallpaper stand-in accessors must be total.
    ///
    /// These feed the backdrop a needsBackdrop pack samples in the preview, and
    /// they are the one pair with no fixture behind them: the answer depends on
    /// the user's desktop, which under the test sandbox is nothing at all. What
    /// matters is that the no-wallpaper case is a clean empty/null rather than a
    /// crash or a garbage path, because that is what leaves such a pack on its
    /// documented fallback appearance instead of sampling nonsense.
    void wallpaper_accessors_are_total_without_a_desktop()
    {
        DecorationPreviewController c(&m_registry, nullptr);

        const QString path = c.wallpaperPath();
        // Either a real resolved file or nothing; never a path that does not
        // exist, which the QML background would bind to a broken Image source.
        if (!path.isEmpty()) {
            QVERIFY2(QFileInfo::exists(path), qPrintable(QStringLiteral("wallpaperPath returned ") + path));
        }

        const QImage img = c.wallpaperImage();
        // A null image is the documented no-wallpaper answer. A non-null one
        // must have real dimensions, since it is uploaded as a texture.
        if (!img.isNull()) {
            QVERIFY(img.width() > 0);
            QVERIFY(img.height() > 0);
        }
        // Repeatable: the decode is cached behind these, and every browser card
        // calls them, so a second call must agree with the first.
        QCOMPARE(c.wallpaperPath(), path);
        QCOMPARE(c.wallpaperImage().isNull(), img.isNull());
    }

    /// The multipass and backdrop flags must carry the pack's real VALUES.
    ///
    /// Asserting only that the keys are present passes for a stage that always
    /// reports false, which is what a fixture set with no multipass pack in it
    /// silently guaranteed. SurfaceDecoration.qml gates its buffer layer on
    /// `multipass === true` and a pack's whole fallback branch hangs off
    /// needsBackdrop, so both directions are worth pinning.
    void stage_flags_carry_the_packs_real_values()
    {
        DecorationPreviewController c(&m_registry, nullptr);

        // Size-checked before first() for the same reason as the theme slot
        // above: an empty chain is undefined behaviour here, not a failure.
        const QVariantList plainChain = c.previewChain(QStringLiteral("border"), {});
        QCOMPARE(plainChain.size(), 1);
        const QVariantMap plain = plainChain.first().toMap();
        QCOMPARE(plain.value(QStringLiteral("multipass")).toBool(), false);

        const QVariantList frostChain = c.previewChain(QStringLiteral("frost"), {});
        QCOMPARE(frostChain.size(), 1);
        const QVariantMap frost = frostChain.first().toMap();
        QCOMPARE(frost.value(QStringLiteral("multipass")).toBool(), true);
        QCOMPARE(frost.value(QStringLiteral("bufferShaderPaths")).toStringList().size(), 2);

        QCOMPARE(c.packInfo(QStringLiteral("frost")).value(QStringLiteral("needsBackdrop")).toBool(), true);
        QCOMPARE(c.packInfo(QStringLiteral("border")).value(QStringLiteral("needsBackdrop")).toBool(), false);
    }

    /// Every `previewController.<name>` the decoration preview QML calls must
    /// exist on this controller.
    ///
    /// The animations route has the same guard for its `bridge.*` surface, but
    /// it only ever instantiates AnimationsPageController, so it cannot speak
    /// for these files — and `previewController` itself is on its
    /// documented-optional list, which excludes it from every check there. A
    /// renamed or mistyped invokable is otherwise a silent runtime TypeError
    /// and a blank preview, not a build failure.
    void every_preview_controller_call_from_the_decoration_qml_is_reachable()
    {
        // The DECORATION-only files. ShaderBrowserCard and
        // ShaderBrowserDetailDialog are deliberately excluded: their
        // `previewController` is whichever controller the route supplied, so
        // the zone route's calls (getShaderInfo, zonesForShaderPreview,
        // shaderPresetDirectory, …) legitimately appear there behind
        // `_zonePreview` guards and belong to ShaderPreviewController. Holding
        // this controller to those would demand the wrong route's API, which is
        // the same mistake the animations guard makes in the other direction.
        const QString settingsQml = QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/shaders");
        const QStringList files{settingsQml + QStringLiteral("/DecorationChainPreview.qml"),
                                settingsQml + QStringLiteral("/DecorationPreviewPane.qml")};

        // A hardcoded list rots: a new decoration QML file that talks to
        // previewController would simply go unchecked, which is the failure
        // this whole slot exists to prevent, one level up. Sweep the directory
        // and require every file that touches `previewController.` to be either
        // listed above or deliberately excluded.
        // ShaderBrowserDetailDialog genuinely calls through previewController
        // for whichever route supplied it, per the note above. ShaderBrowserCard
        // only PASSES the controller down to DecorationChainPreview and never
        // calls through it, so it does not carry the token today — listed
        // anyway, because a card that starts calling one directly belongs to
        // the same route-agnostic exemption rather than to this guard.
        const QStringList excluded{settingsQml + QStringLiteral("/ShaderBrowserCard.qml"),
                                   settingsQml + QStringLiteral("/ShaderBrowserDetailDialog.qml")};
        QDirIterator sweep(settingsQml, QStringList{QStringLiteral("*.qml")}, QDir::Files);
        while (sweep.hasNext()) {
            const QString path = sweep.next();
            if (files.contains(path) || excluded.contains(path)) {
                continue;
            }
            QFile f(path);
            QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(QStringLiteral("cannot read ") + path));
            QString src = QString::fromUtf8(f.readAll());
            // Comments stripped first, matching the real scrape below: a file
            // that only MENTIONS previewController in a doc comment is not a
            // caller, and failing it here would send whoever hits it looking
            // for a call that does not exist.
            static const QRegularExpression sweepBlockCommentRe(QStringLiteral("/\\*.*?\\*/"),
                                                                QRegularExpression::DotMatchesEverythingOption);
            static const QRegularExpression sweepLineCommentRe(QStringLiteral("(?<![:\"'])//[^\n]*"));
            src.remove(sweepBlockCommentRe);
            src.remove(sweepLineCommentRe);
            QVERIFY2(!src.contains(QLatin1String("previewController.")),
                     qPrintable(QStringLiteral("%1 calls previewController but is neither scraped nor "
                                               "documented as excluded — add it to one of the two lists")
                                    .arg(path)));
        }

        // Comments are stripped before scraping so a commented-out call cannot
        // stand in for a real one, and a `//` inside a string cannot swallow
        // the rest of a line that carries one.
        static const QRegularExpression blockCommentRe(QStringLiteral("/\\*.*?\\*/"),
                                                       QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression lineCommentRe(QStringLiteral("(?<![:\"'])//[^\n]*"));
        static const QRegularExpression callRe(QStringLiteral("\\bpreviewController\\.([A-Za-z_][A-Za-z0-9_]*)"));

        QSet<QString> used;
        for (const QString& file : files) {
            QFile f(file);
            QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(QStringLiteral("cannot read ") + file));
            QString src = QString::fromUtf8(f.readAll());
            src.remove(blockCommentRe);
            src.remove(lineCommentRe);
            auto it = callRe.globalMatch(src);
            while (it.hasNext())
                used.insert(it.next().captured(1));
        }
        QVERIFY2(!used.isEmpty(), "scraped no previewController.* names — the QML tree or receiver name moved");

        DecorationPreviewController c(&m_registry, nullptr);
        const QMetaObject* meta = c.metaObject();
        QStringList unreachable;
        for (const QString& name : used) {
            const QByteArray raw = name.toUtf8();
            if (meta->indexOfProperty(raw.constData()) >= 0)
                continue;
            bool found = false;
            for (int i = 0; i < meta->methodCount() && !found; ++i)
                found = meta->method(i).name() == raw;
            if (!found)
                unreachable.append(name);
        }
        QVERIFY2(unreachable.isEmpty(),
                 qPrintable(QStringLiteral("the decoration preview QML calls these on previewController, but the "
                                           "controller lacks them: %1")
                                .arg(unreachable.join(QStringLiteral(", ")))));
    }

private:
    QTemporaryDir m_dir;
    PhosphorSurfaceShaders::SurfaceShaderRegistry m_registry;
};

QTEST_MAIN(TestDecorationPreviewController)
#include "test_decorationpreviewcontroller.moc"
