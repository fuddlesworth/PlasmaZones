// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>

#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QWindow>

#include "plasmazones_export.h"

namespace PlasmaZones {

/// Property keys used by LayerSurface ↔ QPA plugin communication.
/// Centralized here to prevent key mismatches between the two layers.
namespace LayerSurfaceProps {
inline constexpr const char* IsLayerShell = "_pz_layer_shell";
inline constexpr const char* Surface = "_pz_layer_shell_surface";
inline constexpr const char* Layer = "_pz_layer";
inline constexpr const char* Anchors = "_pz_anchors";
inline constexpr const char* ExclusiveZone = "_pz_exclusive_zone";
inline constexpr const char* Keyboard = "_pz_keyboard";
inline constexpr const char* Scope = "_pz_scope";
inline constexpr const char* MarginsLeft = "_pz_margins_left";
inline constexpr const char* MarginsTop = "_pz_margins_top";
inline constexpr const char* MarginsRight = "_pz_margins_right";
inline constexpr const char* MarginsBottom = "_pz_margins_bottom";
} // namespace LayerSurfaceProps

/// Drop-in replacement for LayerShellQt::Window backed by a custom QPA
/// shell integration plugin that speaks zwlr_layer_shell_v1 directly.
/// No KDE dependency required.
class PLASMAZONES_EXPORT LayerSurface : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Layer layer READ layer WRITE setLayer NOTIFY layerChanged)
    Q_PROPERTY(Anchors anchors READ anchors WRITE setAnchors NOTIFY anchorsChanged)
    Q_PROPERTY(int32_t exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged)
    Q_PROPERTY(KeyboardInteractivity keyboardInteractivity READ keyboardInteractivity WRITE setKeyboardInteractivity
                   NOTIFY keyboardInteractivityChanged)
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY scopeChanged)
    Q_PROPERTY(QScreen* screen READ screen WRITE setScreen NOTIFY screenChanged)
    Q_PROPERTY(QMargins margins READ margins WRITE setMargins NOTIFY marginsChanged)

public:
    ~LayerSurface() override;

    enum Layer {
        LayerBackground = 0,
        LayerBottom = 1,
        LayerTop = 2,
        LayerOverlay = 3,
    };
    Q_ENUM(Layer)

    enum KeyboardInteractivity {
        KeyboardInteractivityNone = 0,
        KeyboardInteractivityExclusive = 1,
        KeyboardInteractivityOnDemand = 2,
    };
    Q_ENUM(KeyboardInteractivity)

    enum Anchor {
        AnchorNone = 0,
        AnchorTop = 1,
        AnchorBottom = 2,
        AnchorLeft = 4,
        AnchorRight = 8,
    };
    Q_DECLARE_FLAGS(Anchors, Anchor)
    Q_FLAG(Anchors)

    /// Convenience constant: anchor to all four edges (full-screen overlay).
    static constexpr Anchors AnchorAll = Anchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);

    // ── Mutable properties (can be changed after show()) ──────────────
    // These push protocol updates to the compositor via propertiesChanged().

    void setLayer(Layer layer);
    Layer layer() const;

    void setAnchors(Anchors anchors);
    Anchors anchors() const;

    void setExclusiveZone(int32_t zone);
    int32_t exclusiveZone() const;

    void setKeyboardInteractivity(KeyboardInteractivity interactivity);
    KeyboardInteractivity keyboardInteractivity() const;

    void setMargins(const QMargins& margins);
    QMargins margins() const;

    // ── Immutable properties (baked into get_layer_surface at creation) ─
    // Must be set BEFORE show(). Calling after show() logs a warning and
    // returns without effect — the protocol does not support changing these.

    void setScope(const QString& scope);
    QString scope() const;

    void setScreen(QScreen* screen);
    /// Returns the target screen, or nullptr if unset / screen was unplugged.
    /// After hot-unplug, QPointer ensures this returns nullptr rather than dangling.
    /// Note: the layer surface itself remains bound to the (now-gone) wl_output —
    /// the compositor will either close the surface or leave it on the remaining output.
    /// Callers must not cache the result across event loop iterations.
    QScreen* screen() const;

    /// Get or create a LayerSurface for the given window.
    /// For first-time creation, the window must not yet be shown (call before show()).
    /// Subsequent calls on the same window return the existing LayerSurface even
    /// after show() — this is safe and expected (e.g. for updating margins/anchors).
    /// Ownership follows QObject parent (window).
    static LayerSurface* get(QWindow* window);

    /// Retrieve the existing LayerSurface for the given window, or nullptr if none.
    /// Unlike get(), this never creates a new LayerSurface — use for post-show()
    /// property updates where the surface must already exist.
    static LayerSurface* find(QWindow* window);

    /// Returns true if the compositor supports zwlr_layer_shell_v1.
    /// Only valid after QGuiApplication is constructed and the QPA plugin has initialized.
    static bool isSupported();

    /// Compute layer-shell size: 0 for axes anchored to both edges, clamped otherwise.
    /// Pure static helper shared between QPA plugin and tests.
    static std::pair<uint32_t, uint32_t> computeLayerSize(Anchors anchors, const QSize& windowSize);

Q_SIGNALS:
    void layerChanged();
    void anchorsChanged();
    void exclusiveZoneChanged();
    void keyboardInteractivityChanged();
    void scopeChanged();
    void marginsChanged();
    void screenChanged();

    /// Emitted after any property setter completes (unless batching is active).
    /// The QPA LayerShellWindow connects to this to push changes to the compositor
    /// immediately, rather than waiting for the next configure event.
    void propertiesChanged();

public:
    /// Suppress propertiesChanged() emission until the guard is destroyed.
    /// Use when calling multiple setters in sequence to avoid N round-trips.
    /// Uses QPointer to safely handle LayerSurface destruction while a guard
    /// is on the stack (unlikely in practice, but prevents dangling access).
    class BatchGuard
    {
    public:
        explicit BatchGuard(LayerSurface* surface)
            : m_surface(surface)
        {
            if (m_surface) {
                ++m_surface->m_batchDepth;
            }
        }
        ~BatchGuard()
        {
            if (m_surface) {
                Q_ASSERT(m_surface->m_batchDepth > 0);
                if (--m_surface->m_batchDepth == 0 && m_surface->m_batchDirty) {
                    m_surface->m_batchDirty = false;
                    Q_EMIT m_surface->propertiesChanged();
                }
            }
        }
        BatchGuard(const BatchGuard&) = delete;
        BatchGuard& operator=(const BatchGuard&) = delete;
        BatchGuard(BatchGuard&& other) noexcept
            : m_surface(std::exchange(other.m_surface, nullptr))
        {
        }
        // Move-assign deleted: guards are stack-scoped, reassignment would lose the original surface.
        BatchGuard& operator=(BatchGuard&&) = delete;

    private:
        QPointer<LayerSurface> m_surface;
    };

private:
    friend class BatchGuard;
    explicit LayerSurface(QWindow* window);

    void emitPropertiesChanged();

    QPointer<QWindow> m_window;
    int m_batchDepth = 0;
    bool m_batchDirty = false;
    Layer m_layer = LayerTop;
    Anchors m_anchors;
    // Default -1: surface ignores exclusive zones from other surfaces (panels, bars)
    // and stretches to full output edges. Protocol default is 0 (surface is pushed
    // to avoid exclusive zones). We use -1 because PlasmaZones overlays must cover
    // the entire screen including panel areas. Callers that need the protocol default
    // (e.g. geometry sensors) must call setExclusiveZone(0) explicitly.
    int32_t m_exclusiveZone = -1;
    KeyboardInteractivity m_keyboard = KeyboardInteractivityNone;
    QString m_scope;
    QPointer<QScreen> m_screen;
    QMargins m_margins;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(LayerSurface::Anchors)

} // namespace PlasmaZones

Q_DECLARE_METATYPE(PlasmaZones::LayerSurface*)
