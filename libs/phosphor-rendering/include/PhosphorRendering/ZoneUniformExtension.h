// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorRendering/ZoneShaderCommon.h>
#include <PhosphorShaders/IUniformExtension.h>

#include <QMutex>
#include <QMutexLocker>
#include <QtNumeric>
#include <atomic>
#include <cmath>
#include <cstring>

namespace PhosphorRendering {

/**
 * @brief IUniformExtension implementation for zone data.
 *
 * Appends zone arrays (zoneRects, zoneFillColors, zoneBorderColors, zoneParams)
 * plus the trailing zoneScale scalar after BaseUniforms in the UBO. Total
 * extension size: MaxZones * 4 * sizeof(float) * 4 + 16 = 4112 bytes (the
 * scalar plus the 12 pad bytes std140 needs to close the block on a 16-byte
 * boundary).
 *
 * The zone data layout matches the GLSL UBO declaration in common.glsl exactly,
 * and is binary-compatible with the zone region of ZoneShaderUniforms.
 *
 * @par Threading
 * write() runs on the render thread during prepare(); updateFromZones() runs
 * on the GUI thread (typically from updatePaintNode's sync phase). Sync phase
 * is meant to block the render thread, but some Qt render loops can advance
 * through prepare() before the next sync fires, so m_mutex serialises reads
 * and writes of m_data to prevent torn copies.
 */
class ZoneUniformExtension : public PhosphorShaders::IUniformExtension
{
public:
    ZoneUniformExtension()
    {
        std::memset(&m_data, 0, sizeof(m_data));
        // Identity scale until the item reports the real device-pixel ratio.
        // A zeroed scale would multiply every radius and border width to 0,
        // so the first frames of a fresh overlay would render square-cornered
        // and border-less before the first setScale() lands.
        m_data.zoneScale = 1.0f;
    }

    int extensionSize() const override
    {
        return static_cast<int>(sizeof(m_data));
    }

    void write(char* buffer, int offset) const override
    {
        QMutexLocker lock(&m_mutex);
        std::memcpy(buffer + offset, &m_data, sizeof(m_data));
    }

    bool isDirty() const override
    {
        return m_dirty.load(std::memory_order_acquire);
    }

    void clearDirty() override
    {
        m_dirty.store(false, std::memory_order_release);
    }

    /// Update zone data from a vector of ZoneData. Called on the GUI thread
    /// (typically from updatePaintNode's sync phase). Mutex-guarded against
    /// a concurrent write() on the render thread — see class Threading note.
    void updateFromZones(const QVector<ZoneData>& zones)
    {
        QMutexLocker lock(&m_mutex);
        for (int i = 0; i < MaxZones; ++i) {
            if (i < zones.size()) {
                const ZoneData& zone = zones[i];
                m_data.zoneRects[i][0] = static_cast<float>(zone.rect.x());
                m_data.zoneRects[i][1] = static_cast<float>(zone.rect.y());
                m_data.zoneRects[i][2] = static_cast<float>(zone.rect.width());
                m_data.zoneRects[i][3] = static_cast<float>(zone.rect.height());
                m_data.zoneFillColors[i][0] = static_cast<float>(zone.fillColor.redF());
                m_data.zoneFillColors[i][1] = static_cast<float>(zone.fillColor.greenF());
                m_data.zoneFillColors[i][2] = static_cast<float>(zone.fillColor.blueF());
                m_data.zoneFillColors[i][3] = static_cast<float>(zone.fillColor.alphaF());
                m_data.zoneBorderColors[i][0] = static_cast<float>(zone.borderColor.redF());
                m_data.zoneBorderColors[i][1] = static_cast<float>(zone.borderColor.greenF());
                m_data.zoneBorderColors[i][2] = static_cast<float>(zone.borderColor.blueF());
                m_data.zoneBorderColors[i][3] = static_cast<float>(zone.borderColor.alphaF());
                m_data.zoneParams[i][0] = zone.borderRadius;
                m_data.zoneParams[i][1] = zone.borderWidth;
                m_data.zoneParams[i][2] = zone.isHighlighted ? 1.0f : 0.0f;
                m_data.zoneParams[i][3] = static_cast<float>(zone.zoneNumber);
            } else {
                std::memset(m_data.zoneRects[i], 0, sizeof(m_data.zoneRects[i]));
                std::memset(m_data.zoneFillColors[i], 0, sizeof(m_data.zoneFillColors[i]));
                std::memset(m_data.zoneBorderColors[i], 0, sizeof(m_data.zoneBorderColors[i]));
                std::memset(m_data.zoneParams[i], 0, sizeof(m_data.zoneParams[i]));
            }
        }
        m_dirty.store(true, std::memory_order_release);
    }

    /// Set the logical-to-device scale the shader applies to the lengths in
    /// zoneParams (corner radius, border width). Called from the item's
    /// updatePaintNode sync phase with the same device-pixel ratio the node's
    /// resolution is scaled by, so `radius * uZoneScale` lands in the same
    /// device-px space as iResolution and vFragCoord.
    ///
    /// Separate from updateFromZones() because the two change on different
    /// clocks: zone contents churn per drag frame, the scale only when the
    /// overlay moves to a differently-scaled screen. Guarded by the same mutex
    /// and only marks dirty on an actual change, so a per-frame call from the
    /// sync phase costs nothing.
    ///
    /// @return false if @p scale was rejected as out of contract, true
    ///         otherwise (including a no-op re-set of the current value).
    ///         Rejection keeps the last good scale, which renders correctly but
    ///         is indistinguishable from working code, so the caller is
    ///         expected to report it. The check cannot log here: this library's
    ///         logging category is private to its src/ tree, and the caller
    ///         owns a category that names the actual subsystem anyway.
    [[nodiscard]] bool setScale(float scale)
    {
        // Reject values that would make the shader silently disappear rather
        // than degrade. The GLSL multiplies every radius and border width by
        // this: a NaN propagates through clamp() and takes the whole zone with
        // it, and zero or a negative collapses the corner to square with a
        // hairline border. Neither reads as a fault, so a bad value would be
        // mistaken for a design choice. A device-pixel ratio is always
        // positive and finite, so anything else is a caller bug; keep the last
        // good scale instead of rendering a lie.
        if (!(scale > 0.0f) || std::isinf(scale)) {
            return false;
        }
        QMutexLocker lock(&m_mutex);
        if (qFuzzyCompare(m_data.zoneScale, scale)) {
            return true;
        }
        m_data.zoneScale = scale;
        m_dirty.store(true, std::memory_order_release);
        return true;
    }

private:
    /// Raw zone extension data — matches the zone region of ZoneShaderUniforms exactly.
    struct alignas(16) ZoneExtensionData
    {
        float zoneRects[MaxZones][4];
        float zoneFillColors[MaxZones][4];
        float zoneBorderColors[MaxZones][4];
        float zoneParams[MaxZones][4];
        float zoneScale;
        float _pad_after_zoneScale[3];
    };

    static_assert(sizeof(ZoneExtensionData) == MaxZones * 4 * sizeof(float) * 4 + 4 * sizeof(float),
                  "ZoneExtensionData must be MaxZones * 4 arrays * 4 floats, plus the "
                  "zoneScale scalar and its std140 tail pad");
    // Field offsets must match the GLSL UBO layout in common.glsl exactly.
    // Reordering or inserting fields here silently breaks shader rendering
    // (no compile error, just wrong colors/positions on every zone).
    static_assert(offsetof(ZoneExtensionData, zoneRects) == 0, "zoneRects must be first");
    static_assert(offsetof(ZoneExtensionData, zoneFillColors) == sizeof(float[MaxZones][4]),
                  "zoneFillColors must follow zoneRects with no gap");
    static_assert(offsetof(ZoneExtensionData, zoneBorderColors) == 2 * sizeof(float[MaxZones][4]),
                  "zoneBorderColors must follow zoneFillColors with no gap");
    static_assert(offsetof(ZoneExtensionData, zoneParams) == 3 * sizeof(float[MaxZones][4]),
                  "zoneParams must follow zoneBorderColors with no gap");
    static_assert(offsetof(ZoneExtensionData, zoneScale) == 4 * sizeof(float[MaxZones][4]),
                  "zoneScale must follow zoneParams with no gap");
    // Verify ZoneExtensionData is binary-identical to the zone region of
    // ZoneShaderUniforms (BaseUniforms + this == ZoneShaderUniforms layout).
    static_assert(sizeof(ZoneExtensionData) == sizeof(ZoneShaderUniforms) - sizeof(PhosphorShaders::BaseUniforms),
                  "ZoneExtensionData size must match ZoneShaderUniforms zone region size");

    ZoneExtensionData m_data;
    mutable QMutex m_mutex;
    std::atomic<bool> m_dirty{true};
};

} // namespace PhosphorRendering
