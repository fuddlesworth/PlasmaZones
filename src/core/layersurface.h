// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

    void setLayer(Layer layer);
    Layer layer() const;

    void setAnchors(Anchors anchors);
    Anchors anchors() const;

    void setExclusiveZone(int32_t zone);
    int32_t exclusiveZone() const;

    void setKeyboardInteractivity(KeyboardInteractivity interactivity);
    KeyboardInteractivity keyboardInteractivity() const;

    void setScope(const QString& scope);
    QString scope() const;

    void setScreen(QScreen* screen);
    QScreen* screen() const;

    void setMargins(const QMargins& margins);
    QMargins margins() const;

    /// Get or create a LayerSurface for the given window.
    /// For first-time creation, the window must not yet be shown (call before show()).
    /// Subsequent calls on the same window return the existing LayerSurface even
    /// after show() — this is safe and expected (e.g. for updating margins/anchors).
    /// Ownership follows QObject parent (window).
    static LayerSurface* get(QWindow* window);

    /// Returns true if the compositor supports zwlr_layer_shell_v1.
    /// Only valid after QGuiApplication is constructed and the QPA plugin has initialized.
    static bool isSupported();

Q_SIGNALS:
    void layerChanged();
    void anchorsChanged();
    void exclusiveZoneChanged();
    void keyboardInteractivityChanged();
    void scopeChanged();
    void marginsChanged();
    void screenChanged();

    /// Emitted after any property setter completes. The QPA LayerShellWindow
    /// connects to this to push changes to the compositor immediately,
    /// rather than waiting for the next configure event.
    void propertiesChanged();

private:
    explicit LayerSurface(QWindow* window);

    QWindow* m_window = nullptr;
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
