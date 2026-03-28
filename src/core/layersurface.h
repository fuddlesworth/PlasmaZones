// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMargins>
#include <QObject>
#include <QScreen>
#include <QWindow>

#include "plasmazones_export.h"

namespace PlasmaZones {

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
    /// The window must not yet be shown (call before show()).
    /// Ownership follows QObject parent (window).
    static LayerSurface* get(QWindow* window);

Q_SIGNALS:
    void layerChanged();
    void anchorsChanged();
    void exclusionZoneChanged();
    void keyboardInteractivityChanged();
    void marginsChanged();
    void screenChanged();

private:
    explicit LayerSurface(QWindow* window);

    QWindow* m_window = nullptr;
    Layer m_layer = LayerTop;
    Anchors m_anchors;
    int32_t m_exclusiveZone = -1;
    KeyboardInteractivity m_keyboard = KeyboardInteractivityNone;
    QString m_scope;
    QScreen* m_screen = nullptr;
    QMargins m_margins;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(LayerSurface::Anchors)

} // namespace PlasmaZones
