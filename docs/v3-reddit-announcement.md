# PlasmaZones v3.0: virtual screens, a real animation engine, and a long-overdue apology

Hey r/kde,

Before anything else: if you tried PlasmaZones during v1 or early v2, I'm sorry.
It crashed, drags died mid-gesture, windows didn't stay where you put them, and
snapping behaved like a coin flip. "It's young software" only excuses so much.
v3 is the first release I think it's honest to call stable, so let me tell you
how it got there.

To put it in numbers: v3 took about seven weeks, from late March to the release
on May 12, and landed roughly 1,700 commits. The entire project up to v2.8.8 was
about 1,760 commits total. So in under two months, v3 nearly doubled everything
that came before it. That is where the time went.

## Two people made this happen

[[USER 1]] and [[USER 2]] spent weeks filing detailed bug reports, testing
experimental builds, and reading journal logs with me. Snapping and autotiling
are reliable today because they refused to let the rough edges slide. If you're
reading this: thank you. This release is as much yours as mine.

## The stability story

Here is what actually got fixed.

The drag pipeline was rebuilt from scratch. The KWin effect used to keep its own
copy of drag state, and that copy went stale during settings reloads. That stale
state is what caused the infamous ~40-second "dead drag" windows. The daemon is
now the single source of truth for every drag decision, and the effect is just a
relay.

The Meta+F float toggle used to take 4 D-Bus hops across 3 processes and could
lag for seconds. It is now daemon-local and lands well under 50ms from keypress.

Smaller fixes throughout: snap-assist no longer shows windows from other virtual
desktops, per-activity layouts no longer lose to monitor defaults, fresh virtual
desktops get a sane mode instead of floating everything, and autotile ratio and
master-count adjustments actually persist now.

## What's new in v3

**Virtual screens.** Subdivide an ultrawide, or any monitor, into independent
logical screens. Each one gets its own layouts, autotile state, snap-assist,
OSDs, and shortcuts, and you can swap or rotate the regions on the fly.

**A real animation engine.** Window transitions run through a proper
offscreen-effect pipeline now. It ships with 47 shader transitions: 28 ported
from niri, 9 from Burn-My-Windows, and a batch of originals including an
audio-reactive cityscape and a Matrix-rain effect.

**Animation App Rules.** Assign motion curves, shaders, and per-event timings to
specific app window classes, or turn animations off for an app entirely.

**A unified overlay shell.** OSDs, snap-assist, the layout picker, the zone
selector, and the zone overlay share one surface now instead of five separate
layer-shell windows. Animations run independently and no longer cancel each
other, and click-through is clean.

**Krohnkite-style autotile reorder.** Drag a tiled window onto another to insert
or swap it live. There is also an unlimited-stack overflow mode.

**The Phosphor SDK.** The internals are reusable LGPL libraries now, and a
compositor SDK lets non-KWin Wayland compositors like river host the placement,
tiling, and overlay services.

Also in the release: configurable layout-cycle ordering, per-mode disable lists
for monitors, desktops, and activities, a global auto-assign toggle, and
settings pages that scroll at the same speed as the rest of System Settings.

## A website

We finally have one: [[WEBSITE URL]]. Docs, screenshots, and install
instructions are there.

---

PlasmaZones is Qt6/KF6, KDE Plasma, Wayland-only.
Source and releases: https://github.com/fuddlesworth/PlasmaZones

If v3 still bites you somewhere, file it. The last two releases proved that is
exactly how this thing gets good. Thanks for sticking around.
