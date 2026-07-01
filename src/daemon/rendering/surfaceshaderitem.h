// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorRendering/ShaderEffect.h>

#include <plasmazones_rendering_export.h>
#include <QPointF>
#include <QSizeF>

QT_BEGIN_NAMESPACE
class QSGNode;
QT_END_NAMESPACE

namespace PlasmaZones {

/**
 * @brief QQuickItem for rendering a per-window surface-decoration layer with a
 *        custom shader (border / rounded corners / focus tint / glow).
 *
 * The daemon-side Qt-RHI consumer for the third shader-pack category
 * (`plasmazones/surface`), sibling to ZoneShaderItem (overlay zone backgrounds)
 * and the animation transition runtime. Like ZoneShaderItem it inherits from
 * PhosphorRendering::ShaderEffect, which provides all base shader rendering:
 * Shadertoy uniforms, custom params/colors, user textures, multipass, status,
 * and the createShaderNode() / syncBasePropertiesToNode() seam.
 *
 * Where ZoneShaderItem layers zone-array machinery on top of the base (a zone
 * data snapshot, a labels texture at binding 1, zone counts in appField0/1, and
 * an owned ZoneUniformExtension), SurfaceShaderItem layers NONE of that. A
 * surface pack is a single-pass-or-multipass fragment shader composited over one
 * live window surface; it has no zones, no per-zone glyph labels, and no
 * consumer escape-hatch int slots. The ONLY thing that differs from the base
 * render path is the UBO: the node is created with a
 * PhosphorSurfaceShaders::SurfaceUniformProfile so it uploads the leaner
 * 560-byte surface UBO instead of the overlay UBO. createShaderNode() supplies
 * that profile to a stock PhosphorRendering::ShaderNodeRhi — there is no
 * SurfaceShaderItem-specific node subclass.
 *
 * The surface-state inputs below (scale / focus / geometry) map to the
 * surface-only fields of PhosphorShaders::UboFrameState that
 * SurfaceUniformProfile::fill() reads. They are the binding surface a QML
 * surface host will drive; until that host is wired (Stage d) they hold the
 * UboFrameState defaults (scale 1.0, unfocused, zero geometry), which the
 * profile treats as a safe identity.
 *
 * Registered manually via qmlRegisterType in daemon/main.cpp under the
 * "PlasmaZones" module URI (same as ZoneShaderItem) — QML_ELEMENT here would be
 * inert (no qt_add_qml_module target exists) and misleading.
 */
class PLASMAZONES_RENDERING_EXPORT SurfaceShaderItem : public PhosphorRendering::ShaderEffect
{
    Q_OBJECT

    /** Logical→device pixel scale for the decorated surface. Maps to
     * UboFrameState::surfaceScale (the shader's `uSurfaceScale`). */
    Q_PROPERTY(qreal surfaceScale READ surfaceScale WRITE setSurfaceScale NOTIFY surfaceScaleChanged FINAL)
    /** Whether the decorated window is focused. Maps to UboFrameState::surfaceFocused
     * (1.0 focused / 0.0 not) — the shader mixes active vs inactive appearance on it. */
    Q_PROPERTY(bool surfaceFocused READ surfaceFocused WRITE setSurfaceFocused NOTIFY surfaceFocusedChanged FINAL)
    /** Decorated surface size in device px. Maps to UboFrameState::surfaceSize. */
    Q_PROPERTY(QSizeF surfaceSize READ surfaceSize WRITE setSurfaceSize NOTIFY surfaceSizeChanged FINAL)
    /** Frame (content) top-left in device px. Maps to UboFrameState::surfaceFrameTopLeft. */
    Q_PROPERTY(QPointF surfaceFrameTopLeft READ surfaceFrameTopLeft WRITE setSurfaceFrameTopLeft NOTIFY
                   surfaceFrameTopLeftChanged FINAL)
    /** Frame (content) size in device px. Maps to UboFrameState::surfaceFrameSize. */
    Q_PROPERTY(
        QSizeF surfaceFrameSize READ surfaceFrameSize WRITE setSurfaceFrameSize NOTIFY surfaceFrameSizeChanged FINAL)

public:
    explicit SurfaceShaderItem(QQuickItem* parent = nullptr);
    ~SurfaceShaderItem() override;

    // Note: shaderSource, paramPreamble, shaderParams, iTime, and the
    // customParams / customColors slots are inherited Q_PROPERTYs from
    // PhosphorRendering::ShaderEffect — the QML host binds them directly. The
    // base setShaderParams already maps `customParamsN_<xyzw>` / `customColorN`
    // keys (the slot form SurfaceShaderRegistry::translateSurfaceParams emits)
    // onto the UBO, so no surface-specific param plumbing is needed here.

    qreal surfaceScale() const
    {
        return m_surfaceScale;
    }
    void setSurfaceScale(qreal scale);

    bool surfaceFocused() const
    {
        return m_surfaceFocused;
    }
    void setSurfaceFocused(bool focused);

    QSizeF surfaceSize() const
    {
        return m_surfaceSize;
    }
    void setSurfaceSize(const QSizeF& size);

    QPointF surfaceFrameTopLeft() const
    {
        return m_surfaceFrameTopLeft;
    }
    void setSurfaceFrameTopLeft(const QPointF& topLeft);

    QSizeF surfaceFrameSize() const
    {
        return m_surfaceFrameSize;
    }
    void setSurfaceFrameSize(const QSizeF& size);

Q_SIGNALS:
    void surfaceScaleChanged();
    void surfaceFocusedChanged();
    void surfaceSizeChanged();
    void surfaceFrameTopLeftChanged();
    void surfaceFrameSizeChanged();

protected:
    /**
     * @brief Create the render node with the surface UBO profile.
     *
     * Returns a stock PhosphorRendering::ShaderNodeRhi constructed with a
     * PhosphorSurfaceShaders::SurfaceUniformProfile, so the shared render engine
     * uploads the 560-byte surface UBO. No node subclass — the profile is the
     * only difference from the base/overlay path.
     */
    PhosphorRendering::ShaderNodeRhi* createShaderNode() override;

    /**
     * @brief Create or update the scene graph node for rendering.
     *
     * Mirrors ZoneShaderItem's load semantics (vertex shader resolved + loaded
     * before the fragment, include paths + param preamble pushed) minus all the
     * zone-specific sync. Surface packs ship their own `main()` and no
     * entry-point scaffold, so none is installed.
     */
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    // Surface-state inputs mirroring the UboFrameState surface-only field
    // defaults (an identity decoration: full scale, unfocused, zero geometry).
    qreal m_surfaceScale = 1.0;
    bool m_surfaceFocused = false;
    QSizeF m_surfaceSize;
    QPointF m_surfaceFrameTopLeft;
    QSizeF m_surfaceFrameSize;
};

} // namespace PlasmaZones
