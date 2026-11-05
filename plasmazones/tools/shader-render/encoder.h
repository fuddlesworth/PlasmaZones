// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace PlasmaZones::ShaderRender {

/**
 * @brief Per-frame consumer interface for the renderer.
 *
 * Implementations either write each frame to a numbered PNG, push raw RGBA
 * into an ffmpeg subprocess for VP9 / H.264 encoding, or both. finalize() is
 * called once at the end of the run.
 */
class FrameSink
{
public:
    virtual ~FrameSink() = default;

    /// @return false on a fatal error.
    virtual bool writeFrame(const QImage& frame) = 0;

    /// Flush + close. Returns false on encoder failure.
    virtual bool finalize() = 0;
};

/**
 * @brief Output kinds the tool understands.
 *
 * Centralised here so `makeFrameSink` and the ffmpeg argument builder share
 * one source of truth for extension → format mapping. Adding a format means
 * touching this enum and the `formatFromExtension` / sink dispatch — nowhere
 * else.
 */
enum class OutputFormat {
    Unknown,
    PngSequence,
    Webm,
    Mp4,
};

/// Map `path`'s extension (case-insensitive) to an OutputFormat.
OutputFormat formatFromExtension(const QString& path);

/**
 * @brief Pick a sink based on the output extension:
 *
 *   *.png         — PngSequenceSink: outPath_000001.png, _000002.png, ...
 *   *.webm        — FfmpegPipeSink: VP9 via `ffmpeg -i pipe:0 ...`
 *   *.mp4         — FfmpegPipeSink: H.264 via the same pipe
 *
 * Returns nullptr if the extension is unknown or the encoder binary (ffmpeg)
 * isn't on PATH for the video formats.
 */
std::unique_ptr<FrameSink> makeFrameSink(const QString& outPath, const QSize& size, int fps);

} // namespace PlasmaZones::ShaderRender
