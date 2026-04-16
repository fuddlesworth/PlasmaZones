// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "zoneshadercommon.h"

#include <PhosphorShell/IUniformExtension.h>

#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <cstring>

namespace PlasmaZones {

/**
 * @brief IUniformExtension implementation for PlasmaZones zone data.
 *
 * Appends zone arrays (zoneRects, zoneFillColors, zoneBorderColors, zoneParams)
 * after BaseUniforms in the UBO. Total extension size: MaxZones * 4 * sizeof(float) * 4 = 4096 bytes.
 *
 * The zone data layout matches the GLSL UBO declaration in common.glsl exactly,
 * and is binary-compatible with the zone region of ZoneShaderUniforms.
 *
 * @par Threading
 * write() runs on the render thread during prepare(); updateFromZones()
 * runs on the GUI thread (from ZoneShaderItem::updatePaintNode — sync phase).
 * Sync phase is meant to block the render thread, but some Qt render loops
 * can advance through prepare() before the next sync fires, so m_mutex
 * serialises reads and writes of m_data to prevent torn copies.
 */
class ZoneUniformExtension : public PhosphorShell::IUniformExtension
{
public:
    ZoneUniformExtension()
    {
        std::memset(&m_data, 0, sizeof(m_data));
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

private:
    /// Raw zone extension data — matches the zone region of ZoneShaderUniforms exactly.
    struct alignas(16) ZoneExtensionData
    {
        float zoneRects[MaxZones][4];
        float zoneFillColors[MaxZones][4];
        float zoneBorderColors[MaxZones][4];
        float zoneParams[MaxZones][4];
    };

    static_assert(sizeof(ZoneExtensionData) == MaxZones * 4 * sizeof(float) * 4,
                  "ZoneExtensionData must be MaxZones * 4 arrays * 4 floats");
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
    // Verify ZoneExtensionData is binary-identical to the zone region of
    // ZoneShaderUniforms (BaseUniforms + this == ZoneShaderUniforms layout).
    static_assert(sizeof(ZoneExtensionData) == sizeof(ZoneShaderUniforms) - sizeof(PhosphorShell::BaseUniforms),
                  "ZoneExtensionData size must match ZoneShaderUniforms zone region size");

    ZoneExtensionData m_data;
    mutable QMutex m_mutex;
    std::atomic<bool> m_dirty{true};
};

} // namespace PlasmaZones
