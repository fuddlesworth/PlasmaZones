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

// The most of the canvas that may survive one frame. Decay is per frame
// while the damage rect behind the pointer is wall-clock (trailSeconds), so
// at a low refresh rate a slower decay leaves the stroke's far end visible
// where the rect ends and it is cut flat: at 30 Hz, 0.9 over the 30 frames
// of one window is under the coverage floor, 0.95 was not.
const float kMaxPersistence = 0.9;

#endif // PLASMAZONES_AFTERGLOW_COMMON_GLSL
