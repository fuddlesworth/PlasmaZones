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

#include <PhosphorProtocol/WireTypes.h>

#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

class QScreen;

namespace PhosphorZones {
class Layout;
class Zone;
}

namespace PlasmaZones {

using PhosphorProtocol::EmptyZoneList;
using PhosphorProtocol::SnapAssistCandidateList;

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

    // Counterpart to setIdleForDragPause(): re-push the current zone data to
    // overlay windows so the shader starts drawing zones again. Cheap because
    // the labels-texture build path is hash-cached on unchanged inputs.
    virtual void refreshFromIdle() = 0;

    // PhosphorZones::Zone selector methods
    virtual bool isZoneSelectorVisible() const = 0;
    virtual void showZoneSelector(const QString& targetScreenId = QString()) = 0;
    virtual void hideZoneSelector() = 0;
    virtual void updateSelectorPosition(int cursorX, int cursorY) = 0;
    virtual void scrollZoneSelector(int angleDeltaY) = 0;

    // Mouse position for shader effects (updated during window drag)
    virtual void updateMousePosition(int cursorX, int cursorY) = 0;

    // Filtered layout count (matches what the zone selector actually displays)
    virtual int visibleLayoutCount(const QString& screenId) const = 0;

    // PhosphorZones::Zone selector selection tracking
    virtual bool hasSelectedZone() const = 0;
    virtual QString selectedLayoutId() const = 0;
    virtual int selectedZoneIndex() const = 0;
    virtual QRect getSelectedZoneGeometry(QScreen* screen) const = 0;
    virtual QRect getSelectedZoneGeometry(const QString& screenId) const = 0;
    virtual void clearSelectedZone() = 0;

    // Shader preview overlay (editor dialog - dedicated window avoids multi-pass clear)
    virtual void showShaderPreview(int x, int y, int width, int height, const QString& screenId,
                                   const QString& shaderId, const QString& shaderParamsJson,
                                   const QString& zonesJson) = 0;
    virtual void updateShaderPreview(int x, int y, int width, int height, const QString& shaderParamsJson,
                                     const QString& zonesJson) = 0;
    virtual void hideShaderPreview() = 0;

    // Snap Assist overlay (window picker after snapping)
    virtual void showSnapAssist(const QString& screenId, const EmptyZoneList& emptyZones,
                                const SnapAssistCandidateList& candidates) = 0;
    virtual void hideSnapAssist() = 0;
    virtual bool isSnapAssistVisible() const = 0;
    virtual void setSnapAssistThumbnail(const QString& compositorHandle, const QString& dataUrl) = 0;

    // Layout picker overlay (interactive layout browser)
    virtual void hideLayoutPicker() = 0;
    virtual bool isLayoutPickerVisible() const = 0;

Q_SIGNALS:
    void visibilityChanged(bool visible);
    void zoneActivated(PhosphorZones::Zone* zone);
    void multiZoneActivated(const QVector<PhosphorZones::Zone*>& zones);
    void zoneSelectorVisibilityChanged(bool visible);
    void zoneSelectorZoneSelected(int zoneIndex);

    /**
     * @brief Emitted when a manual layout is selected from the zone selector
     * @param layoutId The UUID of the selected manual layout
     * @param screenId The screen where the selection was made
     */
    void manualLayoutSelected(const QString& layoutId, const QString& screenId);

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
     * @brief Emitted when Snap Assist overlay is shown. KWin script subscribes to create thumbnails.
     */
    void snapAssistShown(const QString& screenId, const EmptyZoneList& emptyZones,
                         const SnapAssistCandidateList& candidates);

    /**
     * @brief Emitted when Snap Assist overlay is dismissed (by selection, Escape, or any other means).
     * WindowDragAdaptor subscribes to unregister the KGlobalAccel Escape shortcut.
     */
    void snapAssistDismissed();

    /**
     * @brief Emitted when a layout is selected from the layout picker overlay
     * @param layoutId The UUID of the selected layout
     */
    void layoutPickerSelected(const QString& layoutId);

    /**
     * @brief Emitted when the layout picker overlay is dismissed for any
     * reason (selection, backdrop click, Escape, screen-removed teardown).
     * Distinct from `layoutPickerSelected` (only fires on explicit pick).
     * Daemon subscribes to release the shared cancel-overlay Escape
     * registration that was active for the picker's lifetime.
     */
    void layoutPickerDismissed();

    /**
     * @brief Emitted when an autotile algorithm layout is selected from the zone selector
     * @param algorithmId The algorithm identifier (e.g. "master-stack")
     * @param screenId The screen where the selection was made
     */
    void autotileLayoutSelected(const QString& algorithmId, const QString& screenId);
};

} // namespace PlasmaZones
