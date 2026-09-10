<!--
SPDX-FileCopyrightText: 2026 fuddlesworth
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Visual development and evidence

## Agent dispatch

Role files live under `.claude/agents/shader-theme/`:
- `pz-shader-art-director.md`
- `pz-shader-material-reviewer.md`
- `pz-shader-motion-reviewer.md`

Technical reviewers live under `.claude/agents/review/`. Read the applicable role file
before dispatch. Use its named agent type when available. In Codex, pass the role file's
path to a spawned agent and instruct it to read and follow it; a Claude agent definition
does not automatically register a Codex agent type. Use the host's available message and
result tools, not hardcoded teammate names. Do not assume all reviewers fit concurrently.
When reusing an idle agent, use the host's resume/follow-up task operation. Sending it an
informational message may not start work; verify that each requested review has started.

Give each visual reviewer the original user brief, chosen design, exact artifact paths,
capture conditions and relevant event assignments. Keep the implementation owner's
self-assessment out of the first critique. Reviewers inspect evidence before source and
report without editing. The art director advises on the visual direction; the parent owns
the final decision and resolves conflicts against the brief and host contracts.

## Direction and prototype

The design artifact records:
- User requirements and verified host restrictions separately from revisable design choices.
  Footprint, border width, effect placement and the initial mechanism are choices unless
  the user or host fixes them. Reviewers cannot silently promote them into requirements.
- Intended material and the visible mechanisms that convey it, at normal viewing size.
- Composition: where detail and contrast belong, what stays quiet, how focus reads.
- Motion: what moves, bends or reveals, where the action originates, how it settles, and
  how inverse actions relate. Derive these from the brief, not a fixed effect vocabulary.
- Which properties unify the theme and how event variants differ in purpose and intensity.
- Observable acceptance criteria and the renders needed to judge them.

Explore two interpretations briefly, then test the critical visual assumption. For a
material-led brief, implement two inexpensive procedural studies using different causal
mechanisms, not merely different colors or parameter values. Use actual shader output.
One diagnostic patch should show whether the material model works at all; a second view
must place it at the proposed window footprint beside unchanged content. This distinguishes
a weak material model from a good model squeezed into too little space. These studies are
scratch artifacts, not extra deliverable packs. Run gate 2 of `validation.md` on them before
rendering, so a study that fails to compile is not mistaken for a weak material.

The art director chooses from the rendered studies or revises the proposed mechanism if
neither works. Color, geometry and lighting should account for the requested appearance;
names, noise overlays and decorative glints alone do not establish a physical material.
For non-material briefs, use the equivalent small composition/motion studies instead.
For an already specified user mechanism, vary its execution rather than replacing it.
Do not require a user approval checkpoint for routine design choices.

Preserving content does not automatically restrict the theme to a thin rectangular border.
Consider available exterior space, silhouettes and event-specific surfaces, within verified
padding, cost and usability limits. Conversely, do not enlarge decoration just to rescue a
weak shader. Test how much visible area the selected mechanism actually needs.

Render the representative chain and motion together early. Assign each pass an explicit
role in the total image: a pane that intentionally preserves content need not independently
look like the material. The theme as a whole must convey it. Require the combined prototype
to survive visual critique before implementing the rest of the coverage matrix.

## Capture actual shader output

Use the runtime's assembly, parameter translation and sampling contracts. The only
scripted capture in the repo is `plasmazones-shader-render` (read
`tools/shader-render/README.md` first), and it renders OVERLAY packs only, at device scale
1.0. Nothing scripts a capture of window animations, surface chains or the settings preview:
the nested-KWin harness's `ScreenShot2` path (`scripts/nested-kwin/capture-output.py`)
bypasses the effect chain by its own docstring and never shows PlasmaZones decorations or
animations, and the nested-shell capture covers shell clients only. So animation, decoration
and settings-preview evidence is MANUAL: run the live session, or
`PZ_NESTED_VISIBLE=1 scripts/nested-kwin/run-nested.sh`, apply the packs, and capture from
the HOST with `spectacle`, `grim` or `wf-recorder`, recording the conditions listed below.
When no session is available, say so and report the "prototype" outcome; do not substitute
a nested screenshot, a mock or a generated concept image, none of which can prove shader
output quality. Record any differences from the real host.

Keep evidence in `scratchpad/<theme>/renders/`, outside pack directories. Record defaults
and overrides, backend, scale, fixture/background, event, direction and duration alongside
each capture. Label untreated/treated pairs and identify the applied chain's pack IDs.
Compare revisions with the same fixture and settings. Attribute only the changes from
the untreated fixture to the shader; artwork or colors already in the fixture are not
evidence that the shader produces them. Record logical dimensions and device scale so
reviewers can judge ordinary desktop size.
Use actual default host opacity for presentation captures. Test reduced opacity, alternate
padding and other contract stress cases separately and label them; they must not become
the visual baseline. View a native-size crop as well as any automatically scaled contact
sheet so resizing by the viewer does not erase the very cue under review.

- Decorations: untreated and treated content, light and dark applications, focused and
  unfocused, small and large windows, native scale and a scaled display. The scripted
  renderer pins scale 1.0, so scaled-display evidence needs a live or nested session running
  at that scale; without one, mark the scale criterion unverified rather than inferring it.
  Inspect the whole chain together. Magnified detail is supplemental, not proof of visibility
  at normal size.
- Overlays: readable labels and clear selection over light, dark and busy backgrounds.
  Composite transparent output before judging color or contrast; an image viewer's alpha
  background is not the user's desktop.
- Motion: capture each event at its intended duration, both directions and rest endpoints.
  Also capture sampled frames around onset, maximum deformation and settling. Exercise
  interrupted/repeated actions when the host supports them and representative window sizes.
  A slow diagnostic view helps inspect geometry; it cannot establish everyday motion feel.

Reviewers must actually open the images and inspect playback using supported tools.
If playback cannot be inspected, use timestamped frames to judge shape and continuity,
and mark real-time pacing unverified. If capture fails, diagnose it or report the visual
gate unavailable. Do not substitute reading GLSL or metadata for looking at the result.

## Critique and revision

For each finding record the artifact/frame/time, observation, affected brief criterion,
proposed adjustment and the capture that would establish improvement. Separate observed
defects, subjective preferences and hypotheses about the cause. Avoid numerical beauty
scores: a high average must not hide unreadable content or missing theme identity.
Classify findings as local execution, structural design, or missing evidence. A recognizably
wrong material or action is a structural finding even if the code is correct. The reviewer
identifies the perceptual problem; the art director and implementation owner choose the fix.
Suggested edits are hypotheses, not mandatory recipes. Do not add restrictions such as
"keep this width" or "only change this corner" without a user or verified host reason.

Prioritize weak material/motion mechanisms before tuning microscopic noise or color values.
Restraint still needs a perceptible identity; increasing detail everywhere is not a general
fix. Preserve acceptable content and event behavior while correcting the weak component.

After a core identity/mechanism failure, dispatch the art director on the actual renders,
original brief and critique before spending another revision. Decide whether to repair
execution or change the mechanism, footprint or composition; record the evidence and
decision. If the same core finding survives one attempted repair, the next round must
test a materially different approach. It must not be another change to highlight position,
curve strength or noise amplitude in the same rejected design. Preserve the earlier
candidate and compare both with the same fixtures. This redesign consumes the existing
revision budget; it does not reset the counter or relax the acceptance criteria.
Record the initial candidate as revision 0. A changed candidate captured after critique
consumes a revision; reading or re-reviewing the same evidence does not. Parallel studies
in one bounded redesign experiment belong to that same revision.

Acceptance criteria derived from the user's brief stay fixed. Criteria introduced by the
initial design may be revised with an explicit rationale. A structural motion finding
cannot pass merely because sampled geometry is smooth or technical endpoints are exact.

Keep `scratchpad/<theme>/visual-review.md` with revision paths, findings and their status.
Do not overwrite earlier renders. A finding is resolved by new evidence, not by a code edit
or a changed description. Apply the milestone revision limit in SKILL.md and report any
unmet criterion. Final review covers every requested family and event, plus their cohesion.
