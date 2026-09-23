// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/CustomParamsKey.h>

#include <QLatin1String>
#include <QStringView>

/// The ONE descriptor-binding layout every Qt-RHI shader family speaks
/// (overlay, animation, surface, pointer) and the daemon's ShaderNodeRhi
/// populates. Each family's shared GLSL header declares its samplers with
/// `layout(binding = N)` from this table, the node builds its SRB from the same
/// constants, and plasmazones-shader-validate reflects every baked stage back
/// against it (`expectedSamplerBinding`), so a header that drifts from the
/// table fails the pack gate instead of the pipeline.
///
/// The table is CONTIGUOUS and declared in this order:
///
///   0          the family's std140 uniform block
///   1          consumer slot (the overlay zone-labels texture, `uZoneLabels`)
///   2 .. 9     `iChannel0` .. `iChannel7`, the multipass buffer outputs
///   10         `uAudioSpectrum`
///   11 .. 14   `uTexture0` .. `uTexture3` (the pointer family's `uCursorSprite`
///              takes slot 11, the one it has no uTexture0 for)
///   15         `uWallpaper` / `uBackdrop` (one slot, two family names)
///   16         `uDepthBuffer`
///   17 .. 31   free for consumers via ShaderNodeRhi::setExtraBinding
///
/// The uniform block carries `iChannelResolution[4]` for the FIRST four
/// channels only, so it keeps its 672-byte base layout across the channel
/// growth; a pass that reads iChannel4 .. iChannel7 sizes them with
/// `textureSize()`, which is what the builtin Kawase passes do.
namespace PhosphorShaders::Bindings {

inline constexpr int kUniformBlock = 0;
inline constexpr int kConsumer = 1;
inline constexpr int kChannelBase = 2;
inline constexpr int kChannelCount = kMaxBufferPasses;
inline constexpr int kAudioSpectrum = kChannelBase + kChannelCount;
inline constexpr int kUserTextureBase = kAudioSpectrum + 1;
inline constexpr int kUserTextureCount = 4;
inline constexpr int kWallpaper = kUserTextureBase + kUserTextureCount;
inline constexpr int kDepth = kWallpaper + 1;
/// Last binding the library manages; everything above is a consumer slot.
inline constexpr int kReservedEnd = kDepth;
inline constexpr int kExtraBase = kReservedEnd + 1;
/// Highest portable SRB binding: Qt RHI's minimum guarantee across backends.
inline constexpr int kMaxBinding = 31;
/// How many channel sizes the uniform block carries (`iChannelResolution[4]`).
inline constexpr int kChannelResolutionSlots = 4;

static_assert(kChannelCount == 8, "the shared GLSL headers declare iChannel0..7; grow them with this");
static_assert(kAudioSpectrum == 10 && kUserTextureBase == 11 && kWallpaper == 15 && kDepth == 16,
              "the shared GLSL headers pin these literal bindings; update every family with this table");
static_assert(kExtraBase <= kMaxBinding, "the reserved range must leave consumer slots");
static_assert(kChannelResolutionSlots <= kChannelCount, "the UBO cannot describe channels that do not exist");

/// The binding a canonical contract sampler must declare, or -1 when @p name
/// is not a contract sampler (a consumer-declared one, which the validator
/// then checks against the consumer range instead).
inline int expectedSamplerBinding(QStringView name)
{
    if (name.startsWith(QLatin1String("iChannel")) && name.size() == 9) {
        const int n = name.at(8).digitValue();
        return (n >= 0 && n < kChannelCount) ? kChannelBase + n : -1;
    }
    if (name.startsWith(QLatin1String("uTexture")) && name.size() == 9) {
        const int n = name.at(8).digitValue();
        return (n >= 0 && n < kUserTextureCount) ? kUserTextureBase + n : -1;
    }
    if (name == QLatin1String("uCursorSprite")) {
        return kUserTextureBase;
    }
    if (name == QLatin1String("uAudioSpectrum")) {
        return kAudioSpectrum;
    }
    if (name == QLatin1String("uWallpaper") || name == QLatin1String("uBackdrop")) {
        return kWallpaper;
    }
    if (name == QLatin1String("uDepthBuffer")) {
        return kDepth;
    }
    if (name == QLatin1String("uZoneLabels")) {
        return kConsumer;
    }
    return -1;
}

/// Whether @p binding is one a consumer may claim through setExtraBinding:
/// the single gap slot, or anything above the reserved range.
inline constexpr bool isConsumerBinding(int binding) noexcept
{
    return binding == kConsumer || (binding >= kExtraBase && binding <= kMaxBinding);
}

} // namespace PhosphorShaders::Bindings
