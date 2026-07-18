# Changelog

All notable changes to PlasmaZones are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [3.2.2] - 2026-07-18

### Added

- **Fuller support reports**: the support report archive now includes your rules, quick layouts, layout settings, and settings profiles, with home paths redacted, and the daemon's report gains a Rules section. Earlier reports were missing these files, which made several bug reports slow to diagnose ([#797](https://github.com/fuddlesworth/PlasmaZones/pull/797)).

### Fixed

- **Global animation settings work again**: since 3.2.0, window animations ignored the global Duration and Curve settings and always played at a fixed 200 ms. Windows follow the global settings again, and a per-event override still wins where you set one ([#797](https://github.com/fuddlesworth/PlasmaZones/pull/797)).
- **Cross-monitor moves no longer hop sideways**: moving a window down or up to another monitor could jump to a monitor beside the current one when your monitors have different heights. The move now only targets a monitor that actually lines up in the direction of the move ([#797](https://github.com/fuddlesworth/PlasmaZones/pull/797)).
- **Settings survive concurrent writers**: when the settings app and the daemon wrote the configuration at the same time, a stale in-memory copy could resurrect old values over a just-saved change, and an external change notification could silently drop unsaved edits. Both paths now stay coherent ([#797](https://github.com/fuddlesworth/PlasmaZones/pull/797)).
- **Per-page Reset and Discard stay on their page**: on the Decoration and Animations settings families, resetting or discarding one page could also revert its sibling pages. Each page now only touches its own settings ([#794](https://github.com/fuddlesworth/PlasmaZones/pull/794)).
- **Resetting OSD and Popup decorations restores the built-in look**: resetting the Decoration → OSDs or Decoration → Popups page removed the built-in card border and shadow instead of restoring them. The built-in chrome is now a default layer underneath your edits, so reset brings it back, and setups that had already lost it heal on their own ([#798](https://github.com/fuddlesworth/PlasmaZones/pull/798)).
- **Upside-down decorations on OpenGL**: window decorations could render flipped vertically on the OpenGL backend when two or more decoration packs were combined, and during show or hide transitions. The Vulkan backend was unaffected ([#798](https://github.com/fuddlesworth/PlasmaZones/pull/798)).

## [3.2.1] - 2026-07-17

### Added

- **Six new translations**: PlasmaZones is now fully translated into German ([#785](https://github.com/fuddlesworth/PlasmaZones/pull/785)), Georgian ([#786](https://github.com/fuddlesworth/PlasmaZones/pull/786)), Swedish ([#787](https://github.com/fuddlesworth/PlasmaZones/pull/787)), Dutch ([#788](https://github.com/fuddlesworth/PlasmaZones/pull/788)), Polish ([#789](https://github.com/fuddlesworth/PlasmaZones/pull/789)), and Russian ([#790](https://github.com/fuddlesworth/PlasmaZones/pull/790)).

### Fixed

- **See-through overlay cards**: the on-screen display, the layout picker, and the zone selector were drawn with a translucent body, so the desktop could bleed through them. Each card is now drawn opaque with a border and drop shadow that follow your theme in both light and dark. If you had customized these surfaces and they still look wrong after upgrading, open the Decoration → OSDs and Decoration → Popups pages and reset each to its default ([#792](https://github.com/fuddlesworth/PlasmaZones/pull/792)).
- **Decoration packs now follow theme colors on windows too**: a custom decoration pack on a window ignored the theme color options, so the neutral frame line and the theme-tinted glow or shadow only took effect on the overlay cards. All three now resolve on the window path as well ([#792](https://github.com/fuddlesworth/PlasmaZones/pull/792)).

## [3.2.0] - 2026-07-17

### Added

- **Theater, a widescreen layout**: keeps a single window centered at a width you set per monitor, so one window never has to stretch across a wide display. With more windows the focused one sits in a centered spotlight and the rest line up on rails in the margins on both sides, and focusing a rail window brings it into the spotlight ([#704](https://github.com/fuddlesworth/PlasmaZones/pull/704)).
- **Window appearance and gaps are now rules**: window borders, title bars, corner radius, border colors, and gaps are controlled through the rule system instead of separate per-mode settings pages. A new Windows page under the Appearance category edits the managed defaults, and per-window or per-context overrides are ordinary rules on the Rules page ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699)).
- **Match by placement mode**: a new Mode match condition matches snapped or tiled windows, so snapping and tiling can carry different gaps or appearance. Mode is a context condition, resolved per screen and layout ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699)).
- **Per-monitor gaps**: pick a monitor from the scope chip on the Gaps card to set that screen's gaps independently. The values are stored as a screen-scoped rule ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699)).
- **Separate focused and unfocused border colors**: the border color is now two actions, "Set focused border color" and "Set unfocused border color", each with its own swatch ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699)).
- **App-managed default rules**: the default rules (Default borders, Default title bars, Default gaps) cannot be deleted and stay pinned to the lowest priority ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699)).
- **Choose which windows the appearance defaults apply to**: the Borders and Title bars cards on the Appearance Windows page have an "Apply to" option with three scopes, tiled and snapped windows, all normal windows, and all windows. New installs default to tiled and snapped windows, so the default border and hidden title bar stay off the desktop, panels, popups, and on-screen displays. Existing setups keep their current scope and can switch at any time ([#721](https://github.com/fuddlesworth/PlasmaZones/pull/721)).
- **Show desktop animations**: peeking at the desktop can now play a full-screen transition. Pick a pack for the new "Peeked at Desktop" event on the Animations → Transitions → Desktop page. Hiding the windows plays the pack forward and bringing them back plays it in reverse. Every desktop transition pack works here, and three new packs ship with it, Peek Recede, Peek Blinds, and Phosphor Peek. While a pack is set, PlasmaZones takes over from KWin's own show desktop animation and hands it back when the pack is cleared ([#777](https://github.com/fuddlesworth/PlasmaZones/pull/777)).
- **Reset or discard one settings page at a time**: most settings pages now have a menu in the breadcrumb row with "Reset page to defaults" and "Discard changes on this page". Both stage the change so you can review it and then Save or Discard like any other edit, and discarding one page leaves your edits on other pages alone ([#726](https://github.com/fuddlesworth/PlasmaZones/pull/726)).
- **More starting points in the New Algorithm wizard**: the template picker offers nine templates instead of four, adding Aligned Grid, Dwindle (Memory), Cluster, Theater, and Deck so the resize-aware, split-remembering, custom-parameter, focus-driven, and overlapping layout styles each have a working example to build on. The blank skeleton's capability checkboxes now also cover script state, single window, and follows focus, and the generated body shows the tiny-area guard pattern ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Genie and Phosphor Siphon, two minimize-to-icon animations**: Genie pours the window into its task manager icon like the classic magic lamp, with the edge nearest the icon leading, and restoring pours it back out. Phosphor Siphon does the same journey as separated luminous streams that carry the brand gradient and shed ember sparks, each stream fading into the icon as it lands. A window that is not in any task manager drains into its own bottom edge instead ([#776](https://github.com/fuddlesworth/PlasmaZones/pull/776)).
- **Window decoration effects**: a new Decoration category adds shader effects that draw behind and around your windows, picked from a pack browser and saved as reusable sets. Glass, Frosted Glass, and Rippled Glass refract the real blurred backdrop, Blur softens it, and Drop Shadow and Glow sit behind the frame. Border packs replace the plain border with animated ones such as Sweep Border, Circuit Border, Pulse Border, RGB Cycle, Gradient Border, and Marching Ants, and effect packs such as Duotone, Fireflies, Focus Fade, Mosaic, and Rain on Glass tint or texture the window. A window uses either the plain border setting or a decoration pack, and any layer of a pack can be turned off on its own. Per-window and per-context overrides are ordinary rules ([#624](https://github.com/fuddlesworth/PlasmaZones/pull/624), [#757](https://github.com/fuddlesworth/PlasmaZones/pull/757), [#762](https://github.com/fuddlesworth/PlasmaZones/pull/762), [#764](https://github.com/fuddlesworth/PlasmaZones/pull/764), [#768](https://github.com/fuddlesworth/PlasmaZones/pull/768), [#770](https://github.com/fuddlesworth/PlasmaZones/pull/770)).
- **Audio-reactive window decorations**: the Audio Border pack drives its border from the audio spectrum through the same CAVA capture the shaders use, and the compositor animation shaders can read the spectrum as well. The full CAVA parameter set is now editable in settings, covering the bar count, smoothing, noise reduction, frequency range, and channel handling ([#624](https://github.com/fuddlesworth/PlasmaZones/pull/624), [#752](https://github.com/fuddlesworth/PlasmaZones/pull/752), [#763](https://github.com/fuddlesworth/PlasmaZones/pull/763)).
- **Virtual desktop switch animations**: switching virtual desktops can now play a full-screen transition. Turn it on for the new desktop switch event on the Animations → Transitions → Desktop page and pick a pack. Eleven packs ship with it, including Desktop Fade, Desktop Slide, Desktop Wipe, Desktop Cube, and Desktop Dissolve, and the motion follows the direction you switch. It is off by default ([#747](https://github.com/fuddlesworth/PlasmaZones/pull/747)).
- **Set window layer, a new rule action**: a rule can keep a matched window above or below other windows, in the spirit of Krohnkite's layers, and PlasmaZones puts the layer back the way it was when the rule stops matching ([#759](https://github.com/fuddlesworth/PlasmaZones/pull/759)).
- **Per-context autotile rule actions**: Set algorithm parameter, Set insert position, Set overflow behavior, and Set drag behavior, together with per-context overrides for the maximum window count, split ratio, and master count. A screen, virtual desktop, or activity can tune its tiling without moving the global default ([#733](https://github.com/fuddlesworth/PlasmaZones/pull/733)).
- **New match conditions for rules**: Tiled window count matches on how many windows are currently tiled, so a rule can switch algorithm as the count grows ([#703](https://github.com/fuddlesworth/PlasmaZones/pull/703)), and Active layout scopes a rule to the layout in use ([#733](https://github.com/fuddlesworth/PlasmaZones/pull/733)).
- **Centered, a single-window layout**: a bundled algorithm that holds the focused window centered at a width you set and keeps the rest behind it, built on new support for single-window layouts in scripts ([#703](https://github.com/fuddlesworth/PlasmaZones/pull/703)).
- **Back and forward navigation in settings**: the settings app remembers the pages you visit, so you can step back to where you were and forward again from the breadcrumb row ([#749](https://github.com/fuddlesworth/PlasmaZones/pull/749)).
- **Randomize and reset for algorithm and shader parameters**: the parameter editor gains a randomize control on each parameter and a button to randomize or reset a whole group at once, which makes it quick to explore how a shader or algorithm's settings look ([#737](https://github.com/fuddlesworth/PlasmaZones/pull/737)).
- **Discord community link**: the Links card on the About page now has a Discord Community button that opens the project's invite.

### Changed

- **The Rules page is one flat priority list**: rules show in a single, drag-reorderable list ordered by precedence, with the highest-priority rule on top, instead of per-section groups. Drag any rule up or down to set which one wins. The filter button narrows the list by source (system or user-created), category, and status. Search and the monitor strip narrow it further ([#720](https://github.com/fuddlesworth/PlasmaZones/pull/720)).
- **"Window Rules" is now "Rules"**: the page and the config file (`windowrules.json` becomes `rules.json`) are renamed, because rules now cover gaps, layout, and animation as well as windows. Existing rules carry over automatically ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699)).
- **Window Appearance and Animations are grouped under an Appearance category** in the settings sidebar ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699)).
- **Gaps use one shared model**: snapping zone padding and tiling inner gap are now the same setting. Set per-monitor gaps on the Appearance Windows page where a monitor should differ ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699)).
- **The action and condition pickers are reorganized**: both are alphabetized, and the action picker groups the context conditions above the window conditions with a divider ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699)).
- **Old per-mode appearance and gap settings are unified on upgrade**: the snapping and tiling border, title bar, and gap values you had customized collapse into shared Window and Gap settings, and per-monitor gaps move into per-monitor settings. A default configuration is unchanged ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699), [#730](https://github.com/fuddlesworth/PlasmaZones/pull/730)).
- **Dragging a window is its own animation page**: drag animations are physics driven and follow the pointer while you hold the window, so they use their own shader type instead of the crossfade shaders. The new Window Dragging page under Animations → Motion holds the event, its picker offers only drag shaders such as Wobbly Move, and it no longer takes the "All Windows" shader from the Window Motion page. If you had Wobbly Move set on "All Windows", pick it again on the new page ([#756](https://github.com/fuddlesworth/PlasmaZones/pull/756)).
- **Layout and zone names are limited to 40 characters everywhere**: the zone editor previously allowed zone names up to 100 characters, and names arriving over D-Bus or from imported files were not limited at all. An existing name that is longer keeps working and is shortened the next time the layout is saved ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Layouts that opt out of minimum window sizes are now left alone**: Tatami, Floating Center, Cluster, and Theater each declare that they do not work with minimum window sizes, but the correction pass ran over their zones anyway and reshaped them. Zones from a layout that opts out are now used as the layout produced them. If a window in one of these four had been nudged to meet its minimum size, it keeps the size the layout gives it instead ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Zone numbers are yours to set**: the number shown on a zone in the editor is now a value the zone carries rather than its position in the list, so reordering or editing the zones no longer renumbers them ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Floating windows restore across monitors**: a window you float and then close now reopens on the monitor and position it closed on when you log back in, the same as snapped windows already did. A guard that limited this to a single monitor was removed ([#727](https://github.com/fuddlesworth/PlasmaZones/pull/727)).

### Removed

- **The Resized animation event**: an interactive resize repaints the window live while you drag its edge, so there is no before and after moment for an animation to play. The event showed a default that never ran and did nothing useful when configured. Size changes from snapping, layout switches, and maximizing keep their animations through those events ([#756](https://github.com/fuddlesworth/PlasmaZones/pull/756)).

### Migration

- **Config schema bumped v4 → v5.** On first launch after upgrade, the per-mode border, title bar, corner radius, and gap values are converted into rules, and per-monitor gaps become screen-scoped rules. The conversion runs once, keeps the values you had customized, and needs no interaction ([#699](https://github.com/fuddlesworth/PlasmaZones/pull/699), [#730](https://github.com/fuddlesworth/PlasmaZones/pull/730), [#733](https://github.com/fuddlesworth/PlasmaZones/pull/733)).

### Fixed

- **Creating an algorithm from a template keeps its behavior intact**: the New Algorithm wizard used to rewrite a template's metadata from the wizard's own four checkboxes, which silently dropped settings that shape how the template behaves, such as the template's own custom parameters, split ratio, and window cap. Only the name and id are personalized now, and the rest of the metadata travels with the copy, which is what lets the wizard offer the newer templates safely ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Double-clicking Create in the New Layout wizard made two layouts**: the Create button stayed live through the dialog's closing animation, so a second click, or holding Return, landed a second layout you then had to delete. The wizard now ignores anything after the first Create, and re-enables it if the create fails so you can correct the name and retry ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **The New Layout wizard tagged layouts with the wrong monitor shape**: every choice in the monitor picker stored the shape one step below the one you picked, so a 16:9 layout was filed as suiting any monitor, a 21:9 layout was filed as 16:9, and a Portrait layout was filed as 32:9. Portrait could not be stored at all. The picker now records the shape you chose, and its first option is named "Any" rather than "Auto" because it always meant "offered on every monitor" and never read a monitor to detect anything ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Some settings and editor animations ignored your animation speed**: the hover feedback in the position picker and the settings button groups, the sidebar rows, the wizard template cards, the page fade-in, and the zone editor's own transitions (its top bar, property panel, notification banner, divider handles, zones, and zone buttons) ran at fixed durations, so turning Plasma's animation speed up or down left them alone while the rest of the app followed it. They now read the same theme duration everything else does. At the default speed they keep the timing they have today, so nothing looks different until you ask it to ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Exporting your settings over the file they live in deleted them**: the export made room for the new file by removing whatever was already at the destination, so picking your own `config.json` removed the very file it was about to copy, and every setting you had was gone with no backup to fall back on. The message blamed the folder for not being writable. Exporting to the file already in use is now refused, and the export no longer clears the destination at all. It writes to a temporary file and swaps it in only once the write has succeeded, so an export that fails leaves whatever was already there untouched. Importing a file onto itself is refused the same way instead of reporting a failure for what is really a no-op ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Exporting or importing a layout said nothing when it failed**: exporting sent the layout off to the daemon and never waited to hear back, so a folder it could not write to, or a disk with no room left, closed the file picker and told you nothing. Importing a file that was not a layout was just as quiet: the page refreshed and you were left to notice that nothing had arrived. Both now say what went wrong, and the export writes to a temporary file and swaps it in only once it has succeeded, so a failed export leaves whatever was already there untouched ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Exporting your settings saved them as well**: Export wrote your unsaved changes into the settings file the app is using, as though you had pressed Save, and moved the point that Discard reverts to. Edits you had not committed became permanent and there was no longer anything to undo them back to. Exporting now writes the settings you can see to the file you picked and leaves the live one alone ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Exporting or importing your settings said nothing at all**: the file picker closed and you were left to guess whether it had worked, or whether it had failed on a path it would not write to, a file that was not a settings file, or a backup it could not take. Both now confirm when they succeed and say what went wrong when they don't. If an import lands but the animation pages cannot be refreshed to match, it now says so rather than reporting plain success, and if your old settings cannot be put back after a failed import it tells you where the backup is ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Turning animations off made some of them play at full length**: with Plasma's animation speed at zero, the animations that scale from the shorter theme durations asked for a zero-length animation, which was read as "no duration set" and fell back to the animation's own full duration. So the setting meant to remove animation gave those the longest one they had. Zero now means zero, and the affected animations are instant as intended ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **Importing an algorithm reported the same failure twice**: a refused file toasted both the specific reason and a generic "could not import" behind it. Only the reason shows now ([#779](https://github.com/fuddlesworth/PlasmaZones/pull/779)).
- **The zone picker did not show which zone a window would snap to**: while dragging a window, every zone in a layout lit up at once instead of only the one under the cursor, so there was no way to tell where the window would land. Only the zone under the cursor highlights now. The cursor also lands on the right zone more reliably, because the picker used to measure zones against the whole card rather than the smaller preview it draws inside it. That was off by a few pixels for most layouts, and by considerably more for a layout tagged with an aspect ratio that does not match the shape of the preview, such as an ultrawide layout shown in a 16:9 preview ([#780](https://github.com/fuddlesworth/PlasmaZones/pull/780)).
- **Minimizing a window skipped its animation**: the Minimized animation event used to fire only when a window was restored, so the way down was always an instant blink. Any animation assigned to the event now plays in both directions, in reverse while minimizing and forward while restoring. Windows excluded from tiling also play their minimize animation now, matching how the other window events already treated them ([#776](https://github.com/fuddlesworth/PlasmaZones/pull/776)).
- **Peek at Desktop did nothing while the effect was loaded**: activating Peek at Desktop left every window exactly where it was. KDE reveals the desktop through its Window Aperture effect, which keeps windows on screen and slides them out to the screen edges, and the PlasmaZones effect ended each window's paint step by jumping straight to the draw stage. That skipped the paint step of every effect that runs after PlasmaZones in KWin's chain, so Window Aperture held the windows visible but never got to move them. The effect now continues the paint chain properly, which also restores any other effect that paints after PlasmaZones in KWin's chain. Decorated windows additionally honor opacity fades from other effects instead of always drawing fully opaque, and focus follows mouse pauses during a peek so the hidden window under the cursor is not re-activated ([#775](https://github.com/fuddlesworth/PlasmaZones/pull/775)).
- **Dragging a window stuttered at the start and end of the drag on disks with slow write flushing**: the daemon bound a temporary global Escape shortcut on every drag and released it on drop, and each bind made KWin rewrite its shortcut config to disk with an fsync. On a drive with slow flush latency that briefly stalled the compositor at pickup and at drop, while a continuous drag stayed smooth. The per-drag binding is gone, because the KWin effect already grabs Escape for the whole drag, so dragging is smooth regardless of disk. Thanks @arinl for the report and the bpftrace diagnosis ([#714](https://github.com/fuddlesworth/PlasmaZones/pull/714), [discussion #167](https://github.com/fuddlesworth/PlasmaZones/discussions/167)).
- **Maximize and restore animations never played**: maximizing a window with an animation assigned made it vanish for the length of the animation and pop in at full size, and restoring played the wrong motion with every pack except Window morph. The maximize event never told the animation which rectangles it was moving between, and the restore leg ran the timeline backwards for the grid-deformation packs. Both directions now morph between the old and new frame with any geometry pack. Restore also waits for the window's real size change before starting, so an app that is slow to shrink no longer snaps first and animates late, and dragging a maximized window free by its title bar no longer fires a stray maximize animation over the drag ([#755](https://github.com/fuddlesworth/PlasmaZones/pull/755)).
- **"Move Window Left" skipped through overlapping zones**: in a layout where zones overlap each other, moving a window left jumped straight to the leftmost zone in one keypress, while moving right stepped one zone at a time. The zone picker broke ties between overlapping zones by their storage order, which only happened to match the expected zone when moving right. Ties now break by distance, so moving, focusing, and swapping a window all step to the nearest zone in every direction. Thanks @Nathorr for the report ([#773](https://github.com/fuddlesworth/PlasmaZones/pull/773), [discussion #771](https://github.com/fuddlesworth/PlasmaZones/discussions/771)).

## [3.1.3] - 2026-07-01

### Changed

- **Rebuilt for KWin 6.1.2**: the PlasmaZones KWin effect is compiled against a specific KWin version and will not load under a different one, so this release rebuilds the effect for KWin 6.1.2. Update to it once your system moves to that KWin, otherwise window dragging, shortcuts, and snapping stop working.
- **Shorter KWin version-mismatch warning**: when the installed effect was built for a different KWin than the one running, the notification now gives just the diagnosis and the rebuild-and-reinstall fix, without the NixOS-specific install note.

## [3.1.2] - 2026-06-25

### Fixed

- **A "Default / Any window" layout rule was ignored**: a per-context rule that set the engine or snapping layout for any window did nothing, so a virtual desktop with no specific assignment fell through to the global default (for example BSP) instead of the configured layout. A layout-only rule (a snapping layout or tiling algorithm with no engine set) was dropped the same way. Per-context rules now resolve each slot on its own, so the catch-all rule applies as the default and a layout-only rule fills its slot without forcing the engine ([#698](https://github.com/fuddlesworth/PlasmaZones/pull/698)).
- **Editor zone edits were lost when the geometry snapped back**: dragging or resizing a zone updated it on screen, but the change was never written to the layout or shown in the properties panel when the committed geometry rounded back to roughly its original spot. The visual is now reconciled with the saved geometry whenever an operation ends ([#697](https://github.com/fuddlesworth/PlasmaZones/pull/697), [discussion #696](https://github.com/fuddlesworth/PlasmaZones/discussions/696)).

## [3.1.1] - 2026-06-24

### Added

- **Route a window to a monitor or virtual desktop**: two new window-rule actions, RouteToScreen and RouteToDesktop, open a matched window on a specific monitor or virtual desktop (or both) and can snap it into a zone there. On its own a rule can just move the window to that monitor. This restores the per-monitor app-to-zone assignment from v3 and generalizes it to "open app X on context Y" for both snapping and tiling ([#691](https://github.com/fuddlesworth/PlasmaZones/pull/691)).
- **Autotile capability badges in layout previews**: the shared preview cards used by the overlay picker, the zone selector, and the settings layout surfaces now show the autotile capability badges, matching the Layouts page ([#688](https://github.com/fuddlesworth/PlasmaZones/pull/688)).

### Changed

- **Curated default layout and algorithm visibility on fresh installs**: the plain `grid` autotile algorithm is hidden by default because the resize-aware `aligned-grid` supersedes it, and the `Wide` snapping layout is now shown by default. This only seeds on a fresh config, so existing users keep their current visibility ([#687](https://github.com/fuddlesworth/PlasmaZones/pull/687)).

### Removed

- **The "is one of" match operator**: this window-rule match operator was unusable in the editor and has been removed ([#691](https://github.com/fuddlesworth/PlasmaZones/pull/691)).

### Fixed

- **Autotiling failed to load any algorithm in non-C locales**: scripted algorithms compile through Luau, which parsed numbers with the system locale, so a regional locale with a decimal comma broke every algorithm, including the shared prelude, and the UI reported "No autotile algorithms available". LC_NUMERIC is now pinned to "C" while Luau compiles and runs ([#692](https://github.com/fuddlesworth/PlasmaZones/pull/692), [discussion #690](https://github.com/fuddlesworth/PlasmaZones/discussions/690)).
- **Per-monitor app assignments stopped working after upgrading from v3**: the v3 to v4 migration dropped the legacy per-monitor `targetScreen` pin and left X11 two-token patterns (`chromium chromium`) that never matched the normalized app id. The migration now carries the pin across as a RouteToScreen action and normalizes the pattern ([#691](https://github.com/fuddlesworth/PlasmaZones/pull/691), [discussion #686](https://github.com/fuddlesworth/PlasmaZones/discussions/686)).
- **Quick Shortcuts applied the wrong layout**: `Meta+Alt+#` applied whatever layout sat at that position in Priority order instead of the layout bound to that quick slot ([#684](https://github.com/fuddlesworth/PlasmaZones/pull/684)).
- **Windows landed in the wrong zones during fast rotation**: holding the rotate shortcut applied superseded geometry updates after the daemon had already moved on, so windows ended up in stale zones. The effect now drops superseded geometry ticks ([#689](https://github.com/fuddlesworth/PlasmaZones/pull/689)).
- **`restoreWindowsToZonesOnLogin` did nothing**: the toggle round-tripped through config, D-Bus, and the UI but its value was never read, so a window snapped at logout always restored to its zone. It is now honored on login ([#685](https://github.com/fuddlesworth/PlasmaZones/pull/685)).
- **Per-context gap and padding overrides were ignored for tiled windows**: window-rule gap overrides applied only to snapped windows, not autotiled ones. They now apply to both ([#685](https://github.com/fuddlesworth/PlasmaZones/pull/685)).
- **Snapping ignored some window-rule exclusions**: window-class, title, and minimum-size exclusion conditions were not honored on the snapping path. The full window query is now consulted ([#685](https://github.com/fuddlesworth/PlasmaZones/pull/685)).
- **Layout names were blank in the overlay previews**: the picker, OSD, and zone-selector preview cards read the old `name` key instead of `displayName`, so the name label rendered empty. They now read `displayName` ([#688](https://github.com/fuddlesworth/PlasmaZones/pull/688)).
- **The layout dropdown highlight washed out the badges**: the layout combo box highlighted the active row with a full opaque band that left the category, capability, and aspect-ratio badges illegible. It now uses the same subtle tint as the rest of the app ([#693](https://github.com/fuddlesworth/PlasmaZones/pull/693)).

## [3.1.0] - 2026-06-23

### Added

- **Rules**: a new unified settings page replaces the old Snapping Assignments, Tiling Assignments, Animations App Rules, and per-mode "disabled apps" lists. Rules are browsed, added, edited, drag-reordered, duplicated, and disabled from one place. Matching composes class, title, role, app-id, virtual desktop, activity, and screen predicates with AND/OR/NOT. A rule's actions cover snapping/tiling assignment, animation-curve and shader overrides, and exclusion from snapping, autotile, and effects, the four surfaces that previously each had their own editor.
- **`org.plasmazones.Rules` D-Bus interface** (`dbus/org.plasmazones.Rules.xml`): `getAllRules`, `setAllRules`, `addRule`, `removeRule`, and related lifecycle methods for programmatic rule management.
- **`phosphor-rules`** LGPL-2.1+ library housing the rule model, parser, and `RuleEvaluator`, so third parties can link the matcher without inheriting GPL.
- **Snapping focus behavior**: two new opt-in toggles on Snapping → Window → Behavior (both default off). *focus new windows* auto-activates a window when it is auto-placed into a zone on open, and *focus follows mouse* activates the snapped window under the cursor. Brings snapping to parity with the existing autotile focus options.
- **Zone span toggle mode** ([#563](https://github.com/fuddlesworth/PlasmaZones/issues/563)): an opt-in switch in the Zone Span card so the span modifier can be tapped to start/stop spanning instead of held down for the whole drag (default off, motivated by accessibility).
- **Restore floated window positions on login** ([#606](https://github.com/fuddlesworth/PlasmaZones/pull/606)): floated windows are now restored to the monitor and position they closed on after a KWin session restore (previously only *snapped* windows were restored cross-screen). Parallel per-engine toggles (both default **on**), *restore unsnapped windows* under Snapping → Window → Behavior and *restore untiled windows* under Tiling → Behavior, plus an engine-neutral per-window `RestorePosition` rule action let specific windows opt in or out for either mode.
- **Per-window appearance for snapping**: snapping gains its own border, corner-radius, hide-title-bar, and accent-color settings, mirroring tiling, and the former "Snapping → Appearance" page is renamed **Zones**. Window restore state across daemon restart and logout/login is now backed by a single `WindowPlacementStore` instead of several overlapping mechanisms.
- **New window-rule actions** for window chrome: per-window border, title-bar, corner-radius, accent-color, gap/padding, and opacity overrides, applied to snapped or floating windows (e.g. "floating windows on monitor 2 → no title bar + red border", "activity Gaming → zero gaps").
- **New window-rule match conditions**: `IsTransient`, `IsNotification`, `Width`, `Height`, and `IsFocused`, plus a built-in **"Don't animate small windows"** template. `IsFocused` lets any action be focus-scoped (e.g. "WHEN NOT focused → dim").
- **Collapsible settings sidebar categories** with smart-expand of the active page's category and animated chevrons, cutting clicks to reach deep pages.
- **Per-monitor scope map**: per-monitor settings cards carry a scope chip that opens a spatial map of the real monitor arrangement to switch outputs, replacing the tall repeated monitor-selector block. The scope choice persists across pages.
- **Shader-driven window-move/resize morph** (`window-morph`): window move, resize, snap, and layout-switch transitions animate as a smooth shader cross-fade geometry morph instead of a plain C++ paint transform, the default for window-move and overridable per event. Overlay show/hide (OSD, zone selector, layout picker, snap assist) now defaults to the shader-based `fade` effect.
- **In-app live shader preview**: the Snapping → Shaders browser gains an animated, interactive preview with mouse and audio input, shader **presets** (load/save, shared with the editor), and an in-app compile-error banner.
- **Shader authoring API**: authors write only the effect body and read parameters by name (`p_<id>`) instead of decoding UBO slots, with a generated preamble and entry-point conventions, plus a `plasmazones-shader-validate` CLI (with `--animation` / `--overlay` modes and did-you-mean diagnostics) wired into CI to catch broken packs offline.
- **dma-buf zero-copy window-preview transport** for snap-assist thumbnails (opt-in via the `PLASMAZONES_DMABUF_THUMBNAILS` env var, with default builds unchanged), the foundation for live window previews.
- **Phosphor SDK groundwork**: the reusable LGPL Phosphor library line gains a Phase 1 foundation tier (`phosphor-theme`, `phosphor-popout`, `phosphor-registry`, `phosphor-ipc` + the `phosphorctl` driver, `phosphor-shell` per-screen helper) and a Phase 2 system-service tier (`phosphor-service-{pipewire,network,bluetooth,brightness,notifications,polkit,idle,clipboard,lock,session,upower,mpris,icontheme,sni}`). All of it is gated behind `BUILD_PHOSPHOR_SHELL` (default off) and driveable only from standalone examples/CLIs. It is groundwork for the standalone Phosphor shell direction and is **not** part of the shipping PlasmaZones tiler.
- **Resize-aware tiling** ([#666](https://github.com/fuddlesworth/PlasmaZones/pull/666)): six split-ratio algorithms (master-stack, wide, focus-sidebar, zen, deck, and horizontal-deck) now reflow on interactive resize. Dragging the master or boundary edge updates the split ratio for that desktop the same way a master-ratio keystroke does, without bleeding into other screens or the global default. The Layouts page shows a per-algorithm **Reflows** badge and can group algorithms by it.
- **Suppress default layout assignment** ([#676](https://github.com/fuddlesworth/PlasmaZones/pull/676)): a new setting, with a matching `DefaultLayoutAssignment` window-rule action, stops a context from falling back to the synthesized default layout. A suppressed context shows no snapping overlay or zone selector, reports no layout, and shows a "No layout assigned" OSD. Switching it into autotile sets the mode without applying the global default algorithm until a concrete one is assigned.
- **Layouts page rebuilt** as a searchable, card-based catalogue with collapsible capability groups, per-layout deep links, and global-search reveal, matching the shader browser and Rules. Tiling algorithms expose a **Script State** capability (filter, group-by, and a card badge) alongside the reflow and persistent-memory badges, layout and algorithm cards show their description on hover, and bundled snapping layouts open in a text editor.

### Changed

- **Single rule format**: window assignments, per-mode disable lists, animation App Rules, and effect exclusion lists are unified into one rule list stored in `~/.config/plasmazones/rules.json`. The KWin effect now consults the same `RuleEvaluator` as the daemon for animation App-Rule resolution and exclusion checks, so the two cannot drift.
- **`LayoutRegistry::walkCascade` removed**, replaced by `RuleEvaluator`. The old per-axis cascade (context-keyed assignments vs window-property matching) no longer exists. All matching goes through the evaluator.
- **`org.plasmazones.WindowTracking.setWindowMetadata`** widened from 4 to 9 arguments to carry the additional fields the evaluator needs (role, app-id, desktop, activity, screen). The KWin effect and daemon must be installed and running as a matched pair. `MinPeerApiVersion` bumped 3 → 4, and either side refuses to register a mismatched peer rather than silently degrading. Packagers must rebuild and ship both binaries together.
- **`org.plasmazones.Layout.assignmentChangesApplied`** signal dropped its second argument (the per-key field tag). Subscribers that depended on that field must update or they will receive the wrong arity.
- **Scripted autotiling moved from QJSEngine (JavaScript) to an embedded, sandboxed Luau VM** (`phosphor-scripting`). The 25 bundled algorithms were ported `*.js` → `*.luau`, written against a new frozen `pz` standard library, with a per-engine CPU-time watchdog and a 64 MiB heap cap. The `TilingAlgorithm` contract, daemon, editor, and settings are unchanged. **Breaking for custom algorithms**: the loader now discovers only `*.luau` files, so user scripts in `~/.local/share/plasmazones/algorithms/` written in the old JavaScript form are no longer loaded and must be rewritten in Luau (see `docs/architecture/luau-algorithm-authoring.md`).
- **Snapping and Tiling settings aligned for parity.** Tiling gains a dedicated Focus card. Section and label naming is unified across both modes ("Inner gap" / "Outer gap", "Window Handling", parallel quick-shortcut labels), and the placement settings are reorganized into a consistent Overlay / Window / Configuration shape with gaps moved into Window → Appearance and per-monitor gap selectors. User settings are preserved (C++ symbol renames only).
- **Performance.** Daemon peak heap is down ~58% (149 → 63 MB) and idle CPU drops from ~25% to ~0. The overlay and snap-assist release their full-screen image buffers when dismissed (≈33 MB per shader-enabled 4K screen, ≈6 MB of thumbnail cache), and a content-addressed on-disk shader cache speeds warm launches. Settings pages that were slow on first visit now open fast via a page-instance cache, background compile-warming, and viewport virtualization of the animation-event card lists.
- **Settings app rebuilt on the reusable `phosphor-control` library** (extracted from the in-app settings chrome, formerly named `phosphor-settings-ui`), reducing the app to a thin consumer with visual parity.
- **Internal `pz` → `p` symbol-prefix debrand** across shader params, classes, macros, CMake helpers, and the Luau tiling global (`pz` → `phosphor_luau`). The user-facing `PlasmaZones` brand and the global-shortcut ID namespace are deliberately unchanged. All ad-hoc registries (shaders, animation, curves, tiles algorithms, layout sources) are unified onto a single thread-safe `Registry<T>` primitive with public APIs preserved.
- **Nix flake restructured around a single package definition.** The 312-line `flake.nix` is now thin wiring, and the build recipe and the KWin-IID rationale live in `packaging/nix/{package,overlays,module,hm-module,devShell,formatter}.nix`. The package is defined once in `overlays.nix` (`final.callPackage`) and every output (`packages`, `devShells`, `checks`, `formatter`) derives from `legacyPackages.<system>.extend overlay`, replacing five independent build call sites that each had to remember to build against the right pkgs. The version is parsed once from the top-level `project(PlasmaZones VERSION …)` in `CMakeLists.txt` inside `flake.nix` (where the flake `self` is a store path, so the read is pure) and threaded to the package as an argument. Reading it inside `package.nix` forced an import-from-derivation when nixpkgs builds from a `fetchFromGitHub` src. LTO is now opt-in (`enableLTO`, default off) instead of forced, since every module/overlay consumer rebuilds against host pkgs with no cache reuse. The build source is `lib.fileset`-scoped so editing docs/CI/flake files no longer invalidates it. `nix fmt` now formats Nix, C++, and QML (reusing the in-tree `.clang-format`), and the NixOS module declares the `plasmazones` systemd user service with autostart opt-in (default off, preserving the per-user "enable it yourself" policy).
- **Autotiling is on by default** ([#671](https://github.com/fuddlesworth/PlasmaZones/pull/671)): tiling now works out of the box so PlasmaZones behaves like a dynamic tiler with no setup. The companion behaviors (focus new windows, smart gaps, respect minimum size, exclude transient windows, and insert at the stack end) were already on by default, and the default algorithm (bsp) and gaps are unchanged. Only fresh installs are affected, and existing saved configs keep their current value.
- **Settings UI polish** across the Layouts and listing pages: a curated default picker shows a starter set of layouts and algorithms with the rest one eye-toggle away, a shared filter menu now drives the Layouts, Rules, and Shaders lists, and the sidebar, global search field, About credits, and virtual-screen preview labels got alignment and spacing fixes.

### Removed

- Legacy `Display.SnappingDisabled*` and `Display.AutotileDisabled*` config keys (auto-migrated into rules).
- **QJSEngine-based scripted-tiling path** (the `ScriptedAlgorithm` runtime, its JS builtins, and the bundled `*.js` algorithms), along with the `Qt6::Qml` dependency in `phosphor-tiles`. Replaced by the Luau path above.
- `setSnappingLayoutEntry`, `setTilingAlgorithmEntry`, and related per-field `Settings`-side `Q_INVOKABLE`s that the legacy KCM Assignments pages used. There is no QML replacement. Use the Rules page.
- Legacy Snapping Assignments, Tiling Assignments, and Animations App Rules settings pages (replaced by Rules).
- **Per-release `plasmazones.nix` asset and its `generate-release-nix.sh` generator.** The asset was a source-pinned build *recipe* (not a binary), so it saved no build time and only served non-flake Nix users, who can instead build any tag against their host's pkgs with `pkgs.callPackage "${builtins.fetchTarball "https://github.com/fuddlesworth/PlasmaZones/archive/v<VERSION>.tar.gz"}/packaging/nix/package.nix" { version = "<VERSION>"; }`. The release notes' standalone-install section now shows that form, and the `build-nix` release job is reduced to a `nix build` smoke gate. Flake users are unaffected.
- **Stray `develop.nix`**: an unrelated dev flake (for "canaanepperson.com", `nodejs_24`) that had no connection to PlasmaZones. The dev environment lives in the flake's `devShells.default`.

### Migration

- **Config schema bumped v3 → v4.** On first launch after upgrade, `~/.config/plasmazones/assignments.json` is automatically converted into `~/.config/plasmazones/rules.json`, and the legacy `Display.SnappingDisabled*` / `Display.AutotileDisabled*` keys in `config.json` are folded into the same rule set. The migration is lossless and runs without user interaction.
- **Backout**: the source file is renamed `assignments.json.migrated` (not deleted), so a downgrade can restore the previous schema by manually renaming it back and starting an older daemon.
- **Recovery**: if migration aborts because the source is malformed, the original file is renamed to `~/.config/plasmazones/assignments.json.corrupt.bak`, the schema version stays at v3, and `rules.json` is not created. The daemon does not silently flush the old assignments to an empty rule set. The user can inspect / repair the quarantined file and rename it back to `assignments.json`, and the next launch then retries the v3→v4 conversion.
- **`hiddenFromSelector` now relocates out of layout files** during the v3→v4 layout-settings conversion. A v3 user who hid a layout previously kept the key embedded in the slimmed layout file instead of having it moved to the `layout-settings.json` sidecar. The migration now carries it across with the other relocated keys. Autotile per-algorithm overrides also fold into `layout-settings.json`, and the standalone `autotile-overrides.json` is retired by a one-time self-deleting migration on load.

### Fixed

- **Layouts rendered stretched / ultrawide when editing on a 16:9 or 4K screen** ([#593](https://github.com/fuddlesworth/PlasmaZones/issues/593)): the editor canvas used a fixed-zone bounding box as its aspect reference, so editing an existing layout could distort it while creating a new one rendered correctly. The canvas now references the live screen unless fixed zones genuinely overflow it.
- **A zone-spanning window blew up to fullscreen when switching layouts** ([#575](https://github.com/fuddlesworth/PlasmaZones/pull/575)): switching to a layout where the previously-spanned zones are non-contiguous (e.g. Grid 2×2 left column → Master+Stack) unioned them into a screen-sized bounding box. Non-contiguous mapped spans now collapse to the primary zone instead.
- **KWin effect plugin silently never installed under Nix.** `packaging/nix/package.nix` set `KDE_INSTALL_QTPLUGINDIR` to an absolute path *inside the read-only `qtbase` store output* (`${qt6.qtbase}/lib/qt6/plugins`). A derivation may only write under its own `$out`, so the effect dropped out of the package closure entirely. The daemon ran but zone overlays never appeared on Nix installs. The plugin now installs into the package's own `$out/${qt6.qtbase.qtPluginPrefix}` (the canonical NixOS `lib/qt-6/plugins` layout), where the running KWin discovers it via the system profile's aggregated `QT_PLUGIN_PATH`.
- **Toggling from autotile back to snapping left windows stuck in their tiled positions** instead of returning to where they were before tiling. The transition fell through to a stale current-assignment resnap that re-pinned the tiled geometry and suppressed the float-back. Windows now float back to their pre-tile positions.
- **Master-ratio and master-count adjustments bled into the global default** ([#666](https://github.com/fuddlesworth/PlasmaZones/pull/666)): the increase/decrease ratio and master-count shortcuts wrote the new value into the global config whenever a screen had no per-screen override, so the tweak propagated to sibling screens and new states on the next algorithm switch or settings refresh. The adjustment now stays local to the active screen, desktop, and activity, and resets only on an algorithm switch or an explicit ratio/count change in settings.

## [3.0.17] - 2026-06-18

### Fixed

- **PlasmaZones held back KWin upgrades, stranding Plasma in a half-upgraded state**: the native packages pinned KWin to the *exact* version PlasmaZones was built against (to guarantee the KWin effect plugin's IID matched the running KWin). On distros that ship KWin and PlasmaZones from separate repos (e.g. Fedora KWin from the distro, PlasmaZones from COPR), a newer KWin landing before a matching PlasmaZones rebuild could not satisfy the exact pin, so the package manager held KWin back. That left KWin out of sync with the rest of Plasma, which surfaced as "No KScreen backend found" and, at worst, a black screen at login (reported on Fedora 44 / Plasma 6.7). The runtime dependency is now a minimum (`kwin >= 6.7.0`) across the RPM, Debian, and Arch (source/bin/git) packages instead of an exact pin, so a newer KWin no longer blocks the upgrade. A mismatched effect is harmless. KWin reads the IID from plugin metadata and never loads a non-matching effect, so the plugin stays inert and only the drag overlay is missing until PlasmaZones is rebuilt. Core tiling (daemon and layer-shell QPA plugin) is unaffected. Nix is unchanged, because it has no install-time pin and rebuilds the whole system, so it cannot strand the desktop.

## [3.0.16] - 2026-06-18

### Changed

- **Support KDE Plasma 6.7** ([#638](https://github.com/fuddlesworth/PlasmaZones/pull/638)): the build baseline moves to the Plasma 6.7 stack (KDE Frameworks 6.26, Qt 6.10, and KWin 6.7), and the KWin C++ effect is ported to the 6.7 effect API. Notable upstream changes were handled. `prePaintScreen`/`prePaintWindow` dropped their `presentTime` argument (the effect now self-sources frame time from a steady clock, matching KWin's own effects), `EffectWindow::frameGeometry()` and `EffectsHandler::clientArea()` now return `KWin::RectF`, the `clientArea` per-desktop overload was removed, `addRepaint()` gained `RectF`/`Rect`/`Region` overloads, and `GLShader::isValid()` was removed. Plasma 6.6 is no longer supported.

### Fixed

- **Snap-assist window thumbnails on Plasma 6.7** ([#638](https://github.com/fuddlesworth/PlasmaZones/pull/638)): KWin 6.7 removed the offscreen-QML readback (`OffscreenQuickView::bufferAsImage()`) that the in-process thumbnail capture relied on. Capture was reworked to render each candidate window into an offscreen `GLFramebuffer` via `effects->drawWindow()` and read it back with `GLTexture::toImage()`, so snap-assist thumbnails keep working without a second screenshot round-trip.

## [3.0.15] - 2026-05-28

### Fixed

- **Zone-selector popup at the screen edge switched the active layout on hover, resnapping every tiled window** ([#542](https://github.com/fuddlesworth/PlasmaZones/pull/542)): the zone-selector slot was supposed to be input-transparent during drag, because cursor coordinates come in via the D-Bus `updateSelectorPosition` path and the snap commits at drag-end via `drop.cpp`, never via a Qt hover event. But the slot's QML `MouseArea`s still fired `zoneSelected` on every pointer-enter, and once snap-assist became visible the shared shell surface flipped to input-grabbing (`anyInputGrabbing = isVisible(snapAssistSlot) || isVisible(layoutPickerSlot)` in `syncPassiveShellSurfaceState`). Those leaked hover events committed `manualLayoutSelected`, which immediately resnapped every other tiled window into the new layout's zones. Visible as "my layout changes to one with more or fewer windows whenever I drag windows up". The QML hover commit path is gone (`ZoneSelectorContent` is now `interactive: false` and the daemon's `manualLayoutSelected` handler / signal are removed), and cross-layout switching on drop still works because `WindowDragAdaptor::dragStopped` reads `m_selectedLayoutId` from the C++ hit-test and applies the layout when the user actually releases the drag on a zone in a different layout.

### Removed

- **Switching the autotile algorithm by hovering an autotile preview in the zone-selector popup** ([#542](https://github.com/fuddlesworth/PlasmaZones/pull/542)): the autotile-hover commit path went away with the input-contract fix above (`drop.cpp` resolves the selected id as a UUID and skips non-UUID autotile ids, so the hover path was the only commit point for autotile-via-zone-selector). Algorithm swaps still work through the existing on-by-default routes such as `NextLayout` / `PreviousLayout`, `QuickLayout1`–`QuickLayout9`, and the Layout Picker (`Meta+Alt+Space` by default). The `IOverlayService::autotileLayoutSelected` signal and its daemon handler were removed as dead code.

## [3.0.14] - 2026-05-27

### Fixed

- **DPMS-wake autotile orphan reassignment still triggered intermittently** ([#527](https://github.com/fuddlesworth/PlasmaZones/discussions/527), [#536](https://github.com/fuddlesworth/PlasmaZones/pull/536)): 3.0.13 closed the dropped-monitor case but missed the dual-monitor wake-up where the second output coming back simply shifts the first output's x-offset. With no output actually removed, `oldScreenStillConnected` stayed true, and `isScreenChangeInProgress()` hadn't flipped on yet because KWin emits the per-window `outputChanged` *before* the `virtualScreenGeometryChanged` that the screen-change debounce listens for, so the orphan reached the autotile-delegation guard with both legs of the check false. `screenAdded` and `screenRemoved` are now also wired into the screen-change handler, latching the pending-change flag at the earliest point KWin tells us the output set is changing. The settle path that runs once `virtualScreenGeometryChanged` catches up is unchanged.

## [3.0.13] - 2026-05-26

### Fixed

- **Windows from disabled monitors got pulled into the active autotile zone after DPMS sleep** ([#527](https://github.com/fuddlesworth/PlasmaZones/discussions/527), [#528](https://github.com/fuddlesworth/PlasmaZones/pull/528)): with one monitor autotile-disabled, waiting for that monitor to power off and then moving the mouse to wake the active monitor would cause windows from the disabled monitor to be tiled into the active zone. KWin reassigns orphaned windows from a dropped-out monitor to a remaining output and fires `outputChanged` for each, indistinguishable from a deliberate cross-screen move. The snapping D-Bus path already guarded for this with `oldScreenStillConnected && !isScreenChangeInProgress()`, but the autotile delegation immediately above ran unconditionally, so the orphan reassignment was mistaken for the window genuinely entering autotile. The disconnect check now feeds both paths via a shared `involuntaryMove` flag, and recovery is owned by the daemon's `virtualScreensReconfigured` handler once the screen change has stopped chattering.

## [3.0.12] - 2026-05-25

### Fixed

- **Focus-follows-mouse stole focus from active floating and overflow windows** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#525](https://github.com/fuddlesworth/PlasmaZones/pull/525)): the 3.0.11 FFM pause fixed the case where an excluded window (emoji picker, notification popup, krunner) was active, but the symmetric case for floating windows still regressed. A manually-floated window, or an overflow window the daemon auto-floats when window count exceeds the `maxWindows` cap, sits on top of the tiled stack while the user works in it. Moving the cursor across an underlying tiled window's visible edge still activated that tiled window and sent the floating one to the background. `handleCursorMoved`'s active-window guard now also bails when `isWindowFloating()` returns true for the focused window. `FloatingCache` covers both code paths (user-toggled float and overflow auto-float via `applyFloatCleanup`), so one predicate handles both scenarios. Resumes naturally on the next cursor move once a tiled window becomes active.

## [3.0.11] - 2026-05-24

### Changed

- **Layer-shell state setters skip unchanged values across configure events** ([#522](https://github.com/fuddlesworth/PlasmaZones/pull/522)): KWin re-emits `zwlr_layer_surface_v1` configure events on every virtual-desktop switch, and `applyProperties` was unconditionally re-sending the full layer-shell state (anchor, layer, exclusive zone, keyboard interactivity, margin, size, exclusive edge), which is six to seven protocol messages per surface per configure. The applied state is now cached per surface and each setter only fires when its source property has actually changed.

### Fixed

- **Focus-follows-mouse activated tiled windows underneath an active popup** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#521](https://github.com/fuddlesworth/PlasmaZones/pull/521)): the 3.0.10 FFM fix made auto-focus-follow-mouse consistent for the common case but missed the case where an excluded or untracked window (emoji picker, notification popup, krunner) was active inside a zone. Moving the cursor across the underlying tiled window's visible area still activated that tiled window, sending the just-opened popup straight to the background. `handleCursorMoved` now also checks the currently active window. If it is an excluded app, dialog, popup, keep-above overlay, or below the min-size threshold, FFM pauses on the cursor's screen until a tileable window becomes active.
- **Stale pending-restore entries for excluded apps grew session.json and logged on every daemon start** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#521](https://github.com/fuddlesworth/PlasmaZones/pull/521)): runtime gates already refused to honor pending restores for excluded apps, but the dead entries persisted on disk in `PendingRestoreQueues` and `AutotilePendingRestores` and reappeared on every restart. Both engines now prune their on-disk queues against the current exclusion lists at startup and whenever the lists change.
- **Drag artifacts, post-snap flicker, and a gray decoration ring during snap drags** ([#516](https://github.com/fuddlesworth/PlasmaZones/issues/516), [#523](https://github.com/fuddlesworth/PlasmaZones/pull/523)): the zone-preview `PassiveShell` mapped on the Overlay layer fullscreen during a drag, masking KWin's Translucency-while-moving effect. On hybrid Intel+NVIDIA setups (CachyOS, Plasma 6.6.5, NVIDIA 595) this also forced a slower compositional path that produced the visible drag artifacts and post-snap flicker. The PassiveShell role is now downgraded to the Top layer, the same layer KDE's own panels live on, which coexists with the translucency effect. Fullscreen apps on Overlay still draw above the zone preview correctly.

## [3.0.10] - 2026-05-23

### Fixed

- **Focus-follows-mouse stopped working after any overlay appeared** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#517](https://github.com/fuddlesworth/PlasmaZones/pull/517)): the focus-follows-mouse stacking-order walk treated the daemon's own full-screen overlay layer-shell surface as a blocking occluder, so once any OSD, snap preview, or layout picker had been shown on an autotile monitor FFM gave up and never resumed. The walk now looks through `plasmazonesd` and `plasmazones-editor` surfaces to the real window beneath. Real overlays (emoji picker, Spectacle, xdg-desktop-portal) still trip the existing guard.
- **Popups and dialogs could consume the main window's saved zone on reopen** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#518](https://github.com/fuddlesworth/PlasmaZones/pull/518)): the `PendingRestore` queue was keyed by appId only, so opening any popup or dialog of an app whose main window had previously closed-with-a-zone-assignment consumed the queue's head entry and snapped the popup into that zone. The closing window's structural kind (normal vs transient) is now recorded on each entry, and the consume path refuses to assign an entry to a window of a different kind, leaving it for the next-opening window that does match. Pre-fix on-disk sessions reload with kind=Unknown and the gate stays permissive for them. Steam-class apps whose popups misreport as normal windows still need a separate title-regex follow-up.
- **Passive overlay shell stayed mapped and prewarmed with effects disabled** ([#515](https://github.com/fuddlesworth/PlasmaZones/discussions/515), [#519](https://github.com/fuddlesworth/PlasmaZones/pull/519)): the passive overlay's `wl_surface` was kept mapped even when no overlay slot was live, and the prewarm path did not respect the effects-enabled toggle, leaking compositor work for users who had turned effects off. The shell now unmaps when idle and prewarm is gated on the effects-enabled setting.
- **Animation shader pattern features scaled inconsistently across monitor sizes** ([#520](https://github.com/fuddlesworth/PlasmaZones/pull/520)): sparkle, streak, and smoke feature density was tied to an arbitrary 800px reference, so pattern density drifted across screen sizes. Features now scale by `iSurfaceScreenPos.zw` (anchor size) for constant per-pixel pitch.

## [3.0.9] - 2026-05-22

### Fixed

- **Per-virtual-desktop and per-activity assignment toggles could not be re-enabled once disabled** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#514](https://github.com/fuddlesworth/PlasmaZones/pull/514)): the 3.0.8 fix kept the per-desktop and per-activity disable Switch's `checked` binding live across controller emissions, but the Switch was declared inside `AssignmentRow.middleContent` and Qt's `Item.enabled` cascade carried the row's disabled state down to the nested Switch, leaving no clickable control to flip the context back on. `AssignmentRow` now exposes a `contentEnabled` property that gates only the combo and clear button, so the Switch in `middleContent` stays clickable while the assignment controls grey out as before. The top-monitor Switch was unaffected because it had always been a sibling of its combo rather than a descendant.

## [3.0.8] - 2026-05-22

### Fixed

- **KWin crashed (SIGSEGV) when snapping a window to a zone on certain monitors** ([#511](https://github.com/fuddlesworth/PlasmaZones/discussions/511), [#513](https://github.com/fuddlesworth/PlasmaZones/pull/513)): PlasmaZones' own overlay surfaces (zone overlays, snap-assist) are KWin internal windows, and `KWin::InternalWindow::minSize()` dereferences its backing `QWindow` with no null check. When that window was not yet realised the call faulted on a null pointer inside Qt and took down the whole compositor, closing every open application. Every `minSize()` call site in the KWin effect now skips internal windows, and internal windows are rejected before they can enter the autotile pipeline.
- **A window snapped near a monitor boundary could jump onto the wrong screen** ([#513](https://github.com/fuddlesworth/PlasmaZones/pull/513)): the drop handler paired the zone rectangle from the last drag tick with the screen resolved at the release point without checking that the two referred to the same monitor. A fast drag across a monitor boundary could therefore apply one monitor's zone rectangle to a window committed on another. The drop path now resolves the captured rectangle's own physical screen and declines the snap on a mismatch, leaving the window at its drop position instead of teleporting it.
- **The "Cancel Zone Overlay" Escape shortcut grabbed Esc system-wide** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#510](https://github.com/fuddlesworth/PlasmaZones/pull/510)): the transient drag-overlay cancel shortcut was written to `kglobalshortcutsrc` like a user-customisable binding, so after an unexpected daemon exit the stale entry survived and KGlobalAccel kept routing Escape to a dead action, including over fullscreen games. Transient shortcuts now carry their non-persistent flag all the way to the backend, which scrubs stale on-disk records on registration and removes them on shutdown.
- **Per-monitor virtual-desktop and activity disable toggles got stuck off** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#510](https://github.com/fuddlesworth/PlasmaZones/pull/510)): disabling a virtual desktop or activity on a monitor assignment row left the switch frozen in the off position, because an imperative property write had severed its QML binding, and re-enabling never re-drove it. The switches are now read-only consumers of a revision-counter binding, so they track the underlying state correctly.
- **Windows opened on monitors or desktops with snapping disabled were still auto-snapped** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#510](https://github.com/fuddlesworth/PlasmaZones/pull/510)): pending-restore entries already held in memory when a context was disabled leaked through on the next window open, so a window could snap to a zone on a screen the user had turned snapping off for, until the daemon was restarted. Snap restore now runs through the same disabled-context gate as the load, save, and close paths, and the effect's instant-restore cache funnels through it too.
- **Electron child popups such as Steam image previews polluted focus tracking** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#510](https://github.com/fuddlesworth/PlasmaZones/pull/510)): transient popups that KWin does not flag with a reliable popup window type still set a transient parent, so the focus-tracking filter recorded them as the active window and downstream placement routed real windows into the popup's zone. The activation filter now rejects the same structural window types as the snap filter, including any window with a transient parent.
- **Session-restored windows were missing from zone-occupancy tracking** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#510](https://github.com/fuddlesworth/PlasmaZones/pull/510)): when the effect's instant-restore cache placed a window it skipped the daemon round-trip that registers the window in the snap state, so the window sat visibly in its zone but counted as unoccupied, and snap assist and empty-zone placement treated the filled zone as free. Instant-restored windows are now registered with the daemon.
- **Snap assist appeared after a bulk resnap that placed nothing** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#510](https://github.com/fuddlesworth/PlasmaZones/pull/510)): a resnap triggered by an autotile or snap toggle, a rotation, or a virtual-screen reconfigure fired the snap-assist continuation unconditionally, so it popped for every empty zone even though no per-window snap had happened. The continuation is now anchored to the active window and shows only when the resnap actually snapped it.

## [3.0.7] - 2026-05-21

### Changed

- **`bounce` drops the window in from above its frame** ([#475](https://github.com/fuddlesworth/PlasmaZones/pull/475)): `bounce` previously revealed the window from its own top edge downward, clipped to the window box. Animation shaders can now opt into a full-surface render mode that gives them room to draw past the window, and `bounce` uses it to play niri's original drop-from-above motion, so the window travels in from above its frame.
- **Overlay cards unified on one shared frame** ([#475](https://github.com/fuddlesworth/PlasmaZones/pull/475)): the layout OSD, navigation OSD, layout picker, and zone selector each hand-rolled their card background, border, and glow. They now share a single `PopupFrame` component, so all four read as the same surface and the soft glow is captured into the transition and animates with the card rather than being clipped away for the duration. The layout picker and zone selector pick up the same glow the OSDs use.
- **`wave-warp` gained a `frontSpeed` parameter** ([#475](https://github.com/fuddlesworth/PlasmaZones/pull/475)): `wave-warp` and the former `crosswarp` ran the identical moving-edge warp, differing only in a front-speed dial. That dial is now `wave-warp`'s `frontSpeed` parameter (default `1.0`).

### Removed

- **`crosswarp` transition** ([#475](https://github.com/fuddlesworth/PlasmaZones/pull/475)): it was `wave-warp` with a fixed front speed. Use `wave-warp` with its new `frontSpeed` parameter instead.
- **`plasma-flow` transition** ([#475](https://github.com/fuddlesworth/PlasmaZones/pull/475)): it was a re-skin of `soft-warp-fade`, the same noise-driven UV warp and fade, differing only in trivial constants.

### Fixed

- **Window open / close animations rendered as ghosted, multi-copy trails on KWin** ([#475](https://github.com/fuddlesworth/PlasmaZones/pull/475)): translation transitions (`bounce`, `fly-in`) drew several overlapping copies of the window during an open or close. `paintWindow` called `OffscreenEffect::drawWindow` directly, leaving KWin's shared draw-window iterator parked at the start, so the offscreen capture re-entered the effect and drew the window's own texture into itself. The transition now routes through `effects->drawWindow` so the capture reaches the real draw stage, and KWin's stock fade / scale / slide builtins are held off the window so they no longer render a second concurrent copy.
- **New windows flashed at their spawn position before animating into place** ([#475](https://github.com/fuddlesworth/PlasmaZones/pull/475)): KWin places a new window (centered or smart) before the effect sees it, and the reposition into a zone or tile is asynchronous on Wayland, so a snap-restored or autotiled window visibly flickered at the centered spawn position for one to three frames. New-window pixels are now withheld until the reposition lands (with a 250 ms safety deadline), so the window first becomes visible already animating into its zone.
- **Snap-restored windows played their open animation from the screen center** ([#475](https://github.com/fuddlesworth/PlasmaZones/pull/475)): a snap-restored window ran its `bounce` / `fly-in` open animation from the centered spawn position and then jumped to the zone once KWin's asynchronous move landed. The in-flight open transition is now pinned to the resolved zone, so the effect plays into the zone from the first frame.
- **OSD glow popped in at the end of a transition** ([#475](https://github.com/fuddlesworth/PlasmaZones/pull/475)): the soft glow behind the layout and navigation OSDs was clipped away for the duration of a show / hide animation and snapped back into place when the animation ended. The glow is now captured together with the card so it scales and moves with it throughout the transition.
- **Faint grey halo around daemon OSDs during `bounce` / `fly-in`** ([#475](https://github.com/fuddlesworth/PlasmaZones/pull/475)): both effects painted a faint grey border around the OSD while animating, because the effect's edge feather fell just outside the captured texture and tinted its clamped edge pixels. The edge crop is now a sub-pixel, edge-aligned antialias band, so the border is gone and the edge stays crisp.

## [3.0.6] - 2026-05-20

### Fixed

- **Settings window (and any 5th+ window) left tiled on autotile→snap** ([#504](https://github.com/fuddlesworth/PlasmaZones/pull/504)): `calculateResnapFromAutotileOrder` capped both passes at `min(windowCount, zoneCount)`, silently dropping windows past the new layout's zone count even when their pre-autotile zone was unique and still available. With 5 autotiled windows resnapping into a 4-zone layout, the 5th was always left tiled, in practice usually the settings window (last in focus order). The first pass (restore-original-zone) now iterates ALL windows, and the positional fallback iterates all windows but breaks once every zone is claimed. `claimedZoneIndices` still prevents double-booking.
- **250+ ms stall before windows start moving on autotile→snap** ([#504](https://github.com/fuddlesworth/PlasmaZones/pull/504)): `slotScreensChanged` ran a synchronous `setNoBorder(false)` loop on autotile disable. Each call is a Wayland decoration round-trip (30–120 ms per window) and the loop blocked kwin's main thread long enough that the queued `applyGeometriesBatch("resnap")` D-Bus signal couldn't dispatch. The affected window IDs are now stashed and drained from `slotApplyGeometriesBatch`'s `onComplete` once the resnap has been dispatched, so windows start animating to their snap positions within ~2 ms of the daemon emit instead of 250+ ms. The drain processes one window per event-loop tick so frames render between calls, keeping the concurrent OSD show animation smooth. A chunk-time check against `m_autotileScreens` skips the restore if the user rapid-cycles back into autotile mid-drain (the new toggle's `setWindowBorderless(true)` is now authoritative).
- **Focus follows mouse not re-focusing after focus steal** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#503](https://github.com/fuddlesworth/PlasmaZones/pull/503)): `AutotileHandler::handleCursorMoved` short-circuited against `m_lastFocusFollowsMouseWindowId`, a local cache of the window it had most recently auto-focused. The cache was only invalidated by `setFocusFollowsMouse(false)`, `onWindowClosed` for the cached ID, and `slotScreensChanged`, not by Alt-Tab, a click, a daemon-driven activate, or a new window stealing focus. After any of those, the cache pointed at a window the compositor was no longer focusing on, and the next cursor pass-over matched the stale cache and skipped `activateWindow`, so focus stayed wherever it had drifted to. The cache is replaced with a live read against `KWin::effects->activeWindow()`, so the no-op gate fires only when the cursor window actually still holds focus.
- **Tiling assignments saved on the settings page silently dropped** ([#497](https://github.com/fuddlesworth/PlasmaZones/discussions/497), [#499](https://github.com/fuddlesworth/PlasmaZones/pull/499)): `LayoutAdaptor::m_algorithmRegistry` was never wired up after the DI refactor. The daemon's composition root set it on `OverlayService`, `ZoneSelectorController`, etc., but missed `LayoutAdaptor`. Every per-monitor / per-activity tiling-mode assignment hit `setAssignmentEntry`'s validation branch (`!m_algorithmRegistry`), logged `unknown tiling algorithm: "<id>"`, and was dropped before being stored. The cascade fell back to the global default, which read like "my changes reverted." Snapping assignments persisted, and only tiling assignments vanished. The registry is now injected.
- **Daemon toggle re-enabling itself after disable** ([#497](https://github.com/fuddlesworth/PlasmaZones/discussions/497), [#499](https://github.com/fuddlesworth/PlasmaZones/pull/499)): `setEnabled(false)` ran `systemctl --user disable`, which only blocks boot-time autostart. The installed `/usr/share/dbus-1/services/org.plasmazones.service` file pins `SystemdService=plasmazones.service`, so any subsequent D-Bus call from the KWin effect (window-lifecycle callbacks like `setWindowMetadata`, `windowClosed`) routed through systemd and started the unit, and the toggle flipped back on within seconds. Switched to `systemctl --user mask`, which blocks both autostart and D-Bus-routed activation. Re-enable chains `unmask` → `enable`.
- **Snapping Assignments Monitor row reverted to old value after Save** ([#497](https://github.com/fuddlesworth/PlasmaZones/discussions/497), [#501](https://github.com/fuddlesworth/PlasmaZones/pull/501)): `SettingsController::getLayoutForScreen` (snapping side) called the daemon's contextual `getLayoutForScreen` D-Bus method, which walks the current-desktop / current-activity cascade. The tiling counterpart already queried the explicit screen-level slot. The mismatch let a stored per-desktop or per-activity entry shadow the screen-level slot the user just wrote, so the Monitor row's own re-read pulled the higher-priority entry's old value and the combo snapped back. The snapping path now mirrors `getTilingLayoutForScreen`'s slot-level shape.
- **OSD announced the wrong layout name after editing a non-current-context slot** ([#497](https://github.com/fuddlesworth/PlasmaZones/discussions/497), [#501](https://github.com/fuddlesworth/PlasmaZones/pull/501)): the OSD callback on `applyAssignmentChanges` resolved the layout via `layoutForScreen(screen, currentDesktop, currentActivity)`, the same cascade as the Monitor-row bug. Editing a non-current-context slot (e.g. a screen-level row while a `Desktop:1:Activity:X` entry held the visible context) produced an OSD that announced the cascade winner, the unchanged OLD layout, as if it were the change. The `assignmentChangesApplied` signal now carries the full `(screenId, virtualDesktop, activity)` of every modified slot, and the OSD resolves the layout at that exact slot.
- **Windows on disabled monitors / desktops still tracked and resurrected after restart** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#498](https://github.com/fuddlesworth/PlasmaZones/pull/498)): a window closing on a monitor or desktop the user had disabled snap/autotile on still got a `PendingRestore` recorded, then resurfaced when the same app reopened or KWin rehomed it after the disabled screen slept. The persisted `WindowZoneAssignmentsFull`, `PendingRestoreQueues`, and `AutotilePendingRestores` are now filtered on both load and save against the current disable lists, and `WindowTrackingService` takes an injected `ShouldTrackPredicate` so live closes on disabled contexts never write the entry in the first place. The autotile engine matches via an injected `ShouldPersistRestorePredicate` at three filter sites (live, save, load), so future engine changes can't drift from the snap side ([#502](https://github.com/fuddlesworth/PlasmaZones/pull/502)).
- **Re-enabling a disabled virtual desktop sometimes silently failing** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#498](https://github.com/fuddlesworth/PlasmaZones/pull/498)): `setDesktopDisabled`/`setActivityDisabled` did an exact-string remove using the screen-name form QML supplied, but the read-side check resolved both connector-name and screen-ID forms. After a screen redetect, the re-enable removed zero entries and the desktop stayed disabled. Both setters now try every variant on remove.
- **Popup menus and Steam image previews snapped to weird zones** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#498](https://github.com/fuddlesworth/PlasmaZones/pull/498)): `isTileableWindow` rejected PopupMenu / DropdownMenu / Menu / Tooltip / keep-above windows, but `shouldHandleWindow` did not. The asymmetry let a window KWin classifies as e.g. PopupMenu (Steam's chat image-preview popup, on a steamwebhelper class with no `transientFor`) pass the snap-side filter while autotile rejected it, surfacing as "snapped to a zone in snap mode" depending on the screen's current assignment. Both filters now reject the same window types.
- **Stale snap assignments from deleted layouts lingering across first-launch** ([#500](https://github.com/fuddlesworth/PlasmaZones/pull/500)): `SnapEngine::onLayoutChanged()`'s `if (!prevLayout) return;` early-out skipped the stale-assignment cleanup on first launch, so session-restored windows whose zone IDs referenced a deleted layout stayed in `m_snapState` forever. The cleanup also kept multi-zone windows when *any* zone survived, leaving dangling zone IDs in the assignment list that downstream `multiZoneGeometry`/`zonesForWindow` would iterate. The cleanup now runs unconditionally on layout change and rebuilds multi-zone assignments to the surviving subset.
- **Critical-notification windows triggering focus tracking and appearing in the app picker** ([#500](https://github.com/fuddlesworth/PlasmaZones/pull/500)): `notifyWindowActivated` and the app-picker enumeration rejected `isNotification` but not `isCriticalNotification`. KWin treats them as distinct window types, so a critical-notification could fire the focus shader or show up in the KCM exclusion-list picker. Both sites now reject both types.
- **`m_shuttingDown` flag not reset on daemon restart** ([#500](https://github.com/fuddlesworth/PlasmaZones/pull/500)): `stop()` set the flag and nothing else cleared it, so any `stop()→start()` cycle (programmatic restart, tests) left every shutdown-guarded code path silenced on the second run. `start()` now resets the flag.
- **Per-screen autotile algorithm change overwrote the global default** ([#500](https://github.com/fuddlesworth/PlasmaZones/pull/500)): changing the autotile algorithm on a single monitor (e.g. spiral on screen B while screen A stays on default) wrote the new algorithm into `Tiling.Behavior.DefaultAlgorithm`, so the next per-screen apply on screen A picked up screen B's choice as if it were the user's global default.

### Changed

- **Nix release-pipeline transform aligned with the new flake shape** ([#496](https://github.com/fuddlesworth/PlasmaZones/pull/496)): `packaging/nix/generate-release-nix.sh` failed on v3.0.5's tag push because PR #489's flake rewrite introduced a `fetchFromGitHub,` argument and a multi-line `version = let ... in ...` block that the awk transform never knew about. The script now drops or injects `fetchFromGitHub,` to match either pre- or post-rewrite shape, and rewrites the multi-line version block to the hardcoded `version = "<VERSION>";` form. The legacy single-line rewrite stays as a fallback.

## [3.0.5] - 2026-05-19

### Fixed

- **Shader picker dropdown disabled in the layout editor** ([#494](https://github.com/fuddlesworth/PlasmaZones/pull/494)): checking "Enable shader effect" left the dropdown disabled and unable to select a shader because the editor's shader list arrived empty. The `availableShaders` D-Bus call returns the list as an `av` (array of variant). Unlike `a{sv}`, QtDBus does not auto-demarshal a bare `av` into a `QVariantList`. It hands the reply back wrapped in a `QDBusArgument`, so `.toList()` silently produced an empty list and every shader was dropped. The reply is now routed through `DBusVariantUtils::convertDbusArgument` first, exactly as the sibling `shaderInfo` and `translateShaderParams` queries already did.
- **Auto-snap-on-open ignored the global snapping toggle** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#492](https://github.com/fuddlesworth/PlasmaZones/pull/492)): with `Snapping.Enabled=false`, newly-opened windows still got auto-snapped to empty or last zones. `SnapEngine::resolveWindowRestore` gated its empty-zone and last-zone fallbacks only on `modeForScreen() == Snapping`, but `modeForScreen` returns the screen's assigned layout mode, independent of the global Snapping toggle. The `snapTo*`, `restoreToPersistedZone`, and `resolveWindowRestore` D-Bus slots and the shared `applySnapResult` gate never checked the global toggle either. Only `dragStarted` did. Both chokepoints now consult `snappingEnabled()`.
- **Keyboard snap-to-zone shortcuts fired with snapping globally off** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#492](https://github.com/fuddlesworth/PlasmaZones/pull/492)): `IPlacementEngine::isEnabled()` defaulted to `false` and `SnapEngine` never overrode it, so the shared keyboard-shortcut dispatcher had no usable gate for snap-mode shortcuts (autotile shortcuts were already gated on `AutotileEngine::isEnabled()`). `SnapEngine::isEnabled()` now mirrors `AutotileEngine` and reports `snappingEnabled()`, and the navigator dispatcher skips any shortcut whose resolved engine is disabled. Together with the auto-snap fix, `Snapping.Enabled=false` is now a total kill-switch at parity with autotiling.
- **Transient child surfaces snapped to zones** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#492](https://github.com/fuddlesworth/PlasmaZones/pull/492)): the effect's `shouldHandleWindow()` filtered dialogs, utilities, splashes, notifications, OSDs, modals, and popups, but never consulted `w->transientFor()`, despite the comment claiming it skipped transient windows. Sibling filters in the same file (`shouldAnimateWindow`, `isTileableWindow`) already had the transient-parent check. Electron/CEF apps (Steam image previews, Discord popups, VS Code dialogs) spawn child surfaces that frequently fail to report an accurate KWin window type but always set `transientFor`, so they passed the filter and got snapped to a zone. Transient children are now excluded.
- **Generic "effect not running" message hid the real cause on bridge timeout** ([#481](https://github.com/fuddlesworth/PlasmaZones/discussions/481), [#485](https://github.com/fuddlesworth/PlasmaZones/pull/485)): the KWin effect plugin's IID embeds KWin's exact upstream version, and KWin's effect loader silently rejects any plugin whose IID does not match the running compositor, even across patch releases. On bridge watchdog timeout the daemon now reads the installed effect plugin's embedded IID via `QPluginLoader::metaData()`, asynchronously queries the running KWin's `supportInformation()`, and names the exact build-vs-running versions and the remediation in the log and desktop notification. The version probe is non-blocking, so the degraded startup path no longer freezes the daemon event loop for the duration of the round-trip.
- **Nix flake hardened to keep PlasmaZones in sync with the host's KWin** ([#481](https://github.com/fuddlesworth/PlasmaZones/discussions/481), [#489](https://github.com/fuddlesworth/PlasmaZones/pull/489)): expanded module documentation, a new `overlays.default` output, and explicit guidance steer users toward the NixOS module, Home Manager module, and overlay (which build against the consumer's pkgs) and away from `nix profile install` / `packages.default` (which pins to the flake's own nixpkgs and breaks silently when the host's KWin moves past `flake.lock`).

## [3.0.4] - 2026-05-18

### Fixed

- **Blank-class windows restored into each other's saved zones** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461)): a window with a blank or whitespace-only KWin class produced a `" "` app identifier that became a live key in the snap restore queue, so unrelated blank-class windows consumed each other's saved zones. App identifiers are now normalized and validated, and corrupt keys are rejected at the persist site and dropped by the session loader, which also discards pre-3.0 `"resourceName resourceClass"` keys left over from an upgrade.
- **Maximized windows ballooning when autotiled** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461)): a user-maximized window kept its `MaximizeFull` state through the autotile `moveResize`, so KWin re-asserted the maximized geometry and the reactive centering loop compounded it into runaway growth. The tile path now clears maximize state before placing the window, and the desktop-switch restore path clears it too.
- **Per-context disable ignored by auto-snap and keyboard snap** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461)): the interactive drag and autotile paths honored the per-monitor, per-desktop, and per-activity disable lists, but snap-on-open and the keyboard snap-to-zone shortcut did not, so windows still snapped on contexts the user had disabled. Both paths, and the float toggle, now gate on the disable lists.
- **Windows stayed tiled after switching to an autotile-disabled desktop**: the desktop-switch handler never restored windows on the desktop being switched *to*, so arriving on an autotile-disabled desktop left its windows tiled and borderless. The handler now runs a per-window restore pass for the arrived-at desktop.
- **Confusing new-window placement toggle labels** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461)): disabling "New windows to last zone" did not stop windows returning to zones. That behavior is governed by "Restore zones on login", whose title implied it applied only at login. Both toggles were retitled to describe what they actually do.
- **Zone geometry not clamped to the screen** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461)): gap math could produce a zone rectangle extending past the screen edge when fed malformed layout data. `applyGapsToZoneGeometry` now clamps the gapped rectangle to the screen, and resolved snap placements are logged at INFO so geometry reports are diagnosable.
- **KWin effect missing on NixOS** ([#481](https://github.com/fuddlesworth/PlasmaZones/discussions/481)): the KWin effect plugin's IID embeds KWin's exact upstream version, and KWin refuses to load an effect whose IID does not match the running compositor, even across patch releases. The flake's `nixosModules`, `homeManagerModules`, and `overlay` built PlasmaZones against the flake's own pinned `nixpkgs`, so on a rolling NixOS system whose KWin had moved past `flake.lock` the effect's IID no longer matched and KWin silently dropped it, leaving PlasmaZones absent from System Settings → Desktop Effects. The module and overlay now build against the consumer's nixpkgs, so the effect is always compiled against the KWin the user actually runs, matching the exact-version pinning the RPM, Debian, and Arch packages already do.
- **Support report crashed on Qt 6.11+** ([#481](https://github.com/fuddlesworth/PlasmaZones/discussions/481)): qttools' `qdbus`/`qdbus6` segfaults at process exit on Qt 6.11+ (a static-destruction-order crash in `registerComplexDBusType`'s `QMetaType` cleanup) when introspecting an object that exposes complex D-Bus types, which `/PlasmaZones` does. The crash could discard the buffered support report before it was printed. `plasmazones-report` now prefers `busctl`, which is unaffected and present on every systemd distro, over `qdbus`, and the bug-report template recommends the `busctl` invocation instead of `qdbus6`.

## [3.0.3] - 2026-05-17

### Fixed

- **Zones sliding under a top panel** ([#461](https://github.com/fuddlesworth/PlasmaZones/discussions/461), [#472](https://github.com/fuddlesworth/PlasmaZones/pull/472)): with a top panel, zones slid *under* the panel and the bottom edge was over-reserved. The available-geometry heuristic could only guess which screen edge a panel's strut belonged to, and guessed wrong when the plasmashell panel query returned no panels. The KWin effect now reports the compositor's authoritative work area (`clientArea`), which carries correct per-edge strut attribution and auto-hide handling, and the heuristic remains the fallback for sessions where the effect is not loaded.
- **openSUSE Build Service packaging**: cleared the remaining issues blocking a clean OBS build: a missing `wayland-scanner` build dependency, missing transitive KWin `find_package` dependencies, unowned directories, development files leaking into the runtime package, actionable rpmlint warnings, and changelog macros expanding inside spec-file comments.

## [3.0.2] - 2026-05-17

### Added

- **KWin effect bridge status in diagnostics**: support reports and the daemon now surface whether the KWin effect bridge is connected, so it is clear at a glance whether the required effect is loaded and communicating with the daemon.
- **openSUSE packages on OBS**: the release pipeline now publishes openSUSE packages to the openSUSE Build Service.

### Changed

- **ScreenManager decoupled from `QScreen`**: the screen add, remove, move, and resize lifecycle moved behind an injectable `IScreenProvider` seam so it can be regression-tested without a live display. This is an internal refactor with no user-visible behavior change. It adds regression coverage for the multi-monitor geometry path.

### Fixed

- **Layout shift after a screen powers off and back on** ([#465](https://github.com/fuddlesworth/PlasmaZones/discussions/465), [#467](https://github.com/fuddlesworth/PlasmaZones/pull/467)): when a monitor reappeared at a transient `(0,0)` origin on DPMS wake or hotplug, its available geometry stayed pinned to the old origin even after the output settled, shifting every layout anchored to that screen toward the desktop origin. Available geometry is now recomputed whenever a screen moves or resizes.
- **Panels ignored on the first layout pass** ([#466](https://github.com/fuddlesworth/PlasmaZones/pull/466)): the `panelGeometryReady` signal could fail to fire on the first panel query, so components computing initial zone geometry at startup laid windows out against the full screen rect instead of the panel-reserved area. The signal now fires reliably on the first panel reading.
- **EPEL RPM builds** ([#462](https://github.com/fuddlesworth/PlasmaZones/pull/462)): macros inside spec-file comments were expanded by `rpmbuild` on EPEL and broke the build. Comment macros are now escaped.

## [3.0.1] - 2026-05-16

### Fixed

- **AUR `plasmazones-bin` package**: ship `LICENSE` and `COPYING.LESSER` in the release tarball. The binary package installs its licenses from the tarball root and was failing in `package()` because those files were never staged into it.
- **COPR builds**: the `kwin_version` spec macro expanded to a multi-line string when `kwin-devel` was absent, as in COPR's minimal SRPM-build chroot, which injected a newline into every `Requires: kwin = ...` and aborted the build with `Unknown tag: 6.6.0`. The macro now always resolves to a single version token.
- **Debian package**: version the `libplasmazones_rendering` shared library so it installs with a SONAME, consistent with every other shipped library and silencing a `dpkg-shlibdeps` warning.

## [3.0.0] - 2026-05-16

### Added

- **Virtual screens for ultrawide monitors**: physical monitors can be subdivided into independent logical screens, each with its own layouts, autotile state, snap-assist, OSDs, shortcuts, and per-desktop or per-activity assignments. A new `VirtualScreenSwapper` exposes swap and rotate D-Bus methods plus global shortcuts, and a monitor-level OSD acknowledges the action so the user sees which regions moved. The layout editor and settings app are virtual-screen aware throughout, and the configuration UI moved from the editor into the settings app.
- **Animation App Rules**: a new Animations → App Rules page assigns motion curves, shader overrides, and per-event timings to specific window classes. The resolver cascades through global, app-rule, and window-filter layers inside the kwin-effect, and the editor supports curve picking, shader-parameter overrides, lock and randomize controls, and window-class filtering with a `Disable animations` short-circuit. A two-layer resolver with leaf seeds gives widgets distinctive tunings even when they share a parent rule.
- **Unified `PassiveOverlayShell`**: OSDs, snap-assist, the layout picker, the zone selector, and the main zone overlay now run inside one shell surface instead of five separate layer-shell windows. The shell is click-through except when a modal popup is up, hides itself when no slot is visible so clicks pass through, releases its input region after every slot show, and enforces explicit z-order. Slots animate independently, so a snap-assist show no longer cancels an in-flight OSD. Replaces the legacy `NotificationOverlay.qml` lifecycle.
- **Krohnkite-style autotile reorder and unlimited stack**: dragging a tiled window onto another inserts or swaps it into the target slot live via a new Reorder drag behavior. A new Unlimited overflow lets the stack grow past the algorithm's natural cap instead of evicting windows. Drag-insert has always-active and toggle modes matching the snap trigger, and the dropdowns are exposed in Settings → Tiling → Behavior.
- **Animation engine and 47 ported shader transitions**: window transitions run through a new `OffscreenEffect` redirect path with vertex-shader plumbing, parent-FBO bounds extent, live-texture sampling of the rendered surface, and direction-aware leg signals. The release ships compatibility shims that port 28 niri animation shaders via `niri_compat` and 9 Burn-My-Windows shaders via `bmw_compat`, plus 10 new BMW-inspired transitions, an audio-reactive `neon-city` cityscape, a Matrix rain port, a 3D `chrome-protocol` rewrite, and user-texture support for image-driven effects. OSDs use surface sampling for morph, slide, slidefade, and popin.
- **Animations settings UI overhaul**: an eight-phase rework adds an `AnimationsPageController` and drill-down sub-pages for OSDs, overlays, shaders, motion sets, app rules, widgets, windows, and side panels. The Shaders page hosts a per-event shader picker backed by a generic shader browser. Motion sets bundle themed overrides users can switch between. A timing-mode toggle on the General page and change detection on save round it out. A separate Snapping → Shaders page exposes the same browser for snap-assist, zone-selector, and layout-picker effects.
- **Layout ordering for cycling**: the cycle shortcut and zone-selector cycling follow a user-configurable order set under Settings → Snapping → Ordering and Settings → Tiling → Ordering, instead of the implicit save-time order.
- **Per-mode disable lists**: snapping and autotiling each have their own exclusion lists for monitors, virtual desktops, and activities, replacing the single global disable. The disabled-context OSD explains which exclusion is active so the user can find the toggle.
- **Native-feeling wheel scroll in settings** ([#405](https://github.com/fuddlesworth/PlasmaZones/issues/405)): every Flickable settings page uses `Kirigami.WheelHandler` so scroll speed matches the rest of Plasma System Settings, while preserving the Flickable drag-to-flick path.
- **Hold/Toggle controls now work in always-active mode** ([#249](https://github.com/fuddlesworth/PlasmaZones/issues/249)): "Hold to activate" and "Toggle mode" stay enabled when "Activate on every drag" is on, with inverted semantics. The configured trigger deactivates the overlay instead of activating it, so a binding like Right Mouse Button activates when always-active is off and deactivates when on. Esc continues to cancel the drag entirely.
- **Global "auto-assign new windows for all layouts" toggle** ([#370](https://github.com/fuddlesworth/PlasmaZones/issues/370)): a master switch in Snapping Behavior → Window Handling that forces auto-assign on for every manual layout, regardless of the per-layout icon. Effective behavior is `globalToggle OR layout.autoAssign`, and the default is off so existing semantics are preserved on upgrade. While the global is on, the per-layout toggle is disabled and the Auto/Manual badge in the Layouts grid, layout combo, zone selector, layout picker, and layout OSD shows the effective on state.
- **Separate "Desktop switch OSD" toggle** ([#372](https://github.com/fuddlesworth/PlasmaZones/issues/372), [#373](https://github.com/fuddlesworth/PlasmaZones/pull/373)): the layout-preview OSD that fires on virtual-desktop change, activity change, and daemon startup is now governed by its own setting, `Snapping.Effects/OsdOnDesktopSwitch`, independent of `OsdOnLayoutSwitch`. Manual layout switches continue to gate on the existing layout-switch toggle. Default is `true` to preserve current behavior. **Upgrade note**: users who previously set `OsdOnLayoutSwitch=false` to silence the startup or VD-switch flash will see those OSDs return on first launch under the new schema. Toggle "Desktop switch OSD" off in Settings → On-Screen Display to restore the old quiet behavior.
- **New D-Bus methods**: `setPerScreenSettings` accepts a batched per-screen settings map so the editor and KCM can apply screen-scoped changes in one round-trip. `requestRunningWindows` and the `runningWindowsAvailable` signal give the App Rules editor an async window picker that doesn't block while the daemon enumerates KWin clients.
- **Phosphor SDK foundation libraries**: core services are extracted into reusable LGPL libraries, each with its own export macro, headers, and minimal Qt dependencies. The set covers `phosphor-engine` (renamed from `phosphor-engine-api`), `phosphor-layout-api`, `phosphor-animation`, `phosphor-shaders` (extracted from `phosphor-shell`), `phosphor-workspaces` (VirtualDesktopManager and ActivityManager), `phosphor-placement` (WindowTrackingService and WindowRegistry), `phosphor-tiles`, `phosphor-zones` for manual zone-layout primitives, `phosphor-screens`, `phosphor-snap-engine` and `phosphor-tile-engine` for the physically moved SnapEngine and AutotileEngine, `phosphor-config`, `phosphor-fsloader` (renamed from `phosphor-jsonloader`, with the `WatchedDirectorySet` base extracted), `phosphor-shell-patterns`, `phosphor-wayland` (renamed from `phosphor-shell`), and `phosphor-layer`. Geometry primitives use domain-neutral identifiers so the libraries are reusable outside PlasmaZones. `SnapState` is now the single source of truth for snap window state, owned by `SnapEngine`. Forwarding headers were removed in favor of direct library includes.
- **Phosphor compositor SDK**: a new `phosphor-compositor` library exposes `DaemonClient` plus handler interfaces, extracted from the old `compositor-common`, so third-party Wayland compositors such as river can host the placement, tiling, and overlay services without depending on KWin.

### Changed

- **KCM Apply now respects the layout-switch OSD toggle**: the per-screen preview OSDs emitted after a System Settings → Apply previously fired unconditionally, ignoring the user's `OsdOnLayoutSwitch` setting. They now gate on the same flag as cycle, quick-layout, and zone-selector drop. The locked-context OSD remains unconditional so it can explain why a requested change had no visible effect on a locked screen.
- **Drag protocol refactor** ([#310](https://github.com/fuddlesworth/PlasmaZones/discussions/310)): the kwin-effect is now a dumb relay for drag events. The daemon owns all drag routing decisions through a new `beginDrag`, `updateDragCursor`, and `endDrag` protocol with typed `DragPolicy` and `DragOutcome` structs. The previous effect-side distributed state (`m_dragBypassedForAutotile`, `m_cachedZoneSelectorEnabled`, `m_autotileScreens` local cache) went stale during the asynchronous settings-reload window and produced ~40 seconds of dead drags in the reporter's log on #310. The new protocol makes the daemon the single source of truth at drag start, drag cursor update (30Hz fire-and-forget), and drag end. Cross-VS mid-drag flips are emitted by the daemon via `dragPolicyChanged` and applied locally by the plugin, replacing the effect-side flip loop that walked `KWin::effects->screens()` against a local autotile-screen cache. Pinned against drift by a new 8-state truth table test.
- **Meta-F float toggle now runs daemon-local** ([#310](https://github.com/fuddlesworth/PlasmaZones/discussions/310)): the keyboard shortcut previously made 4 D-Bus hops across 3 processes (KWin → daemon → effect → daemon → engine → effect), stalling under any D-Bus backpressure and producing the "pressed Meta-F, nothing happened, seconds later it toggled" symptom. It now reads the active window from `WindowTrackingAdaptor::m_lastActiveWindowId`, fresh frame geometry from a 50ms-debounced shadow (`setFrameGeometry`), and dispatches to the engine in-process. One D-Bus hop remains for the daemon → effect `applyGeometryRequested` paint, targeting sub-50ms latency from keypress to visible toggle. The 100ms debounce on `handleFloat` has been dropped since the in-process path has no D-Bus latency to coalesce.
- **Settings nesting made compile-time**: `ISettings` gains `isZoneSelectorActive()` and `isSnapAssistActive()` composite accessors that return `snappingEnabled() && <child>Enabled()`. Consumers can no longer forget the parent-gate check when reading a nested `Snapping.*` flag. Raw child accessors remain for KCM settings UI code that genuinely needs the unaffected value.

### Fixed

- **No default layout assignment on brand-new virtual desktops** ([#368](https://github.com/fuddlesworth/PlasmaZones/issues/368)): a fresh virtual desktop had no stored entry in the per-context assignment table, so the cascade fell through to a snap-only `defaultLayout()` that had no concept of mode. Users with autotile configured globally saw new desktops behave as if no mode were set at all: autotile never activated, snapping was disabled, drag overlays did not appear, and windows just floated. The cascade now consults a settings-derived `AssignmentEntry` provider as the final step, applying the user's intended mode and default to fresh desktops.
- **Autotile shortcut-adjusted ratio and master count did not persist** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): shortcut adjustments to the autotile master ratio and master count updated runtime state but not the config, the per-algorithm saved settings, or Settings, so the change reverted on the next reload. Shortcut-driven adjustments now propagate through all three with signal-blocking so the values survive every propagation path.
- **Snap-assist showed occupied windows from other virtual desktops** ([#323](https://github.com/fuddlesworth/PlasmaZones/issues/323)): the drag-path occupancy filter walked the live window list without scoping to the current virtual desktop, so a zone that was empty on the user's desktop showed as occupied because a window from another desktop matched it. Now filtered to the current desktop before the occupancy check.
- **Per-activity layout assignments could be overridden by per-monitor defaults** ([#413](https://github.com/fuddlesworth/PlasmaZones/issues/413)): in the layout registry, the fallback chain resolved monitor defaults before per-activity overrides, so a user-set activity-scoped layout could lose to a monitor default that predated it. Activity assignments now win in the precedence order, matching what the assignments UI implies.
- **Layouts context menu eventually stopped opening** ([#406](https://github.com/fuddlesworth/PlasmaZones/issues/406)): in the Layouts page, the right-click context menu would silently fail to open after enough show and hide cycles because separator visibility was imperatively flipped on each show, eventually leaving a dead `null`-context binding. Separator visibility is now declarative and the dead null-context guard was removed.
- **Cycle-triggered resnap affected windows on other virtual desktops**: the daemon's resnap pass after a cycle shortcut walked every tracked window instead of scoping to the active virtual desktop, so cycling on one VD could nudge windows on another VD that happened to share a layout. Now scoped to the active VD.

## [2.8.8] - 2026-05-13

### Fixed
- **KWin effect plugin fails to load after a KWin patch update**: The kwin-effect plugin's IID embeds KWin's exact upstream version string (`KWIN_PLUGIN_VERSION_STRING` in `config-kwin.h`). KWin refuses to load any effect whose IID doesn't match its own version, even across patch releases (e.g. 6.6.4 to 6.6.5). All 2.8.7 packages were built against KWin 6.6.4 and stopped loading the moment distros shipped 6.6.5. Package metadata now pins KWin to the exact upstream patch version captured at build time (RPM `Requires: kwin = %{kwin_version}`, Debian `kwin-common (= ${kwin:Version})`, Arch `kwin=$_kwin_ver`). Packages now refuse to install on a mismatched KWin instead of installing silently broken, and users get a clean dependency error and CI rebuilds against each KWin patch release.

## [2.8.7] - 2026-04-14

### Added
- **`BUILD_KWIN_EFFECT` CMake option** ([#321](https://github.com/fuddlesworth/PlasmaZones/pull/321)): The `kwin-effect` subdirectory hard-requires `find_package(KWin 6.6 REQUIRED)`, which aborted the entire configure step on distros shipping older KWin (Debian 13 / KWin 6.3.6, Ubuntu 24.04, Fedora 40) even though the daemon, editor, KCM, settings app, and QPA plugin build cleanly there. The new `BUILD_KWIN_EFFECT` option (default `ON`) lets packagers pass `-DBUILD_KWIN_EFFECT=OFF` to build everything except the C++ effect plugin. Thanks @iubayb.

### Fixed
- **Qt 6.9 `QWaylandWindow::updateExposure()` compile error on Qt 6.8** ([#321](https://github.com/fuddlesworth/PlasmaZones/pull/321)): `layershellwindow.cpp` called a private method added in Qt 6.9, breaking the build on Qt 6.8.x. Guarded the call with `QT_VERSION_CHECK(6, 9, 0)` and fell back to the underlying public QPA mechanism (`QWindowSystemInterface::handleExposeEvent`) on older Qt. Same visible effect, same expose-event delivery. Thanks @iubayb.

## [2.8.6] - 2026-04-11

### Fixed
- **Emby and other Electron/CEF apps silently break after class rename** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): The daemon's runtime primary key was `"appId|uuid"`, a composite that baked a mutable attribute into the identity used for every per-window map. Emby opens as `emby-beta`, gets tracked, then KWin rebroadcasts it as `media.emby.client.beta`. Every subsequent lookup under the new composite missed, so `toggleWindowFloat`, focus navigation, and snap operations silently failed until a mode toggle rebuilt state from scratch. Introduced `WindowRegistry` as the single source of truth for live-window metadata keyed by the stable KWin instance id. The kwin-effect pushes class/desktop-file/title on `windowAdded` and on every `windowClassChanged` / `desktopFileNameChanged` / `captionChanged` so the daemon always reads the live class instead of parsing a frozen first-seen composite. Session persistence uses `currentAppIdFor()` at save time so a renamed window lands under its live class on disk.

## [2.8.5] - 2026-04-10

### Fixed
- **Master window balloons to 100% on notifications** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): KWin transiently flipped a tiled window's `isMinimized()` state to true and back within ~1-2ms whenever a plasmashell notification popup rearranged stacking. The autotile engine recalculated with N-1 windows, tiling the surviving master to the full screen, before unfloating ~ms later. Users saw the master briefly balloon to 100% on every notification. In the worst reported log, the stack window cycled float/unfloat 34 times in a single day with no user interaction. Fixed with two layers of defense: (1) a class-based filter for Plasma shell layer-shell surfaces (`plasmashell`, `plasma.emojier`, `plasma.notifications`, `krunner`) that don't reliably set `isNotification()`/`isPopupWindow()` on Wayland, removing 508 stray tracking events a day, and (2) a 75ms debounce on the minimize→float commit that coalesces spurious minimize/unminimize cycles to zero D-Bus calls. Real user minimizes always last longer than 75ms so they commit normally.
- **Cannot re-enable PlasmaZones from settings after killing a terminal-run daemon** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): When users ran `plasmazonesd` manually for log collection, systemd's managed instance lost the D-Bus name race and exited. `Restart=on-failure` retried until `StartLimitBurst` was exhausted, leaving the unit wedged in `failed` state. `systemctl --user start` then silently did nothing until logout. `DaemonController::startDaemon` now chains `reset-failed` → `start` so the settings toggle recovers the unit automatically.

## [2.8.4] - 2026-04-09

### Fixed
- **Ephemeral windows entering autotile tree** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): The KWin effect's minimum window size filter initialized to 0x0 and was only populated after an async D-Bus settings load. During that startup race window, all windows, including Steam splash screens and Electron notification popups, bypassed the size check and entered the tiling tree. The cache now initializes to 200x150 (matching daemon defaults) so the filter is active from effect load.

### Added
- **`--log-file` flag for daemon**: `plasmazonesd --log-file /tmp/pz.log` redirects all log output to a file (append mode, thread-safe). Combines with `--debug` for easy bug report capture without piping or `journalctl`.
- **Autotile eligibility diagnostics**: Windows accepted into the autotile tree now log their window class, skipSwitcher, keepAbove, and transient properties at debug level, making it possible to identify why specific windows pass the filter.

## [2.8.3] - 2026-04-09

### Added
- **`--debug` flag for daemon** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): `plasmazonesd --debug` (or `-d`) enables debug-level logging for all `plasmazones.*` categories, replacing the need for `qtlogging.ini` or environment variables when capturing diagnostic output.

### Improved
- **Autotile diagnostic depth**: Debug logging now includes per-window min-sizes used in zone calculation, before/after zone comparison from `enforceWindowMinSizes`, per-window applied geometries in `applyTiling`, min-size cap values, stale min-size clearing on unfloat, and split tree ratio restoration detail. These additions target the intermittent master-goes-to-100% ratio bug reported in #271.

## [2.8.2] - 2026-04-08

### Fixed
- **Autotile windows pushed off-screen on retile** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): When a window's minimum size exceeded its assigned zone (common with browsers on ultrawide monitors), the Wayland centering code centered the oversized window within the zone, pushing it to a negative x/y position, literally off the left edge of the screen. Oversized windows are now left/top-aligned in their zone instead of centered, staying on-screen while the daemon adjusts zone sizes.
- **Min-size clearing regression from 2.8.1**: Removed the indiscriminate `m_windowMinSizes` clearing in `onScreenGeometryChanged()` added in 2.8.1. The min-size feedback loop it guarded against was already eliminated by removing the `targetZone.width()` fallback, so the clearing just forced windows through unnecessary centering discovery cycles, triggering the off-screen push above.

### Improved
- **Autotile diagnostic logging**: Added logging to key autotile paths. Window open/remove events now log IDs and min-sizes, `recalculateLayout` logs zone geometries and split ratios, screen geometry changes are logged, and window eligibility rejections now include the reason. This makes autotile ratio issues diagnosable from `journalctl` without code changes.

## [2.8.1] - 2026-04-08

### Fixed
- **Autotile ratio stuck after notification dismiss** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): Eliminated a min-size feedback loop where the KWin effect's Wayland centering code fell back to reporting the target zone width as a discovered minimum size for apps without a declared `minSize()`. This self-reinforcing constraint locked the master/stack split ratio until the user minimized and restored a window. Only the compositor's declared minimum is now reported.
- **Stale min-sizes after screen geometry change**: Discovered min-sizes for windows on a screen are now cleared when the available geometry changes (e.g., panel/systray resize), preventing stale constraints from overriding the user's split ratio on geometry-triggered retiles.

## [2.8.0] - 2026-04-07

### Added
- **Support report generator** ([#302]): `plasmazones-report` script collects daemon logs, config, and data directory into a tar.gz archive for bug reports and discussions.
- **Autotile window preservation** ([#301]): Autotiled windows now survive layout switches, mode toggles, and daemon restarts, matching the preservation behavior that snapped windows already had.
- **Disabled-context OSD** ([#297]): Visual feedback when toggling PlasmaZones on a disabled desktop, activity, or screen. Shows why nothing happened and where to change it.
- **Config v2 nested schema** ([#295]): `config.json` restructured from flat keys to a nested hierarchy mirroring the settings UI. Existing v1 configs are migrated automatically.
- **Systemd service autostart**: Enabling the daemon toggle now also enables the systemd user service so PlasmaZones starts on login.

### Changed
- **Assignments split to `assignments.json`** ([#300]): Layout-to-screen assignments moved out of the main config into a dedicated file, reducing config churn and merge conflicts.
- **Session state split to `session.json`** ([#298]): Ephemeral session data (window positions, floating state) moved to its own file so it doesn't dirty the user config.
- **Autotile persistence refactor** ([#296]): Session persistence moved from `AutoTileState` to `WindowTrackingAdaptor` for cleaner separation between tiling logic and state serialization.
- **Settings consolidation**: `settings-window.conf` merged into `plasmazones-settings.conf`.

### Fixed
- **Assignment persistence across restart** ([#303]): Layout assignments and tiling window order now survive daemon restarts and mode-cycle toggling.
- **Autotile ratio retry** ([#299]): Bounded retry for transient geometry failures during autotile layout. Stale min-size overrides are cleared after resize settles.
- **Config purge unknown keys** ([#300]): Unknown root-level groups are removed on save, preventing config pollution from obsolete or misspelled keys.
- **Window restore after config v2 migration** ([#295]): `loadState` was bypassing `IConfigBackend` group API after the schema change, breaking window restore on first launch.
- **Watcher double-delete guard** ([#302]): Fixed a use-after-free race in the file watcher teardown path.
- **Format warning in `splitDotPath`**: Fixed `qsizetype` vs `%d` printf mismatch.

## [2.7.1] - 2026-04-06

### Added
- **Custom algorithm parameter UI** ([#294]): Scripted algorithms declaring `@param` metadata now get auto-generated controls in the Tiling settings page. Sliders for numbers, switches for bools, and combo boxes for enums.
- **Ratio step size slider** ([#292]): Configurable step size for master ratio keyboard shortcut adjustments.

### Fixed
- **Per-screen master ratio and count** ([#292]): Per-screen overrides for master ratio and count were not persisted correctly. Fixed key constants, slider bindings, and race conditions in the per-screen config path.
- **Reset to defaults clears per-algorithm settings** ([#292]): Resetting to defaults now properly clears saved per-algorithm autotile settings.
- **OSD shown at ratio bounds** ([#292]): Master ratio OSD was suppressed when the value hit min/max. Now always shown on shortcut press.
- **Shader dark band at adjacent zone edges**: Eliminated a visible seam in the Aretha Shell shader where neighboring zones shared an edge.
- **Shader category duplication in settings**: Fixed duplicate shader categories and easing preview binding errors.
- **Editor shader error box**: Restored the shader compilation error display in the layout editor preview.

## [2.7.0] - 2026-04-04

### Added
- **JSON config backend** ([#286]): Config migrated from INI (`~/.config/plasmazonesrc`) to JSON (`~/.config/plasmazones/config.json`). Existing INI configs are migrated automatically on first launch. The JSON backend supports nested groups, proper array/object serialization, and atomic writes.
- **Master ratio/count OSD values** ([#289]): Shortcut adjustments now show the actual value in the navigation OSD: "Master ratio → 65%" and "Master count → 2".
- **Tumbleweed Drift shader improvements**: New wind-blown sand streams, stronger audio reactivity across all bands, faster animation speeds, larger dust devils, and full-surface treble sparkle. Sand streams follow the configured wind direction.

### Changed
- **Settings navigation**: App Rules moved under Snapping section. Child navigation pages have dividers for visual grouping.

### Fixed
- **Drag-to-float keeps drop position** ([#289]): Dragging a window off the autotile layout no longer snaps it back to its pre-autotile position. The daemon's geometry restore is skipped for drag-initiated floats.
- **Shortcut ratio/count persistence** ([#289]): Master ratio and count changes via keyboard shortcuts were only in memory due to QSignalBlocker suppressing the save path. Added debounced save so values survive reboots.
- **Algorithm switch persistence** ([#289]): Algorithm changes via settings also use QSignalBlocker and had the same missing-save issue. Now triggers the same debounced save.
- **Minimum window size for autotile** ([#290]): The user-configured minimum window width/height was never checked in the autotile path. Small utility windows like the Plasma emoji picker entered the tiling tree, causing rapid float/unfloat cycles that broke the master ratio. Now enforced in the KWin effect before windows reach the daemon.
- **Focus-follows-mouse through small windows** ([#290]): Windows below the minimum size threshold no longer steal focus on hover. They block focus-through like excluded apps and popups.
- **Shortcut debouncing** ([#288]): Rotate, float toggle, and layout cycle shortcuts are now debounced to prevent rapid-fire D-Bus calls from key repeat.
- **Overlay Vulkan surface churn**: Non-shader overlays are hidden instead of destroyed and recreated, reducing Vulkan surface create/destroy cycles.
- **Snap assist window pooling**: Snap assist reuses its window instead of creating a new one each time, and rejects stale layout requests.
- **JSON config serialization**: Arrays and objects in the config file are properly serialized through the flat map representation.

## [2.6.0] - 2026-04-03

### Added
- **Tumbleweed Drift shader**: openSUSE Tumbleweed branded zone overlay with animated desert landscape, rolling pinwheel logos, dust devils, sand particles, erosion flow lines, and responsive audio reactivity.
- **Neon Venom and Chrome Protocol shaders**.
- **Voxel-terrain improvements**: Multipass with depth buffer, highlight visibility, pulse-flow label effects.
- **Shader engine**: Depth buffer via MRT, per-channel filter/wrapping modes for multipass buffers, shader presets from registry.
- **Autotile JS API enrichment** ([#274]): Per-window context, custom parameters, and lifecycle hooks for scripted algorithms.
- **Layout ordering settings** ([#281]): Configure snapping and tiling cycle order in settings.
- **What's New dialog** ([#282]): Per-release highlights shown on first launch after update.
- **Settings `.desktop` file** for application launchers.

### Changed
- **Shared config backend** ([#276]): Single `QSettingsConfigBackend` shared across daemon instead of per-component instances.
- **Stale config key purging** ([#280]): Settings save removes obsolete keys to prevent config pollution.
- **ConfigDefaults reference constants** ([#278]): Autotile defaults reference `Defaults::` constants instead of duplicating values.

### Fixed
- **Shader parameter defaults**: `customParams` initialized to `-1.0` sentinel so fallback checks work on first frame.
- **DOF focal depth stability**: Multi-sample region averaging prevents jittery depth-of-field.
- **Vulkan surface lifecycle**: Fixes for surface creation on rapid show/hide, teardown crashes, keep-alive window management, deferred `QVulkanInstance` creation, scissor state, and swapchain colorspace.
- **Rendering pipeline stability**: RHI resource release on scene stop, multipass crash guards, SRB resets on label texture resize, texture upload ordering.
- **OSD improvements**: Navigation self-destruct fix, masterCount wiring, autotile metadata in layout OSD, per-screen dismiss.
- **Focus-follows-mouse**: No longer focuses through excluded or overlay windows.
- **Autotile shortcut sync**: Master ratio and count changes from shortcuts now persist to config.
- **Settings stability**: Single-instance enforcement, daemon toggle desync from broken QML bindings, RenderingBackend read from ungrouped config keys.
- **i18n**: Correct plural form selection in `i18np` without `.ts` files.
- **Layout picker**: No longer overwrites the global default layout when selecting per-screen.

## [2.5.3] - 2026-04-01

### Fixed
- **Autotile split ratio feedback loop** ([#273]): The KWin effect reported the window's actual frame geometry as its "minimum size" when a window resisted shrinking (e.g., during browser media loading). This inflated min-size was never cleared, creating a feedback loop that expanded the master zone to ~90% or full width. Now uses the window's declared `minSize()` from the compositor, with zone-size fallback for apps without declared minimums. Daemon caps all received min-sizes at 90% of screen dimension as a secondary safety net.
- **Overlay/popup focus guard for autotile** ([#272]): Windows with `keepAbove` set (Spectacle, color pickers, screen rulers) were entering the autotile tree and stealing focus when `focusNewWindows` was enabled. Added `keepAbove()` check to `isTileableWindow()` so overlay/utility tools are excluded from autotiling.
- **"Hold to activate" trigger reset on daemon restart** ([#275]): Qt's QConfFile cache was shared between the Settings and LayoutManager backends (same file, same format). When `reparseConfiguration()` destroyed the Settings QSettings, the LayoutManager's instance kept the stale QConfFile alive, preventing re-read from disk. Settings changed by the external settings app were invisible to the daemon's reload, silently reverting `DragActivationTriggers` to the default (Alt). Now reads the file directly and overwrites the QSettings cache.

## [2.5.2] - 2026-03-31

### Fixed
- **Layout popup algorithm previews**: Algorithm previews in the zone selector and layout picker popup now respect algorithm metadata (zone number display mode, producesOverlappingZones, master indicator dots) like the settings app does.
- **Window picker exclusion lists**: WindowPickerDialog was shadowing the `appSettings` context property, causing addExcludedApplication/addExcludedWindowClass to silently fail. Now routes through settingsController.settings.
- **Autotile split ratio state corruption**: Reset suspiciously high split ratios (> max - 0.05) on state load to prevent layouts from being stuck with unusable splits.
- **Autotile cursor hover focus**: The hover-to-focus check in AutotileHandler was using the old `shouldHandleWindow` method. It now uses `isTileableWindow` for consistency.

## [2.5.1] - 2026-03-30

### Added
- **Independent autotile sticky window handling**: Separate setting for how autotiling handles sticky windows (on all desktops), independent from the snapping setting. Configurable in Tiling > Behavior.

### Fixed
- **Editor shader crash**: Null pointer dereference in `ZoneShaderNodeRhi::render()` when switching between multipass shaders, from a missing null check on `m_multiBufferTextures[i]`.
- **Editor undo crash**: Guard `m_undoController` dereferences in `setCurrentShaderParams()`, `setShaderParameter()`, `resetShaderParameters()`, and `switchShader()` to match existing pattern.
- **Arch packaging**: PKGBUILDs referenced `kbuildsycoca.hook` and `plasmazones-refresh-sycoca` as standalone source files instead of using in-tree paths, causing `makepkg` to fail with "cannot stat" errors.

## [2.5.0] - 2026-03-30

### Added
- **Scripted tiling algorithms** ([#256], [#259]): All 24 tiling algorithms are now JavaScript, running in a sandboxed QJSEngine with hot-reload. The 15 former C++ algorithms have been converted to JS with identical behavior. Six new algorithms added: Cascade, Corner Master, Floating Center, Horizontal Deck, Paper, and Stair. Custom user algorithms are loaded from `~/.local/share/plasmazones/algorithms/`.
- **Dwindle (Memory) algorithm**: Dwindle variant with a persistent split tree, where resizing one tile does not affect others. Split positions survive window close/reopen.
- **Multi-compositor support** ([#261]): Custom `pz-layer-shell` QPA plugin replaces the `LayerShellQt` dependency. PlasmaZones now works on any Wayland compositor with `zwlr_layer_shell_v1` support (Hyprland, Sway, Wayfire, niri, COSMIC, river, labwc).
- **Vulkan rendering backend** ([#264]): Optional Vulkan backend for zone overlay rendering with automatic fallback to OpenGL on unsupported hardware or driver crash. User-selectable in **Settings → General**.
- **New Layout / New Algorithm wizards** ([#263]): Guided dialogs for creating zone layouts from templates and tiling algorithms from a starter script, accessible from the settings app.
- **Layout filter bar** ([#265]): Replace hardcoded layout groups with configurable group-by, sort-by, and filter controls in the layout list.
- **Disable per virtual desktop / activity** ([#260]): Disable PlasmaZones on specific virtual desktops or activities per screen. Overlay hides automatically on disabled contexts.
- **Settings UX polish** ([#268], [#269]): Two-line description rows, collapsible card sections, consolidated footer bar with Apply/Reset/Defaults, sidebar page badges, inline toggle switches replacing disable checkboxes.
- **Single-instance settings app** ([#270]): `plasmazones-settings` is now single-instance via D-Bus. Launching it again raises the existing window and navigates to the requested page (`--page <name>` / `-p <name>`).
- **Unsaved changes confirmation**: Settings app prompts before closing with unsaved changes. Reset and Defaults buttons require confirmation.
- **Master indicator dots**: Algorithm grid cards show dots indicating which zones are master positions.
- **Memory indicator icon**: Stateful algorithms (Dwindle Memory) show a persistence icon in the algorithm selector.
- **Per-algorithm settings storage**: Split ratio, master count, and other parameters are saved per-algorithm rather than globally.
- **Algorithm import and open folder**: Import `.js` algorithm files and open the user algorithm directory from the tiling settings page.
- **Algorithm capability grouping**: Tiling algorithms grouped by capability (Built-in / Extras / Custom) in the settings UI.
- **D-Bus `zoneIds` array**: Window state responses now include the full list of zone IDs a window occupies.

### Changed
- **LayerShellQt replaced**: Custom `pz-layer-shell` QPA plugin is now the sole layer-shell backend. Packagers: drop `layer-shell-qt` build dependency, add `qt6-wayland` and `wayland-scanner`.
- **Config keys centralized** ([#266]): All config group names and key strings extracted to `ConfigDefaults` accessors. No more inline string literals in settings code.
- **Settings page IDs renamed**: `snap-*` / `tile-*` page IDs renamed to `snapping-*` / `tiling-*` for consistency.
- **Algorithm registry**: Hardcoded algorithm ID constants replaced with a data-driven registry. Algorithm metadata (name, capabilities, flags) comes from JS `@tag` annotations.
- **README streamlined**: Detailed algorithm table, shader table, D-Bus API reference, and project structure moved to the wiki. README reduced from 742 to 593 lines with summary + wiki links.

### Removed
- **LayerShellQt dependency**: No longer required. Replaced by `pz-layer-shell` QPA plugin.
- **C++ tiling algorithm implementations**: All algorithms are now JavaScript. The C++ implementations have been removed.
- **Legacy migration code**: Removed all config key migration and backward-compatibility shims from settings save/load paths.
- **kcfg/kcfgc schema files**: Unused KConfig schema files removed.
- **Accent stripe feature**: Removed from `SettingsCard` component.

### Fixed
- **Window restore on daemon restart**: Bypass QSettings cache in `loadState` so window-to-zone mappings are read fresh from disk after daemon restart ([#268]).
- **Multipass shader rendering**: Fix ping-pong buffer handling in overlay renderer for multi-pass shaders.
- **Editor shader menu crash**: Rewrite shader submenu to prevent Qt 6 `finalizeExitTransition` use-after-free when selecting a shader while the menu is animating closed.
- **Editor context menu crash**: Use shared context menu instance to prevent `QQmlData` use-after-free when zones update while the popup is open.
- **NVIDIA Vulkan crash**: Suppress spurious shutdown crash on NVIDIA drivers and auto-fallback to OpenGL.
- **NVIDIA EGL crash in editor**: Guard shader preview reload against EGL context loss on NVIDIA.
- **Layout picker dimmed backdrop**: Remove unintended dimmed background overlay from the fullscreen layout picker.
- **CAVA crash suppression**: Suppress misleading "CAVA Crashed" error message caused by process-group SIGTERM during daemon shutdown.
- **Shader preview bindings**: Restore reactive label and wallpaper texture bindings in the editor shader preview.
- **Settings dirty flag on navigation**: Prevent spurious unsaved-changes prompts when switching between settings pages.
- **Slider snap-back bug**: Fix master count control snapping back to previous value, replacing SpinBox with SettingsSlider.
- **Aspect ratio menu**: Replace flat menu items with nested submenu for aspect ratio presets.
- **Layer-shell window recovery**: Recover shader preview when the Wayland `LayerSurface` is unexpectedly destroyed.

### Migration Notes (Packagers)
- Drop `layer-shell-qt` / `liblayershellqtinterface-dev` build dependency
- Add `qt6-wayland` / `qt6-wayland-dev` build dependency
- Add `wayland-scanner` build dependency (usually in `wayland-devel`)
- Add `vulkan-headers` and `vulkan-loader` build dependencies (optional, for Vulkan backend)

## [2.4.7] - 2026-03-27

### Added
- **Center-distance zone selection for overlapping zones** ([#258]): When zones overlap (e.g. quadrants + halves + fullscreen), the zone whose center is closest to the cursor now wins instead of always picking the smallest zone. This lets users reach background zones by dragging toward their center, matching the FancyZones behavior. The multi-zone span path is unaffected, preserving the [#211] fix.

### Fixed
- **Window picker inserts unmatchable values** ([#251]): "Pick from running windows" in App Rules and Exclusions inserted raw X11 window class format (e.g. `"signal signal"`) instead of the normalized form used for matching (`"signal"`). Manually typing the name worked. Using the picker did not.
- **Keyboard shortcuts move excluded windows** ([#251]): Move, Push, and Swap keyboard shortcuts ignored exclusion rules, moving the window behind an excluded app instead of doing nothing. All navigation shortcuts now check exclusions consistently.
- **Drag-out unsnap doesn't clear persisted zone** ([#251]): Dragging a window out of its zone and closing it would still persist the zone, causing the window to snap back on reopen. The floating state flag was not always set due to an overly strict guard condition.
- **D-Bus zone detection missing geometry recalculation**: `detectMultiZoneAtPosition` did not call `recalculateZoneGeometries` before detection, potentially using stale zone coordinates.

## [2.4.6] - 2026-03-27

### Added
- **Plasma Sigil shader**: Animated energy sigil based on the PlasmaZones icon with glowing rune effects.

### Fixed
- **System Settings crash when opening PlasmaZones KCM**: The KCM linked the entire `plasmazones_core` library (with layer-shell QPA plugin, PlasmaActivities, 21 static initializers) just to read a version string. When the daemon was not running this caused heap corruption and SIGABRT during QML binding creation. Replaced with a compile-time version define so the KCM no longer loads the core library at all.
- **Editor context menu crash on zone updates**: Use shared context menu to prevent QQmlData use-after-free crash when zones update while the menu is open.

## [2.4.5] - 2026-03-26

### Added
- **SVG support for shader textures**: SVG/SVGZ files can now be used as user texture parameters in shaders, rasterized at configurable resolution (64–4096px, default 1024) via `QSvgRenderer`. An inline resolution spinbox appears in the shader settings UI when an SVG is selected.

### Fixed
- **Exclusions UI: can't add new entries** ([#251]): The QML JS array mutation pattern (`slice()` + `push()` + reassign) silently fails in Qt 6.10 due to `QStringList`↔JS Array round-trip type confusion. Replaced with `Q_INVOKABLE` C++ methods that modify the `QStringList` directly and emit proper `NOTIFY` signals.
- **Excluded app keyboard shortcuts: no feedback** ([#251]): Snap-to-zone shortcuts blocked by exclusion rules now emit OSD feedback instead of failing silently.
- **Neon Phantom shader white-out**: Reduced brightness multipliers and widened energy smoothstep range to prevent the effect from blowing out to featureless white at high energy accumulation.

## [2.4.3] - 2026-03-26

### Fixed
- **Identical monitors showing as duplicates in settings** ([#252]): Two monitors with the same EDID (manufacturer/model/serial) got the same screen ID, causing the settings UI to show the primary monitor twice and tiling/snapping to only work on one monitor. Screen IDs now append `/ConnectorName` when duplicates are detected, with backward-compatible fallback matching for saved configs.
- **App-to-Zone rules not working** ([#254]): Rule matching used raw substring comparison that failed when appId format differed from user input (e.g. "firefox" vs "org.mozilla.firefox"). Replaced with `appIdMatches()`, segment-aware dot-boundary matching that handles both directions and partial last-segment prefixes.
- **Exclusions ignored by auto-snap and keyboard shortcuts** ([#254]): The exclusion settings interface existed but was never checked. Added exclusion gates in both the auto-snap chain (`resolveWindowRestore`) and keyboard shortcut path (`snapToZoneByNumber`).
- **Unsnapped windows re-snap on reopen** ([#254]): Manually unsnapping a window didn't clear its pending restore entry, so closing and reopening it snapped it back. Now consumes the pending entry on unsnap (multi-instance safe).
- **Drag-out unsnap doesn't restore window size** ([#254]): The geometry validation path didn't pass the release screen ID, causing cross-screen coordinate validation to fail silently. Also fixed premature pre-tile geometry cleanup that prevented later float-toggle restore.
- **Render node use-after-free during hot-reload**: The scene graph render thread could dereference a dangling `QQuickItem` pointer after shader hot-reload when `bufferFeedback` (ping-pong) was active. Added atomic invalidation flag with acquire/release ordering.

### Added
- **Ember Trace shader**: Fractal fire patterns via ping-pong feedback buffer, the first shader to use the `bufferFeedback` feature. Zone borders emit flames that spiral inward via feedback zoom, with 7 layered visual systems including reaction-diffusion-like dynamics, curl-noise advection, and per-band audio (bass eruption shockwaves, mids feedback phase shift, treble turbulent mixing).
- **Neon Phantom shader**: Neon-lit cyberpunk zone overlay.

## [2.4.2] - 2026-03-25

### Fixed
- **Multi-zone span pulls in background zones** ([#249]): Spanning adjacent sub-zones (e.g. zones 7 & 9) while a larger zone existed underneath incorrectly included the background zone, making the window much larger than intended. Fixed both proximity-snap and paint-to-span code paths to exclude background/overlay zones from the span.
- **Settings UI not refreshing when daemon starts from toggle**: UI data now reloads when the daemon is started via the settings toggle.

## [2.4.1] - 2026-03-25

### Fixed
- **Edge threshold resets to 100px** ([#237]): The UI allowed up to 500px but the C++ setter/loader still clamped to 100. Values above 100 were silently clamped back on save. Also fixed the `.kcfg` schema max to match.
- **Per-screen autotile split ratio using wrong default**: Per-screen overrides fell back to the algorithm default (0.6) instead of the config default (0.5) when no override was stored.
- **Shortcuts KCM shows "plasmazonesd" with no icon**: Added KGlobalAccel component desktop file and restored `setApplicationDisplayName`/`setWindowIcon` lost during KAboutData removal.
- **Retile shortcut conflicts with Spectacle**: Changed default from Meta+Shift+R (Spectacle rectangular region capture) to Meta+Ctrl+R.

### Changed
- **ConfigDefaults is now the single source of truth** for all setting defaults, min/max bounds, and shortcut defaults. Previously duplicated across `configdefaults.h`, `settings.h`, `setters.cpp`, `loadsave.cpp`, `perscreen.cpp`, QML pages, `.kcfg`, and tests. Now every consumer references `ConfigDefaults`. Changing a bound or default requires editing exactly one place.
- **Settings bounds exposed to QML** via `SettingsController` constant Q_PROPERTYs. All QML `from:`/`to:` values reference the controller instead of hardcoded literals.
- **Removed dead code**: Unused `ZoneSelectorCard.qml`.
- **Added `FilterLayoutsByAspectRatio` to `.kcfg`**: Was implemented in code but missing from the schema.
- **Editor defaults consolidated**: Previously hardcoded in `loadsave.cpp`, now in `ConfigDefaults`.

## [2.4.0] - 2026-03-25

### Added
- **KZones layout import** ([PR #244]): Import zone layouts from KZones configuration files.
- **Aspect ratio layouts** ([PR #242]): Auto-detect monitor aspect ratio, editor selector for ratio-specific layouts, and filter setting for the layout grid.
- **Portable Wayland build** ([PR #231]): `USE_KDE_FRAMEWORKS=ON/OFF` CMake option. Pluggable backends for config (`IConfigBackend`), shortcuts (`IShortcutBackend`), wallpaper (`IWallpaperProvider`), and i18n (Qt Linguist). Runs on non-KDE Wayland compositors.
- **Standalone settings app** ([PR #238]): `plasmazones-settings` with KCM-style sidebar drill-down, subpages, visual monitor selector, search, keyboard shortcuts overlay, unsaved-changes indicator, and DPI-aware animations. Meta+Shift+P shortcut to launch.
- **D-Bus API audit** ([PR #246]): New `CompositorBridge`, `Control`, and `Shader` interfaces. Convenience methods and full API specification. Unit tests for all new methods.
- **Arch Drift branded shader**: Terminal rain columns, isometric chevron grid, traveling data packets, and CRT scan lines. 95-vertex SDF polygon from the official Crystal SVG.
- **EndeavourOS Drift branded shader**: Constellation network background with 24 dots on Lissajous orbits, warm gradient wash, and tri-sail SDF logo (58 vertices total).
- **Wind currents + sails shader**: Replaced constellation network with flowing wind current effect.

### Changed
- **KWin effect reduced to thin interface layer** ([PR #245]): Effect code is now a minimal bridge between KWin and the daemon over D-Bus. All tiling/snapping logic lives in the daemon.
- **Removed unused `LayoutType` enum**: Cleaned up `Layout` model. The `type` field was never used.
- **Dropped `IConfigBackend` interface indirection**: `QSettingsConfigBackend` used directly after KConfig removal stabilized.
- **Removed KConfig dependency from test suite**: Tests use the standalone config backend.
- **Removed daemon toggle from KCM**: Moved `DaemonController` to `src/common`.

### Fixed
- **Synchronous D-Bus calls freeze compositor**: Eliminated blocking D-Bus calls in the KWin effect that caused compositor hangs.
- **Qt6 SIGSEGV in context menu**: Moved context menu outside `Loader` to avoid crash.
- **Qt6 crash in aspect ratio submenu**: Flattened submenu to avoid nested popup crash.
- **Tooltip flickering on layout cards in Flow layout** ([#235]): Fixed hover handler conflicts.
- **Zone previews clamped for fixed-geometry layouts**: Preview rendering no longer overflows card bounds.
- **Edge threshold max increased**: From 100px to 500px.
- **Settings live lock sync and screenId resolution**: OSD lock state now syncs immediately.
- **EndeavourOS Drift GLSL type errors**: Fixed vec2/float mismatches and wired dead parameters.
- **Shader file watcher dropped after cmake install**: Re-watches directories after inode replacement.
- **Layout card icon hover flicker** ([#235]): Replaced `HoverHandler` with `MouseArea` to fix grab conflicts.

## [2.3.16] - 2026-03-21

### Fixed
- **Layout card button flicker on hover** ([#235]): The Auto-assign and Visibility toggle buttons flickered when hovered. There were two cooperating causes. First, the right-anchored Row reflowed leftward when buttons toggled `visible:`, shifting button positions and destabilizing hover. Second, `ToolTip.visible: hovered` with no delay opened a popup that stole pointer focus on some compositors. Wrapped each ToolButton in a fixed-size Item to eliminate geometry reflow and added `ToolTip.delay` to break the feedback loop.
- **Zone selector grid ignoring columns/rows at fixed preview sizes**: The `maxRows` setting was only applied when preview size was "Auto". Fixed to apply for all size modes in Grid layout.

### Changed
- **Improved zone selector edge scrolling**: Widened auto-scroll trigger zone from 32px to 48px and increased max scroll speed by 75% for more responsive scrolling during window drag.
- **Zone selector scroll D-Bus API**: Added `selectorScrollWheel` D-Bus method and `applyScrollDelta` QML function for programmatic scrolling. Infrastructure is wired and ready for when KWin exposes pointer axis events to effects.

## [2.3.15] - 2026-03-21

### Fixed
- **Shader preview crash on hover** ([#235]): Moving the mouse over the shader preview in the editor dialog crashed with SIGSEGV in `QV4::Lookup::getterQObject`. The `MouseArea.onPositionChanged` handler's QObject-backed event parameter was invalidated mid-evaluation by cascading signal chains on Qt 6.10. Replaced `MouseArea` with `HoverHandler` which uses a value-type point property.
- **CAVA settings not applied dynamically**: Enabling/disabling CAVA audio visualizer or changing bar count/sample rate in KCM required a daemon restart. The KCM's batch `setSettings` applied values with `QSignalBlocker`, then `load()` saw no change (in-memory values already updated). Added `syncCavaState()` called from `updateSettings()` on every settings reload.
- **Layout card button flicker at fractional DPI**: The Auto-assign and Visibility toggle buttons flickered when hovered at 125% display scaling. The `visible:` property toggling caused Row geometry reflow that shifted button positions by sub-pixel amounts, creating a hover feedback loop. Replaced `visible:` with `opacity:`/`enabled:` and added `ToolTip.delay`.

### Changed
- **Enhanced label effects for branded shaders**: CachyOS, Fedora, Neon, and NixOS Drift shaders had label text bodies that appeared solid white. Text fill patterns used screen-space UV which barely varied within characters, and `smoothstep(0.3, 0.9, labels.a)` washed to white. Rewrote all four with pixel-space patterns, edge rim detection, and `x/(0.6+x)` tonemapping. Each shader gets a unique style. Digital shatter (CachyOS), frost crystalline (Fedora), neon tube flicker (Neon), hash grid verification (NixOS).

## [2.3.14] - 2026-03-21

### Fixed
- **Flickering icons on layout card hover** ([#235]): The visibility and auto-assign toggle icons in the KCM layouts grid flickered when hovering over them. The `ToolButton` controls stole hover from the underlying `MouseArea`, causing a containsMouse feedback loop. Replaced `MouseArea` hover tracking with a `HoverHandler` which doesn't lose hover state when child controls intercept mouse events.

## [2.3.13] - 2026-03-21

### Fixed
- **Resnap buffer empty on layout change**: Cycling layouts, using the layout picker, or selecting from the zone selector never resnapped windows to the new layout's zones. `UnifiedLayoutController::applyEntry()` blocks `activeLayoutChanged` via `QSignalBlocker`, which prevented `onLayoutChanged()` from populating the resnap buffer. All layout change paths now explicitly populate the buffer via `populateResnapBufferForAllScreens()` before resnapping.
- **Per-screen mode toggle and layout cycle scoped to target screen**: Mode toggle (autotile/snapping) and layout cycling now correctly operate on the focused screen only, rather than affecting the global active layout state for all screens.

### Changed
- **Unified layout change resnap path**: Layout picker, zone selector, cycle, and quick-layout shortcuts all route through `resnapIfManualMode()` instead of using separate inline resnap logic. Eliminates duplicate code and ensures consistent resnap behavior across all layout change entry points.
- **Renamed screenName variables to screenId/connectorName**: Internal refactor for consistent naming of screen identifiers throughout the codebase.

## [2.3.12] - 2026-03-21

### Fixed
- **EDID serial mismatch between effect and daemon**: KWin 6's `Output::serialNumber()` returns the EDID text serial descriptor (e.g. `HNTY800697`), while Qt's `QScreen::serialNumber()` returns the EDID header serial (e.g. `810700097`). This caused the daemon to fail screen lookups for IDs received from the KWin effect. `findScreenByIdOrName` now falls back to manufacturer + model matching when serials differ and there's exactly one screen of that make/model.

## [2.3.11] - 2026-03-20

### Fixed
- **Move/Swap hotkeys on dual same-model monitors**: On setups with two identical-model monitors (e.g. dual Samsung Odyssey G93SC with different serials), KWin's `EffectWindow::screen()` can return the wrong output. Navigation hotkeys now trust the daemon's stored screen assignment for snapped windows (set at snap time, always correct) and fall back to the effect-provided screen only for unsnapped windows. Cross-screen moves are handled by `outputChanged` → `windowScreenChanged` which unsnaps the window before navigation fires.

## [2.3.10] - 2026-03-20

### Fixed
- **Move/Swap hotkeys use wrong screen's layout**: When a window was moved between monitors externally (KDE's Move-to-Screen shortcut, manual drag racing with `outputChanged`), keyboard navigation hotkeys used the stored screen assignment instead of the window's actual screen, snapping the window back to the old monitor or computing zone geometry from the wrong layout. Now always uses the effect-provided screen (`EffectWindow::screen()`) and detects stale assignments: if the stored screen differs from the actual screen, treats the window as unsnapped and navigates from scratch on the correct screen's layout.

## [2.3.9] - 2026-03-20

### Fixed
- **Modifier key settings zeroed on save**: Changing the activation modifier in the KCM and saving silently wrote all-zero triggers to KConfig, permanently disabling zone activation. Complex D-Bus types (`QVariantList` of `QVariantMap`s) arrived as `QDBusArgument` objects that `toList()`/`toMap()` couldn't extract. Now applies `DBusVariantUtils::convertDbusArgument()` in both `setSetting()` and `setSettings()` before passing values to setters. Affects `dragActivationTriggers`, `zoneSpanTriggers`, and `snapAssistTriggers`.

## [2.3.8] - 2026-03-19

### Added
- **Batch settings D-Bus method**: New `setSettings(QVariantMap)` method applies multiple settings in one D-Bus call with a single KConfig save. Complete settings registry with 87 entries covering all autotile, zone selector, shortcut, and behavior settings.
- **Per-screen settings D-Bus methods**: New `setPerScreenSetting`/`clearPerScreenSettings`/`getPerScreenSettings` D-Bus methods for autotile, snapping, and zone selector categories. Per-screen calls use async D-Bus to avoid blocking the KCM UI during slider interactions.
- **Zone previews in autotile dropdowns**: Autotile algorithm dropdowns now show layout thumbnails with zone previews, matching the snapping layout dropdown UX.

### Changed
- **Daemon is sole KConfig writer**: All KCM settings writes now route through D-Bus to the daemon. No KCM sub-page calls `m_settings->save()` directly. Eliminates dual-writer race conditions between the KCM and daemon.
- **Batch signal suppression**: `setSettings()` wraps setter calls in `QSignalBlocker` to prevent N intermediate `settingsChanged` emissions mid-batch. The KCM's `notifyReload()` triggers a single `settingsChanged` with all values committed.

### Fixed
- **Mode toggle affects all monitors**: `Meta+Shift+T` autotile toggle now only affects the focused screen's context. Previously, `seedAutotileOrderForScreen` and the resnap-from-autotile-order loop processed all screens instead of the shortcut's target screen.
- **Clearing autotile algorithm to "Use default" switches to snapping mode**: `activeLayoutId()` now returns `"autotile:"` (recognized by `isAutotile()`) when mode is Autotile with empty algorithm, instead of returning empty string which caused `updateAutotileScreens` to drop the screen from autotile.
- **showBorder and hideTitleBars are independent toggles**: Turning off borders no longer forces title bars to reappear. Turning on hideTitleBars now immediately hides title bars on all currently tiled windows instead of waiting for the next retile.
- **KCM save ordering for per-screen overrides**: `setDaemonSettings()` now runs before `m_settings->save()` so per-screen overrides written by the KCM are not overwritten by the daemon's stale values.
- **Navigation fallback screen**: Fall back to the KWin effect's screen when the daemon's stored screen assignment is gone, preventing navigation failures after monitor reconfiguration.

## [2.3.7] - 2026-03-19

### Fixed
- **Windows unsnap when monitor enters standby**: When a monitor (e.g., a TV) entered standby or disconnected, KWin reassigned orphaned windows to remaining outputs, firing `outputChanged` signals. The effect interpreted these as cross-screen moves and told the daemon to unsnap every affected window. Now suppresses `windowScreenChanged` when the old screen has disappeared from KWin's output list or a screen geometry change is in progress.

## [2.3.6] - 2026-03-19

### Fixed
- **Navigation shortcuts snap to wrong screen on multi-monitor**: MoveWindow and SwapWindow keyboard shortcuts computed zone geometry for the wrong screen when KWin's `EffectWindow::screen()` disagreed with the daemon's stored screen assignment (common with similarly-named monitors like dual Samsung Odyssey G93SC ultrawides). The window would land on a third screen and immediately unsnap. Navigation targets now use the daemon's authoritative stored screen assignment, and the effect reads the corrected screen from the daemon's response.

### Added
- **KDE dependency migration plan**: `docs/kde-dependency-migration.md` documents how to make the daemon portable across Wayland compositors (GNOME, Hyprland, Sway) while retaining full KDE integration, behind a `USE_KDE_FRAMEWORKS` CMake option.

## [2.3.5] - 2026-03-19

### Fixed
- **KCM assignment save rewrites modes**: The KCM decomposed per-screen AssignmentEntry (mode + snappingLayout + tilingAlgorithm) into two flat maps and merged them on save, losing mode information, reverting layouts after Apply, and corrupting autotile/snapping mode when editing the other mode's field. Replaced with per-entry D-Bus writes via new `setAssignmentEntry` method that preserves all fields independently.
- **Clearing per-desktop override inherits base autotile mode**: Setting a per-desktop layout to "Use default" removed the entire entry, inheriting the base screen's mode (often autotile). Now keeps a mode-only marker entry that preserves the desktop's mode while cascading layout resolution to the parent scope.
- **Monitor-level layout changes revert after Apply**: The batch `setAllScreenAssignments` D-Bus method used `fromLayoutId(id, existing)` which preserved the old mode instead of setting it from the layout ID type. Combined with the merge logic sending the wrong mode's ID, screens appeared to revert.
- **KCM combo shows resolved layout instead of "Use default"**: After clearing a per-desktop assignment, the combo showed the inherited layout name instead of "Use default" because the D-Bus echo signal overwrote the KCM cache with the cascaded base layout. Added `setSaveBatchMode` to suppress `screenLayoutChanged` signals during the entire save batch.
- **Layout cascade stops at mode-only entries**: `layoutForScreen` returned nullptr immediately when finding a mode-only entry (empty snapping in Snapping mode) instead of cascading to the parent scope. Now continues cascading through mode-only entries to find the effective layout.

### Added
- **Resnap/retile on KCM assignment changes**: Changing layouts via KCM assignments now triggers window resnap (snapping) or retile (autotile) with per-screen OSD, matching the behavior of the layout picker overlay and keyboard shortcuts. Uses dedicated `applyAssignmentChanges` D-Bus method to avoid feedback loops with the settings handler.
- **Per-screen resnap buffer**: New `populateResnapBufferForAllScreens` method builds resnap data using a global zoneId-to-position map from all loaded layouts, independent of the single global active layout. Supports multi-monitor setups where each screen has a different layout assignment.
- **Synchronous notifyReload**: KCM-to-daemon settings reload is now synchronous with `m_ignoreNextSettingsChanged` flag, preventing race conditions where the daemon's queued `settingsChanged` signal would trigger a spurious reload that reverts just-saved assignments.

### Changed
- **Assignment edits never change mode**: Selecting a snapping layout or tiling algorithm in the KCM only updates that field. Mode is controlled exclusively by the daemon through global snapping/autotile toggle, not by individual assignment edits.
- **Full AssignmentEntry tracking in KCM**: Replaced redundant flat pending maps (`m_pendingDesktopAssignments`, `m_pendingActivityAssignments`) with full `AssignmentEntry` pending maps that track mode + snappingLayout + tilingAlgorithm per context.
- **Field-level clearing**: Clearing a snapping layout only clears the `snappingLayout` field, preserving mode and `tilingAlgorithm`. Prevents unintended mode inheritance from parent scopes.

## [2.3.3] - 2026-03-17

### Fixed
- **Autotile not activating when snapping disabled first**: When users disabled snapping (Apply) then enabled autotiling (Apply) in separate steps, per-screen autotile assignments were never created because the activation guard required both changes in the same settings event. Windows fell through to fullscreen stacking instead of tiling. Now also fires when autotile is toggled on while snapping is already off.

## [2.3.2] - 2026-03-17

### Changed
- **Batched D-Bus signals for autotile transitions**: Overflow float notifications, resnap snap confirmations, and window-opened announcements are now batched into single D-Bus calls instead of per-window round-trips. On a 15-window autotile toggle this reduces D-Bus messages from ~45 to 3, eliminating compositor-thread stalls during mode switches and daemon restarts.

## [2.3.1] - 2026-03-17

### Fixed
- **Window state lost on daemon reload**: Zone assignments were purged during `loadState()` when the saved active layout differed from the default layout. Restoring the active layout emitted `activeLayoutChanged` before `currentVirtualDesktop` was set, causing `onLayoutChanged()` to resolve effective layouts against the wrong desktop and fall back to `defaultLayout()`, whose zones didn't match the saved assignments. Fixed by suppressing the signal during state restoration.
- **Screen not found on Wayland (hex serial mismatch)**: The daemon's `screenIdentifier()` used `QScreen::serialNumber()` as-is, but on KDE Plasma Wayland this returns the EDID header serial in hex (e.g., `"0x0001C1A3"`). The KWin effect already normalized to decimal (`"115107"`), causing screen ID mismatches across the D-Bus boundary. Both sides now produce identical decimal serials.
- **Screen not found from KCM queries ([#223])**: `getScreenInfo()` only matched screens by connector name (`"eDP-1"`), but `getScreens()` returns EDID-based screen IDs (`"Sharp Corporation:LQ134N1JW53"`). Every KCM screen info query failed, causing autotile assignments to revert and persistent "screen not found" errors on multi-monitor setups. Now accepts both connector names and screen IDs.
- **Autotile window order lost on rapid mode toggle**: When `setInitialWindowOrder()` was called twice for the same screen, the first call's 10-second safety timer would fire and remove the second call's pending order. Added a generation counter so stale timers from superseded calls become no-ops.
- **KCM fallback screen IDs inconsistent**: When the daemon was unavailable, the KCM screen provider fell back to connector names instead of EDID-based screen IDs, causing per-screen config mismatches. Now uses `Utils::screenIdentifier()` in the fallback path.

## [2.3.0] - 2026-03-17

### Added
- **Stable EDID-based screen identifiers**: All screen identification now uses stable EDID-based IDs (e.g., `LG Electronics:LG Ultra HD:115107`) instead of connector names (`DP-2`). Monitors survive replug/reboot without losing layout assignments.
- **Per-screen layout filtering**: Layout cycle shortcuts, layout picker popup, and zone selector now filter layouts based on the focused screen's mode. Snapping screens only show zone layouts. Autotile screens only show tiling algorithms.
- **Per-screen layout locking**: Lock the current layout or tiling algorithm per screen/desktop/activity context. Prevents accidental changes from layout cycling, zone selector, or keyboard shortcuts. Toggle with `Meta+Ctrl+L` shortcut. OSD notification shows lock/unlock state.
- **Per-mode context-aware locking**: Locking is mode-aware. Locking on a snapping screen locks the zone layout, and locking on an autotile screen locks the tiling algorithm. Each screen/desktop combination has independent lock state.
- **Shader parameter locking**: Lock individual shader parameters in the editor to preserve their values during randomize. Locked parameters show a lock icon and are excluded from random generation.
- **Quick Layout slot labels**: KCM now shows "Quick Layout 1" / "Quick Tiling 1" instead of raw shortcut keys (`Meta+Alt+1`). Shortcut key shown as secondary text.
- **Hot reload for shaders**: System and user shaders reload automatically on file changes.

### Fixed
- **Snap assist sending windows to wrong monitor**: On dual-monitor setups sharing the same layout, zones occupied on one screen appeared occupied on the other. Empty zone detection is now per-screen.
- **Cross-screen drag not clearing snap/float state**: Dragging a snapped window to a different monitor now clears the zone assignment and pre-tile geometry. Float toggle no longer restores to the original monitor.
- **Window restore unsnapping on cross-screen restore**: Windows persisted on a secondary monitor would immediately unsnap after restore. The daemon now decides whether to unsnap based on its own assignment state.
- **Snap assist flashing during rapid layout cycling**: Snap assist continuation now runs after stagger animations complete, with a generation counter to invalidate stale callbacks from previous layouts.
- **Zone selector showing on autotile-managed screens**: The zone selector popup now correctly skips screens in autotile mode.

### Changed
- **Screen ID migration**: 34+ files refactored to use EDID-based screen IDs consistently across the entire effect↔daemon D-Bus boundary, autotile engine, snap engine, and window tracking service.
- **Shader categories from metadata**: Removed hardcoded category translations. Category names come directly from shader `metadata.json`.
- **German translations**: All three domains 100% complete (KCM: 529, Editor: 449, Daemon: 61).

## [2.2.1] - 2026-03-14

### Fixed
- **Primary screen detection on Wayland**: `QGuiApplication::primaryScreen()` could diverge from KDE Display Settings on some multi-monitor Wayland configurations. The KWin effect now queries `Workspace::outputOrder()` (the compositor's authoritative output priority) and pushes the true primary to the daemon via D-Bus. Updates automatically when display settings change.

### Changed
- **Complete German translations**: All three translation domains are now 100% translated (KCM: 520, Editor: 446, Daemon: 61 strings).
- **Translation extraction fix**: Sub-KCM QML files were missing from pot extraction (glob only scanned `kcm/ui/`, not `kcm/*/ui/`).

## [2.2.0] - 2026-03-14

### Added
- **Independent border and title bar toggles** (fixes [#210](https://github.com/fuddlesworth/PlasmaZones/discussions/210)): New "Show focus border" setting draws a colored border around the focused tiled window without requiring title bars to be hidden. Border width, corner radius, and color are configurable independently.
- **Border corner radius setting**: New corner radius option (0–20px) for the autotile focus border. For borderless windows, the window content is clipped to match the rounded border.
- **Right-click context menu for layouts**: Edit, Set as Default, Show/Hide from Zone Selector, Enable/Disable Auto-assign, Duplicate, Export, and Delete actions are now accessible via right-click on layout cards. The toolbar has been simplified to New Layout, Import, Open Folder, and view switching.
- **Monitor selector for layout editor**: Layouts KCM now shows a screen selector (multi-monitor setups) so the editor opens on the correct monitor instead of always using the first screen.

### Fixed
- **Double toggle-float on multi-monitor setups**: Navigation signal connections were registered twice (constructor + daemon ready), causing float toggle and other shortcuts to fire their handlers twice and immediately cancel themselves.
- **Snap-mode zone changes leaking into autotile engine**: The autotile engine's `windowZoneChanged` listener incorrectly called `onWindowAdded()` for snap-mode windows, triggering "not in m_windowToStateKey" warnings and potentially inserting windows into the wrong screen's tiling state.
- **Window tiling state preserved when moved to another desktop**: Moving a tiled window to a different virtual desktop now properly removes it from the source desktop's tiling and retiles to fill the gap. Title bars are restored for borderless windows.
- **Editor opens on wrong monitor** (fixes [#216](https://github.com/fuddlesworth/PlasmaZones/discussions/216)): The Layouts KCM always sent the editor to the first screen in the list. Now uses user selection, falling back to the primary monitor.
- **"Open Layouts Folder" error on fresh install** (fixes [#216](https://github.com/fuddlesworth/PlasmaZones/discussions/216)): The layouts directory is now created if it doesn't exist before opening.
- **Layout grid not refreshing after duplicate/delete/import**: The daemon never emitted layout change D-Bus signals, so the KCM grid relied on manual reloads. Added `scheduleLoad()` after all mutating operations.
- **QML compiler warning in layout editor**: Fixed `threshold` variable used before declaration in `DividerManager.qml`.
- **Update check button spacing**: Increased padding between the "Check for Updates" button and the result message.

### Changed
- **KCM layout reorganization**: Autotiling settings split into Appearance (Colors, Decorations, Focus Border) and Behavior cards. Snapping "Snapping Behavior" card renamed to "Behavior" for consistency.
- **Enable toggles restyled**: Snapping and Tiling enable toggles now use the same bold-label + switch pattern as the main "Enable PlasmaZones" toggle.
- **Layout toolbar simplified**: Per-layout actions moved to right-click context menu. Toolbar retains only global actions (New Layout, Import, Open Folder, view switcher).

## [2.1.0] - 2026-03-13

### Breaking Changes
- **Assignment storage migrated from JSON to KConfig**: Per-screen layout assignments (snapping layout and tiling algorithm) are now stored in `plasmazonesrc` KConfig groups (`[Assignment:*]`) instead of `assignments.json`. Existing `assignments.json` files are automatically migrated on first startup and renamed to `assignments.json.migrated`.

### Added
- **Per-desktop/activity tiling state isolation** (fixes [#212](https://github.com/fuddlesworth/PlasmaZones/discussions/212)): Each virtual desktop and activity now maintains independent tiling state (window membership, master count, split ratios, floating state). Switching desktops no longer interferes with tiling on other desktops.
- **Zone shaders**: Added Fedora Drift, NixOS Drift, openSUSE Drift, and KDE Neon gear zone shaders

### Fixed
- **Editor: new zones could not reach canvas edges during drag** (fixes [#215](https://github.com/fuddlesworth/PlasmaZones/discussions/215)): Grid-aligned snap clamping prevented zones from reaching canvas boundaries, and gap calculation used stale data during drag operations
- **Login freeze from startup OSD**: Prevented OSD from blocking the D-Bus event loop during daemon startup

### Changed
- **KCM refactored into sub-KCMs**: Split into Layouts, Snapping, Tiling, Shortcuts, Apps & Windows, and About sub-modules with a shared common library

## [2.0.2] - 2026-03-11

### Fixed
- **Missing `retileAllScreens` D-Bus slot**: KWin effect called `retileAllScreens` but the adaptor only exposed `retile(screenName)`, causing D-Bus errors on border width changes and other bulk retile triggers
- **Negative zone geometries from constraint solver**: When window minimum sizes exceed available space, the constraint solver could produce non-positive zone dimensions, now clamped to minimum 1x1 after layout calculation

### Changed
- **Generic tarball built on Arch Linux**: Switched the release pipeline's generic tarball build from Fedora 43 to Arch Linux, eliminating lib64/lib path mismatches at the source instead of working around them post-build

## [2.0.1] - 2026-03-11

### Fixed
- **Arch lib64 file conflict with generic tarball** (fixes [#203](https://github.com/fuddlesworth/PlasmaZones/discussions/203)): Force `CMAKE_INSTALL_LIBDIR=lib` in the generic tarball build so it doesn't inherit Fedora's `lib64` default, which conflicts with Arch's `filesystem` package owning `/usr/lib64` as a symlink
- **Global show-zone-numbers toggle ignored when layout active** (fixes [#208](https://github.com/fuddlesworth/PlasmaZones/discussions/208)): Changed zone number visibility so the global toggle is a master switch. Per-layout setting can only further restrict, not override it
- **Easing preset dropdown UX** (fixes [#207](https://github.com/fuddlesworth/PlasmaZones/discussions/207)): Replaced single 30+ item flat dropdown with two-dropdown Style + Direction selector, and clamped elastic/bounce preview animation to prevent overshoot overflow
- **Modifier key capture silently broken by qmlformat** (fixes [#205](https://github.com/fuddlesworth/PlasmaZones/discussions/205)): Replaced large Qt::KeyboardModifier integer literals with bit-shift expressions (`1 << 25` etc.) that qmlformat cannot mangle into lossy scientific notation
- **Fedora COPR repo name case sensitivity**: Fixed COPR repo reference to use correct casing

### Added
- **Fedora COPR and openSUSE OBS install instructions** in README (fixes [#209](https://github.com/fuddlesworth/PlasmaZones/discussions/209))

## [2.0.0] - 2026-03-10

### Added

#### Autotiling Engine
- **Pluggable algorithm architecture** with 10 tiling layouts: Master+Stack, Centered Master, BSP, Dwindle, Spiral, Columns, Rows, Grid, Wide, Three-Column, and Monocle
- **Per-screen algorithm selection** with independent settings per monitor
- **Separate centered-master settings**: Split ratio and master count are independent from master+stack (defaults: 0.5 vs 0.6)
- **Per-screen maxWindows cap** to limit tiled window count per monitor
- **Per-side outer gaps** (top/bottom/left/right) for independent screen edge spacing
- **Overflow window management**: Auto-float excess windows when maxWindows is reached, auto-recover when room opens
- **Hide title bars** on tiled windows with configurable active-window border rendering (color, width)
- **Deterministic zone-ordered window transitions** when switching between snapping and autotiling modes
- **Float/unfloat toggle** with geometry preservation and cross-screen fallback
- **Smart gaps**, insert position config, focus-follows-mouse, focus-new-windows
- **Minimum window size** respect with algorithm-level constraint solving
- **D-Bus interface** (`org.plasmazones.Autotile`) for runtime control
- **Read-only preview mode** for autotile layouts in the editor
- **Pre-autotile geometry persistence** across session restarts for accurate float restore

#### Animation System
- **Translate-only slide animations** that avoid Wayland buffer desync (no scale transforms)
- **Staggered cascading window animations** with configurable overlap
- **Cubic bezier easing curve editor** in KCM with live preview
- **Elastic and bounce easing curve types** with customizable parameters (amplitude, period, overshoot)

#### KCM Improvements
- **Dual-view layout picker** with separate Snapping/Tiling modes and default autotile algorithm selection
- **Dual-mode per-screen assignments** (snapping layouts + autotile algorithms per monitor)
- **Snapping enable/disable toggle**
- **Auto-select default layout** in Layout tab
- **Live algorithm preview widget**
- **Per-monitor snapping gap/padding overrides**

### Changed
- **Renamed Zones tab to Snapping** in KCM for clarity alongside the new Tiling tab
- **BSP algorithm made deterministic**: Removed persistent tree state that caused non-reproducible layouts
- **Major codebase refactoring**: Split 20+ oversized files (>500 lines) into DRY translation units organized in subdirectories
  - Extracted `OverflowManager`, `PerScreenConfigResolver`, `NavigationController`, `SettingsBridge` from `AutotileEngine`
  - Extracted `AssignmentManager`, `DaemonController`, `LayoutManager` from monolithic KCM
  - Extracted `AutotileHandler`, `ScreenChangeHandler`, `SnapAssistHandler` from KWin effect
  - Split `Settings`, `OverlayService`, `WindowTrackingService`, D-Bus adaptors, and editor into subdirectories
- **Comprehensive unit tests** for all algorithms, engine, tiling state, overflow manager, geometry utils, and algorithm registry (11 test suites)

## [1.15.15] - 2026-03-09

### Fixed
- **Shortcuts not working until window is manually snapped**: The async D-Bus key grab refactor (v1.15.14) bypassed `KGlobalAccel::setShortcut()`, which connects to the component's `globalShortcutPressed` D-Bus signal. Without this connection, key grabs succeeded but press events were never received. Fixed by bootstrapping the component signal with one synchronous `setShortcut()` call (~490ms) before firing async D-Bus for the remaining ~50 key grabs.

## [1.15.14] - 2026-03-09

### Fixed
- **Nix build failure after systemd service template change**: Removed stale `postInstall` `substituteInPlace` that tried to replace `/usr/bin/plasmazonesd` in the systemd service file. Since the service now uses `configure_file(@ONLY)` with `@KDE_INSTALL_FULL_BINDIR@`, the path resolves correctly at build time and no post-install patching is needed.

## [1.15.13] - 2026-03-08

### Fixed
- **Login freeze with many shortcuts** (fixes [#200](https://github.com/fuddlesworth/PlasmaZones/discussions/200)): Replaced blocking `KGlobalAccel::setGlobalShortcut()` with a two-step approach where `setDefaultShortcut()` registers all shortcuts without key grabs, then async D-Bus calls activate key grabs in parallel without blocking the event loop. Eliminates 20-40s hangs during login when kglobalacceld is under contention.

## [1.15.12] - 2026-03-08

### Fixed
- **Global shortcuts broken after v1.15.9/v1.15.10** (fixes [#200](https://github.com/fuddlesworth/PlasmaZones/discussions/200)): Reverted async D-Bus shortcut registration (v1.15.10) and deferred batching (v1.15.9) which left the KGlobalAccel component inactive, preventing all shortcut dispatch. Restored direct `KGlobalAccel::setGlobalShortcut()` calls which properly register actions and set up key grabs through the official API.

## [1.15.11] - 2026-03-08

### Fixed
- **Release workflow retry loop**: Replaced `softprops/action-gh-release` with native `gh` CLI to fix releases getting stuck in a retry loop ([action-gh-release#704](https://github.com/softprops/action-gh-release/issues/704)).

## [1.15.10] - 2026-03-08

### Fixed
- **Login freeze persisted despite v1.15.9 batching** (fixes [#200](https://github.com/fuddlesworth/PlasmaZones/discussions/200)): The v1.15.9 deferred batch approach still blocked because each batch made synchronous D-Bus round-trips whose replies stalled for ~25s while kglobalaccel processed key grabs (QTBUG-34698). Replaced with true async D-Bus. `setDefaultShortcut()` registers actions synchronously (fast, no key grabbing), then `setShortcutKeys` calls fire via `QDBusPendingCallWatcher` so the event loop never blocks on key grabbing.

## [1.15.9] - 2026-03-08

### Fixed
- **Login freeze with autostart apps** (fixes [#200](https://github.com/fuddlesworth/PlasmaZones/discussions/200)): Shortcut registration made 86+ synchronous D-Bus calls to KGlobalAccel at startup, blocking the event loop for 20-40 seconds when competing with other KDE services during login. Registration is now batched and deferred, yielding the event loop between batches.
- **systemd service ordering**: Added `After=plasma-kglobalaccel.service` to ensure the shortcut daemon is ready before PlasmaZones registers shortcuts.

## [1.15.8] - 2026-03-08

### Fixed
- **RPM: remove exact KWin version pin** (fixes [#199](https://github.com/fuddlesworth/PlasmaZones/discussions/199)): RPM package required `kwin = <build-version>` which blocked installation when KWin received patch updates (e.g. 6.6.1 -> 6.6.2). Changed to `kwin >= 6.6.0`. Soname-level deps handle ABI safety automatically.

## [1.15.7] - 2026-03-06

### Fixed
- **KWin 6.6.2 compatibility**: Rebuild for KWin 6.6.2 minor release. Effect plugin is version-locked and requires exact KWin version match to load.

## [1.15.6] - 2026-02-28

### Fixed
- **Debian package build**: Re-enabled .deb creation using KDE Neon container (`kdeneon/all:dev-stable`) which ships Plasma/KF6 6.6+, replacing the disabled Ubuntu 25.10 build.

## [1.15.5] - 2026-02-27

### Fixed
- **Multi-zone snap cascade in tiling layouts**: Edge-adjacent detection no longer flood-fill expands through shared edges, which caused all zones to highlight in tiling layouts. Seed zones are now used directly for multi-zone snap. Bounding-rect expansion is retained only for paint-to-span mode where rectangular gap-filling is needed.

## [1.15.4] - 2026-02-26

### Fixed
- **Overlapping zone multi-zone cascade**: Placing cursor on a zone fully inside a larger zone no longer highlights all zones. Fixed detectMultiZone to separate overlapping zones (cursor inside) from edge-adjacent zones (cursor near edge). Only edge-adjacent zones trigger multi-zone snap. Replaced bounding-rect expansion with edge-adjacency flood-fill that skips zones spatially overlapping the seed. Removed duplicated smallest-area loop in paint-to-span.
- **Edge tolerance now respects settings**: Zone-to-zone edge detection uses the user's adjacentThreshold setting instead of a hardcoded 5px value, so manually-gapped layouts work correctly with the configured proximity.

## [1.15.3] - 2026-02-26

### Fixed
- **KWin effect plugin version lock**: Effect plugin embeds EffectPluginFactory version in its IID. It only loads when runtime KWin matches. Added build-time version visibility in CMake and RPM spec now requires exact KWin version match, preventing 6.6.0-built plugins from installing on 6.6.1 systems where they fail to load.

## [1.15.2] - 2026-02-22

### Fixed
- **Overlapping zone snapping**: When zones overlap, the smallest zone at the cursor position is now selected instead of the first in list order. Matches FancyZones' area-covered heuristic so the more specific zone wins.

## [1.15.1] - 2026-02-22

### Fixed
- **RPM packaging**: Added KCM and editor translation files (`kcm_plasmazones.mo`, `plasmazones-editor.mo`) to spec `%files` section, fixing "unpackaged file(s) found" build failure on Fedora.

## [1.15.0] - 2026-02-22

### Added
- **Mosaic Pulse shader**: Audio-reactive stained glass mosaic with colorful tiles, pulsing shapes (circles, diamonds, squares), sparkles, and dithered posterization. Bass drives shape pulse, mids shift hue, treble triggers sparkles. 12 configurable parameters across 6 groups.
- **User-supplied image textures**: Shader effects can now sample up to 4 user-provided images (bindings 7-10) with configurable wrap modes.
- **Shared GLSL utilities**: Extracted `common.glsl`, `audio.glsl`, `textures.glsl`, and `multipass.glsl` as shared includes. All shaders updated to use common helpers (hash, noise, SDF, blending, audio bands).

### Fixed
- **System layout restore**: Deleting a user layout override from KCM now correctly restores the system-provided layout instead of leaving a blank state.
- **System layout label**: Label now includes "zones" suffix for consistency with other layout names.
- **Translation extraction**: 33 missing source files added to the extraction list so all translatable strings are captured.
- **German .po headers**: Normalized header fields for consistency across all 3 translation domains.

### Changed
- **German translations**: Complete coverage for all 3 domains (daemon, KCM, effect). Removed 49 obsolete entries.
- Removed outdated shader presets.

## [1.14.1] - 2026-02-21

### Fixed
- **Zone persistence on daemon restart**: Windows that were snapped to zones are now correctly re-registered when the daemon is stopped and started. Root cause: `pendingRestoresAvailable` was never emitted because the layout was set before the WindowTrackingAdaptor connected to `activeLayoutChanged`. Now sets `m_hasPendingRestores` at init when pending assignments are loaded. Also saves window tracking state on daemon shutdown so snapped windows persist across restarts.

## [1.14.0] - 2026-02-21

### Added
- **Per-side edge gaps**: Independent top/bottom/left/right outer gap values instead of a single uniform gap, useful for transparent panels or asymmetric screen setups. Global toggle in KCM with per-layout overrides in the editor. Full undo/redo support. ([#187], [#188])
- **Per-zone fixed pixel geometry**: Zones can now use absolute pixel coordinates instead of relative 0.0-1.0 values, enabling precise pixel-perfect layouts that don't scale with resolution. Per-zone toggle between Relative and Fixed modes in the editor. ([#180], [#182])
- **Full screen geometry toggle**: Per-layout option to use the full screen area (ignoring panels/taskbars) for zone calculations, allowing zones to extend behind auto-hide or transparent panels. ([#179], [#181])
- **AlwaysActive zone activation**: New activation mode that shows zones on every window drag without requiring a modifier key or mouse button. Configurable in KCM Zones tab. ([#185], [#186])

### Changed
- **Copy-on-write layout saving**: Layouts are only written to disk when actually modified, with per-layout dirty tracking to avoid unnecessary I/O during bulk operations.

### Fixed
- Hardcoded 1920x1080 fallback removed from D-Bus zone detection. Uses actual screen geometry.
- Fixed preview rendering for fixed-geometry zones in KCM new layout dialog.
- All layouts recalculated on startup and screen changes to prevent stale geometry.
- Per-side edge gap: -1 sentinel no longer leaks into geometry calculations when settings are unavailable.
- Per-side edge gap: `usePerSideOuterGap` toggle now persists across save/load even with all-default side values.
- Per-side edge gap: clearing override in editor is now undoable.

## [1.13.0] - 2026-02-20

### Added
- **Layout Picker Overlay**: Full-screen interactive layout browser triggered via configurable keyboard shortcut. Browse all available layouts in a centered card grid with keyboard navigation (arrow keys + Enter) and mouse support. Selecting a layout switches to it and resnaps all windows. ([#176])
- **Shared LayoutCard component**: Extracted reusable `LayoutCard.qml` and `PopupFrame.qml` into `org.plasmazones.common` QML module, shared between the Zone Selector and Layout Picker overlays.
- **Snap Assist after resnap**: Snap Assist now triggers after resnapping windows when switching layouts via the layout picker, offering to fill any empty zones.

### Fixed
- **Zone activation broken when Zone Selector disabled** ([#175]): Disabling the Zone Selector popup caused the zone activation hotkey (e.g. Alt+drag) to stop working entirely. The root cause was that D-Bus deserialization of trigger settings could silently fail (Qt delivering `QDBusArgument` instead of native `QVariantList<QVariantMap>`), but this was masked when `zoneSelectorEnabled=true` because a bypass gate let all drag events through regardless. Added robust `QDBusArgument` unwrapping, a permissive `m_triggersLoaded` flag that allows drags through until triggers are confirmed loaded, and diagnostic logging for trigger load failures.
- **Layout Picker double-trigger**: Rapidly pressing the layout picker shortcut could create multiple overlay windows with competing `KeyboardInteractivityExclusive` keyboard grabs on Wayland, causing shortcuts to stop working. Replaced toggle guard with a simple existence guard that prevents re-triggering while any picker window exists.

## [1.12.2] - 2026-02-19

### Fixed
- **Audio visualizer**: CAVA process silently failed when bar count was odd (exit code 1, "must have even number of bars with stereo output"). Even bar counts are now enforced at all layers: CavaService, KCM setter, and UI slider (stepSize=2).
- **Audio shader data**: QML `|| []` fallback on `audioSpectrum` binding forced V4 JavaScript conversion, losing the native `QVector<float>` type needed by `ZoneShaderItem`'s fast path. Replaced with a `Binding` element guarded by `when` to preserve type identity through the binding chain.
- **CAVA stderr capture**: Switched from `ForwardedErrorChannel` to `SeparateChannels` so CAVA error output is captured in daemon logs instead of lost.
- **CAVA exit diagnostics**: Moved `exitCode()`/`readAllStandardError()` from `stateChanged` to `finished` signal handler per Qt API contract. Only warns on non-zero exit code (stderr on exit 0 is normal for CAVA).

### Changed
- **Shared audio constants**: `Audio::MinBars`/`Audio::MaxBars` moved to `src/core/constants.h` with `static_assert` for even values, eliminating magic number duplication across CavaService, KCM, and QML.
- **Nix**: Re-enabled Nix CI, release builds, flake.lock updater, and `plasmazones.nix` release asset now that nixpkgs-unstable has the Plasma 6.6 stack (NixOS/nixpkgs#479797).

## [1.12.1] - 2026-02-18

### Fixed
- **KCM Editor tab**: Use unique QML type `PlasmaZonesKeySequenceInput` so the Editor tab always loads the bundled shortcut component (with `defaultKeySequence`). Fixes "Type EditorTab unavailable" / "Cannot assign to non-existent property 'defaultKeySequence'" when the KCM runs from system install (e.g. NixOS) where another `KeySequenceInput` could be resolved first.
- **KWin effect**: Remove explicit `Id` from plugin metadata so the loader uses the filename-derived id and the kf.coreaddons warning is resolved.

## [1.12.0] - 2026-02-18

### Added
- **Reapply window geometries after geometry updates**: When zones or panel geometry change (e.g. after closing the KDE panel editor), the daemon requests the KWin effect to reapply snapped window positions so windows stay correctly placed in zones.
- **D-Bus**: `reapplyWindowGeometriesRequested` signal and `getUpdatedWindowGeometries` method on WindowTracking for the effect to fetch and apply geometries.
- **ScreenManager**: `delayedPanelRequeryCompleted` signal when the delayed panel requery finishes (used for documentation, and the reapply path is unified).

### Fixed
- **Panel editor / geometry**: Zones and snapped windows no longer shift incorrectly after editing the KDE panel and closing the panel editor. Geometry debounce (400ms), delayed panel requery (400ms), and immediate reapply (0ms) after each geometry batch keep overlay and window positions correct.
- **Multiple windows in same zone**: Reapply now updates every snapped window. Previously only one window per zone (same app) was updated due to stableId-only lookup. Effect now maps by full window ID with stableId fallback.
- **Effect reapply safety**: Reapply-in-progress guard prevents overlapping async reapply runs. QPointer in async callback avoids use-after-free if the effect is unloaded during reapply.
- **Effect JSON**: Robust validation and skip of invalid geometry entries. QLatin1String for JSON keys (Qt6). Single-pass window map.

### Changed
- **Reapply timing**: Reapply runs after every geometry batch (0ms delay). Removed redundant 1100ms/450ms reapply path. Delayed panel requery still triggers the same debounce → processPendingGeometryUpdates → reapply flow.
- **Daemon**: Reapply timer stopped in `stop()`. Named constants for geometry and panel delays.
- **Nix**: Build asserts layer-shell QPA plugin compatibility and fails with a clear message when nixpkgs provides the 6.5 stack. Nix CI and release Nix build/artifact disabled until nixpkgs has Plasma 6.6.

## [1.11.8] - 2026-02-16

### Performance
- **Signal-driven drag detection**: Replaced the QTimer-based poll loop (32ms stacking-order scans during drag) with KWin's per-window `windowStartUserMovedResized` / `windowFinishUserMovedResized` signals for zero-cost, event-driven drag start/end detection. Eliminates the `m_pollTimer` entirely, with no more periodic stacking-order iteration on the compositor thread, even as a safety net ([#167])

## [1.11.7] - 2026-02-16

### Performance
- **Event-driven cursor tracking**: Cursor position updates during drag are now driven by `slotMouseChanged` instead of the poll timer, eliminating QTimer jitter from the compositor frame path and providing more accurate cursor tracking at input-device cadence
- **Throttled dragMoved signals**: `DragTracker::updateCursorPosition()` throttles `dragMoved` emissions to ~30Hz via `QElapsedTimer`, preventing D-Bus flooding from high-frequency (1000Hz) mouse input
- **Eliminated QDBusInterface for WindowDrag**: Replaced `QDBusInterface` with `QDBusMessage::createMethodCall` for all WindowDrag D-Bus calls (`dragStarted`, `dragMoved`, `dragStopped`, `cancelSnap`), avoiding synchronous D-Bus introspection that could block the compositor thread with a ~25s timeout if the daemon is registered but slow to respond
- **Pre-parsed activation triggers**: Activation triggers are now parsed from `QVariantList` to POD structs (`ParsedTrigger`) once at load time, removing per-call `QVariant` unboxing overhead from `anyLocalTriggerHeld()` (~30 calls/sec during drag)

## [1.11.6] - 2026-02-16

### Performance
- **Dynamic poll timer**: Poll rate switches from 500ms (idle) to 32ms (~30Hz) only when LMB is pressed, eliminating continuous 60Hz stacking-order scans on the compositor thread when no drag is active ([#167])
- **Early-exit idle polls**: `DragTracker::pollWindowMoves()` skips the full stacking-order iteration when no drag is active and no button is held
- **Reduced D-Bus traffic during drag**: Active-drag poll rate lowered from 16ms (60Hz) to 32ms (30Hz) because zone detection doesn't need sub-33ms updates, and halving D-Bus message serialization on the compositor thread reduces frame-time jitter on high-refresh-rate displays ([#167])
- **Guard redundant daemon work**: `hideOverlayAndClearZoneState()` now short-circuits when overlay is already hidden and zone state is clear, preventing 30Hz `clearHighlights()`/`clearHighlight()` calls that could congest the daemon event loop and create D-Bus back-pressure ([#167])

## [1.11.5] - 2026-02-16

### Fixed
- **KCM tab buttons**: Prevent tab buttons from resizing when switching tabs

### Performance
- **Deferred D-Bus calls during drag**: Keyboard grab and D-Bus `dragStarted`/`dragMoved` calls are now deferred until an activation trigger is actually detected, eliminating 60Hz D-Bus traffic and keyboard grab/ungrab overhead for non-zone window drags ([#167])
- **Deduplicated trigger read**: `WindowDragAdaptor::dragMoved()` now reads activation triggers once per call instead of twice

## [1.11.4] - 2026-02-16

### Fixed
- **Layout grid cards**: Badge and zone count text were left-aligned instead of centered under each card

## [1.11.3] - 2026-02-16

### Added
- **Master toggle for zone span**: New checkbox to fully enable/disable paint-to-span zone selection. Defaults to on. When off, modifier and threshold controls are greyed out.
- **Master toggle for snap assist**: New checkbox to fully enable/disable the snap assist (window picker) feature. Defaults to on. When off, all snap assist sub-options are greyed out.

### Fixed
- **Nix flake evaluation error**: `lib.mkPackageOption` received a derivation instead of an attribute path, causing evaluation failures when `package` was not explicitly specified. Users can now use `programs.plasmazones.enable = true` without setting `package`.

## [1.11.2] - 2026-02-15

### Added
- **Snap Assist trigger override** ([#166]): When "Always show" is off, hold a configurable modifier or mouse button when releasing a window to enable Snap Assist for that snap only. Uses the same multi-trigger widget as zone activation and zone span.

### Changed
- **Snap Assist UI**: "Always show" checkbox. When off, configure hold-to-enable trigger via ModifierAndMouseCheckBoxes (shown disabled when always-on).
- **D-Bus breaking**: `org.plasmazones.WindowDrag.dragStopped` now requires `modifiers` and `mouseButtons` at release (for Snap Assist triggers). KWin effect and daemon must be from the same PlasmaZones version.

### Fixed
- **KCM UX consistency**: Section titles added for all groups in Zones tab cards: Appearance (Colors, Border), Effects (Visual Effects), Activation (Triggers), so every section has a consistent heading

## [1.11.1] - 2026-02-15

### Fixed
- **Drag stutter**: Removed redundant `windowFrameGeometryChanged` handler that flooded D-Bus with 60-144+ calls/sec during window drag, causing visible stutter and leaving the daemon in a stale state ([#167])
- **Login hang**: Replaced synchronous `QDBusInterface` introspection in `loadExclusionSettings()` with async `QDBusMessage` call, preventing the compositor from blocking for up to 25 seconds during login when the daemon is registered but not yet responding ([#167])

## [1.11.0] - 2026-02-15

### Added
- **EDID-based monitor identification**: Monitors are now identified by manufacturer, model, and serial number instead of connector name (e.g. "DP-2"). Layouts stay assigned to the correct physical monitor regardless of which port it's connected to ([#164])
- **Nix flake**: Proper `flake.nix` with NixOS module (`programs.plasmazones.enable`), Home Manager module, overlay, and dev shell
- **Multi-arch Nix CI**: Flake-based CI builds on both x86_64-linux and aarch64-linux with magic-nix-cache
- Weekly `flake.lock` auto-update workflow

### Fixed
- **NixOS KCM loading failure**: KCM QML components failed to load in System Settings on NixOS due to missing `qmldir` files and unresolvable QML runtime dependencies (kirigami, qqc2-desktop-style) ([Discussion #160])
- Zone selector popup layouts are now sorted alphabetically by name
- D-Bus `toggleActivation` method was not registered in the settings adaptor

### Changed
- Nix CI uses `nix build`/`nix flake check` instead of legacy `nix-build --expr`
- Nix install action upgraded from v30 to v31
- Release notes now include flake-based NixOS installation instructions alongside standalone `plasmazones.nix`

## [1.10.6] - 2026-02-14

### Added
- **Toggle activation mode**: Zone activation modifier can be toggled on/off with a single press instead of requiring hold. Useful for trackpad and accessibility users ([#159])

### Changed
- **Proximity snap always active**: Removed the multi-zone modifier setting entirely. Adjacent zone detection now always works during drag with no modifier required
- **Zone span default changed to Ctrl**: Paint-to-span modifier defaults to Ctrl instead of Meta, avoiding conflict with KDE's Meta shortcut in toggle activation mode
- CI: Replaced DeterminateSystems/FlakeHub Nix actions with cachix/install-nix-action

### Fixed
- Nix build: ECM's KDEInstallDirs resolved an absolute systemd unit path outside the Nix store, causing postInstall to fail ([#159])

## [1.10.5] - 2026-02-13

### Fixed
- Snap Assist D-Bus calls made fully async to prevent compositor freeze when daemon is busy with overlay teardown ([#158])
- Zone selector snapping now uses the same geometry pipeline as overlay snapping, so gap handling is consistent

## [1.10.4] - 2026-02-13

### Fixed
- Session restore places windows on wrong display in multi-monitor setups. Active screen and desktop assignments were lost on daemon restart ([#156])
- Escape during drag now dismisses overlay without cancelling the drag. Re-pressing the activation trigger re-shows the overlay
- Keyboard grab released in effect destructor to prevent input loss if effect unloads mid-drag

## [1.10.3] - 2026-02-13

### Fixed
- CAVA audio visualizer not starting without daemon restart after enabling in KCM ([#150])
- Shader effects toggle, frame rate, and spectrum bar count changes also required restart, from the same root cause
- No default layout selected on fresh install. Columns (2) now gets the star badge out of the box
- `defaults()` uses `defaultOrder` from layout metadata instead of hardcoded name match

## [1.10.2] - 2026-02-13

### Fixed
- Release workflow: delete pre-existing GitHub release before recreating with build assets (fixes HTTP 422 on asset upload)
- RPM spec and Debian changelog no longer manually maintained. CI generates both from CHANGELOG.md via `generate-changelog.sh`
- RPM spec Version field uses `0.0.0` placeholder (CI replaces from git tag)
- Avoid literal `%changelog` in spec header comments (broke `sed` in changelog generator)

## [1.10.0] - 2026-02-13

### Added
- **Multiple binds per action**: Configure up to 4 independent triggers for zone activation, proximity snap, and paint-to-span, e.g. Alt key + Right mouse button as separate triggers for different input devices ([#150])
- Click-to-edit existing triggers in the KCM. Click a trigger label to replace it in-place
- AND semantics for combined modifier+button triggers (both must be held)
- Conflict detection warns when the same trigger is used across multiple actions

### Fixed
- Multi-zone threshold setting not applied correctly ([#147])
- Modifier shortcuts now exclude the activation key to prevent conflicts
- Legacy config keys cleaned up on save (stale DragActivationModifier, mouse button keys removed)
- Empty trigger list prevented. At least one trigger is always required per action

### Changed
- Settings stored as JSON trigger lists (automatic migration from single-value format)
- KWin effect simplified. The daemon handles all trigger matching via `anyTriggerHeld()`
- D-Bus API: new `dragActivationTriggers`, `multiZoneTriggers`, `zoneSpanTriggers` list properties replace individual modifier/mouse button getters

## [1.9.5] - 2026-02-13

### Added
- Zones can overlap Plasma panels set to autohide/dodge windows ([#148])
- Force-end drag on mouse button release for safer drag lifecycle
- Proximity snap always active by default (no modifier required)

### Fixed
- **Compositor freeze**: Remove `processEvents()` calls that deadlock with Wayland compositor during drag ([#152])
- **Compositor stall on layout change**: Hide overlay/zone selector before layout switch in zone selector drop path, skip heavy QML updates for hidden windows
- **Snap assist Escape not working**: Keep KGlobalAccel Escape shortcut registered through snap assist phase. Add `snapAssistDismissed` signal for proper cleanup
- **Snap assist not dismissing**: Dismiss snap assist on any window zone change (navigation, snap, unsnap, float toggle)
- **Snap assist wrong window**: Use full windowId (not stableId) for per-instance floating/geometry tracking
- Snap assist Escape handling, dismiss on new drag, zone selector layout sync
- Paint-to-snap raycasting and shader highlight for multi-zone selection
- Mouse-button zone activation now latches until drag ends (no flicker)
- Shortcut clear button resets to default instead of empty
- Inverted panel-hiding check for usable geometry
- KCM linker errors from missing kcfg sources

### Changed
- Remove 66 dead code items across 48 files
- Remove dead multiZoneEnabled code

## [1.9.3] - 2026-02-12

### Fixed
- Proximity snap "always on" no longer bypasses overlay activation. It now only enables proximity snap when the overlay is already open via the activation key

## [1.9.2] - 2026-02-12

### Added
- KCM: "Proximity snap always on" checkbox enables always-on proximity snap without holding the modifier (per [#143])
- Escape key cancels overlay during window drag. Overlay stays hidden until the next drag of a stationary window

## [1.9.1] - 2026-02-12

### Added
- Snap Assist: Aero Snap style window picker after snapping, allowing users to fill empty zones with unsnapped windows ([#95])
- Snap Assist overlay with window thumbnails, zone-mapped layout, and KCM setting to enable/disable
- `getEmptyZonesJson` and `showSnapAssist` D-Bus APIs for Snap Assist integration

### Fixed
- Zone padding and outer gap in individual layout settings now persist correctly when saving ([#145])
- Default layouts no longer include redundant zone padding override (use global setting by default)
- Snap Assist: overlay zone appearance matches zone colors and borders. Thumbnail caching across continuation
- Snap Assist: KWin effect default for snapAssistEnabled until D-Bus loaded (avoids race)

### Changed
- Packaging: add env.d to RPM %files. Remove redundant Snap Assist message from Arch install

## [1.8.4] - 2026-02-11

### Added
- Shader preset load/save in editor ShaderSettingsDialog
- Preview shader effects in zone editor ([#132])
- Restore window size immediately when dragging between zones ([#133])

### Fixed
- Overlay follows cursor when dragging to another monitor ([#136])
- Defer window resize until drag release. Keep restore-to-float on unsnap
- Hide shader preview overlay when dialogs open or app loses focus
- PR review feedback for shader preview

### Removed
- Dead `zoneGeometryDuringDrag` slot

## [1.8.2] - 2026-02-09

### Added
- Full zone label font customization: family, size scale, weight, italic, underline, strikeout ([#97])
- Font picker dialog in KCM Zones tab with live preview and search
- Sonic Ripple audio-reactive shader

### Changed
- Rename `NumberColor` setting to `LabelFontColor` for consistent `LabelFont*` naming across all layers ([#97])
- Sort layouts alphabetically by name in KCM
- Use generic `adjustlevels` icon for shader settings button in editor (replaces app-specific icon)

### Fixed
- Self-referencing `font.family` QML binding preventing font reset from updating previews ([#97])
- Font reset button now also resets label size scale
- `qFuzzyCompare` edge case in KCM font scale setter (clamp before compare)
- Remove dead `labelFontColor` property from zone selector window
- Auto badge distinguished from Manual badge using `activeTextColor`

## [1.8.1] - 2026-02-09

### Added
- Paint-to-span zone modifier: hold a modifier while dragging to progressively paint across zones, window snaps to bounding rectangle on release ([#94], [#96])
- Configurable "Paint-to-span modifier" in KCM Zones tab (default Alt+Meta)
- Renamed "Multi-zone modifier" to "Proximity snap modifier" for clarity

### Changed
- Replaced `middleClickMultiZone` bool setting with `zoneSpanModifier` DragModifier enum
- Config migration: users who had middle-click multi-zone disabled keep zone span disabled after upgrade
- Extracted `prepareHandlerContext()`, `computeCombinedZoneGeometry()`, and `zoneIdsToStringList()` helpers in drag handling (DRY)
- Added `setOsdStyleInt` range validation

### Removed
- Dead `skipSnapModifier` setting (fully scaffolded but never consumed in drag handling)

### Fixed
- Missing `restoreWindowsToZonesOnLoginChanged` signal in KCM defaults and settings sync
- 12 missing signal emissions in KCM `onSettingsChanged()`
- Painted zone state not cleared on `dragStarted()` causing stale highlights
- Modifier conflict warning using `static bool` instead of per-instance member

## [1.8.0] - 2026-02-09

### Added
- CAVA audio visualization service for audio-reactive shaders ([#92])
- Spectrum Pulse shader: audio-reactive neon energy with bass glow, spectrum aurora, and CAVA integration ([#92])
- Audio-reactive shader uniforms: spectrum data and audio levels passed to GPU ([#92])
- KCM settings for audio visualizer (enable/disable, spectrum bar count)
- Auto-assign windows to first empty zone per layout ([#90])
- App-to-zone auto-snap rules per layout with screen-targeting
- Window picker dialog for exclude lists
- Per-monitor zone selector settings ([#89])
- Snap-all-windows shortcut (`Meta+Ctrl+S`)

### Changed
- Replace global active layout with `defaultLayout()` for user-facing surfaces
- DRY per-screen config validation and shared layout computation
- Audit and normalize log levels across entire codebase

### Fixed
- Mutual exclusion between overlay and zone selector during drag ([#92])
- Per-screen shader decisions for multi-monitor setups ([#92])
- Comprehensive multi-monitor per-screen targeting and isolation ([#87])
- Per-screen layout isolation and shortcut screen guards ([#87])
- Zone selector showing on all monitors instead of target screen
- Per-screen zone selector validation and edge cases
- Zone selector defensive setActiveLayout and QML signal verification
- Per-screen override message/button not updating reactively in KCM
- Daemon survives monitor power-off (DP hotplug disconnect)
- Editor: defer window destroy during mid-session screen switch
- Unfloat: fall back when saved pre-float screen no longer exists
- Remove misleading shortcut hint from zone overlay
- WrapVulkanHeaders noise in feature summary. ColorUtils.js QML warning

## [1.7.0] - 2026-02-06

### Added
- Layout visibility filtering: control which layouts appear in zone selector per screen, virtual desktop, and activity
  - Tier 1 (KCM): eye toggle to globally hide a layout from the zone selector
  - Tier 2 (Editor): visibility popup to restrict layouts to specific screens, desktops, or activities
  - Empty allow-lists = visible everywhere (opt-in model)
  - Active layout always bypasses filters to prevent empty selector state
  - Undo/redo support for visibility changes in the editor
  - Filter badge on KCM layout cards when Tier 2 restrictions are active
- Layout cycling (Meta+[/]) now respects per-screen visibility filtering

### Changed
- OSD style defaults to visual preview instead of text for new installs

### Fixed
- Duplicated and imported layouts no longer inherit visibility restrictions from the source
- Stale screen names auto-cleaned from layout restrictions when monitors are disconnected
- Layout cycling skips hidden/restricted layouts correctly in all directions

## [1.6.2] - 2026-02-06

### Fixed
- Editor not moving to selected screen when switching monitors in TopBar or via D-Bus `openEditorForScreen`
- Editor defaulting to wrong screen on Wayland (now uses cursor screen instead of unreliable `primaryScreen`)

## [1.6.1] - 2026-02-06

### Added
- Liquid Metal shader: mercury-like fluid surface with environment reflections, Fresnel, bloom, and mouse interaction

### Fixed
- AUR `-bin` package build failure due to `.INSTALL` dotfile left in package root
- Liquid Metal: surface drifting to bottom-left (use standing waves instead of travelling)
- Liquid Metal: inverted mouse Y coordinate
- Liquid Metal: outer glow rendering outside zones due to zoneParams swizzle bug

### Removed
- 5 low-quality shaders: minimalist, aurora-sweep, warped-labels, prism-labels, glitch-labels

## [1.6.0] - 2026-02-05

### Added
- Multi-zone snapping support in window tracking

### Fixed
- Shader parameters from previously-used shaders accumulating in layout JSON
- Atomic undo for shader switching (single undo step instead of two)
- Post-install messages now note that KWin restart is required to load the effect

### Removed
- Dead properties: Layout::author, Layout::shortcut, Zone::shortcut (never wired up)
- Dead files: ZoneEditor.qml, LayoutPicker.qml, ShaderOverlay.qml, shadercompiler.cpp, zonedataprovider.cpp

## [1.5.9] - 2026-02-05

### Changed
- Release pipeline now generates Debian, RPM, and GitHub release notes from CHANGELOG.md

### Fixed
- Missing pacman hook files for sycoca cache refresh in Arch package
- POSIX awk compatibility in changelog generator (mawk on Ubuntu)
- AUR publish: mount PKGBUILD read-only and generate .SRCINFO via stdout to avoid docker chown breaking host git ownership

## [1.5.2] - 2026-02-05

### Added
- Multi-pass shader rendering with up to 4 buffer passes and inter-pass texture channels (iChannel0-3) ([#78])
- Multi-channel shaders: buffer passes read outputs from previous passes ([#79])
- Zone labels rendered as shader textures for custom number styling
- Voronoi Stained Glass shader with 3D raymarching and bloom
- Mouse button zone activation as alternative to modifier-key drag ([#80])
- Zone selector auto-scroll and screen-clamped positioning
- Update notification banner with GitHub release checking
- About tab with version info and links in KCM
- Pywal color import error feedback
- Automatic AUR publishing on release

### Fixed
- Buffer pass alpha blending in RHI renderer (SrcAlpha darkened output on RGBA-cleared textures)
- Duplicate zone IDs and use-after-free in editor undo system
- Help dialog redesigned. Fullscreen exit button repositioned
- Update check button layout shift when status message appears

## [1.3.4] - 2026-02-03

### Changed
- RHI/Vulkan zone overlay renderer replaces OpenGL path ([#76])
- Packaging updated to use Fedora 43

### Added
- `#include` directive support in shaders (`common.glsl`, `multipass.glsl`)
- Shader performance improvements and error recovery

### Fixed
- Context loss and reinitialization in overlay renderer

## [1.3.3] - 2026-02-03

### Added
- Resnap-to-new-layout shortcut ([#75])
- Shortcut consolidation: merged redundant key bindings

### Changed
- Build/install only installs files. Packaging (postinst, RPM %post) handles sycoca refresh and daemon enable

### Fixed
- Logging alignment issues

## [1.3.2] - 2026-02-03

### Changed
- KWin PlasmaZones effect enabled by default on install

### Removed
- Autotiling feature removed ([#74])

## [1.3.1] - 2026-02-02

### Fixed
- Debian releases now include debug symbol packages (.ddeb)

## [1.3.0] - 2026-02-02

### Fixed
- Build paths for Arch and Debian packaging
- Session restore validates layout matches before restoring windows

### Added
- CI and release version badges in README

## [1.2.6] - 2026-02-02

### Changed
- KWin effect metadata aligned with KWin conventions
- CI pipeline simplified and packaging reorganized
- Debug symbol packages enabled for all distros

### Fixed
- Float/unfloat preserves pre-snap geometry across window close/reopen cycles ([#72])

### Added
- Autotiling settings page in KCM ([#71])

## [1.2.5] - 2026-02-01

### Improved
- Navigation OSD with multi-monitor support and UX fixes ([#70])

## [1.2.4] - 2026-02-01

### Fixed
- All remaining synchronous D-Bus calls in KWin effect converted to async, preventing compositor thread blocking
- Startup freezes from `syncFloatingWindowsFromDaemon()`
- Window event stutters from `ensurePreSnapGeometryStored()`
- Screen change delays from `applyScreenGeometryChange()`
- Navigation stutters across focus, restore, swap, cycle, and float toggle handlers

## [1.2.2] - 2026-02-01

### Fixed
- Async D-Bus call for floating toggle prevents compositor freeze

## [1.2.1] - 2026-02-01

### Added
- GitHub Actions CI/CD for Arch, Debian (Ubuntu 25.10), and Fedora builds
- Floating state persisted across sessions and restored correctly

### Fixed
- Debian packaging file paths and dependency declarations
- Packaging file paths and package names for release builds

## [1.2.0] - 2026-02-01

Initial packaged release. Wayland-only (X11 support removed). Requires KDE Plasma 6, KF6, and Qt 6.

### Features
- Drag windows to predefined zones with modifier key activation (Shift, Ctrl, etc.)
- Custom zone layouts with visual editor
- Multi-monitor support
- Custom shader overlays for zone visualization ([#1])
- Visual layout preview OSD ([#13])
- Per-layout gap overrides with separate edge gap setting ([#14])
- Snap-to-zone shortcuts: Meta+Ctrl+1-9 ([#15])
- Swap windows between zones: Meta+Ctrl+Alt+Arrow ([#16])
- Rotate windows clockwise/counterclockwise: Meta+Ctrl+\[ / Meta+Ctrl+\] ([#18])
- Per-activity layout assignments ([#19])
- Cycle windows within same zone ([#20])
- Navigation OSD feedback ([#21])
- Autotiling with Master-Stack, Columns, and BSP algorithms ([#40], [#42]-[#51])
- Unified layout model with autotile integration ([#58], [#60])
- KCM split into modular tab components ([#56])
- German translation ([#57])
- Systemd user service for daemon management
- KWin effect for drag feedback
- D-Bus interface for external control
- Multi-distro packaging (Arch, Debian, RPM)

### Fixed
- Settings freeze and excessive file saves ([#55])
- Session restoration and rotation after login ([#66])
- Window tracking: snap/restore behavior, zone clearing, startup timing, rotation zone ID matching, floating window exclusion ([#67])

[3.2.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.1.3...v3.2.0
[3.1.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.1.2...v3.1.3
[3.1.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.1.1...v3.1.2
[3.1.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.1.0...v3.1.1
[3.1.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.17...v3.1.0
[3.0.17]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.16...v3.0.17
[3.0.16]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.15...v3.0.16
[3.0.15]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.14...v3.0.15
[3.0.14]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.13...v3.0.14
[3.0.13]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.12...v3.0.13
[3.0.12]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.11...v3.0.12
[3.0.11]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.10...v3.0.11
[3.0.10]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.9...v3.0.10
[3.0.9]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.8...v3.0.9
[3.0.8]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.7...v3.0.8
[3.0.7]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.6...v3.0.7
[3.0.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.5...v3.0.6
[3.0.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.4...v3.0.5
[3.0.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.3...v3.0.4
[3.0.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.2...v3.0.3
[3.0.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.1...v3.0.2
[3.0.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v3.0.0...v3.0.1
[3.0.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.8.7...v3.0.0
[2.8.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.8.1...v2.8.2
[2.8.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.8.0...v2.8.1
[2.8.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.7.1...v2.8.0
[2.7.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.7.0...v2.7.1
[2.7.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.6.0...v2.7.0
[2.6.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.5.3...v2.6.0
[#286]: https://github.com/fuddlesworth/PlasmaZones/pull/286
[#288]: https://github.com/fuddlesworth/PlasmaZones/pull/288
[#289]: https://github.com/fuddlesworth/PlasmaZones/pull/289
[#290]: https://github.com/fuddlesworth/PlasmaZones/pull/290
[#292]: https://github.com/fuddlesworth/PlasmaZones/pull/292
[#294]: https://github.com/fuddlesworth/PlasmaZones/pull/294
[#295]: https://github.com/fuddlesworth/PlasmaZones/pull/295
[#296]: https://github.com/fuddlesworth/PlasmaZones/pull/296
[#297]: https://github.com/fuddlesworth/PlasmaZones/pull/297
[#298]: https://github.com/fuddlesworth/PlasmaZones/pull/298
[#299]: https://github.com/fuddlesworth/PlasmaZones/pull/299
[#300]: https://github.com/fuddlesworth/PlasmaZones/pull/300
[#301]: https://github.com/fuddlesworth/PlasmaZones/pull/301
[#302]: https://github.com/fuddlesworth/PlasmaZones/pull/302
[#303]: https://github.com/fuddlesworth/PlasmaZones/pull/303
[2.5.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.5.2...v2.5.3
[2.5.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.5.1...v2.5.2
[2.5.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.5.0...v2.5.1
[2.5.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.7...v2.5.0
[2.4.7]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.6...v2.4.7
[2.4.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.5...v2.4.6
[2.4.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.3...v2.4.5
[2.4.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.2...v2.4.3
[2.4.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.1...v2.4.2
[#211]: https://github.com/fuddlesworth/PlasmaZones/discussions/211
[#249]: https://github.com/fuddlesworth/PlasmaZones/issues/249
[#251]: https://github.com/fuddlesworth/PlasmaZones/discussions/251
[#252]: https://github.com/fuddlesworth/PlasmaZones/issues/252
[#254]: https://github.com/fuddlesworth/PlasmaZones/issues/254
[#258]: https://github.com/fuddlesworth/PlasmaZones/discussions/258
[2.4.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.0...v2.4.1
[2.4.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.16...v2.4.0
[2.3.16]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.15...v2.3.16
[2.3.15]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.14...v2.3.15
[2.3.14]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.13...v2.3.14
[2.3.13]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.12...v2.3.13
[2.3.12]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.11...v2.3.12
[2.3.11]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.10...v2.3.11
[2.3.10]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.9...v2.3.10
[2.3.9]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.8...v2.3.9
[2.3.8]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.7...v2.3.8
[2.3.7]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.6...v2.3.7
[2.3.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.5...v2.3.6
[2.3.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.3...v2.3.5
[2.3.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.2...v2.3.3
[2.3.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.1...v2.3.2
[2.3.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.0...v2.3.1
[2.3.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.2.1...v2.3.0
[2.2.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.2.0...v2.2.1
[2.2.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.1.0...v2.2.0
[2.1.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.0.2...v2.1.0
[2.0.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.0.1...v2.0.2
[2.0.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.0.0...v2.0.1
[2.0.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.15...v2.0.0
[1.15.15]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.14...v1.15.15
[1.15.14]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.13...v1.15.14
[1.15.13]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.12...v1.15.13
[1.15.12]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.11...v1.15.12
[1.15.11]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.9...v1.15.11
[1.15.9]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.8...v1.15.9
[1.15.8]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.7...v1.15.8
[1.15.7]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.6...v1.15.7
[1.15.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.5...v1.15.6
[1.15.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.4...v1.15.5
[1.15.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.3...v1.15.4
[1.15.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.2...v1.15.3
[1.15.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.1...v1.15.2
[1.15.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.14.1...v1.15.1
[1.14.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.14.0...v1.14.1
[1.14.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.13.1...v1.14.0
[1.13.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.12.2...v1.13.1
[1.12.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.12.1...v1.12.2
[1.12.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.12.0...v1.12.1
[1.12.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.8...v1.12.0
[1.11.8]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.7...v1.11.8
[1.11.7]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.6...v1.11.7
[1.11.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.5...v1.11.6
[1.11.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.4...v1.11.5
[1.11.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.3...v1.11.4
[1.11.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.2...v1.11.3
[1.11.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.1...v1.11.2
[1.11.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.0...v1.11.1
[1.11.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.6...v1.11.0
[1.10.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.5...v1.10.6
[1.10.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.4...v1.10.5
[1.10.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.3...v1.10.4
[1.10.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.2...v1.10.3
[1.10.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.0...v1.10.2
[1.10.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.9.5...v1.10.0
[1.9.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.9.3...v1.9.5
[1.9.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.9.2...v1.9.3
[1.9.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.9.1...v1.9.2
[1.9.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.8.4...v1.9.1
[1.8.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.8.3...v1.8.4
[1.8.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.8.1...v1.8.2
[1.8.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.8.0...v1.8.1
[1.8.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.7.0...v1.8.0
[1.7.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.6.2...v1.7.0
[1.6.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.6.1...v1.6.2
[1.6.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.6.0...v1.6.1
[1.6.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.5.9...v1.6.0
[1.5.9]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.5.3...v1.5.9
[1.5.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.4...v1.5.3
[1.3.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.3...v1.3.4
[1.3.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.2...v1.3.3
[1.3.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.1...v1.3.2
[1.3.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.6...v1.3.0
[1.2.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.5...v1.2.6
[1.2.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.4...v1.2.5
[1.2.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.2...v1.2.4
[1.2.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.1...v1.2.2
[1.2.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.0...v1.2.1
[1.2.0]: https://github.com/fuddlesworth/PlasmaZones/releases/tag/v1.2.0

[#1]: https://github.com/fuddlesworth/PlasmaZones/pull/1
[#13]: https://github.com/fuddlesworth/PlasmaZones/pull/13
[#14]: https://github.com/fuddlesworth/PlasmaZones/pull/14
[#15]: https://github.com/fuddlesworth/PlasmaZones/pull/15
[#16]: https://github.com/fuddlesworth/PlasmaZones/pull/16
[#18]: https://github.com/fuddlesworth/PlasmaZones/pull/18
[#19]: https://github.com/fuddlesworth/PlasmaZones/pull/19
[#20]: https://github.com/fuddlesworth/PlasmaZones/pull/20
[#21]: https://github.com/fuddlesworth/PlasmaZones/pull/21
[#40]: https://github.com/fuddlesworth/PlasmaZones/pull/40
[#42]: https://github.com/fuddlesworth/PlasmaZones/pull/42
[#43]: https://github.com/fuddlesworth/PlasmaZones/pull/43
[#44]: https://github.com/fuddlesworth/PlasmaZones/pull/44
[#45]: https://github.com/fuddlesworth/PlasmaZones/pull/45
[#48]: https://github.com/fuddlesworth/PlasmaZones/pull/48
[#49]: https://github.com/fuddlesworth/PlasmaZones/pull/49
[#50]: https://github.com/fuddlesworth/PlasmaZones/pull/50
[#51]: https://github.com/fuddlesworth/PlasmaZones/pull/51
[#55]: https://github.com/fuddlesworth/PlasmaZones/pull/55
[#56]: https://github.com/fuddlesworth/PlasmaZones/pull/56
[#57]: https://github.com/fuddlesworth/PlasmaZones/pull/57
[#58]: https://github.com/fuddlesworth/PlasmaZones/pull/58
[#60]: https://github.com/fuddlesworth/PlasmaZones/pull/60
[#66]: https://github.com/fuddlesworth/PlasmaZones/pull/66
[#67]: https://github.com/fuddlesworth/PlasmaZones/pull/67
[#70]: https://github.com/fuddlesworth/PlasmaZones/pull/70
[#71]: https://github.com/fuddlesworth/PlasmaZones/pull/71
[#72]: https://github.com/fuddlesworth/PlasmaZones/pull/72
[#74]: https://github.com/fuddlesworth/PlasmaZones/pull/74
[#75]: https://github.com/fuddlesworth/PlasmaZones/pull/75
[#76]: https://github.com/fuddlesworth/PlasmaZones/pull/76
[#78]: https://github.com/fuddlesworth/PlasmaZones/pull/78
[#79]: https://github.com/fuddlesworth/PlasmaZones/pull/79
[#80]: https://github.com/fuddlesworth/PlasmaZones/pull/80
[#87]: https://github.com/fuddlesworth/PlasmaZones/pull/87
[#89]: https://github.com/fuddlesworth/PlasmaZones/pull/89
[#90]: https://github.com/fuddlesworth/PlasmaZones/pull/90
[#92]: https://github.com/fuddlesworth/PlasmaZones/pull/92
[#94]: https://github.com/fuddlesworth/PlasmaZones/issues/94
[#95]: https://github.com/fuddlesworth/PlasmaZones/issues/95
[#96]: https://github.com/fuddlesworth/PlasmaZones/pull/96
[#97]: https://github.com/fuddlesworth/PlasmaZones/pull/97
[#132]: https://github.com/fuddlesworth/PlasmaZones/pull/132
[#133]: https://github.com/fuddlesworth/PlasmaZones/pull/133
[#136]: https://github.com/fuddlesworth/PlasmaZones/pull/136
[#139]: https://github.com/fuddlesworth/PlasmaZones/pull/139
[#143]: https://github.com/fuddlesworth/PlasmaZones/discussions/143
[#145]: https://github.com/fuddlesworth/PlasmaZones/issues/145
[#147]: https://github.com/fuddlesworth/PlasmaZones/issues/147
[#148]: https://github.com/fuddlesworth/PlasmaZones/issues/148
[#150]: https://github.com/fuddlesworth/PlasmaZones/issues/150
[#152]: https://github.com/fuddlesworth/PlasmaZones/issues/152
[#158]: https://github.com/fuddlesworth/PlasmaZones/issues/158
[#159]: https://github.com/fuddlesworth/PlasmaZones/issues/159
[#160]: https://github.com/fuddlesworth/PlasmaZones/discussions/160
[#164]: https://github.com/fuddlesworth/PlasmaZones/issues/164
[#168]: https://github.com/fuddlesworth/PlasmaZones/issues/168
[#169]: https://github.com/fuddlesworth/PlasmaZones/pull/169
[#170]: https://github.com/fuddlesworth/PlasmaZones/pull/170
[#172]: https://github.com/fuddlesworth/PlasmaZones/issues/172
[#174]: https://github.com/fuddlesworth/PlasmaZones/pull/174
[#156]: https://github.com/fuddlesworth/PlasmaZones/discussions/156
[#166]: https://github.com/fuddlesworth/PlasmaZones/discussions/166
[#167]: https://github.com/fuddlesworth/PlasmaZones/issues/167
[#175]: https://github.com/fuddlesworth/PlasmaZones/issues/175
[#176]: https://github.com/fuddlesworth/PlasmaZones/pull/176
[#179]: https://github.com/fuddlesworth/PlasmaZones/issues/179
[#180]: https://github.com/fuddlesworth/PlasmaZones/issues/180
[#181]: https://github.com/fuddlesworth/PlasmaZones/pull/181
[#182]: https://github.com/fuddlesworth/PlasmaZones/pull/182
[#185]: https://github.com/fuddlesworth/PlasmaZones/issues/185
[#186]: https://github.com/fuddlesworth/PlasmaZones/pull/186
[#187]: https://github.com/fuddlesworth/PlasmaZones/issues/187
[#188]: https://github.com/fuddlesworth/PlasmaZones/pull/188
[#256]: https://github.com/fuddlesworth/PlasmaZones/pull/256
[#259]: https://github.com/fuddlesworth/PlasmaZones/pull/259
[#260]: https://github.com/fuddlesworth/PlasmaZones/pull/260
[#261]: https://github.com/fuddlesworth/PlasmaZones/pull/261
[#263]: https://github.com/fuddlesworth/PlasmaZones/pull/263
[#264]: https://github.com/fuddlesworth/PlasmaZones/pull/264
[#265]: https://github.com/fuddlesworth/PlasmaZones/pull/265
[#266]: https://github.com/fuddlesworth/PlasmaZones/pull/266
[#268]: https://github.com/fuddlesworth/PlasmaZones/pull/268
[#269]: https://github.com/fuddlesworth/PlasmaZones/pull/269
[#270]: https://github.com/fuddlesworth/PlasmaZones/pull/270
[#272]: https://github.com/fuddlesworth/PlasmaZones/discussions/272
[#273]: https://github.com/fuddlesworth/PlasmaZones/discussions/273
[#275]: https://github.com/fuddlesworth/PlasmaZones/discussions/275
[#274]: https://github.com/fuddlesworth/PlasmaZones/pull/274
[#276]: https://github.com/fuddlesworth/PlasmaZones/pull/276
[#277]: https://github.com/fuddlesworth/PlasmaZones/pull/277
[#278]: https://github.com/fuddlesworth/PlasmaZones/pull/278
[#279]: https://github.com/fuddlesworth/PlasmaZones/pull/279
[#280]: https://github.com/fuddlesworth/PlasmaZones/pull/280
[#281]: https://github.com/fuddlesworth/PlasmaZones/pull/281
[#282]: https://github.com/fuddlesworth/PlasmaZones/pull/282
