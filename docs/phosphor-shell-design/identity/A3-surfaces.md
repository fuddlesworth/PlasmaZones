<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# A3 — Phosphor shell: non-bar surfaces

Behaviour, layout and choreography for every non-bar surface. Written against what
the tree ships today in `libs/phosphor-shell-{launcher,control-center,notifications,osd,power}`
and `docs/phosphor-shell-design/mockups/*.svg`, which are DMS / Noctalia parity
clones. Everything below is designed *away* from those.

## Visual anchor

The look is the one the `data/surface` and `data/overlays` packs already share:
dark navy glass with a coloured spectrum running through it. `phosphor-glass`
(navy over blur with a brand-gradient response), `phosphor-motes` (drifting
sparks in the spectrum colours), `phosphor-flux`, `prismata` and
`spectrum-bloom` (the same gradient used as flowing light on zone overlays).
Every shell surface is built from those two materials:

- **Ground**: navy `#0B1730`, abyss `#070F22` or void `#050916` over the
  compositor's blur, never opaque grey, never a white card.
- **Spectrum**: one gradient `cyan #22D3EE → blue #3B82F6 → purple #A855F7 →
  rose #F43F5E`, drawn as thin lines (edges, bands, underlines, outlines).
  Light-coloured lines on dark glass are the only chrome; there are no drop
  shadows and no filled buttons.

The spectrum is used **structurally**: a position along it means something
(a value, a progress, a state, an urgency). It is never used as decoration.

| Colour | Hex | Meaning |
|---|---|---|
| cyan | `#22D3EE` | idle / informational / the focused thing at rest |
| blue | `#3B82F6` | active interaction / selected / in progress |
| purple | `#A855F7` | attention that can wait (unread, pending, charging) |
| rose | `#F43F5E` | urgent / destructive / at a limit (muted, 100%, critical battery, shut down) |

A "spectrum position" is a 0..1 sample of that gradient and is how numeric
intensity is coloured (volume 0..100, battery 100..0, and so on). Text is
`#E6EDFF` primary and `#94A3B8` secondary.

## Motion vocabulary

Envelopes are asymmetric: fast in, long shaped out. Five named primitives,
used by every surface. Durations and curves live in the consistency table at
the end, which is the single swap point shared with A1 (identity-motion).

- **Enter**: content appears in place. Opacity 0 → 1 quickly; the surface's
  main edge line draws itself from a meaningful point outward (the caret, the
  value, the band's centre). Small one-axis translate at most; never a scale-up.
- **Settle**: the short overshoot at the end of an enter (edge line at 140%
  brightness dropping to 100%) or of a value change.
- **Hold**: the resting state.
- **Release**: the exit. Opacity and line brightness fall on a fast-start,
  long-tail ease-out; nothing translates on exit. A release interrupted by a
  new enter resumes from its current brightness, never from zero.
- **Follow**: a line or readout tracking a live value or a moving window: the
  fill end moves, the band travels to the newly focused window, the readout
  rides the fill point.

**Pulse**: a 90 ms settle on an edge or glyph, used for live updates while a
surface is already up. Rate limited to one per 120 ms.

Placement map: the compositor's own model of the active screen (zones for
snapping, the algorithm's rects for tiling, the strip for scrolling). Several
surfaces draw a miniature of it. Miniature scale is always the screen's
work-area aspect at a fixed width (stated per surface) with 2 px gaps between
rects, 3 px radius, 1 px spectrum outline.

---

## 1. Launcher

### a) The idea

The launcher is a **viewfinder over the placement map**, not a slab of results.
The query field sits at the top of a full-height column on the left; the right
two-thirds of the surface is a live miniature of the current screen's placement
map. The "Windows" provider draws each result *in its real rect* on that
miniature, coloured on the spectrum by match score, and Enter focuses that
rect. The "Apps" provider does the reverse: the miniature shows where the app
*will* land (the engine's next-placement answer for this screen, mode and
desktop), so launching is a placement act, not a blind spawn. Files, Clipboard
and Calc collapse the miniature to a single preview pane in the same region.

### b) Layout

- Surface: 1040 × 620 px at 1× (clamped to 78% × 72% of the work area on small
  screens), centred horizontally, top edge at 14% of screen height. Radius 14.
  Ground: abyss `#070F22` at 92% over the compositor's blur. Edge: 1 px cyan
  line at 35% alpha, 100% on the top edge only (the query is the source).
- Left column: 340 px wide. Query field 56 px tall, 20 px type, text `#E6EDFF`,
  caret cyan, placeholder `#94A3B8`. Below it the result list at 44 px rows:
  20 px icon, 14 px title, 12 px secondary in `#94A3B8`. Provider is NOT a pill
  row. It is a 12 px uppercase tracked label above the list ("WINDOWS",
  "APPS"...) that changes as the query is classified, plus the manual prefix
  characters (`'`, `>`, `=`, `~`, `@` for windows/shell/calc/files/clipboard).
- Right region: 700 × 620, holds the placement-map miniature at 640 px width
  (aspect of the screen work area), centred, with 30 px inset. Under the
  miniature a single 12 px status line: mode name and desktop, e.g.
  `Scrolling · desktop 2 · 6 columns`, numeric-tabular.
- Scrolling mode miniature: the strip is drawn as a horizontal ribbon that
  extends past the miniature's frame on both sides at 40% alpha, so off-screen
  columns are visible and reachable. Tiling and snapping draw the rects inside
  the frame.
- Footer: none. Hints live in the query placeholder (`Type to search · ' windows
  · > run · = calc`), disappearing on first keystroke.
- Numeric-tabular: the status line, calc results, file sizes, clipboard ages.

### c) Choreography

- Open (0 → 220 ms): ground enters in place; the top edge line draws from the
  caret outward left and right until it spans the full edge; the miniature
  rects enter staggered 12 ms apart in placement order, each with a pulse. No
  translate, no scale.
- Live update: as the query changes, result rows do not reflow with animation.
  Rows are replaced in place; each new row's left 2 px line pulses blue. On the
  miniature, matching rects follow their new spectrum position; rects that stop
  matching drop to 25% outline alpha and stay drawn so the map keeps its shape.
- Selection move (Up/Down): the selected row's left edge is a 2 px blue line
  that *slides* 44 px; the corresponding miniature rect gains a 2 px blue
  outline with the same timing.
- Close on launch: the surface releases. The selected miniature rect does not
  release with it. It grows to the real screen rect, stops there as a 1 px
  cyan outline around the window that just took focus or launched, holds
  400 ms and releases. This is the only time a launcher element translates and
  scales; it is a placement gesture, not a UI transition.
- Close on Escape: release, nothing else.
- Interruption: re-toggling during release resumes from current alpha,
  keeping the query text. A new keystroke during open cancels the stagger and
  snaps everything to hold.

### d) Interaction

- Pointer: hover over a result row lights its edge cyan (no fill change); hover
  over a miniature rect lights the row it belongs to. Clicking a miniature rect
  that has no result (empty zone / empty strip slot) with an app selected
  launches the app *into that rect* (the engine is asked to place at that
  zone / column / tile index). Drag a result row onto a miniature rect does
  the same with a 6 px blue outline preview during drag.
- Keyboard: Up/Down rows; Left/Right move the selection *spatially* on the
  miniature (nearest rect in that direction), which for Windows means "the
  window to the left"; Tab cycles providers in the fixed order
  apps → windows → files → clipboard → calc; Shift+Tab reverse; Enter primary;
  Alt+Enter secondary (windows: close; apps: launch floating; files: reveal in
  folder; clipboard: paste plain); Ctrl+1..9 pick the Nth row; Ctrl+Enter with
  Apps launches into the *selected* rect of the miniature rather than the
  engine's default. Escape clears the query first, closes on empty.
- Gesture: three-finger swipe up on the touchpad opens; swipe down closes.
- Compositor-native: the launcher is a layer surface on the `overlay` layer and
  takes exclusive keyboard focus, but it never takes pointer focus from the
  placement map underneath it. Because the compositor is ours, the miniature
  is the engine's live state, not a screenshot, so a window closing while the
  launcher is open removes its rect with a release.

### e) Spectrum

- Match score → spectrum position: weak matches cyan, strong blue, the top
  result purple. Rose appears only on a Windows result whose window is
  unresponsive (compositor ping timeout) or on a calc error.
- Provider label colour follows the same rule: idle cyan, a classified query
  blue.

### f) Not copied

No provider pill bar. No centred slab with a centred icon grid. No fullscreen
"skin" and no bar-connected "skin"; there is one launcher whose right region
is the placement map. No result-row keyboard hint footer. No emoji provider
(deferred, and if it lands it is a `:` prefix, not a pill).

---

## 2. Control center

### a) The idea

The control center is **a real tile placed by the engine**. It is not a popout
anchored to a bar button. Opening it asks the placement mode for a rect (snapping:
the zone nearest the bar's control-center button; tiling: a new leaf inserted at
the focused window's position; scrolling: a new 420 px column inserted right of
the focused column) and the control center is a shell window placed there,
including the mode's gaps and the user's surface pack. Closing it removes the
tile and the mode reflows. The control center therefore obeys the user's own
layout rules and looks like the rest of their windows.

### b) Layout

- Size: the engine's rect. Minimum content width 380 px; the control center
  asks for 420 × 560 when the mode lets it (scrolling column width, snapping
  zone). In tiling it takes whatever leaf it gets and stacks its sections.
- Ground: navy `#0B1730` at 96% over blur. Radius follows the surface pack's
  corner radius (default 8 to match `border-*` packs). Edge: whatever the
  user's surface pack draws for a focused window. The control center has no
  chrome of its own.
- Content is a single vertical list of **rails**, 52 px tall, full width, 16 px
  side padding, 8 px between rails. Each rail: 20 px glyph, 14 px label,
  right-aligned 13 px numeric-tabular value in `#94A3B8`, and a 2 px spectrum
  underline across the rail's full width which *is* the slider for the
  continuous rails and is the on/off line for toggles. There are no 2-up
  toggle tiles and no separate slider tiles.
- Rail order (fixed): Output volume, Input volume, Brightness, Night light,
  Wi-Fi, Bluetooth, VPN, Power profile, Keep awake, Do not disturb, Airplane.
  Below the rails, one 36 px footer row of text actions: `Wallpaper` ·
  `Display` · `Settings`, 13 px, cyan.
- Expanding a rail (Wi-Fi, Bluetooth, Power profile, Night light) grows it in
  place to hold its detail list (36 px rows) and the other rails shift down. In
  scrolling mode the column grows taller only up to the strip height; then the
  rail list scrolls. Only one rail is expanded at a time.
- Header: none. The clock and screen name belong to the bar. The top 8 px of
  the content is the top edge line.

### c) Choreography

- Open: the engine places the tile with the user's normal placement animation
  (this is deliberate: the control center opens exactly like a window). Inside,
  rails enter bottom-to-top with a 10 ms stagger, each underline drawing from
  its value point outward (a slider at 62% draws from x=62% both ways).
- Live update: a value change from elsewhere (a hardware key, another device)
  pulses the rail's glyph and the underline follows to the new value. A toggle
  flips with a pulse on its underline.
- Rail expand: height change with the detail rows entering at an 8 ms stagger.
  Collapse: the detail rows release before the height shrinks.
- Close: the tile leaves with the user's close animation. The mode reflows;
  nothing is left behind.
- Interruption: toggling during open reverses in place using the mode's normal
  cancel path.

### d) Interaction

- Pointer: drag anywhere on a continuous rail's underline sets the value; the
  glyph is the toggle target for volume/brightness rails (mute / auto). Hover
  lights the rail's edge cyan. Scroll wheel over a continuous rail steps it by 5.
  Because the control center is a real tile, it can be *moved and resized* like
  any window by the mode's normal gestures, and the user can pin it (Ctrl+P)
  so it stays as a persistent tile; the bar button then toggles focus rather
  than open/close.
- Keyboard: Up/Down between rails, Left/Right change a continuous value by 5
  (Shift: 1), Space toggles or expands, Enter expands, Escape collapses then
  closes, Home/End, `m` mutes the focused audio rail. Typing a letter jumps to
  the first rail whose label starts with it.
- Gesture: two-finger horizontal swipe on a continuous rail adjusts it.
- Compositor-native: the tile is placed in the same mode and desktop the user
  is on; switching desktop leaves it behind like any window (unless pinned
  sticky). In scrolling mode it is inserted as a column and the strip scrolls
  to reveal it with the normal centre-on-focus behaviour.

### e) Spectrum

- Continuous rails colour their underline by spectrum position of their value:
  0..100 → cyan..rose. A muted output is a rose glyph with a dark underline.
- Toggle rails: off = `#94A3B8` at 40%, on = blue, "on with attention" (Wi-Fi
  connecting, Bluetooth pairing) = purple, error = rose.
- Battery (in the Power profile rail's value) samples the spectrum inverted:
  full cyan, 20% purple, 10% rose.

### f) Not copied

No 2-up toggle tile grid. No separate slider tiles. No inverted-corner bar
socket; the control center is not attached to the bar. No header with clock and
screen. No "tap a tile to expand" hint text.

---

## 3. Notifications: toasts and the notification center

### a) The idea

Toasts are **a line on the edge of the window they concern**. A notification
from an app that has a visible window draws as a 2 px spectrum band on the top
edge of that window's rect (purple, rose for critical), with the toast card
hanging 8 px below that band inside the window's rect. If the app has no
visible window, the toast hangs from the top edge of the *screen* at the
horizontal position of the app's bar entry (or centred if none). The
notification center is a **scrolling-strip column**: opening it inserts a
column at the left end of the strip (or a left zone / left leaf in the other
modes) containing the history, with each notification as a row you can drag
out into the placement map to open the app there.

### b) Layout

Toast:
- Card 360 × auto (min 64, max 168 px). Radius 10. Ground abyss at 94%. Edge:
  1 px on the top only, spectrum-coloured by urgency, plus the 2 px band on the
  window edge above it. Content: 28 px app icon, 13 px app name in `#94A3B8`
  uppercase tracked, 14 px summary in `#E6EDFF`, 13 px body (max 3 lines),
  actions as 13 px cyan text separated by ` · ` on one row. Image
  notifications show the image 64 × 64 at the right.
- Stack: toasts for the same window stack downward inside the window's rect, 8
  px apart, newest on top, max 3 then "+N" in the band label. Toasts for the
  screen edge stack downward from the top edge, centred on their anchor x.
- Right-side age: 12 px numeric-tabular `2m`, `18m`, `1h`.

Notification center:
- A column, 380 px wide (scrolling: a strip column; tiling: a left leaf;
  snapping: the leftmost zone or, if the layout has one column only, a 380 px
  overlay on the left edge; that overlay case is the only non-tile fallback).
- Content: top row is a 40 px header with `Notifications` 14 px, the DND toggle
  as a single glyph, and `Clear` as text. Groups by app with a 12 px uppercase
  tracked group label and the count in numeric-tabular. Rows 64 px, same
  anatomy as the toast card but without the band. Group headers are sticky.
- Empty state: no text. The column ground shows the `phosphor-glass` sweep and
  nothing else.

### c) Choreography

- Toast arrive: the band on the window edge draws first, from its centre
  outward to the full window width, then the card enters in place under it.
  No slide-in from the right.
- Toast live: a replacement (same notification id updated) pulses the band and
  swaps content in place; a progress-hint notification follows its progress
  value as the band's fill length.
- Toast timeout: the card releases; the band then **retracts** back toward its
  centre over a longer envelope, so a glance at the window edge still shows
  where something happened for a moment. Critical bands do not retract until
  the toast is dismissed.
- Toast dismiss (click X, swipe, Escape): card releases; band retracts on the
  short envelope.
- Center open: inserted with the mode's placement animation; rows enter
  top-to-bottom with 8 ms stagger. Center close: mode's close animation.
- Interruption: a new toast while an old one is retracting stops the
  retraction where it is and inserts the new card above the old one.

### d) Interaction

- Pointer: click a toast to invoke its default action and focus the window (the
  focus change is compositor-native and immediate). Middle-click dismisses.
  Drag a toast: the card lifts 2 px and a 6 px blue outline previews the rect
  under the pointer on the placement map; drop into a zone / tile / strip slot
  opens the app there (the engine places the app's window at that rect; if the
  app already has a window it is *moved* there). Drag off the screen edge
  dismisses. Hover pauses the timeout (the band holds).
- Keyboard (global chords, remappable): `Super+N` opens/closes the center;
  `Super+Shift+N` invokes the newest toast's default action; `Super+Escape`
  dismisses the newest toast; with the center focused Up/Down rows, Enter
  action, Delete dismiss, `d` toggles DND, `c` clears the group, Left/Right
  moves between groups.
- Gesture: two-finger swipe right on a toast dismisses it; swipe left invokes
  the default action.
- Compositor-native: the band is drawn by the compositor on the window's rect,
  so it follows the window when it moves, is clipped by the window's own
  surface-pack corner radius, and it is visible on a window on another
  desktop's strip preview in the bar. A toast for a minimized window anchors
  at the bar entry instead.

### e) Spectrum

Urgency: low cyan, normal purple, critical rose. Unread count in the center's
group label is purple; zero is `#94A3B8`. DND active turns the header glyph
rose and all bands to 30% alpha.

### f) Not copied

No top-right toast stack. No slide-in-from-right. No "TOASTS" region on the
notification-center popout. No DND text button. No history popout anchored to
the bar clock. Toasts never appear over a window they do not concern.

---

## 4. OSDs (volume, mic, brightness, caps, idle, power profile, media)

### a) The idea

An OSD is **a value drawn on the edge that concerns it**. Output volume and
mic are a horizontal line along the bottom edge of the *focused* window
(the window that will actually make the sound), brightness is a vertical
line along the screen's right edge (the physical panel), caps lock is a
band on the top edge of the focused window (where the text is going), idle
inhibit and power profile are a short band on the bar's clock segment, and
the media OSD is a 2 px band on the bottom edge of the player's window (or
the screen if none) whose length is playback position. There is no centred
pill.

### b) Layout

- Value band: 3 px thick, running the full length of the chosen edge, inset 0 px
  (it sits *on* the edge, over the surface pack's border). Filled portion is
  the spectrum colour at the value's position; the unfilled remainder is the
  same hue at 15%. The band is rounded to the window's corner radius at the
  ends.
- Readout: a 22 px numeric-tabular figure in `#E6EDFF` with a 12 px glyph to its
  left, placed just inside the edge at the band's fill point (so `62` sits at
  62% along the bottom edge, 10 px above the band). It follows the value.
  Ground: none; the readout has a 1 px abyss text outline for legibility over
  bright content.
- Caps lock: band only plus a 12 px uppercase `CAPS` at the top-right corner
  inside the window. Idle inhibit: band under the clock, `AWAKE` 11 px. Power
  profile: band under the clock in the profile's colour with the profile name
  11 px. Media: band + a 12 px title label at the band's left end, and the
  `▶` / `⏸` glyph replaces the figure.
- Stacking: different OSDs concern different edges so they do not stack.
  Two bottom-edge OSDs (volume and mic) split the edge: volume on the left
  half, mic on the right half, with a 12 px dark gap at the middle.

### c) Choreography

- Show: the band draws from the fill point outward in both directions (a
  volume jump to 62 draws from x=62% toward 0 and 100), settles, and the
  readout enters in place.
- Live update: each key press moves the fill end and the readout together
  (follow); the band pulses at the new fill point. Holding a key gives
  continuous movement with the pulse rate limit.
- Hide: after 1.2 s of no change the readout releases, then the band retracts
  toward its fill point on the long envelope. A mute or a limit hit (0 or 100)
  holds the band 600 ms longer before retracting.
- Interruption: a key press during retraction re-draws from the current
  length. Focus moving to another window while a volume OSD is up moves the
  band to the new window's bottom edge (follow); the line travels, it does not
  re-appear.

### d) Interaction

- Pointer: the band is not interactive by default (it must never steal a
  click from the window edge). Hovering the readout for 300 ms enables a
  drag on it that sets the value.
- Keyboard: the hardware keys. Additionally, while an OSD is up, `Super+scroll`
  adjusts the value it shows.
- Gesture: none beyond the hardware keys.
- Compositor-native: the band is rendered by the compositor on the window's
  edge, so it follows moves and resizes and respects the surface pack's
  corner radius. The compositor knows which window will make sound (the
  focused window, or the window that owns the active PipeWire stream if that
  differs; the OSD prefers the stream owner). A per-app volume change draws on
  that app's window edge while the global volume draws on the focused window.

### e) Spectrum

Fill colour by spectrum position of the value: 0..100 cyan..rose, so a loud
volume reads hot. Mute: band goes rose at 40% with a 2 px gap every 12 px
(a dashed line, the only dashed element in the shell). Brightness: the same
rule, bright is hot. Caps: blue. Idle inhibit: purple. Power profile:
power-saver cyan, balanced blue, performance rose. Media: blue while playing,
`#94A3B8` paused.

### f) Not copied

No bottom-centre pill. No card with glyph, label and progress bar. No OSD
that is a window over the desktop. No "one OSD at a time" swap: different
edges coexist.

---

## 5. Power / session menu

### a) The idea

The power menu is **a column of words over your dimmed desktop**. Invoking it
does not draw a dialog; the compositor dims and desaturates every window and
the session actions appear as a single vertical column of words on the left
edge of the screen, coloured on the spectrum by how destructive they are. The
desktop stays visible, dim, behind the words, so the user sees what they are
about to lose.

### b) Layout

- The column is 320 px wide, anchored to the left screen edge, full height.
  Ground: void `#050916` at 70%, no radius, right edge 1 px cyan line. The
  rest of the screen is the dimmed desktop with no ground.
- Six rows, 72 px tall, vertically centred as a block: `Lock`, `Suspend`,
  `Hibernate` (hidden if logind says no), `Log out`, `Restart`, `Shut down`.
  24 px type, `#E6EDFF`. Each row has a 3 px left edge line in its spectrum
  colour. The single-letter hint is the underlined first letter of the word;
  there is no chip.
- Under the block, 13 px `#94A3B8` lines, numeric-tabular: uptime, battery,
  pending updates count, and the number of open windows on this and other
  desktops ("14 windows · 3 desktops"), because that is what is lost.
- Confirmation for Restart / Shut down / Log out: the row expands in place to
  96 px and its label becomes `Shut down · Enter again`, with a 3 s countdown
  drawn as the left line shortening.

### c) Choreography

- Open: the desktop dims to 35% brightness and 40% saturation on the long
  envelope; the column enters at 200 ms; rows enter top-to-bottom with 30 ms
  stagger, each left line drawing from the middle of the row.
- Live: selection moves as the row's left line widening to 6 px and the label
  brightening; the previously selected row's line returns to 3 px.
- Close (Escape or click on the desktop): the column releases, the desktop
  returns to full brightness with a short settle.
- Commit: the chosen row's line spreads to fill the whole screen (a wipe from
  the left edge) in its spectrum colour at 20%, then the action runs. Lock
  reuses the lockscreen's own open choreography instead.

### d) Interaction

- Pointer: hover selects, click commits (destructive actions require a second
  click within 3 s). Click on the dimmed desktop cancels.
- Keyboard: Up/Down; the underlined letter jumps and commits after the
  confirmation step; Enter commits; Escape cancels. Default focus is `Lock`.
- Gesture: none.
- Compositor-native: the dim is the compositor's, so it applies to every screen
  at once and the column appears only on the screen with the pointer. Modal
  input grab across all screens.

### e) Spectrum

Lock cyan, Suspend and Hibernate blue, Log out purple, Restart and Shut down
rose. Battery under the block samples the inverted spectrum.

### f) Not copied

No 6-tile grid. No letter-hint chips. No centred modal popout with a dimmed
backdrop as its own surface. No `SESSION` caption.

---

## 6. Lockscreen

### a) The idea

The lockscreen shows **the layout you left as an outline**. On lock the
compositor takes the placement map and draws every window rect as a static
1 px spectrum outline with an 8% navy fill and the app icon centred, on a
void ground. The clock and the auth field live in the largest empty region
the placement map has (or the centre if none). The outlines are the
lockscreen's only decoration. Unlocking fills them back in with the real
windows, which are already where they were.

### b) Layout

- Full-screen per output. Ground void `#050916` with the wallpaper at 12%
  saturation and 20% brightness beneath the outlines.
- Outline rects: the placement map at 1:1. Each has the window's app icon at
  32 px centred at 30% alpha. No titles.
- Clock: 96 px numeric-tabular, `#E6EDFF`, in the region described above, with
  the date 16 px `#94A3B8` under it. Both left-aligned to the region.
- Auth field: 320 × 44, radius 8, abyss ground, 1 px cyan edge, placed 24 px
  below the date. No user avatar. The user name is 13 px `#94A3B8` above the
  field only when more than one user session exists.
- Bottom-left, 13 px `#94A3B8`, numeric-tabular: battery, keyboard layout,
  and the count of unread notifications as a purple figure. Bottom-right: the
  media title and a 2 px band whose length is playback position; no album art.

### c) Choreography

- Lock: the windows release to their outlines on the long envelope (fill
  100% → 8%, the outline drawing itself around each rect); the clock enters
  at 800 ms. The outlines then hold, static.
- Live: a notification arriving pulses the outline of its window purple and
  increments the unread figure. Media progress follows the band.
- Wrong password: the auth field's edge pulses rose and returns to cyan; the
  field does not shake.
- Unlock: the outlines fill back in to the real windows (fill 8% → 100% with
  a settle on the outline); the clock releases. The windows do not slide or
  scale because they are already where they were.
- Interruption: a wake from sleep re-runs only the clock enter.

### d) Interaction

- Pointer: any movement wakes the display; clicking an outline puts its app
  name in the auth field placeholder (`unlock to return to Firefox`), nothing
  more.
- Keyboard: typing goes to the auth field without clicking it; Enter submits;
  Escape clears; `Super+L` re-locks (no-op here). Media keys work.
- Gesture: none.
- Compositor-native: `ext-session-lock-v1` implemented by our compositor, PAM
  in-process. Because the outlines are the engine's own rects, a layout change
  requested while locked (a monitor unplug) reflows the outlines with the
  mode's own reflow animation, dimmed.

### e) Spectrum

Field edge cyan idle, blue while authenticating, rose on failure. Outlines
cyan, purple for windows with unread notifications.

### f) Not copied

No wallpaper-forward lockscreen with a thin centred clock. No media card with
album art. No user avatar. No `Accessibility` and `EN · US` chip row.

---

## 7. Dashboard

### a) The idea

The dashboard is **the placement map of every desktop at once**, an overview.
Invoking it pulls back from the current screen to a grid of every desktop's
placement map, live, at reduced scale, with the current one outlined blue. It
replaces the widget-panel dashboard entirely: calendar, weather and media are
three fixed cells in the grid's last row, drawn in the same visual grammar as
the desktop cells, so the dashboard is one grid of things you can go to.

### b) Layout

- Full-screen per output. Ground void at 90% over blur.
- Grid: desktop cells at 1/4 screen scale (a 2560-wide screen gives 640 px
  cells), 24 px gaps, centred, wrapping at the screen width. Each cell is the
  desktop's live placement map with real window thumbnails inside the rects
  (compositor textures, not screenshots), a 1 px spectrum outline and a 12 px
  label top-left (`2 · Scrolling`). In scrolling mode the cell shows the strip
  extending past the cell edge at 40% alpha like the launcher miniature.
- Last row, always: `Calendar` (month grid, 13 px numeric-tabular days, today
  blue), `Weather` (22 px numeric-tabular temperature, 13 px condition, no
  icon art beyond one 20 px glyph), `Media` (title, artist, 2 px position
  band, three 13 px text transport glyphs). Each is the same cell size and
  outline as a desktop cell.
- A `+` cell at the end of the desktop cells adds a desktop.

### c) Choreography

- Open: the current screen's content shrinks into its cell while the other
  cells enter in place with a 20 ms stagger outward from the current cell.
  This is the single scale transition in the shell and it is the desktop
  itself moving, not a UI element.
- Live: windows moving on other desktops follow in their cells in real time.
- Close: the chosen cell grows to fill the screen; the other cells release.
- Drag a window between cells: the rect lifts 2 px and follows the pointer at
  cell scale; dropping into another cell runs the engine's cross-desktop move.

### d) Interaction

- Pointer: click a cell to go there; click a window thumbnail to go there and
  focus it; drag windows between cells; scroll wheel over a scrolling-mode
  cell pans its strip.
- Keyboard: arrows move between cells spatially, Enter goes, Escape closes,
  1..9 jump to desktop N, `n` new desktop, Delete on an empty desktop removes
  it. Tab moves between windows inside the selected cell.
- Gesture: four-finger swipe up opens, pinch-out on the trackpad closes into
  the selected cell.
- Compositor-native: this surface exists only because the compositor owns
  every desktop's textures and rects.

### e) Spectrum

Current desktop outline blue, others cyan, a desktop with an urgent window
rose, a desktop with unread notifications purple. Today in the calendar blue.

### f) Not copied

No widget panel with `Overview / Wallpaper / Weather / Media` tabs. No
wallpaper preview tile. No stat gauges (the `82` / `62` figures).

---

## 8. Wallpaper and theme picker

### a) The idea

The picker **tries the palette on your real layout**. Choosing a wallpaper
candidate does not show a preview thumbnail; it retints the live desktop
behind the picker (wallpaper swapped, extracted palette applied to the
surface packs, the bar and the shell) for as long as the candidate is
hovered or selected, with the real windows still there. Apply commits;
Escape returns to the previous look. The picker itself is a single row
along the bottom edge.

### b) Layout

- A 220 px tall strip anchored to the bottom screen edge, full width. Ground
  abyss at 90% over blur. Top edge 1 px cyan.
- Left 200 px: a vertical list of three 13 px words `Wallpaper`, `Theme`,
  `Cycle`, the active one blue with a 2 px left line.
- Middle: a horizontal scroller of 160 × 100 candidate thumbnails, radius 6,
  16 px gap, 1 px outline; the selected one has a 2 px blue outline and a
  five-swatch palette strip (14 px squares) under it, each swatch labelled
  with its hex in 11 px numeric-tabular. Theme view: the same scroller of
  palettes drawn as five-swatch tiles without images. Cycle view: an interval
  rail (same rail grammar as the control center) and a per-monitor toggle.
- Right 260 px: `Apply` 14 px blue text with a 2 px underline, `Apply on all
  screens`, `Light / Dark / Auto` as three words with the active one lit.
  Below them the fan-out target count as 13 px `#94A3B8` text ("12 targets").

### c) Choreography

- Open: strip enters in place, thumbnails enter with 10 ms stagger from the
  current wallpaper outward.
- Hover / select: the live desktop retints (follow) with every window edge
  pulsing once in the new accent as the palette lands. Moving off a candidate
  without applying returns the desktop on the long envelope.
- Apply: the strip releases; the desktop holds the new look; every window
  edge pulses once more.
- Interruption: a new hover during a retint blends from the current colours.

### d) Interaction

- Pointer: hover previews, click selects, double-click applies. Drag a
  thumbnail onto a screen (multi-monitor) applies it to that screen only.
- Keyboard: Left/Right candidates, Up/Down between the three views, Enter
  applies, Shift+Enter applies on all screens, Escape reverts and closes,
  `l` / `d` / `a` set light/dark/auto.
- Gesture: two-finger horizontal scroll on the scroller.
- Compositor-native: the retint is the compositor swapping the palette
  uniform for all surface packs at once, which no reference shell can do.

### e) Spectrum

Selected candidate blue. A candidate whose extracted palette fails contrast
(text on surface under 4.5:1) gets a rose outline and `Apply` reads `Apply
anyway`.

### f) Not copied

No modal window with `Wallpapers & Themes` title. No large preview pane with
the brand gradient. No `EXTRACTED PALETTE` and `FAN-OUT TARGETS` boxes; the
swatches live under the thumbnail and the fan-out is one figure.

---

## 9. Polkit prompt

### a) The idea

The prompt is **attached to the window that asked**. It appears as a 360 px
card hanging from the top edge of the requesting window's rect (identified
through the compositor's knowledge of which client's pid triggered the
request), with a rose band on that window's top edge. If the requester has
no window, the card hangs from the screen's top edge centred.

### b) Layout

- Card 360 × auto (min 140). Radius 10. Ground abyss at 96%. Edge 1 px rose
  on top, and the 2 px band on the window edge above it.
- Content: 13 px uppercase tracked `AUTHENTICATION` in `#94A3B8`, 14 px message,
  the action id in 12 px numeric-tabular `#94A3B8`, the password field
  (same anatomy as the lockscreen field), `Cancel` and `Authenticate` as text
  actions on one row.

### c) Choreography

- Open: band draws on the window edge, card enters. The requesting window is
  *not* dimmed; the rest of the screen is dimmed 20% for the card's lifetime.
- Wrong password: rose pulse on the field edge, as the lockscreen.
- Close: card releases; the band retracts on the short envelope.

### d) Interaction

- Pointer: click actions. Dragging the card is not allowed.
- Keyboard: focus lands in the field; Enter authenticates; Escape cancels.
- Compositor-native: exclusive keyboard focus; the requesting window's rect is
  followed if the window moves.

### e) Spectrum

Rose throughout; it is an interruption that grants power.

### f) Not copied

No centred dialog. No user avatar.

---

## 10. Keybind cheatsheet

### a) The idea

The cheatsheet is **drawn on the placement map**. Movement and placement
chords are shown as small chord labels on the actual rects of the current
screen (the "move window left" chord sits on the left edge of the focused
window's rect, "focus column right" sits between columns, "send to zone 3"
sits on zone 3), and only the non-spatial chords (launcher, control center,
lock, screenshot) are listed in a column on the right edge.

### b) Layout

- Full-screen overlay per output, ground void at 60% over the live desktop
  (the windows stay visible, dimmed 30%).
- Chord labels: 12 px numeric-tabular, `#E6EDFF`, in a 20 px tall pill with
  abyss ground at 90% and a 1 px cyan edge, placed at the rect edge or gap they
  act on. Label text is the chord as keys (`Super ←`), not a description; a
  hover or the selected label shows the description as 13 px text under the
  pill.
- Right column: 300 px, non-spatial chords as 32 px rows, key on the left in
  a pill, description on the right in 13 px.
- Top-left: 13 px `Scrolling · desktop 2` and a `Search` field that filters
  both the spatial labels and the column.

### c) Choreography

- Open: ground enters; labels enter outward from the focused window with a
  6 ms stagger. Close: everything releases.
- Live: pressing a chord while the sheet is up performs it *and* pulses its
  label blue, and the sheet stays up so the user can watch the placement map
  change. This is a learning mode.

### d) Interaction

- Pointer: hover shows a label's description; click performs the chord.
- Keyboard: type to filter; Escape closes; every real chord works.
- Gesture: none.
- Compositor-native: labels are positioned from the engine's rects.

### e) Spectrum

Labels cyan; the chord just performed blue; chords that cannot apply in the
current mode (a snapping chord in scrolling mode) are not shown; a chord
that is unbound because of a conflict is rose.

### f) Not copied

No categorised list-of-lists modal.

---

## Cross-surface consistency table

This table is the single swap point shared with A1 (identity-motion). If A1
lands different names, curves or durations, change them here and every
surface above follows.

| Token | Value | Notes |
|---|---|---|
| Ground: floating card (toast, polkit, launcher) | abyss `#070F22` at 92–96% over blur | |
| Ground: tile (control center, notification center) | navy `#0B1730` at 96% over blur | Takes the user's surface pack |
| Ground: full-screen (dashboard, power, cheatsheet, lock) | void `#050916` at 60–90% | Lock is 100% |
| Radius: card | 10 px | toast, polkit |
| Radius: launcher | 14 px | the one large radius |
| Radius: tile | surface pack's corner radius, default 8 | |
| Radius: thumbnail | 6 px | picker, dashboard rects |
| Radius: miniature rect | 3 px | |
| Radius: field | 8 px | lock, polkit; the launcher query has none (it is the top edge) |
| Edge line | 1 px spectrum colour at 35% alpha, 100% on the "source" edge | Never a drop shadow anywhere |
| Band (on a window edge) | 2 px (notification, polkit, caps) or 3 px (value OSD) | Drawn by the compositor, clipped to the window's radius |
| Selection line | 2 px on the left edge, blue, slides between rows | launcher, power (3 → 6 px), picker views |
| Type: readout | 22 px numeric-tabular | OSD, weather |
| Type: display | 96 px numeric-tabular (lock clock), 24 px (power rows) | |
| Type: title | 14 px `#E6EDFF` | |
| Type: secondary | 13 px `#94A3B8` | |
| Type: label | 12 px uppercase, +0.08 em tracking, `#94A3B8` | provider, group, `AUTHENTICATION`, `CAPS` |
| Type: chord | 12 px numeric-tabular in a 20 px pill | cheatsheet only |
| Numeric-tabular everywhere numbers change | ages, percentages, counts, hex, dates | |
| Motion: enter | opacity 0 → 1 in 120–140 ms, `OutQuint`; edge line draws from its source point in 90–220 ms, `OutExpo` | never scale |
| Motion: settle | line overshoots to 140% and returns by 200 ms | |
| Motion: hold | resting state | |
| Motion: release | `OutExpo`, 180–400 ms | never translate on exit |
| Motion: retract (band) | short 600 ms, long 1.4–2.5 s, `OutExpo`, back toward the band's source point | toast, OSD, polkit bands |
| Motion: follow | 90–160 ms `OutQuint` per value or focus change | fill ends, readouts, travelling bands, retint |
| Motion: pulse | 90 ms settle on a line or glyph | rate limited to one per 120 ms |
| Motion: slide (selection) | 110 ms `OutQuint` | the only translating UI element |
| Motion: placement | the mode's own placement animation | control center, notification center, launcher's final rect |
| Motion: scale | dashboard open/close only, 280 ms `OutQuint` | it is the desktop that scales |
| Motion: dim (desktop) | 600 ms `OutExpo` to 35% brightness / 40% saturation; return 350 ms with settle | power menu; polkit uses 20% dim |
| Stagger | 6–30 ms per item, spreading outward from the focus point | never top-to-bottom for its own sake |
| Hold residues | launched rect outline 400 ms; OSD band at a limit +600 ms | the only two |
| Interrupt rule | resume from current brightness or length; never restart from zero | |
| Hover | edge line turns cyan; fill never changes | |
| Destructive confirm | second activation within 3 s; the line shortens as the countdown | power, clear-all |

## Surface-pack hooks

Which pack draws what, by default, on which surface. Surface packs live in
`data/surface`; the spectrum overlays named here live in `data/overlays` and
are reused as shell grounds through the same compositor path. Today only
`border-audio` and `frosted-glass` declare `osd` / `popup` support in their
metadata. The table states the intended default assignments so the metadata
can be extended to match.

| Pack | Surface(s) | Role by default |
|---|---|---|
| `phosphor-glass` (surface) | launcher, toast, polkit, picker strip, notification center empty state | The ground material: navy glass over blur, brand-gradient response to bright content behind, slow sweep as the idle life of a surface. |
| `spectrum-bloom` (overlay) | dashboard cell outlines, launcher miniature outlines, lockscreen outlines | The spectrum drawn along a rect edge; the one outline shader every placement-map drawing uses. |
| `phosphor-flux` (overlay) | drop-target preview during any drag (toast, launcher, dashboard) | Flowing spectrum on the 6 px preview outline while a drag is live. |
| `prismata` (overlay) | picker live retint transition | The gradient crossing the screen as the palette lands. |
| `border-phosphor` (surface) | control center, notification center (as tiles) | Their window border, so they match the user's windows when the user has not picked another border pack. |
| `border-audio` (surface) | OSD value bands, media OSD, lockscreen media band | The band moves with the bass when the visualizer is on; static otherwise. Already declares OSD and popup support. |
| `border-pulse` (surface) | power menu selected row line, polkit band | A slow pulse on things awaiting a decision. |
| `focus-fade` (surface) | dashboard non-current cells, cheatsheet dimmed desktop, power-menu dimmed desktop | The dim of unfocused content. |
| `phosphor-motes` (surface) | dashboard ground, lockscreen empty regions | Motes drift between cells and between the lockscreen outlines. |
| `fireflies` (surface) | none by default | Opt-in alternative to `phosphor-motes` on the lockscreen. |
| `rain-glass` (surface) | none by default | Opt-in for the launcher and lockscreen ground; too busy behind text to be a default. |
| `frosted-glass` (surface) | fallback ground when `phosphor-glass` is disabled | Declares popup support already. |
| `glow`, `blur`, `shadow`, `opacity-tint`, `duotone`, `mosaic`, `rippled-glass`, `border`, `border-double`, `border-gradient`, `border-rgb`, `border-sweep`, `border-circuit`, `border-marching` | user-selectable on the two tile surfaces | Never a default. `shadow` is specifically off on every shell surface; the spectrum lines are the only chrome. |
