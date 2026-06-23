// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace PlasmaZones {

/// DRM "no/invalid modifier" sentinel (DRM_FORMAT_MOD_INVALID). Defined here so
/// the Vulkan importer (dmabuftextureprovider.cpp) and the OpenGL importer
/// (dmabufglimport.cpp) share one definition instead of each redeclaring it.
constexpr uint64_t DrmFormatModInvalid = 0x00ffffffffffffffULL;

/**
 * @brief Single-plane DMA-BUF descriptor for a window thumbnail.
 *
 * Handed from the kwin-effect (which renders the thumbnail from the live
 * compositor texture) to the daemon, which imports it as a GPU texture —
 * the zero-copy alternative to the raw-ARGB32 path in
 * @c IOverlayService::setSnapAssistThumbnail.
 *
 * Spike scope: one RGBA plane (a thumbnail is a single render target);
 * multi-plane / YUV formats are intentionally out of scope.
 *
 * Lifetime: @ref fd is BORROWED for the duration of the receiving call only.
 * An importer that needs the buffer beyond the call (the Vulkan/EGL import
 * does) MUST @c dup() it; the D-Bus layer closes its copy when the call
 * returns.
 */
struct DmabufThumbnailDesc
{
    int fd = -1; ///< Borrowed dma-buf file descriptor (plane 0).
    int width = 0; ///< Image width in pixels.
    int height = 0; ///< Image height in pixels.
    uint32_t fourcc = 0; ///< DRM FourCC pixel format (e.g. DRM_FORMAT_ABGR8888).
    uint64_t modifier = 0; ///< DRM format modifier (DRM_FORMAT_MOD_* / vendor-specific).
    uint32_t stride = 0; ///< Plane 0 row stride in bytes.
    uint32_t offset = 0; ///< Plane 0 byte offset.
    /// Borrowed sync_file fence fd that signals when the producer's GPU render
    /// into @ref fd has completed. The consumer waits on it (event-driven, e.g.
    /// DmabufFenceWaiter) before revealing/sampling the buffer, so a
    /// repeated/live producer can't be read mid-render. Same
    /// borrowed-for-the-call lifetime as @ref fd. -1 = no fence supplied.
    int fenceFd = -1;
};

} // namespace PlasmaZones
