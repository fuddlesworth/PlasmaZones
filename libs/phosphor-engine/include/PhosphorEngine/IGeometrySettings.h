// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengine_export.h>
#include <QString>
#include <QVariantMap>

namespace PhosphorEngine {

// Gap-override maps are keyed by the shared PerScreenKeys namespace
// (PhosphorEngine/PerScreenKeys.h) — both engines read the same key strings.

namespace GeometryDefaults {
inline constexpr int InnerGap = 8;
inline constexpr int OuterGap = 8;
/// Ceiling every engine clamps a resolved gap to, whatever layer supplied it
/// (settings, per-screen override, or context rule). Lives here rather than in
/// the app tree because the scroll and tile engines are LGPL libraries that
/// cannot include the GPL `core/types/constants.h`; `PlasmaZones::Defaults::MaxGap`
/// and `PhosphorTiles::AutotileDefaults::MaxGap` are pinned to it by
/// static_asserts in src/daemon/daemon.cpp.
inline constexpr int MaxGap = 200;
} // namespace GeometryDefaults

class PHOSPHORENGINE_EXPORT IGeometrySettings
{
public:
    virtual ~IGeometrySettings() = default;

    virtual int innerGap() const = 0;
    virtual int outerGap() const = 0;
    virtual bool usePerSideOuterGap() const = 0;
    virtual int outerGapTop() const = 0;
    virtual int outerGapBottom() const = 0;
    virtual int outerGapLeft() const = 0;
    virtual int outerGapRight() const = 0;
    virtual QVariantMap getPerScreenSnappingSettings(const QString& screenId) const = 0;
};

} // namespace PhosphorEngine
