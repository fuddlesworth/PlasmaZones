// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengineapi_export.h>
#include <QLatin1String>
#include <QString>
#include <QVariantMap>

namespace PhosphorEngineApi {

namespace PerScreenSnappingKey {
inline constexpr QLatin1String ZonePadding{"ZonePadding"};
inline constexpr QLatin1String OuterGap{"OuterGap"};
inline constexpr QLatin1String UsePerSideOuterGap{"UsePerSideOuterGap"};
inline constexpr QLatin1String OuterGapTop{"OuterGapTop"};
inline constexpr QLatin1String OuterGapBottom{"OuterGapBottom"};
inline constexpr QLatin1String OuterGapLeft{"OuterGapLeft"};
inline constexpr QLatin1String OuterGapRight{"OuterGapRight"};
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
