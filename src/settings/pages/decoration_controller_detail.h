// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Inline helpers for the DecorationPageController translation units (currently
// decorationpagecontroller.cpp; the class is split across several TUs). They
// convert surface-pack and pointer-pack effect / parameter values to
// QVariantMap for QML and build the sparse / resolved DecorationProfile ->
// QVariantMap projections. The pointer is a decoration surface like any other,
// so its packs are projected beside the surface family: their half lives in
// pointer_controller_detail.h (included here) so the pointer preview
// controller can share it without the surface headers.
// Inline definitions here let any consuming TU get its own copy without relying
// on unity-build TU merging for cross-TU linkage.

#include "pointer_controller_detail.h"

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QLatin1Char>
#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {
namespace decoration_controller_detail {

/// Convert a surface-pack ParameterInfo to a QVariantMap. Keys mirror
/// `animations_controller_detail::parameterInfoToMap` (and the surface
/// page's old `parameterInfoToMap`) so the shared QML editor components
/// consume animation, overlay, and surface packs identically. Optional
/// fields are emitted only when valid/non-empty.
inline QVariantMap parameterInfoToMap(const PhosphorSurfaceShaders::SurfaceShaderEffect::ParameterInfo& p)
{
    QVariantMap m;
    m.insert(QLatin1String("id"), p.id);
    m.insert(QLatin1String("name"), p.name);
    m.insert(QLatin1String("type"), p.type);
    if (!p.description.isEmpty())
        m.insert(QLatin1String("description"), p.description);
    if (!p.group.isEmpty())
        m.insert(QLatin1String("group"), p.group);
    if (p.defaultValue.isValid())
        m.insert(QLatin1String("default"), p.defaultValue);
    if (p.minValue.isValid())
        m.insert(QLatin1String("min"), p.minValue);
    if (p.maxValue.isValid())
        m.insert(QLatin1String("max"), p.maxValue);
    if (p.stepValue.isValid())
        m.insert(QLatin1String("step"), p.stepValue);
    return m;
}

inline QVariantMap effectToMap(const PhosphorSurfaceShaders::SurfaceShaderEffect& effect)
{
    QVariantMap m;
    m.insert(QLatin1String("id"), effect.id);
    m.insert(QLatin1String("name"), effect.name);
    m.insert(QLatin1String("description"), effect.description);
    m.insert(QLatin1String("author"), effect.author);
    m.insert(QLatin1String("version"), effect.version);
    m.insert(QLatin1String("category"), effect.category);
    m.insert(QLatin1String("isUserEffect"), effect.isUserEffect);
    m.insert(QLatin1String("providesBorder"), effect.providesBorder);
    m.insert(QLatin1String("providesOpacityTint"), effect.providesOpacityTint);
    // No previewPath: the browser previews live decoration chains now, and
    // no QML reads the key (see the animation twin's note).
    QVariantList params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters)
        params.append(parameterInfoToMap(p));
    m.insert(QLatin1String("parameters"), params);
    return m;
}

/// Project the ENGAGED chain, parameters and presetIds fields of @p p into a sparse
/// QVariantMap as a QVariant projection (chain as a QStringList, parameters as
/// a nested QVariantMap). Only fields whose optional is engaged appear — an
/// inherited (nullopt) field is omitted, so QML can tell "set here" from
/// "inherited" by key presence. The remaining engaged field, disabledPacks, is
/// deliberately NOT projected here — it is surfaced through the dedicated
/// disabledPacksAt() invokable, not this map. Border width / radius / colour
/// are NOT decoration fields — they live in `parameters["border"]` and ride
/// the parameters projection.
inline QVariantMap profileToSparseMap(const PhosphorSurfaceShaders::DecorationProfile& p)
{
    using DP = PhosphorSurfaceShaders::DecorationProfile;
    QVariantMap m;
    if (p.chain)
        m.insert(QLatin1String(DP::JsonFieldChain), QVariant(*p.chain));
    if (p.parameters)
        m.insert(QLatin1String(DP::JsonFieldParameters), *p.parameters);
    if (p.presetIds)
        m.insert(QLatin1String(DP::JsonFieldPresetIds), *p.presetIds);
    return m;
}

/// Project @p p with library defaults filled in (via withDefaults()) into a
/// QVariantMap. Used for the resolved/effective view so QML always reads
/// concrete values. Same key/value encoding as profileToSparseMap, so every
/// chain / parameters / presetIds field is present (disabledPacks is likewise via
/// disabledPacksAt(), not this map).
inline QVariantMap profileToResolvedMap(const PhosphorSurfaceShaders::DecorationProfile& profile)
{
    return profileToSparseMap(profile.withDefaults());
}

} // namespace decoration_controller_detail
} // namespace PlasmaZones
