// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorshell_export.h>

#include <utility>

#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QWindow>

namespace PhosphorShell {

/// Property keys used by LayerSurface ↔ QPA plugin communication.
namespace LayerSurfaceProps {
inline constexpr const char* IsLayerShell = "_ps_layer_shell";
inline constexpr const char* Surface = "_ps_layer_shell_surface";
inline constexpr const char* Layer = "_ps_layer";
inline constexpr const char* Anchors = "_ps_anchors";
inline constexpr const char* ExclusiveZone = "_ps_exclusive_zone";
inline constexpr const char* Keyboard = "_ps_keyboard";
inline constexpr const char* Scope = "_ps_scope";
inline constexpr const char* MarginsLeft = "_ps_margins_left";
inline constexpr const char* MarginsTop = "_ps_margins_top";
inline constexpr const char* MarginsRight = "_ps_margins_right";
inline constexpr const char* MarginsBottom = "_ps_margins_bottom";
} // namespace LayerSurfaceProps

/// Wayland layer-shell surface backed by zwlr_layer_shell_v1.
/// Pure Qt API — no Wayland types exposed.
class PHOSPHORSHELL_EXPORT LayerSurface : public QObject
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

    static constexpr Anchors AnchorAll = Anchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);

    // ── Mutable properties (can be changed after show()) ─────────────
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

    // ── Immutable properties (must set before show()) ────────────────
    void setScope(const QString& scope);
    QString scope() const;

    void setScreen(QScreen* screen);
    QScreen* screen() const;

    /// Get or create a LayerSurface for the given window (call before first show()).
    static LayerSurface* get(QWindow* window);

    /// Retrieve existing LayerSurface for the window, or nullptr.
    static LayerSurface* find(QWindow* window);

    /// True if the compositor supports zwlr_layer_shell_v1.
    static bool isSupported();

    /// Compute layer-shell size: 0 for axes anchored to both edges.
    static std::pair<uint32_t, uint32_t> computeLayerSize(Anchors anchors, const QSize& windowSize);

Q_SIGNALS:
    void layerChanged();
    void anchorsChanged();
    void exclusiveZoneChanged();
    void keyboardInteractivityChanged();
    void scopeChanged();
    void marginsChanged();
    void screenChanged();
    void propertiesChanged();

public:
    /// RAII guard: suppresses propertiesChanged() until destroyed.
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
    int32_t m_exclusiveZone = -1;
    KeyboardInteractivity m_keyboard = KeyboardInteractivityNone;
    QString m_scope;
    QPointer<QScreen> m_screen;
    QMargins m_margins;
    QMetaObject::Connection m_destroyedConnection;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(LayerSurface::Anchors)

} // namespace PhosphorShell

Q_DECLARE_METATYPE(PhosphorShell::LayerSurface*)
