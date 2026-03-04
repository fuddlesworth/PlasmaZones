// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file rendering_macros.h
 * @brief DRY setter macros for ZoneShaderItem and ZoneShaderNodeRhi
 *
 * Modeled on src/config/settings_macros.h. Each macro encapsulates
 * a repeated change-detect + assign + dirty-flag pattern.
 */

// ZoneShaderItem: vec4 property setter with equality check, emit, and update()
#define SHADERITEM_VEC4_SETTER(name, member, signal) \
    void ZoneShaderItem::set##name(const QVector4D& value) \
    { \
        if (member == value) \
            return; \
        member = value; \
        Q_EMIT signal(); \
        update(); \
    }

// ZoneShaderNodeRhi: vec4 param setter (assigns member + sets dirty flags)
#define RHINODE_PARAMS_SETTER(name, member) \
    void ZoneShaderNodeRhi::set##name(const QVector4D& params) \
    { \
        if (member == params) return; \
        member = params; \
        m_uniformsDirty = true; \
        m_zoneDataDirty = true; \
    }

// ZoneShaderNodeRhi: QColor setter (assigns member + sets dirty flags)
#define RHINODE_COLOR_SETTER(name, member) \
    void ZoneShaderNodeRhi::set##name(const QColor& color) \
    { \
        if (member == color) return; \
        member = color; \
        m_uniformsDirty = true; \
        m_zoneDataDirty = true; \
    }
