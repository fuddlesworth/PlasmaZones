// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The kwin-effect's wobble-lattice spring integrator, included by relative
// path on purpose: this controller is compiled into the settings app AND
// eleven animation test executables, and a quote-include resolves against
// this file so none of those targets needs the kwin-effect include dir.
// Both trees are GPL-3.0, so no license boundary is crossed.
#include "../../../kwin-effect/plasmazoneseffect/mesh_sim.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QVariantMap>

#include <array>
#include <memory>

class QQuickItem;

namespace PhosphorAudio {
class CavaSpectrumProvider;
}

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
}

namespace PlasmaZones {

class ISettings;

/**
 * @brief QML data source for the live ANIMATION (transition-pack) preview.
 *
 * Third sibling of ShaderPreviewController (zone/overlay) and
 * DecorationPreviewController (surface packs), kept separate for the same
 * reason those two are: an animation preview is a shader played OVER one
 * captured stand-in card with a driven progress clock, which is neither a
 * zone array nor a composed decoration chain.
 *
 * What this deliberately does NOT do is assemble the shader item's static
 * configuration itself. configurePreviewItem delegates to
 * PhosphorAnimationLayer::applyEffectStaticConfig, the same function every
 * production shader leg (SurfaceAnimator's attach and reuse paths) runs, so
 * the previewed pack is configured exactly as a real transition configures
 * it. The preview pane owns the geometry and the progress clock; this
 * controller owns everything that needs C++ (registry lookups, the uniform
 * extension, parameter translation, CAVA capture).
 *
 * The registry and settings are borrowed; the owner must outlive this
 * object. Both may be null (the unit-test / degraded construction path), in
 * which case every accessor returns an empty result rather than crashing.
 */
class AnimationPreviewController : public QObject
{
    Q_OBJECT

    /// Live CAVA spectrum for an audio-reactive pack (`"audio": true`).
    /// Empty while capture is stopped or the audio visualizer is disabled.
    Q_PROPERTY(QVariant audioSpectrum READ audioSpectrumVariant NOTIFY audioSpectrumChanged)

    /// Whether the user's audio visualizer setting is on. A PROPERTY rather
    /// than only an invokable so the pane's "the visualizer is off" notice
    /// tracks the setting while the dialog is open.
    Q_PROPERTY(bool audioVisualizerEnabled READ audioVisualizerEnabled NOTIFY audioVisualizerEnabledChanged)

    /// Bumped when the registry rescans. A COUNTER rather than a bare signal
    /// because the QML bindings are over Q_INVOKABLE calls, which record no
    /// dependency — referencing this property inside them is what puts them
    /// in the dependency set. Same idiom as DecorationPreviewController.
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewRevisionChanged)

public:
    /// The live DecorationPreviewCard's colour roles, handed over by the
    /// pane (setSceneColors) so the composed scene textures paint the same
    /// stand-in window the card classes stage. Invalid members fall back
    /// to QPalette approximations in the painter.
    struct SceneColors
    {
        QColor titleBar;
        QColor titleText;
        QColor body;
        QColor rule;
        QColor accent;
        bool operator==(const SceneColors&) const = default;
    };

    explicit AnimationPreviewController(PhosphorAnimationShaders::AnimationShaderRegistry* registry = nullptr,
                                        ISettings* settings = nullptr, QObject* parent = nullptr);
    ~AnimationPreviewController() override;

    /// Pack metadata the pane needs: `valid`, `audio`, `fboExtentSurface`
    /// (the shader item must fill the preview field rather than the card),
    /// and `eventClass` — which transition subject the pane should stage:
    /// "appearance" (card show/hide), "desktop" (two desktop endpoints),
    /// "geometry"/"move" (card plus old-content snapshot and from/to
    /// rects), "strip" (a scrolling scene), or "tab" (card plus the
    /// outgoing tab's snapshot). A pack whose appliesTo includes
    /// "appearance" (or is universal) classifies as appearance: that is
    /// the leg the daemon actually runs it for. Empty map when the pack
    /// is unknown.
    Q_INVOKABLE QVariantMap packInfo(const QString& packId) const;

    /// Advance the move-class simulation by one pane frame and upload its
    /// products: the 15 ms trail ring (phosphor-vortex's ghosts) and the
    /// wobble lattice, stepped through the same mesh_sim spring integrator
    /// the compositor runs, gripped at the titlebar's centre (where a real
    /// drag holds the window). @p x/y/w/h is
    /// the card's rect in field coordinates and @p dtMs the frame delta.
    /// Re-seeds itself when the item changes or the card teleports (the
    /// glide loop wrapping), so the wrap does not read as a violent yank.
    Q_INVOKABLE void driveMoveState(QQuickItem* item, qreal x, qreal y, qreal w, qreal h, qreal dtMs);

    /// Push the transition-class scalars for the current clock frame onto
    /// an already-configured item's uniform extension. Recognised keys:
    /// `switchDelta` / `stripMotion` (vector4d), `stripAxis` (vector2d),
    /// `stripRect` / `fromRect` / `toRect` / `iconRect` (rect),
    /// `oldWindowOpacity` (number), `hasOldWindow` (bool). Absent keys are left as they are;
    /// every setter no-ops on identity, so calling this per frame is cheap.
    Q_INVOKABLE void driveTransitionState(QQuickItem* item, const QVariantMap& state) const;

    /// Upload the stand-in textures @p eventClass needs onto the item's
    /// user-texture slots (the UBO branch's sampler aliases): desktop packs
    /// get a windows-scene "from" (slot 1) and a bare-wallpaper "to"
    /// (slot 2), strip packs a scrolling scene (slot 1), tab / geometry /
    /// move packs an old-window snapshot (slot 3). Call AFTER
    /// configurePreviewItem — setShaderParams re-parses texture slots.
    /// The images are composed once and cached per wallpaper.
    /// Hand over the Kirigami theme colours the live DecorationPreviewCard
    /// paints with (title bar, its text, card body, text rules, the accent
    /// block), so the composed scene textures use the SAME colours in the
    /// user's actual theme instead of QPalette approximations — the accent
    /// especially has no QPalette role at all. Call before
    /// bindClassTextures; a colour change invalidates the scene caches, so
    /// re-binding after a theme switch recomposes.
    Q_INVOKABLE void setSceneColors(const QColor& titleBar, const QColor& titleText, const QColor& body,
                                    const QColor& rule, const QColor& accent);

    Q_INVOKABLE void bindClassTextures(QQuickItem* item, const QString& eventClass) const;

    /// Configure @p item (a PhosphorRendering::ShaderEffect the pane
    /// created) for @p packId exactly as a production shader leg would:
    /// install the animation uniform extension, run
    /// applyEffectStaticConfig with the registry's shared include dirs (and
    /// the shared animation.vert fallback, mirroring SurfaceAnimator's
    /// runLeg), seed iTime / isReversed for a show leg, and upload the
    /// translated parameters. Returns false — leaving the item unconfigured
    /// — for an unknown, invalid or compositor-only pack.
    Q_INVOKABLE bool configurePreviewItem(QQuickItem* item, const QString& packId,
                                          const QVariantMap& friendlyParams) const;

    /// Re-translate and upload @p friendlyParams onto an already-configured
    /// item, for live parameter editing in the detail dialog.
    Q_INVOKABLE void updatePreviewParams(QQuickItem* item, const QString& packId,
                                         const QVariantMap& friendlyParams) const;

    /// Push the per-frame spatial uniforms a real leg's syncShaderGeometryNow
    /// pushes, with the pane's field standing in for the wl_surface: the
    /// card rect (field-local logical px) becomes iAnchorSize /
    /// iSurfaceScreenPos.xy / (for surface extent) iAnchorPosInFbo, and the
    /// field size becomes iSurfaceScreenPos.zw. iResolution follows the
    /// production convention: card size for anchor extent, field size for
    /// surface extent. The pane calls this whenever card or field geometry
    /// moves; every setter no-ops on identity.
    /// @p paddedCapture: true when the captured texture is the WHOLE field
    /// (a padded canvas with the card inset at cardX/cardY — the kwin
    /// window-canvas framing the geometry/move classes use), so
    /// iAnchorRectInTexture becomes the card's sub-rect within it. False
    /// when the capture holds the bare card (every other class), which is
    /// the (0,0,1,1) identity.
    Q_INVOKABLE void syncPreviewGeometry(QQuickItem* item, bool surfaceExtent, qreal cardX, qreal cardY, qreal cardW,
                                         qreal cardH, qreal fieldW, qreal fieldH, bool paddedCapture = false) const;

    /// Absolute path to the user's current desktop wallpaper, or empty when
    /// it cannot be resolved. Drawn behind the stand-in card so a transition
    /// is judged against a desktop rather than flat grey. Same resolver the
    /// zone and decoration previews use, so all three agree on what "the
    /// desktop" is.
    Q_INVOKABLE QString wallpaperPath() const;

    /// Whether the user's audio visualizer setting is on.
    Q_INVOKABLE bool audioVisualizerEnabled() const;

    int previewRevision() const
    {
        return m_previewRevision;
    }

    Q_INVOKABLE void startAudioCapture();
    Q_INVOKABLE void stopAudioCapture();

    QVariant audioSpectrumVariant() const;

Q_SIGNALS:
    void audioSpectrumChanged();
    void audioVisualizerEnabledChanged();
    void previewRevisionChanged();

private:
    /// Stop the provider without clearing the standing capture request —
    /// same split as DecorationPreviewController::stopCapture and for the
    /// same reason (the visualizer-setting handler suspends and resumes).
    void stopCapture();
    void bumpPreviewRevision();
    /// Stand-in scene painters for bindClassTextures. Composed once and
    /// cached in the members below; setSceneColors invalidates the caches
    /// when the theme moves.
    QImage desktopFromImage() const;
    QImage desktopToImage() const;
    QImage stripSceneImage() const;
    QImage oldWindowImage() const;

    SceneColors m_sceneColors;
    mutable QImage m_desktopFromCache;
    mutable QImage m_desktopToCache;
    mutable QImage m_stripSceneCache;
    mutable QImage m_oldWindowCache;

    PhosphorAnimationShaders::AnimationShaderRegistry* m_registry = nullptr;
    ISettings* m_settings = nullptr;
    std::unique_ptr<PhosphorAudio::CavaSpectrumProvider> m_audio;
    /// Whether a host currently wants a live spectrum (see the decoration
    /// controller's twin member for the request-vs-running distinction).
    bool m_captureRequested = false;
    int m_previewRevision = 0;
    QVector<float> m_spectrum;

    // driveMoveState's simulation state. One live glide at a time — the
    // detail dialog previews one pack — so a single sim re-seeded on item
    // change suffices; the QObject* is only compared, never dereferenced.
    QObject* m_moveSimItem = nullptr;
    MeshSim m_moveMesh;
    std::array<QPointF, 16> m_moveTrail{};
    QPointF m_moveLastOrigin;
    double m_moveTrailAccumMs = 0.0;
};

} // namespace PlasmaZones
