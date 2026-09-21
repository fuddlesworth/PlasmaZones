// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The pointer-pack half of decoration_controller_detail.h: the projections of
// a PointerShaderEffect and its ParameterInfo to the QVariantMap shapes QML
// reads. Split out so PointerPreviewController, which knows only the pointer
// family, can share the one parameter-row builder with DecorationPageController
// without pulling the surface headers (and their library) into every target
// that links the preview controller alone. decoration_controller_detail.h
// includes this, so the page controller sees both families through one
// header as before. Same namespace, so the two parameterInfoToMap overloads
// keep resolving side by side.

#include <PhosphorPointer/PointerShaderEffect.h>

#include <QLatin1String>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {
namespace decoration_controller_detail {

/// Convert a pointer-pack ParameterInfo to the same QVariantMap shape the
/// surface twin emits, so the shared QML editor components consume a
/// pointer pack's parameters without knowing which family it came from.
inline QVariantMap parameterInfoToMap(const PhosphorPointerShaders::PointerShaderEffect::ParameterInfo& p)
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

/// Row for one pointer pack. Shares every key the surface row carries so the
/// chain editor and the pack browser render both families with one code path.
/// `layer`, `needsCursor` and `trailSeconds` are pointer-only extras the
/// surface family has no analogue for, and they are the same three
/// PointerPreviewController::packInfo surfaces, so a browser row and a
/// pack-info map answer the pointer notices identically. `providesBorder` /
/// `providesOpacityTint` are surface-only and are absent here, which reads as
/// false in QML. No previewPath, for the reason the surface twin gives: the
/// browser previews live chains and no QML reads the key.
inline QVariantMap effectToMap(const PhosphorPointerShaders::PointerShaderEffect& effect)
{
    QVariantMap m;
    m.insert(QLatin1String("id"), effect.id);
    m.insert(QLatin1String("name"), effect.name);
    m.insert(QLatin1String("description"), effect.description);
    m.insert(QLatin1String("author"), effect.author);
    m.insert(QLatin1String("version"), effect.version);
    m.insert(QLatin1String("category"), effect.category);
    m.insert(QLatin1String("isUserEffect"), effect.isUserEffect);
    m.insert(QLatin1String("layer"), PhosphorPointerShaders::PointerShaderEffect::layerToken(effect.layer));
    m.insert(QLatin1String("needsCursor"), effect.needsCursor);
    m.insert(QLatin1String("trailSeconds"), effect.trailSeconds);
    QVariantList params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters)
        params.append(parameterInfoToMap(p));
    m.insert(QLatin1String("parameters"), params);
    return m;
}

} // namespace decoration_controller_detail
} // namespace PlasmaZones
