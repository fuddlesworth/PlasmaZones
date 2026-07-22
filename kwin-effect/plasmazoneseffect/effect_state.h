// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// @file effect_state.h
/// Small value/helper types that PlasmaZonesEffect holds by value or in maps.
/// Extracted from plasmazoneseffect.h to keep that header focused on the class
/// surface. These were nested types of PlasmaZonesEffect; none needs private
/// access to the class, so they live at namespace scope. Every reference in the
/// .cpp files is unqualified (inside PlasmaZonesEffect member functions), so it
/// resolves here via ordinary namespace lookup. Included by plasmazoneseffect.h.

#include <PhosphorCompositor/DecorationDefaults.h> // WindowAppearanceScope

#include <QColor>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <type_traits>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

struct CompiledSurfacePack; // surface_types.h

/// Smoothed focus value per window driving the uSurfaceFocused ramp. `value < 0`
/// is the uninitialised sentinel; `lastMs` dedupes the per-frame advance. See
/// PlasmaZonesEffect::m_focusFade.
struct FocusFadeState
{
    float value = -1.0f;
    qint64 lastMs = -1;
};

/// Config-backed window-decoration appearance default, filling the slots a
/// window's per-window rules leave unset (rules win per slot). See
/// PlasmaZonesEffect::m_windowAppearanceDefault.
struct WindowAppearanceDefault
{
    bool showBorder = false;
    QString borderScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
    int borderWidth = 0;
    int borderRadius = 0;
    QString activeColor;
    QString inactiveColor;
    bool hideTitleBar = false;
    QString titleBarScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
    // Plain opacity+tint layer (Windows.* ShowOpacityTint/Opacity/Tint*),
    // rendered by the built-in "opacity-tint" pack in easy mode. The tint
    // colour carries hex or the accent sentinel like the border colours.
    bool showOpacityTint = false;
    QString opacityTintScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
    double opacity = 1.0;
    double tintStrength = 0.0;
    QString tintColor;
};

/// Debounced frame-geometry shadow push state per window. The window pointer
/// rides along so the flush runs the exclusion gate once per flush. See
/// PlasmaZonesEffect::m_pendingFrameGeometry.
struct PendingFrameGeometry
{
    QRect geometry;
    QPointer<KWin::EffectWindow> window;
};

/// Resolves a pack id to its compiled program, compiling on a cache miss. The fold
/// memoises the decoration-profile lookup behind this, so the input side takes it
/// as a callable rather than resolving the tree a second time.
///
/// A NON-OWNING reference to the caller's lambda, not a std::function. The fold's
/// resolver captures three pointers (24 bytes), which is past libstdc++'s 16-byte
/// small-object buffer — so every conversion to std::function HEAP-ALLOCATED, and the
/// fold converts twice. At eight decorated windows across two outputs at 60Hz that is
/// some four thousand malloc/free pairs a second, added by a refactor that shipped
/// inside a performance PR. The callee only ever invokes it during the call, so a
/// borrowed reference is all it ever needed.
class CompiledPackResolver
{
public:
    template<typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, CompiledPackResolver>>>
    CompiledPackResolver(F&& fn) // NOLINT(google-explicit-constructor): behaves as a callable
        : m_ctx(static_cast<void*>(std::addressof(fn)))
        , m_invoke([](void* ctx, const QString& packId) -> CompiledSurfacePack* {
            return (*static_cast<std::remove_reference_t<F>*>(ctx))(packId);
        })
    {
    }
    CompiledSurfacePack* operator()(const QString& packId) const
    {
        return m_invoke(m_ctx, packId);
    }

private:
    void* m_ctx;
    CompiledSurfacePack* (*m_invoke)(void*, const QString&);
};

/// Pre-rule keepAbove/keepBelow pair captured the first time a SetWindowLayer rule
/// is applied to a window. See PlasmaZonesEffect::m_ruleWindowLayerSnapshots.
struct WindowLayerSnapshot
{
    bool keepAbove = false;
    bool keepBelow = false;
};

/// Minimize-shader stamp: the time and generation of the transition a minimize
/// event installed, so a spurious minimize→unminimize pair can cancel the exact
/// reverse leg. See PlasmaZonesEffect::m_minimizeShaderStamp.
struct MinimizeShaderStamp
{
    qint64 timeMs = 0;
    quint64 generation = 0;
};

} // namespace PlasmaZones
