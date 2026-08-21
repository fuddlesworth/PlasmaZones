// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMargins>
#include <QMetaObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <optional>

#include <PhosphorLayer/Role.h>

namespace PhosphorOverlay {
class ShellState;
} // namespace PhosphorOverlay

class QQuickItem;
class QQuickWindow;
class QScreen;

namespace PhosphorLayer {
class Surface;
} // namespace PhosphorLayer

namespace PlasmaZones {

/**
 * @brief Per-screen overlay state, grouping window pointers, physical screen
 * references, and geometry that were previously stored in parallel QHash maps.
 *
 * Namespace-scope value type consumed by OverlayService (aliased there as
 * OverlayService::PerScreenOverlayState). The slot accessors are defined in
 * overlayservice.cpp - they resolve through the shell's generic slot map.
 */
struct PerScreenOverlayState
{
    // Library-owned shell-host state: borrowed pointer into the
    // PhosphorOverlay::ShellHost-owned map. Set by the
    // @c ensurePassiveShellFor shim after @c ShellHost::ensureShell
    // materializes a lib-side entry; the pointer is stable across
    // map operations because ShellHost stores entries through
    // raw owning pointers. nullptr until the first ensure call wires
    // it up.
    //
    // The pointee is the single source of truth for shell mechanism
    // fields (shellSurface / shellWindow / physScreen / slots);
    // writes route through @c ShellHost::ensureShell /
    // @c ShellHost::destroyShell.
    //
    // Per-content "is this slot wired up?" sentinels (overlayPhysScreen
    // / zoneSelectorPhysScreen / ...) live below - they're PZ-content
    // state, not lib-mechanism state.
    PhosphorOverlay::ShellState* shell = nullptr;

    /// Convenience accessors that resolve the named PZ slot through
    /// the shell's generic slot map. Returns nullptr when no shell
    /// is wired up, or when the QML aliasing in PassiveOverlayShell.qml
    /// did not expose the requested slot Item.
    QQuickItem* osdSlot() const;
    QQuickItem* snapAssistSlot() const;
    QQuickItem* layoutPickerSlot() const;
    QQuickItem* zoneSelectorSlot() const;
    QQuickItem* mainOverlaySlot() const;
    QQuickItem* cheatsheetSlot() const;
    QQuickItem* scrollDropIndicatorSlot() const;

    // overlayPhysScreen != nullptr is the sentinel for "main overlay
    // mode is active on this screen" - set in createOverlayWindow,
    // cleared in destroyOverlayWindow / by the PreDestroyCallback
    // registered on m_shellHost (OverlayService::unwirePassiveShellSlots).
    QScreen* overlayPhysScreen = nullptr;
    QRect overlayGeometry;
    QMetaObject::Connection overlayGeomConnection; ///< geometryChanged connection for overlay
    // Cache key for the last successful labelsTexture rebuild on this window.
    // Hashes (size, showNumbers, font settings, per-zone {number,x,y,w,h}). When
    // updateLabelsTextureForWindow is called with the same hash, both the sparse
    // glyph-tile payload rebuild AND the labelsTexture property write (with its
    // value compare) are skipped. 0 = never computed / cache invalid.
    quint64 labelsTextureHash = 0;
    QScreen* zoneSelectorPhysScreen = nullptr;
    /// Intended geometry of the zone selector inside its shell. On
    /// Wayland LayerShell, QWindow::geometry() is unreliable until
    /// the compositor acknowledges surface position; this field
    /// stores the geometry we requested so hit-testing in
    /// updateSelectorPosition() has a stable reference.
    QRect zoneSelectorGeometry;
};

/// Per-screen layout-family filter used for the zone selector. `manual`
/// enables PhosphorZones layout entries, `autotile` enables algorithm
/// previews, and `templates` enables native scrolling-template cards. The
/// first two default true and the third false, so the unnarrowed answer is
/// "every non-template family". The resolver narrows to a single family
/// when the screen has an explicit assignment.
struct LayoutIncludeFlags
{
    bool manual = true;
    bool autotile = true;
    /// Native scrolling-template entries — default false: only a
    /// live-Templates (scrolling) screen offers them, and it offers ONLY
    /// them.
    bool templates = false;
};

/// Shared property-push parameters for layout-OSD content. Used by both
/// OverlayService::showLayoutOsdImpl (PhosphorZones::Layout* path) and the
/// showLayoutOsd(QString,...) overload (autotile / pre-built-zones
/// path). The struct lets the two callers diverge only on the values
/// they compute, not on the property-write sequence
/// (OverlayService::pushLayoutOsdContent).
struct LayoutOsdContentParams
{
    QString id; ///< layoutId - UUID for manual layouts, "autotile:..." for algorithms
    QString name; ///< layoutName as shown in the OSD label
    QVariantList zones; ///< pre-built zone variant list (empty for locked-with-no-zones)
    int category = 0; ///< PhosphorZones::LayoutCategory enum value
    bool autoAssign = false; ///< per-layout autoAssign flag (raw, pre-OR with global)
    bool globalAutoAssign = false; ///< master "auto-assign for all layouts" toggle (#370)
    bool locked = false; ///< draws lock badge + " (Locked)" suffix
    /// True on a live-Templates (scrolling) screen: the layout shown is the
    /// screen's sizing TEMPLATE, and the OSD captions it "Column template"
    /// so a template pick never reads as a snap-layout switch.
    bool isTemplate = false;
    qreal screenAspectRatio = 16.0 / 9.0;
    QString aspectRatioClass = QStringLiteral("any");
    bool showMasterDot = false;
    bool producesOverlappingZones = false;
    QString zoneNumberDisplay = QStringLiteral("all");
    int masterCount = 1;
    QString screenId; ///< effective screen id, resolves the context overlay-appearance override
};

/**
 * @brief Parameters for OverlayService::createLayerSurface, kept as a
 *        named-member aggregate so call sites read top-to-bottom rather
 *        than relying on positional arg ordering. Required fields up top,
 *        optional below; Qt6 designated-init form is
 *        `LayerSurfaceParams{.qmlUrl=...}`.
 */
struct LayerSurfaceParams
{
    // Required.
    QUrl qmlUrl = {}; ///< QML file (Window-rooted - PZ's overlay QML convention)
    QScreen* screen = nullptr; ///< target screen (physical; virtual-screen positioning is the caller's job)
    PhosphorLayer::Role role = {}; ///< protocol-level preset (see phosphor_roles.h)
    const char* windowType = ""; ///< debug/telemetry label

    // Optional - explicit `= {}` suppresses GCC's
    // -Wmissing-field-initializers warning on designated-init call sites
    // that omit these. (For `QUrl` / `Role` above the same is true; we
    // just want one consistent style across the struct.)
    QVariantMap windowProperties = {}; ///< Applied as dynamic properties before QML loads.
    std::optional<PhosphorLayer::Anchors> anchorsOverride =
        std::nullopt; ///< Overrides the role's anchors (virtual-screen positioning).
    std::optional<QMargins> marginsOverride =
        std::nullopt; ///< Overrides the role's margins (virtual-screen positioning).
    bool keepMappedOnHide = false; ///< See SurfaceConfig::keepMappedOnHide.
    /// Initial swapchain size committed during warm-up. Empty (default)
    /// → screen geometry, which guarantees a non-zero buffer for every
    /// anchor configuration but allocates a full-screen Vulkan swapchain
    /// (~25 MB at 4K × triple buffer on NVIDIA). Non-empty → a swapchain
    /// proportional to the passed size. The eventual visible size is
    /// still driven by per-show setWidth/setHeight; this only governs
    /// the warm-up commit. Forwarded verbatim to
    /// SurfaceConfig::initialSize, including the empty-as-unset sentinel.
    QSize initialSize = {};
};

/**
 * @brief Everything OverlayService::prepareLayoutOsdWindow resolves for an
 *        OSD show: the shell window/surface/slot, the backing physical
 *        screen, the (virtual-screen-aware) geometry, its clamped aspect
 *        ratio and the effective screen id the per-screen context lookups
 *        must use. Returned as std::optional (nullopt = preparation failed),
 *        replacing the earlier seven-out-parameter signature — same
 *        params-struct convention as LayerSurfaceParams above.
 */
struct PreparedLayoutOsdWindow
{
    QQuickWindow* window = nullptr;
    PhosphorLayer::Surface* surface = nullptr;
    QQuickItem* osdSlot = nullptr;
    QRect screenGeom = {};
    qreal aspectRatio = 0.0;
    QString effectiveScreenId = {};
};

} // namespace PlasmaZones
