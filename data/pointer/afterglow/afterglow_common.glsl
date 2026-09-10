// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Constants the Afterglow buffer pass and main pass must agree on. Both
// stages include this rather than repeating the numbers, since the idle cut
// is the pack's liveness guarantee and the two halves of it (the canvas
// energy in buffer.frag, the coverage in effect.frag) have to close on the
// same clock. Included with the QUOTED form: that is the one the include
// resolver looks up beside the including file, on every host; the angle
// form searches only the shared roots.

#ifndef PLASMAZONES_AFTERGLOW_COMMON_GLSL
#define PLASMAZONES_AFTERGLOW_COMMON_GLSL

// The idle cut: 1 until the pointer has rested kIdleCutStart, exactly 0 at
// kIdleCutSeconds, which is inside the metadata trailSeconds (1.0) so the
// last live frame is already clear on any refresh rate.
const float kIdleCutStart = 0.6;
const float kIdleCutSeconds = 0.9;

// The coverage floor and the persistence cap are one decision, not two.
//
// The canvas is an 8-bit target and its decay is per frame, so the energy
// stalls wherever v * persistence rounds back to v, which is at every level
// under 0.5 / (1 - persistence) LSB: 4/255 at 0.87. That residue never
// clears on its own while the pointer moves, so the main pass must cut
// coverage to zero at a floor ABOVE the stall level, and the persistence
// must stay low enough that the stall level is under the floor: 0.87 stalls
// at 0.0157 against a floor of 0.02, while 0.92 would stall at 6/255 =
// 0.0235 and leave a permanent smear.
//
// The cap also keeps the far end of a moving stroke under the floor by the
// time the damage rect ends behind the pointer (the rect covers one
// trailSeconds of samples): at 30 Hz that is 30 frames, and 0.87^30 = 0.015
// is under the floor where 0.9^30 = 0.042 was not.
const float kCoverageFloor = 0.02;
const float kMaxPersistence = 0.87;

#endif // PLASMAZONES_AFTERGLOW_COMMON_GLSL
