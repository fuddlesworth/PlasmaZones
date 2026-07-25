// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QObject>
#include <QStringList>

namespace PhosphorEngine {

/// Settings surface the scrolling engine reads, injected by the daemon via
/// PlacementEngineBase::setEngineSettings (qobject_cast at point of use,
/// mirroring IAutotileSettings). The gap accessors deliberately mirror the
/// shared Tiling.Gaps values — the implementing settings object forwards
/// them, so scrolling and autotile screens read one gap model without this
/// library depending on the tile-engine headers.
class IScrollSettings
{
public:
    virtual ~IScrollSettings() = default;

    virtual int scrollingInnerGap() const = 0;
    virtual bool scrollingUsePerSideOuterGap() const = 0;
    virtual int scrollingOuterGap() const = 0;
    virtual int scrollingOuterGapTop() const = 0;
    virtual int scrollingOuterGapBottom() const = 0;
    virtual int scrollingOuterGapLeft() const = 0;
    virtual int scrollingOuterGapRight() const = 0;
    /// Whether newly opened windows take focus (shared Tiling.Behavior
    /// value, forwarded like the gaps).
    virtual bool scrollingFocusNewWindows() const = 0;

    /// CenterFocusedColumn as int (0 = never, 1 = always, 2 = on-overflow).
    virtual int scrollingCenterFocusedColumn() const = 0;
    virtual bool scrollingAlwaysCenterSingleColumn() const = 0;
    /// Default width for new columns: kind (0 = proportion, 1 = fixed px,
    /// 2 = client decides) + value (proportion in [0,1] or pixels).
    virtual int scrollingDefaultColumnWidthKind() const = 0;
    virtual qreal scrollingDefaultColumnWidthValue() const = 0;
    /// ColumnDisplay new columns open in (0 = normal, 1 = tabbed).
    virtual int scrollingDefaultColumnDisplay() const = 0;
    /// Preset proportion lists, serialized as decimal strings.
    virtual QStringList scrollingPresetColumnWidths() const = 0;
    virtual QStringList scrollingPresetWindowHeights() const = 0;
};

} // namespace PhosphorEngine

Q_DECLARE_INTERFACE(PhosphorEngine::IScrollSettings, "org.plasmazones.IScrollSettings")
