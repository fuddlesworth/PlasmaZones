// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// TU-local helpers of snapassistthumbnailcapture.cpp: the capture timing
// constants, the fd owner, and the two small predicates. Split out only to keep
// that file under the 1150-line ceiling; nothing here is shared with another
// translation unit and nothing here belongs to the class's interface.

#include <QImage>
#include <QSize>
#include <QtGlobal>

#include <algorithm>
#include <unistd.h>

namespace PlasmaZones::SnapAssistCaptureHelpers {

/// Settle delay before the first capture attempt for a candidate. A freshly
/// mapped window may not have a renderable compositor frame the instant it is
/// queued; one frame at 60Hz (~16ms) is reliably enough for drawWindow to read
/// non-empty content, while not adding meaningful latency to the snap-assist UI
/// (which already shows icons immediately and fades thumbs in asynchronously).
inline constexpr int RENDER_SETTLE_MS = 16;
/// Retry delay used when the first render produced an empty buffer. Four
/// frames at 60Hz: long enough for a stalled compositor frame to clear,
/// short enough that the user still sees the thumbnail before the eye
/// notices the fallback icon.
inline constexpr int RENDER_RETRY_MS = 64;
/// Consecutive dma-buf capture failures (export failure or daemon import
/// rejection) before the session permanently falls back to the raw-pixel path.
/// >1 so a single transient bad frame doesn't disable the zero-copy path,
/// while a genuine capability gap (every frame fails) trips it quickly.
inline constexpr int DmabufFailureThreshold = 2;
/// Smallest useful thumbnail axis. A fit below this (an extreme-aspect
/// window rounding one axis toward 1px) produces a sliver that passes the
/// daemon's `width > 0` validation and then latches into the dedup window
/// as a useless thumbnail — treat it as a capture failure so the candidate
/// falls back to its icon instead.
inline constexpr int MinThumbnailAxisPx = 8;

/// RAII holder for a raw fd. exportTextureToDmabuf juggles a dma-buf fd and
/// a fence fd across six distinct early-return paths; a scoped owner makes
/// "every path closes what it opened" structural instead of per-branch
/// bookkeeping the next edit can silently break.
struct ScopedFd
{
    int fd = -1;
    ScopedFd() = default;
    explicit ScopedFd(int f)
        : fd(f)
    {
    }
    ~ScopedFd()
    {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    /// Movable so an owner can live in a container. Non-copyable stays: two
    /// owners of one descriptor is a double close.
    ScopedFd(ScopedFd&& other) noexcept
        : fd(other.fd)
    {
        other.fd = -1;
    }
    ScopedFd& operator=(ScopedFd&& other) noexcept
    {
        if (this != &other) {
            reset();
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }
    /// Close now rather than at scope exit, for an owner whose scope outlives
    /// the point the descriptor stops being needed.
    void reset()
    {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
    /// Transfer ownership out (the success path hands the fd to the caller).
    int release()
    {
        const int f = fd;
        fd = -1;
        return f;
    }
    bool valid() const
    {
        return fd >= 0;
    }
};

/// True when every pixel of an ARGB32 image has zero alpha. A cleared FBO
/// whose drawWindow produced nothing reads back as a valid, fully transparent
/// image — isNull() cannot distinguish that from a real capture, so the
/// "window had no renderable frame yet" retry must test content, not
/// nullness. Bounded by SnapAssistThumbnailMaxDimension (1024², so 4 MiB worst
/// case) and icon-sized in practice; a linear byte scan at capture cadence is
/// negligible next to the GPU readback that produced the image, and this runs
/// off the paint path on a timer.
inline bool isFullyTransparent(const QImage& image)
{
    if (image.isNull() || image.format() != QImage::Format_ARGB32) {
        return false;
    }
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const quint32*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (line[x] & 0xff000000u) {
                return false;
            }
        }
    }
    return true;
}

/// The dedup key's size dimension: a thumbnail already posted at a larger major
/// axis satisfies a request for a smaller one.
inline int boxMajorAxis(QSize box)
{
    return std::max(box.width(), box.height());
}

} // namespace PlasmaZones::SnapAssistCaptureHelpers
