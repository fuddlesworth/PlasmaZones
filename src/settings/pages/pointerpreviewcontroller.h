// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorPointer/PointerHistory.h>

#include <QHash>
#include <QPointF>
#include <QObject>
#include <QVariantMap>

class QQuickItem;

namespace PhosphorPointerShaders {
class PointerShaderRegistry;
}

namespace PlasmaZones {

/**
 * @brief QML data source for the live POINTER (cursor-decoration) preview.
 *
 * Fourth sibling of ShaderPreviewController (zone/overlay),
 * DecorationPreviewController (surface packs) and AnimationPreviewController
 * (transitions), kept separate for the same reason those three are: a pointer
 * preview is one screen-space pass over a stand-in desktop driven by a
 * simulated cursor, which is neither a zone array, nor a composed decoration
 * chain, nor a progress-clocked transition leg.
 *
 * The canvas contract is the compositor's. The shader item IS the output, its
 * bounds are the canvas, and every position uniform is in canvas pixels with
 * the origin at the top-left. The pane hands over a cursor position in its own
 * logical coordinates and this controller does the rest, feeding the same
 * PhosphorPointerShaders::PointerHistory sampler the KWin pass feeds and
 * pushing the resulting frame state through PointerUniformExtension::apply. So
 * the trail a user judges in the browser is built by the same ring buffer,
 * with the same sampling rule and the same velocity model, as the trail the
 * compositor will draw.
 *
 * Several previews can be live at once — the chain editor expands one per
 * open layer row and the browser's detail dialog draws through this same
 * instance — so the simulated pointer is kept per canvas rather than shared.
 * The registry is borrowed and may be null (the unit-test / degraded
 * construction path), in which case every accessor returns an empty result
 * rather than crashing.
 */
class PointerPreviewController : public QObject
{
    Q_OBJECT

    /// Bumped when the registry rescans. A COUNTER rather than a bare signal
    /// because the QML bindings are over Q_INVOKABLE calls, which record no
    /// dependency — referencing this property inside them is what puts them in
    /// the dependency set. Same idiom as the decoration and animation
    /// controllers.
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewRevisionChanged)

public:
    explicit PointerPreviewController(PhosphorPointerShaders::PointerShaderRegistry* registry = nullptr,
                                      QObject* parent = nullptr);
    ~PointerPreviewController() override;

    /// Pack metadata the pane needs: `valid`, `layer` ("below" / "above"),
    /// `trailSeconds`, `needsCursor`, plus `id` / `name` / `parameters`
    /// (ParameterEditor rows). Empty map when the pack is unknown.
    Q_INVOKABLE QVariantMap packInfo(const QString& packId) const;

    /// Configure @p item (a PhosphorRendering::ShaderEffect the pane created)
    /// for @p packId the way the compositor pass configures its compiled
    /// program: install the pointer uniform extension, point the item at the
    /// pack's fragment and vertex sources, splice the `#define p_<id>`
    /// preamble, install the `pPointer` entry scaffold, wire the include
    /// search paths and the multipass buffer set, then upload the translated
    /// parameters. Returns false — leaving the item unconfigured — for an
    /// unknown or invalid pack.
    ///
    /// The extension goes on BEFORE the sources, mirroring the animation
    /// path's ordering rationale: the first prepare() must allocate the UBO
    /// with the tail's trailing bytes, or the shader reads garbage past
    /// sizeof(BaseUniforms) until the next allocation cycle.
    Q_INVOKABLE bool configurePreviewItem(QQuickItem* item, const QString& packId, const QVariantMap& friendlyParams);

    /// Re-translate and upload @p friendlyParams onto an already-configured
    /// item, for live parameter editing in the detail dialog.
    Q_INVOKABLE void updatePreviewParams(QQuickItem* item, const QString& packId,
                                         const QVariantMap& friendlyParams) const;

    /// Advance the simulated pointer by one pane frame and push the whole
    /// frame state onto the item's uniform extension.
    ///
    /// @p x / @p y are the cursor position in the item's own logical
    /// coordinates, @p cursorW / @p cursorH the drawn size of the host's
    /// stand-in cursor (logical too, hotspot at @p x / @p y) so the frame's
    /// cursor rect matches what the host paints, and @p dtMs the frame delta.
    /// @p pressed drives a synthetic
    /// left button, so a click pack's ring fires on the press edge and its
    /// release ring on the release edge, exactly as the compositor's
    /// noteButtons produces them. Each canvas gets its own history, created
    /// when the item is configured (or on its first frame, whichever comes
    /// first), so a newly opened pack starts empty instead of inheriting
    /// wherever another preview's pointer happened to be.
    Q_INVOKABLE void drivePointer(QQuickItem* item, qreal x, qreal y, qreal cursorW, qreal cursorH, qreal dtMs,
                                  bool pressed);

    /// Forget one canvas's simulated pointer so its next drivePointer starts a
    /// fresh trail. The pane calls it when the loop restarts or the pack
    /// changes. Takes the item because several canvases can be live at once
    /// and clearing all of them would wipe a trail the user is still watching.
    Q_INVOKABLE void resetPointer(QQuickItem* item);

    /// Absolute path to the user's current desktop wallpaper, or empty when it
    /// cannot be resolved. The preview draws it as the ground so a trail or a
    /// halo is judged over a desktop rather than flat grey, which is the same
    /// stand-in the other three previews use.
    Q_INVOKABLE QString wallpaperPath() const;

    int previewRevision() const
    {
        return m_previewRevision;
    }

Q_SIGNALS:
    void previewRevisionChanged();

private:
    void bumpPreviewRevision();

    PhosphorPointerShaders::PointerShaderRegistry* m_registry = nullptr;
    int m_previewRevision = 0;

    /// One simulated pointer per live preview item. The chain editor expands
    /// as many rows as the user likes and the browser's detail dialog draws
    /// through this same controller, so several canvases can be driving at
    /// once; a single shared sampler would have each one resetting the others'
    /// ring every frame, and none of them would ever hold more than one
    /// sample. Keyed on the item because that is what identifies a canvas —
    /// the pointer is only ever compared and used as a key, never
    /// dereferenced. Entries are dropped when their item is destroyed, which a
    /// preview does on every collapse and every refresh pulse, so a stale ring
    /// cannot outlive the canvas that owned it.
    struct PointerState
    {
        PhosphorPointerShaders::PointerHistory history;
        /// Monotonic pane clock in ms, accumulated from the frame deltas the
        /// pane reports. A pane clock rather than the wall clock, so a frozen
        /// preview (the app loses focus and the pane stops calling) resumes
        /// without a gap the history would read as a long idle and let the
        /// trail vanish.
        qint64 nowMs = 0;
        bool pressed = false;
        /// Where the last tick left the simulated pointer, in device px, so a
        /// tick can fill in the path it travelled rather than teleporting.
        QPointF lastDevicePos;
        bool hasLastDevicePos = false;
        /// The scale lastDevicePos was measured in, so a move to an output
        /// with another one is caught rather than interpolated across.
        qreal lastDpr = 1.0;
    };
    QHash<QObject*, PointerState> m_states;

    /// The state for @p item, created (and hooked to the item's destruction)
    /// on first use. Both configurePreviewItem and drivePointer go through
    /// this so an entry is only ever inserted, and only ever connected, once.
    PointerState& stateFor(QObject* item);
};

} // namespace PlasmaZones
