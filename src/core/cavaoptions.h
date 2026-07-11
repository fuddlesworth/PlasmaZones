// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "isettings.h"

#include <PhosphorAudio/IAudioSpectrumProvider.h>

namespace PlasmaZones {

/// Assemble the CAVA provider's full parameter set from ISettings. The
/// provider normalizes (clamps/sanitizes) on apply, so values pass through
/// verbatim; the representation differences (channel-mode string → enum,
/// "auto" input method → empty detect sentinel, smoothing % → fraction) go
/// through the shared phosphor-audio conversion helpers so every in-process
/// consumer (daemon overlays, settings-app preview) maps them identically.
/// The KWin effect and the editor cannot use this directly (they read the
/// settings over D-Bus, not through ISettings) but mirror the same field set.
inline PhosphorAudio::SpectrumOptions cavaOptionsFromSettings(const ISettings* settings)
{
    PhosphorAudio::SpectrumOptions opts;
    opts.barCount = settings->audioSpectrumBarCount();
    opts.framerate = settings->shaderFrameRate();
    opts.autosens = settings->audioAutosens();
    opts.sensitivity = settings->audioSensitivity();
    opts.noiseReduction = settings->audioNoiseReduction();
    opts.lowerCutoffHz = settings->audioLowerCutoffHz();
    opts.higherCutoffHz = settings->audioHigherCutoffHz();
    opts.monstercat = settings->audioMonstercat();
    opts.waves = settings->audioWaves();
    opts.channelMode = PhosphorAudio::channelModeFromString(settings->audioChannelMode());
    opts.reverse = settings->audioReverse();
    opts.extraSmoothing = PhosphorAudio::extraSmoothingFromPercent(settings->audioExtraSmoothing());
    opts.inputMethod = PhosphorAudio::inputMethodFromSetting(settings->audioInputMethod());
    opts.inputSource = settings->audioInputSource();
    return opts;
}

} // namespace PlasmaZones
