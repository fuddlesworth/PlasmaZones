// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file zoneshadernoderhi_internal.h
 * @brief Shared constants and helper declarations for ZoneShaderNodeRhi TUs
 *
 * Provides RhiConstants (quad vertices, uniform indices) and namespace detail
 * declarations for cross-TU access after splitting zoneshadernoderhi.cpp.
 */

#include <QList>
#include <QString>

#include <rhi/qshaderbaker.h>

namespace PlasmaZones {

namespace RhiConstants {

inline constexpr float QuadVertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
};

static constexpr int UniformVecIndex1 = 0;
static constexpr int UniformVecIndex2 = 1;
static constexpr int UniformVecIndex3 = 2;
static constexpr int UniformVecIndex4 = 3;
static constexpr int UniformVecIndex5 = 4;
static constexpr int UniformVecIndex6 = 5;
static constexpr int UniformVecIndex7 = 6;
static constexpr int UniformVecIndex8 = 7;
static constexpr int ComponentX = 0;
static constexpr int ComponentY = 1;
static constexpr int ComponentZ = 2;
static constexpr int ComponentW = 3;

} // namespace RhiConstants

namespace detail {

/// Shader bake targets (SPIR-V 1.0 + GLSL 330 + ES variants). Defined in zoneshadernoderhi.cpp.
const QList<QShaderBaker::GeneratedShader>& bakeTargets();

/// Load shader file, expand #include directives. Defined in zoneshadernoderhi.cpp.
QString loadAndExpandShader(const QString& path, QString* outError);

} // namespace detail

} // namespace PlasmaZones
