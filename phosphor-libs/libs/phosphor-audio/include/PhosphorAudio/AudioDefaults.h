// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace PhosphorAudio {

namespace Defaults {
inline constexpr int MinBars = 16;
inline constexpr int MaxBars = 256;
inline constexpr int DefaultBarCount = 64;
inline constexpr int DefaultFramerate = 60;
inline constexpr int MinFramerate = 30;
inline constexpr int MaxFramerate = 144;

// CAVA analysis parameters (see SpectrumOptions). Ranges mirror upstream
// cava's accepted/recommended values; the settings schema clamps to the
// same bounds so both layers agree on the canonical range.
inline constexpr bool DefaultAutosens = true;
inline constexpr int DefaultSensitivity = 100; // percent; initial gain when autosens is on
inline constexpr int MinSensitivity = 10;
inline constexpr int MaxSensitivity = 500;
inline constexpr int DefaultNoiseReduction = 77; // cava config form: 0-100
inline constexpr int MinNoiseReduction = 0;
inline constexpr int MaxNoiseReduction = 100;
// Lower bound of the analyzed band. Max 500 stays below MinHigherCutoffHz so
// lower < higher holds for every pair of in-range values (cava rejects
// lower_cutoff_freq >= higher_cutoff_freq).
inline constexpr int DefaultLowerCutoffHz = 50;
inline constexpr int MinLowerCutoffHz = 20;
inline constexpr int MaxLowerCutoffHz = 500;
inline constexpr int DefaultHigherCutoffHz = 10000;
inline constexpr int MinHigherCutoffHz = 1000;
inline constexpr int MaxHigherCutoffHz = 20000;
inline constexpr bool DefaultMonstercat = false;
inline constexpr bool DefaultWaves = false;
inline constexpr bool DefaultReverse = false;
// Provider-side exponential smoothing applied on top of cava's own
// noise_reduction: the fraction of the previous frame retained per update.
// 0 disables the extra pass; capped below 1 so the spectrum always converges.
inline constexpr double DefaultExtraSmoothing = 0.5;
inline constexpr double MinExtraSmoothing = 0.0;
inline constexpr double MaxExtraSmoothing = 0.95;
}

} // namespace PhosphorAudio
