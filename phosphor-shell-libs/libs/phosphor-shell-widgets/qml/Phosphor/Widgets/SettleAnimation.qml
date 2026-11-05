// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.SettleAnimation, the shell's positional spring (A1 M4).
//
// Every positional or dimensional change in the chrome (a slider knob, a
// band's fill end, a toast travelling with its window, a pane's height)
// settles on the one `shell.settle` profile: a spring at omega 22, zeta
// 0.85, one 3 % overshoot, at rest in about 250 ms. A spring retargets
// from its current value and velocity, so a drag that keeps moving the
// target never restarts the motion (R7), which no bezier can do.
//
// The profile lives in the process's PhosphorProfileRegistry (the shell
// host registers it under `shell.settle`, and a user JSON at that path
// wins). A process without a registry falls through to the library
// default, which is the case in the module tests.
//
//   Behavior on x { SettleAnimation {} }

import org.phosphor.animation

PhosphorMotionAnimation {
    profile: "shell.settle"
}
