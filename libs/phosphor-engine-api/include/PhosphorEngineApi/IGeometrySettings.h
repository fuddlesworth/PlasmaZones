// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengineapi_export.h>
#include <QString>
#include <QVariantMap>

namespace PhosphorEngineApi {

namespace PerScreenSnappingKey {
inline constexpr const char ZonePadding[] = "ZonePadding";
inline constexpr const char OuterGap[] = "OuterGap";
inline constexpr const char UsePerSideOuterGap[] = "UsePerSideOuterGap";
inline constexpr const char OuterGapTop[] = "OuterGapTop";
inline constexpr const char OuterGapBottom[] = "OuterGapBottom";
inline constexpr const char OuterGapLeft[] = "OuterGapLeft";
inline constexpr const char OuterGapRight[] = "OuterGapRight";
} // namespace PerScreenSnappingKey

// Library-level fallback defaults. The app-level authority is ConfigDefaults (GPL);
// these must be kept in sync manually since the LGPL library cannot depend on it.
namespace GeometryDefaults {
inline constexpr int ZonePadding = 8;
inline constexpr int OuterGap = 8;
} // namespace GeometryDefaults

class PHOSPHORENGINEAPI_EXPORT IGeometrySettings
{
public:
    virtual ~IGeometrySettings() = default;

    virtual int zonePadding() const = 0;
    virtual int outerGap() const = 0;
    virtual bool usePerSideOuterGap() const = 0;
    virtual int outerGapTop() const = 0;
    virtual int outerGapBottom() const = 0;
    virtual int outerGapLeft() const = 0;
    virtual int outerGapRight() const = 0;
    virtual QVariantMap getPerScreenSnappingSettings(const QString& screenId) const = 0;
};

} // namespace PhosphorEngineApi
