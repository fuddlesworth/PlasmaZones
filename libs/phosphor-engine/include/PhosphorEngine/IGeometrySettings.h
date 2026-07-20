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
