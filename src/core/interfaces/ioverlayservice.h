// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// IOverlayService — PZ compositor-overlay contract (zone highlights, selector,
// shader preview, snap assist).
//
// Split out of interfaces.h. Not a candidate for phosphor-zones extraction —
// overlays are a PZ-specific concern (KWin effect + QML renderer + Wayland
// layer-shell surfaces).

#include "plasmazones_export.h"
#include "core/types/dmabufthumbnail.h"

#include <PhosphorProtocol/ZoneTypes.h>

#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class QScreen;

namespace PhosphorZones {
class Layout;
class Zone;
}

namespace PlasmaZones {

class ISettings;

/**
 * @brief Abstract interface for overlay management
 *
 * Separates UI concerns from the daemon.
 */
class PLASMAZONES_EXPORT IOverlayService : public QObject
{
    Q_OBJECT

public:
    explicit IOverlayService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~IOverlayService() override;

    virtual bool isVisible() const = 0;
    virtual void show() = 0;
    virtual void showAtPosition(int cursorX, int cursorY) = 0; // For Wayland - uses cursor coords from KWin
    virtual void hide() = 0;
    virtual void toggle() = 0;

    virtual void updateLayout(PhosphorZones::Layout* layout) = 0;
    virtual void updateSettings(ISettings* settings) = 0;
    virtual void updateGeometries() = 0;

    // PhosphorZones::Zone highlighting for overlay display
    virtual void highlightZone(const QString& zoneId) = 0;
    virtual void highlightZones(const QStringList& zoneIds) = 0;
    virtual void clearHighlight() = 0;

    // Mid-drag idle: blank the overlay's shader output (clear zones + highlights)
    // WITHOUT destroying the underlying QQuickWindow. Used when a drag's activation
    // trigger is released mid-drag. The full hide() path pays a Vulkan swap-chain
    // teardown that blocks the main thread (~QQuickWindow waits on the scene graph
    // render thread), which — combined with modifier-key thrash during a drag —
    // stalled D-Bus dispatch long enough for kwin-effect endDrag calls to time out.
    // Callers must pair with refreshFromIdle() before the overlay needs to render
    // zones again.
    virtual void setIdleForDragPause() = 0;

    // Forget the last-shown screen. Pair with setIdleForDragPause() when the
    // cursor moves onto a screen that has no snapping context (disabled, or
    // its zone layout suppressed): the drag pipeline gates such screens out
    // before showAtPosition is ever called, so without this the service keeps
    // pointing at the PREVIOUSLY shown screen and the next refreshFromIdle()
    // re-lights it — zones flashing on the wrong monitor (#724).
    virtual void forgetCurrentScreen() = 0;

    // Counterpart to setIdleForDragPause(): re-push the current zone data to
    // overlay windows so the shader starts drawing zones again. Cheap because
    // the labels-texture build path is hash-cached on unchanged inputs.
    virtual void refreshFromIdle() = 0;

    // Drop-target indicator for a scrolling drag re-insert: paints the slot
    // the dragged window would land in, in absolute px on the named screen.
    // An invalid or empty rect hides it. Display-only — it takes no input,
    // because it is drawn underneath a cursor that is mid-drag.
    //
    // The drag pipeline pushes this because the scroll engine defers structure
    // to the drop, so unlike autotile's live restructure nothing in the strip
    // moves to show where the window is going.
    // @p animate distinguishes a TARGET CHANGE (true — the transitions exist
    // to make one legible) from a scroll-tracking re-projection (false). No
    // default argument on purpose: a default on a virtual binds statically to
    // the declared type, so a caller holding the concrete service would
    // silently get a different one.
    virtual void updateScrollDropIndicator(const QString& screenId, const QRect& rect, bool animate) = 0;

    // Per-DRAG drop-indicator colour overrides, resolved from the dragged
    // window's rules at drag start and cleared with an empty map when the drag
    // ends. Keyed by the QML property names the slot reads. On the interface
    // rather than the concrete service because the DRAG adaptor is what
    // resolves them, and it holds this interface.
    //
    // Only the per-window half is here. The per-CONTEXT map is pushed by the
    // daemon's own scrolling re-derive, which holds the concrete service, so
    // widening the interface for it would buy nothing.
    virtual void setScrollDropIndicatorWindowOverrides(const QVariantMap& overrides) = 0;

    // PhosphorZones::Zone selector methods
    virtual bool isZoneSelectorVisible() const = 0;
    virtual void showZoneSelector(const QString& targetScreenId = QString()) = 0;
    virtual void hideZoneSelector() = 0;
    virtual void updateSelectorPosition(int cursorX, int cursorY) = 0;
    virtual void scrollZoneSelector(int angleDeltaY) = 0;

    // Mouse position for shader effects (updated during window drag)
    virtual void updateMousePosition(int cursorX, int cursorY) = 0;

    // Filtered layout count in the LAYOUT/TEMPLATE vocabulary — the candidate
    // list the picker and cycle shortcuts consult (their empty-list gates test
    // this against zero). Never answers strip cards; selectorCardCount is the
    // popup-sizing question.
    virtual int visibleLayoutCount(const QString& screenId) const = 0;

    // Number of cells the drag-selector popup renders on @p screenId — strip
    // cards (floored at 1 for the empty-strip cell) on strip-selector screens,
    // the filtered layout count everywhere else. The trigger-edge sizing
    // contract: isNearTriggerEdge consults this so the keep-visible band and
    // the rendered popup can never disagree. Deliberately a separate virtual
    // from visibleLayoutCount — folding the strip answer into that one made
    // the shortcut gates' zero test unreachable on scrolling screens.
    virtual int selectorCardCount(const QString& screenId) const = 0;

    // Strip-selector screens only: one work-area width share per rendered
    // strip card, in card order (empty everywhere else, and for an empty
    // strip). The other half of the trigger-edge sizing contract above:
    // strip cards render variable-width, so isNearTriggerEdge needs the
    // fractions, not just the count, for its computeZoneSelectorLayout call
    // to reproduce the real bar width.
    virtual QList<qreal> selectorStripFractions(const QString& screenId) const = 0;

    // PhosphorZones::Zone selector selection tracking
    virtual bool hasSelectedZone() const = 0;
    virtual QString selectedLayoutId() const = 0;
    virtual int selectedZoneIndex() const = 0;
    virtual QRect getSelectedZoneGeometry(QScreen* screen) const = 0;
    virtual QRect getSelectedZoneGeometry(const QString& screenId) const = 0;
    virtual void clearSelectedZone() = 0;

    // Strip-mode selector selection tracking (scrolling screens whose engine
    // provides the drag-insert selector). The target is an int-only mirror
    // of IPlacementEngine::DragInsertTarget's index fields — columnIndex/
    // newColumn map to primary/newSlot, tileIndex to secondary (-1 appends);
    // the presentation-only leadingEdge tag is deliberately not carried
    // (commit ignores it) — kept engine-header-free like the rest of this
    // interface. Exactly one of the zone triple and this target is ever set;
    // clearSelectedZone clears both.
    // Default-implemented as "no strip selection" so implementations without
    // strip support stay source-compatible.
    struct SelectorStripTarget
    {
        int columnIndex = -1;
        int tileIndex = -1;
        bool newColumn = false;

        bool isValid() const
        {
            return columnIndex >= 0;
        }
    };
    virtual bool hasSelectedStripTarget() const
    {
        return false;
    }
    virtual SelectorStripTarget selectedStripTarget() const
    {
        return {};
    }
    virtual QString selectedStripTargetScreenId() const
    {
        return {};
    }
    /// Rebuild the strip cards and drop the strip selection on @p screenId.
    /// The drag adaptor calls this at the drag-insert preview begin/cancel
    /// boundaries — the only moments the frozen (DETACH-ONCE) strip the
    /// cards mirror can change shape mid-drag.
    virtual void refreshStripSelector(const QString& screenId)
    {
        Q_UNUSED(screenId)
    }
    /// The window id of the live drag, so the strip cards can exclude a
    /// not-yet-detached drag window. Set at drag start, cleared (empty id)
    /// at drag end.
    virtual void setActiveDragWindowId(const QString& windowId)
    {
        Q_UNUSED(windowId)
    }

    // Shader preview overlay (editor dialog - dedicated window avoids multi-pass clear)
    virtual void showShaderPreview(int x, int y, int width, int height, const QString& screenId,
                                   const QString& shaderId, const QString& shaderParamsJson,
                                   const QString& zonesJson) = 0;
    virtual void updateShaderPreview(int x, int y, int width, int height, const QString& shaderParamsJson,
                                     const QString& zonesJson) = 0;
    virtual void hideShaderPreview() = 0;

    // Snap Assist overlay (window picker after snapping)
    virtual void showSnapAssist(const QString& screenId, const PhosphorProtocol::EmptyZoneList& emptyZones,
                                const PhosphorProtocol::SnapAssistCandidateList& candidates) = 0;
    virtual void hideSnapAssist() = 0;
    virtual bool isSnapAssistVisible() const = 0;
    /// Deliver a thumbnail as raw ARGB32 pixels. @p pixels MUST be exactly
    /// @p width * @p height * 4 bytes, tightly packed (no row padding),
    /// non-premultiplied. Implementations must validate dimensions and the
    /// pixel-buffer size before reconstructing a QImage.
    ///
    /// @return true iff the implementation stored the thumbnail in its
    ///         cache. False on validation failure or post-shutdown teardown.
    ///         Callers (compositor effects) use this to decide whether to
    ///         mark the handle as recently-posted; treating a false reply
    ///         as success would strand future snap-assists on icons because
    ///         the dedup window would skip a re-capture for an entry the
    ///         daemon never actually stored.
    virtual bool setSnapAssistThumbnail(const QString& compositorHandle, int width, int height,
                                        const QByteArray& pixels) = 0;

    /// Deliver a thumbnail as an imported DMA-BUF (zero-copy GPU path), the
    /// alternative to the raw-ARGB32 @ref setSnapAssistThumbnail above. @p desc
    /// carries a borrowed single-plane dma-buf fd plus its DRM
    /// format/modifier/stride; the fd is valid only for the duration of this
    /// call. The implementation imports it into a GPU texture for display.
    ///
    /// @return true iff the implementation imported and stored the thumbnail.
    ///         False when the dma-buf path is unavailable (experimental gate
    ///         off, driver/RHI backend unsupported, or import failed) — the
    ///         caller MUST then fall back to @ref setSnapAssistThumbnail so a
    ///         preview still appears.
    virtual bool setWindowThumbnailDmabuf(const QString& compositorHandle, const DmabufThumbnailDesc& desc) = 0;

    // Layout picker overlay (interactive layout browser)
    virtual void hideLayoutPicker() = 0;
    virtual bool isLayoutPickerVisible() const = 0;

    // Shortcut cheatsheet overlay (display-only shortcut reference card).
    // Mirrors the layout-picker split: the interface carries the hide/query
    // surface an interface holder needs to arbitrate Escape-consuming
    // modals; the model-typed show/refresh calls stay on the concrete
    // OverlayService (daemon-mediated push, like showLayoutPicker's data
    // plumbing).
    virtual void hideCheatsheet() = 0;
    virtual bool isCheatsheetVisible() const = 0;

    /**
     * @brief The wl_surface object id of every screen's live tab-indicator
     *        surface, keyed by effective screen id.
     *
     * A replay source, not a second announcement channel. scrollTabSurfaceChanged
     * fires only on change, and the surfaces outlive anything that merely
     * re-reads them — so a consumer created AFTER a surface was announced has
     * no way to learn about it, and the change gate means it never will. The
     * D-Bus adaptor is exactly that consumer: the daemon deletes and re-news
     * the whole adaptor set on every init(), while this service is
     * constructor-owned and survives, so a fresh adaptor starts empty and the
     * gate suppresses every re-announce.
     *
     * @return screen id to object id; empty when no screen has one.
     */
    virtual QHash<QString, quint32> liveScrollTabSurfaces() const = 0;

Q_SIGNALS:
    void visibilityChanged(bool visible);
    void zoneActivated(PhosphorZones::Zone* zone);
    void multiZoneActivated(const QVector<PhosphorZones::Zone*>& zones);
    void zoneSelectorVisibilityChanged(bool visible);

    /**
     * @brief Emitted when user selects a window from Snap Assist to snap to a zone
     * @param windowId Window identifier to snap
     * @param zoneId Target zone UUID
     * @param geometryJson JSON {x, y, width, height} - used for display only; daemon fetches authoritative geometry
     * @param screenId Screen where the zone is for geometry lookup
     */
    void snapAssistWindowSelected(const QString& windowId, const QString& zoneId, const QString& geometryJson,
                                  const QString& screenId);

    /**
     * @brief A tab of the scrolling tab indicator was clicked.
     *
     * Carries only the canonical window id: the consumer focuses that window,
     * and the strip follows through its ordinary focus notification, so this
     * path never needs to know which screen or column the tab belonged to.
     *
     * @param windowId Canonical id of the window whose tab was clicked
     */
    void scrollTabActivated(const QString& windowId);

    /**
     * @brief The Wayland surface carrying @p screenId's tab indicators changed.
     *
     * Relayed to the compositor, which slides that surface with the scrolling
     * strip so the indicators travel with the columns they label. The
     * compositor cannot work out which surface it is on its own: every one of
     * the daemon's overlays reports the same window class, and a layer
     * surface's scope, role and geometry are either identical between them or
     * not exposed per surface. The protocol object id is the one handle both
     * sides can name.
     *
     * @param screenId  Effective screen id whose indicator surface this is
     * @param surfaceId wl_surface protocol object id, or 0 when the surface is
     *                  gone. Zero MUST be sent before the surface is destroyed:
     *                  Wayland reuses object ids, so a stale registration can
     *                  come to name an unrelated surface.
     */
    void scrollTabSurfaceChanged(const QString& screenId, quint32 surfaceId);

    /**
     * @brief Load-bearing signal (NOT merely informational: shortcuts_wiring.cpp's
     * handler is the ONLY binder of the shared Escape grab for the snap-assist
     * phase, which drop.cpp defers to it, and overlayadaptor.cpp relays it onto
     * the bus — removing or reordering the emission leaves snap assist
     * un-dismissable). Also emitted when the Snap Assist overlay is shown.
     *
     * The kwin-effect drives thumbnail capture independently as part of the
     * `showSnapAssist` call sequence (not in response to this signal); the
     * signal is exposed for external observers (KCMs, debugging tools).
     */
    void snapAssistShown(const QString& screenId, const PhosphorProtocol::EmptyZoneList& emptyZones,
                         const PhosphorProtocol::SnapAssistCandidateList& candidates);

    /**
     * @brief Emitted when Snap Assist overlay is dismissed (by selection, Escape, or any other means).
     * WindowDragAdaptor subscribes to unregister the KGlobalAccel Escape shortcut.
     */
    void snapAssistDismissed();

    /**
     * @brief Emitted when the idle-grace trim fires, after clearing every
     * live snap-assist thumbnail store (the pixel LRU and, when its
     * provider is live, the dma-buf descriptor store).
     *
     * Load-bearing for cache coherence with the kwin-effect: the producer
     * keeps a recently-posted dedup set mirroring the daemon's cache
     * residency, and its skip path re-promotes entries — so without an
     * explicit invalidation edge, a trim left the two sides permanently
     * desynchronised (the effect skipped re-capture forever and snap-assist
     * fell back to icons until a daemon restart). OverlayAdaptor relays this
     * onto the bus; the effect drops its dedup set in response.
     */
    void snapAssistThumbnailCacheTrimmed();

    /**
     * @brief Emitted when a layout is selected from the layout picker overlay
     * @param layoutId The UUID of the selected layout
     */
    /// @p screenId is the screen the picker was BOUND to when the pick was
    /// made — captured before the hide clears the binding, because the
    /// controller's current screen is a single mutable slot that desktop
    /// switches and cycle presses on other screens can retarget while the
    /// picker sits open.
    void layoutPickerSelected(const QString& layoutId, const QString& screenId);

    /**
     * @brief Emitted when the layout picker overlay is dismissed for any
     * reason (selection, backdrop click, Escape, screen-removed teardown).
     * Distinct from `layoutPickerSelected` (only fires on explicit pick).
     * Daemon subscribes to release the shared cancel-overlay Escape
     * registration that was active for the picker's lifetime.
     */
    void layoutPickerDismissed();

    /**
     * @brief Emitted when the shortcut cheatsheet overlay is dismissed for
     * any reason (re-press of the toggle, backdrop click, Escape,
     * screen-removed teardown). Daemon subscribes to release the
     * cheatsheet's dedicated Escape ad-hoc grab.
     */
    void cheatsheetDismissed();
};

} // namespace PlasmaZones
