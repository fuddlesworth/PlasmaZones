// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Not forward-declared: moc needs the complete type to register the
// ShaderSetStore* Q_PROPERTY below as a pointer meta-type.
#include "settings/stores/shadersetstore.h"

// By value rather than forward-declared: applyShaderGroupWrite's builder
// signature puts ShaderProfile inside a std::optional, which needs the
// complete type at the point of declaration.
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorControl/PageController.h>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
// Forward-declared rather than included: only a const reference to it appears
// in this header (paramsAreStaleAt), so the definition is a .cpp concern.
class ShaderProfileTree;
}

namespace PlasmaZones {

class AnimationPresetLibrary;
class AnimationPreviewController;
class ISettings;

/// Q_PROPERTY surface for the "Animations" settings page.
///
/// Edits per-event motion-profile overrides and surfaces the built-in
/// `PhosphorAnimation::ProfilePaths` taxonomy as a section-grouped list
/// for the QML drilldown.
///
/// ## Persistence model
///
/// Per-event overrides live in ONE config key,
/// `Animations/MotionProfileTree`, beside the pack assignment in
/// `Animations/ShaderProfileTree` — so one animation event is one unit in one
/// store, the way a decorated surface always has been. Each composition root
/// installs that tree into its `PhosphorProfileRegistry` on start-up and on
/// every change (`installMotionProfileTree`), so a write here updates QML
/// thumbnails in-process AND
/// live-updates the daemon at runtime.
///
/// ## Effective-value resolution
///
/// `resolvedProfile()` walks the path's parent chain and, at each level,
/// reads the stored timing tree FIRST, falling back to the process-wide
/// `PhosphorProfileRegistry::defaultRegistry()` for levels with no user
/// override (the shell animation family seeds), then fills in library
/// defaults. Config leads because the registry is repopulated from a settings
/// change handler in the composition root, while this page re-reads
/// synchronously inside the signal it is about to emit. With no registry
/// published (unit tests without a bootstrap) the walk is purely
/// config-backed, which is self-consistent.
///
/// ## Composition
///
/// The controller delegates persistence-heavy concerns to two child
/// QObjects: `AnimationPresetLibrary` (preset CRUD) and `ShaderSetStore`
/// (motion-set CRUD). The preset library's signals are forwarded to the
/// controller's own signals via `connect()` so QML rebinds without poking
/// at it directly. The set store is the exception: QML binds it straight
/// as `setsBridge` (the shared ShaderSetsPage talks to it), so only its
/// `pendingChangesChanged` is forwarded, for the dirty flag.
class AnimationsPageController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(qreal springOmegaMin READ springOmegaMin CONSTANT)
    Q_PROPERTY(qreal springOmegaMax READ springOmegaMax CONSTANT)
    Q_PROPERTY(qreal springZetaMin READ springZetaMin CONSTANT)
    Q_PROPERTY(qreal springZetaMax READ springZetaMax CONSTANT)

    /// The motion-set store, bound by AnimationsMotionSetsPage as its `bridge`.
    Q_PROPERTY(PlasmaZones::ShaderSetStore* setsBridge READ setsBridge CONSTANT)

    /// Live-preview data source for the shader browser's detail dialog —
    /// an AnimationPreviewController, typed QObject* for the shared dialog.
    /// See DecorationPageController's twin for the bridge contract.
    Q_PROPERTY(QObject* previewController READ previewController CONSTANT)
    /// Selects the animation preview pane in the shared detail dialog.
    Q_PROPERTY(QString previewKind READ previewKind CONSTANT)

    /// Animation event paths whose stock KWin effect the compositor
    /// suppresses session-wide because a tree-assigned pack owns the event
    /// (the settings-side mirror of the effect's syncStockEffectSuppression
    /// gate). The rule editor's stock-animation conflict chip hides for
    /// these events: with the stock effect unloaded there is no
    /// double-animation for a rule shader to conflict with.
    Q_PROPERTY(QStringList stockSuppressedEvents READ stockSuppressedEvents NOTIFY stockSuppressedEventsChanged)

public:
    /// @param shaderRegistry Optional — when null, all `*ShaderEffects()` /
    ///        `*ShaderProfile()` Q_INVOKABLEs return empty results so unit
    ///        tests can construct the controller without an animation
    ///        bootstrap.
    /// @param settings Optional — when null, shader-tree CRUD is a no-op.
    explicit AnimationsPageController(PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry = nullptr,
                                      ISettings* settings = nullptr, QObject* parent = nullptr);
    ~AnimationsPageController() override;

    /// PhosphorControl::StagingDomain contract. The animations page's
    /// own pendingChanges API is the per-page staging — wire it through.
    /// dirtyChanged() (inherited from StagingDomain) is emitted alongside
    /// pendingChangesChanged() so ApplicationController can react.
    bool isDirty() const override;
    void apply() override;
    void discard() override;

    // Slider bounds for the spring editor. These are a deliberately narrower,
    // usable subset of the engine's accepted clamp range (PhosphorAnimation::
    // Spring clamps omega to [0.1, 200] and zeta to [0, 10]). The slider only
    // needs to cover values a user can perceive: above omega ~40 the spring
    // settles in well under ~75 ms (visually instant), and zeta > ~4 is a
    // barely-moving overdamped crawl. zeta is floored at 0.1 (not 0) so the
    // edited spring always settles rather than oscillating forever. A
    // hand-edited config outside this band still parses — the engine clamp,
    // not the slider, is the validity boundary.
    qreal springOmegaMin() const;
    qreal springOmegaMax() const;
    qreal springZetaMin() const;
    qreal springZetaMax() const;

    QObject* previewController() const;
    QString previewKind() const;

    /// Built-in event paths, grouped by section. Each entry:
    /// ```
    /// { "section": "window", "label": "Window",
    ///   "paths": [ { "path": "window", "label": "Window",
    ///                "parent": "global", "isCategory": true,
    ///                "acceptsWindowRules": false },
    ///              { "path": "window.appearance.open", "label": "Open",
    ///                "parent": "window.appearance", "isCategory": false,
    ///                "acceptsWindowRules": true }, ... ] }
    /// ```
    /// All built-in paths from `ProfilePaths::allBuiltInPaths()` are included.
    /// QML reads these key names directly, so keep this sample in step with
    /// what the builder inserts.
    Q_INVOKABLE QVariantList eventSections() const;

    /// The UI section @p path groups under, driving the sidebar grouping.
    /// Usually the first dotted segment (`"window.appearance.open"` →
    /// `"window"`), and `"global"` for the global root, but four top-level
    /// segments are remapped: `osd`, `popup`, and `panel` all collapse into
    /// `"overlays"`, and `cursor` into `"widget"`. Empty for an empty path.
    Q_INVOKABLE QString sectionForPath(const QString& path) const;

    /// Whether a per-window Rule can reach @p path. Forwards to
    /// `ProfilePaths::eventPathResolvesPerWindow()` and so mirrors the
    /// `acceptsWindowRules` flag on each `eventSections()` entry, for the
    /// callers that hold a path rather than a model row — notably a rule that
    /// ALREADY stores a windowless event, which the picker no longer lists and
    /// which therefore has no row to read the flag from.
    Q_INVOKABLE bool eventPathAcceptsWindowRules(const QString& path) const;

    /// Translated label for @p path's last segment (e.g. `"editor.snapIn"`
    /// → `"Snap In"`), through @ref segmentLabel. Empty for an empty path.
    Q_INVOKABLE QString eventLabel(const QString& path) const;

    /// Translated label for one taxonomy segment: every built-in profile-path
    /// segment and the merged "overlays" section key have a `tr()` entry; an
    /// unknown (plugin-added) segment falls back to its title-cased form. The
    /// one label source for the animations page's drilldown, the rule
    /// editor's event picker and the rule-list summary, so a path reads the
    /// same everywhere.
    static QString segmentLabel(const QString& segment);

    /// Inheritance chain from @p path up to (but excluding) the empty
    /// root. Useful for "snap → zone → global" breadcrumbs.
    Q_INVOKABLE QStringList parentChain(const QString& path) const;

    /// The isolation root governing @p path's SHADER resolution, or an empty
    /// string when the path inherits normally.
    ///
    /// `parentChain()` above walks to the root unconditionally, which is right
    /// for TIMING: ProfileTree isolates nothing. It is wrong for the shader
    /// axis, where `ShaderProfileTree::resolve` cuts the chain at an isolation
    /// root and substitutes an empty baseline, so nothing above that root
    /// reaches in. A card that renders the parent chain as the inheritance
    /// story for both axes therefore states something false about the pack on
    /// every `shell.*` event.
    ///
    /// Exposed rather than re-derived in QML from a path prefix, which is what
    /// `ShaderProfileTree.h` explicitly asks callers not to do — a second copy
    /// of "where does inheritance start" drifts from the resolver. The
    /// decoration twin is `DecorationPageController::isBaselineIsolated`; this
    /// one returns the ROOT rather than a bool because the card has to name it.
    Q_INVOKABLE QString shaderIsolationRoot(const QString& path) const;

    /// True iff @p path is a member of `ProfilePaths::allBuiltInPaths()`.
    /// All filesystem-touching methods reject non-member paths so a
    /// crafted `path` (e.g. `"../etc/passwd"`) cannot escape the
    /// profiles directory.
    ///
    /// Q_INVOKABLE so a card can validate its declared mirrorPaths at load.
    /// The group writers cannot report WHICH path failed (their return is a
    /// single bool / count over the whole batch), so a typo'd mirror path
    /// would surface only as a permanently latched divergence banner (the
    /// mirror's stored state can never match the primary's) with nothing
    /// naming the culprit.
    Q_INVOKABLE bool isValidEventPath(const QString& path) const;

    /// True iff a user timing override is stored for @p path. Returns false
    /// for any @p path that is not a built-in event path (rejecting
    /// traversal attempts).
    Q_INVOKABLE bool hasOverride(const QString& path) const;

    /// Per-path stored timing override as a QVariantMap, normalised exactly as
    /// `resolvedProfile` normalises it — otherwise a card would show a live
    /// revert link beside the INHERITED value it claims is overridden. The
    /// `name` field is stripped.
    ///
    /// Normalised, NOT uniformly stripped: a field `Profile::fromJson` leaves
    /// unset is dropped here (so the ancestor's value shows through), and a
    /// field fromJson substitutes a default for is substituted here too (so it
    /// keeps blocking inheritance, as the daemon's copy does). One documented
    /// exception: an unresolvable `curve` SPEC is kept rather than dropped,
    /// because resolving it needs a registry this layer has no access to. See
    /// `animations_controller_detail::sanitizedProfileMap` for the per-field
    /// table and the reasoning.
    ///
    /// An empty map therefore means EITHER no override is stored OR no field
    /// in it survived. Callers that must tell those apart ask `hasOverride()`,
    /// which tests the file itself.
    Q_INVOKABLE QVariantMap rawProfile(const QString& path) const;

    /// Effective Profile for @p path: walks the parent chain reading this
    /// controller's own stored override at each level, falling back to the
    /// process-wide registry where no user file exists, and fills any
    /// still-missing fields with `Profile::Default*` constants. Always
    /// returns a populated map. See the class docs for why disk leads.
    Q_INVOKABLE QVariantMap resolvedProfile(const QString& path) const;

    /// Write @p profileJson as the user override at @p path. The map
    /// follows `Profile::toJson()` shape (curve / duration / minDistance /
    /// sequenceMode / staggerInterval / presetName); a top-level `name`
    /// field is STRIPPED — the tree keys entries by path, so a name carried
    /// over from a preset would be dead weight. Emits `overrideChanged(path)` on
    /// success. Rejects any @p path that isn't a built-in event path —
    /// path traversal (`../etc/passwd`) and arbitrary names cannot reach
    /// the disk.
    /// @return true when the entry was written.
    Q_INVOKABLE bool setOverride(const QString& path, const QVariantMap& profileJson);

    /// Remove the stored override at @p path. Same path validation as
    /// `setOverride`. Emits `overrideChanged(path)` when the file was removed
    /// AND when it was found already gone (it existed a moment earlier, so the
    /// registry may still hold the vanished entry); a FAILED removal emits
    /// neither, the file being still there.
    /// @return true only when this call did the removing.
    Q_INVOKABLE bool clearOverride(const QString& path);

    /// Clear every per-event timing override (each built-in event path falls back
    /// to its built-in default). Backs the settings app's per-page "Reset to
    /// defaults" for the animation pages: the clear is one write to
    /// `Animations/MotionProfileTree`, staged like any other edit, so a
    /// subsequent Discard restores it from the committed baseline.
    /// The shader tree, animation Profile blob, and window filtering are separate
    /// Settings keys the caller resets alongside this.
    /// @return the number of entries actually removed, or -1 when the reset did
    /// not run at all. Since schema v8 the only cause of -1 is a missing settings
    /// object, i.e. a wiring bug: the async-discard refusal and the
    /// partial-removal case both went with the per-event files, and a partial
    /// result is no longer possible because the whole clear is a single tree
    /// write. A caller must not treat -1 as "nothing to clear".
    int clearAllOverrides();

    /// Scoped sibling of clearAllOverrides, called from the per-page kebab AND
    /// from the event card's Override toggle (its OFF branch clears the card's
    /// whole write-path group in one batch). Q_INVOKABLE for that second
    /// caller: clear only the
    /// overrides at @p eventPaths (one settings page's own event-path
    /// subtree), leaving every other page's overrides untouched. Same
    /// return-code contract as clearAllOverrides (-1 = did not run). @p
    /// eventPaths must be built-in event paths; non-built-in entries are skipped.
    Q_INVOKABLE int clearOverridesUnder(const QStringList& eventPaths);

    // ─── Group writes ─────────────────────────────────────────────────────
    //
    // An event card writes to a GROUP of paths, not one: its own event path
    // plus any `mirrorPaths` it declares (the two window.appearance legs, for
    // instance). Every one of the calls below takes the whole group and applies
    // one policy across it, so a card cannot write a group partially and cannot
    // reach a per-path mutator directly and bypass its mirrors.
    //
    // These live here rather than as JS loops in the card for three reasons.
    // The merge and field-removal rules are the same drop-versus-substitute
    // semantics `rawProfile` already documents, and having them in two
    // languages meant two places to keep in step. Each read is also taken once
    // per call here rather than once per reader, which is what let the card
    // drop its own per-path snapshot caches, and `divergentPathCount` reads
    // the shader tree once for the whole group. Neither tree accessor is
    // memoised — each rebuilds on every call — which is precisely why anything
    // here must read one once and pass it down rather than call the accessor
    // in a loop. And each is now
    // directly testable without driving QML.
    // Why it is not memoised is in animationspagecontroller_groupwrites.cpp.
    //
    // What deliberately stays in the card: the `_committing` /
    // `_committingShader` re-entrancy latches and the refresh that follows a
    // group write. Those are view state about which signals the card should
    // ignore while its own write is in flight, and they mean nothing here.
    //
    // How each writer treats an invalid entry, and why none of them caps
    // `paths.size()`, is in animationspagecontroller_groupwrites.cpp beside the
    // dedup it describes.

    /// Merge @p fields into the stored override at every path in @p rawPaths and
    /// write each result back.
    ///
    /// Merged over each path's OWN stored profile rather than replacing it, so
    /// fields the caller does not mention (minDistance, sequenceMode,
    /// staggerInterval, presetName) survive. A motion set can write those to a
    /// leaf, and a caller that replaced the whole map would drop them the
    /// moment the user nudged Duration.
    ///
    /// Survival is of the SANITIZED form: the base comes from `rawProfile`, so
    /// a hand-edited out-of-range value is normalised by an unrelated Duration
    /// nudge. No resolved value changes — the sanitizer mirrors the daemon's own
    /// parser — but the rewrite is real. clearFieldOnPaths says the same.
    ///
    /// @p fields is ALLOWLISTED to six keys — the four named above plus curve
    /// and duration — because a stray one would land in the user's profile file
    /// and stay until some later write rewrote the object. Values are bounded
    /// as well; see `boundedWrittenMap`.
    ///
    /// @p curveFromCommit distinguishes the two things a caller can mean about
    /// the curve, which a plain map cannot express. An INVALID or NULL QVariant
    /// (QML `undefined` or `null`) means "the user did not touch the curve":
    /// each path keeps its own, so a path that owns one keeps it and a path that
    /// inherits stays inheriting. A valid non-null string means the user edited
    /// the curve and it travels to every path. Never decide a curve on the
    /// user's behalf by passing the resolved one here.
    ///
    /// @return the number of paths written. Never negative: with no settings
    /// object the batch write fails and this reports 0. A partial failure
    /// TOASTS and reports the count that did land, exactly as clearFieldOnPaths
    /// does. A caller must not branch on -1 — the refusal that once produced it
    /// went with the per-event files.
    ///
    /// A path whose merged object already matches disk comes back Unchanged and
    /// is NOT counted, so a call where every path was already in the desired
    /// state returns 0. That is the clearFieldOnPaths convention, and the
    /// OPPOSITE of the two applyShaderGroupWrite-backed setters
    /// (setShaderOverrideOnPaths, setShaderParametersOnPaths), which count an
    /// already-satisfied path as written. The shader CLEARS do not — they count
    /// removals, so a path with nothing to clear is skipped uncounted, the same
    /// way this one skips an Unchanged write. Test success with `>= 0`, never
    /// with `> 0`.
    Q_INVOKABLE int setOverrideMergedOnPaths(const QStringList& rawPaths, const QVariantMap& fields,
                                             const QVariant& curveFromCommit);

    /// Remove ONE field (`"curve"` or `"duration"`) from the stored override at
    /// every path in @p rawPaths, returning that field to inheritance while the
    /// other timing field and the motion-set fields stay put. A path whose
    /// override becomes empty has its ENTRY removed rather than left as an empty
    /// object: the two resolve identically, but the card's toggle and the
    /// pending-changes walk both key on the entry being present.
    /// @return the number of paths actually changed, or -1 on refusal. Paths
    /// that did not carry the field are skipped, so 0 means the field was
    /// already inherited everywhere and nothing needed doing. -1 is reserved
    /// for "nothing was attempted", which since schema v8 has exactly one
    /// cause: @p field is not one this owns, i.e. a caller bug. A caller must
    /// not read -1 as "there was nothing to clear".
    Q_INVOKABLE int clearFieldOnPaths(const QStringList& rawPaths, const QString& field);

    /// True when ANY path in @p rawPaths takes a shader leg. A group mutation must
    /// gate on this rather than on the primary path alone: a mirror that does
    /// support a leg would otherwise keep its shader override across a toggle
    /// off, and `divergentPathCount` would then report a divergence no control
    /// on the card could clear.
    Q_INVOKABLE bool anyPathSupportsShaderLeg(const QStringList& rawPaths) const;

    /// True iff every shader-capable path in @p rawPaths already carries
    /// @p effectId as its DIRECT shader override. Non-supporting paths are
    /// SKIPPED, mirroring setShaderOverrideOnPaths (which never writes to
    /// them), so a mixed group can report true right after a successful group
    /// write. The empty string is the engaged-empty "None" sentinel, which is
    /// a real stored value and distinct from carrying no override at all.
    ///
    /// FALSE when nothing was compared, as an empty list is: a group whose
    /// every member was skipped has tested no path, and the answer must not be
    /// true off the back of zero comparisons.
    Q_INVOKABLE bool allPathsHoldShaderEffect(const QStringList& rawPaths, const QString& effectId) const;

    /// Set @p effectId (with @p parameters) as the shader override on every
    /// path in @p rawPaths that can host a shader leg. Non-supporting paths are
    /// SKIPPED rather than attempted: `setShaderOverride` would reject them
    /// anyway, and skipping keeps the warning out of the log for a call that
    /// was never going to land.
    /// The whole group is applied to ONE tree read and written back ONCE, so a
    /// card cannot observe a half-written group, and a drag over a shader
    /// parameter costs one settings write per tick rather than one per path.
    /// @return the number of paths written, or -1 for exactly ONE refusal: an
    /// @p effectId that `acceptableShaderEffectId` rejects (over-length,
    /// NUL-bearing, carrying a separator, or naming no installed pack). That is
    /// a caller bug rather than something the user did, so it only warns and
    /// does not toast. With no settings object this reports 0, not -1.
    /// @p parameters is BOUNDED rather than validated against the pack's
    /// schema: an over-long key or string value is dropped, and the map is
    /// capped in size. Which parameter ids a pack declares is not checked here,
    /// because an id the pack does not know is inert at resolve time, whereas
    /// an unbounded value reaches disk and stays there.
    Q_INVOKABLE int setShaderOverrideOnPaths(const QStringList& rawPaths, const QString& effectId,
                                             const QVariantMap& parameters);

    /// Set @p parameters on every shader-capable path in @p rawPaths WITHOUT
    /// touching which pack each path uses.
    ///
    /// The distinction from setShaderOverrideOnPaths is the whole point, and it
    /// is what a parameter slider needs. That writer stamps `effectId`
    /// unconditionally, so tweaking one slider on a leaf that INHERITS its pack
    /// pinned the inherited id as a direct override: the leaf stopped following
    /// its parent, and the parent's card immediately raised a "descendant
    /// shadows this parent" warning whose clear action deleted the tweak the
    /// user had just made. Here `effectId` is left exactly as stored — engaged
    /// where the path owns a pack, unengaged where it inherits one — so a
    /// params-only override rides the cascade instead of severing it.
    ///
    /// An EMPTY @p parameters means "own no parameter values", which strips the
    /// parameters off a path that owns a pack and REMOVES the entry outright
    /// from one that owned only parameters — so it is also the vehicle for
    /// "revert my parameters to inherited". That, and why this replaces rather
    /// than merges the map, are written up beside the implementation.
    ///
    /// @return the number of paths that hold the requested end state, which
    /// counts a path whose stored value already matched and a path that had
    /// nothing to clear, not just the ones mutated. -1, and only -1, means
    /// nothing was attempted because there is no settings object (it
    /// toasts). A missing ISettings returns 0, not -1 — a caller that must
    /// distinguish "refused" from "no-op" gets that from -1 alone.
    Q_INVOKABLE int setShaderParametersOnPaths(const QStringList& rawPaths, const QVariantMap& parameters);

    /// Number of DISTINCT shadowing descendant overrides beneath the paths in
    /// @p rawPaths, using the one definition of "shadowing descendant" that
    /// clearShaderOverrideDescendantsOnPaths clears — so the count a parent
    /// card shows is exactly what its clear action would remove. Unioned rather
    /// than summed, so a group holding both an ancestor and its descendant
    /// cannot report an override twice that the clear removes once.
    ///
    /// Exists because the per-path shaderOverrideDescendantCount rebuilds the
    /// whole shader tree on every call (a store read, a JSON parse and a prune
    /// walk; `rawShaderProfile` is not memoised), and a card called it once per
    /// write path inside a refresh that runs at drag rate. That is precisely
    /// the read-in-a-loop shape the block comment above forbids.
    /// @return the number of DISTINCT shadowing descendants, UNIONED across the
    /// group rather than summed, so one shared by two paths counts once. 0 with
    /// no ISettings, never negative: this reads and cannot be refused.
    Q_INVOKABLE int shaderOverrideDescendantCountForPaths(const QStringList& rawPaths) const;

    /// True when at least ONE path in @p rawPaths directly owns a shader pack,
    /// i.e. holds an engaged, NON-EMPTY `effectId`.
    ///
    /// "Owns a pack" is deliberately narrower than "has an override". The
    /// engaged-EMPTY sentinel is an explicit "no shader here" rather than a
    /// pack this event chose, and a params-only override rides the ancestor's
    /// pack, so neither counts. This is what the shader row's remove control
    /// keys on: with a pack owned somewhere in the group the control removes
    /// it and lets inheritance resume, and with none owned there is nothing to
    /// remove and the control writes the "no shader" sentinel instead.
    ///
    /// ANY rather than ALL because the two arms are not symmetric in cost. A
    /// mixed group (one path owns a pack, another inherits) resolves to the
    /// remove arm, which clears the owner and is a no-op on the rest. Taking
    /// the other arm would write the blocking sentinel over a pack the user
    /// really did choose. The card's divergence banner already reports that
    /// the group disagrees.
    /// @return false when there is no ISettings, and for an empty list.
    Q_INVOKABLE bool anyPathOwnsShaderPack(const QStringList& rawPaths) const;

    /// Clear the shader override on every path in @p rawPaths, returning the event
    /// to inheritance. Distinct from writing the engaged-empty sentinel, which
    /// is an explicit "None" that BLOCKS inheritance.
    /// One tree read and one write for the whole group, like its setter twin.
    ///
    /// Deliberately does NOT skip a path with no shader leg, which its setter
    /// twin does: such an entry can exist (an import, or a path that lost leg
    /// support after it was written), clearing is idempotent, and refusing
    /// would strand it with nothing able to remove it.
    /// @return the number of paths whose override was removed. Never negative:
    /// with no settings object this reports 0, and there is no refusal path
    /// left that would toast. A caller must not branch on -1.
    Q_INVOKABLE int clearShaderOverrideOnPaths(const QStringList& rawPaths);

    // Orphaned parameter overrides: a descendant storing params but no pack of
    // its own, whose stored ids the pack it now resolves does not declare. What
    // an ancestor pack SWITCH leaves behind, since parameter ids are per-pack.
    // Kept separate from the shadowing count above, which is about paths that
    // override the parent's PACK; these follow it. The predicate, its carve-outs
    // and why the clear is destructive are in the _groupwrites.cpp beside it.
    Q_INVOKABLE bool shaderParamsAreStale(const QString& path) const;
    Q_INVOKABLE int staleParamDescendantCountForPaths(const QStringList& rawPaths) const;
    Q_INVOKABLE int clearStaleParamDescendantsOnPaths(const QStringList& rawPaths);

    /// Clear the shader overrides BELOW every path in @p rawPaths.
    /// @return the total number cleared. Never negative: with no settings
    /// object each pass reports 0, and no refusal path remains that would
    /// toast. A caller must not branch on -1.
    Q_INVOKABLE int clearShaderOverrideDescendantsOnPaths(const QStringList& rawPaths);

    /// How many paths in a card's write group have stored state differing from
    /// @p primaryPath's, expressed the way the card's divergence banner needs
    /// it: 0 when everything agrees, otherwise the number of diverging mirrors
    /// PLUS ONE for the primary, which each of them differs from and which the
    /// converging edit also rewrites.
    ///
    /// Comparison is on exactly what a single edit can converge, not on
    /// everything stored. The duration and the whole shader leg always count.
    /// The curve counts only when @p compareCurve is true, which the caller
    /// sets false in simple mode: there is no curve control there, so no edit
    /// could converge a divergent curve and counting it would latch the banner
    /// on permanently. The motion-set fields do not count: the merged writer
    /// preserves each path's own for any field the caller does not name, and no
    /// caller names one today. One that did would converge that field while
    /// this kept reporting 0, so the two would have to move together.
    ///
    /// The shader axis is compared only for paths that can host a shader leg. A
    /// non-supporting path always stores nothing on that axis, so comparing its
    /// permanently-empty leg against a supporting path's real one would report
    /// a divergence over an axis nothing could converge.
    Q_INVOKABLE int divergentPathCount(const QString& primaryPath, const QStringList& rawMirrorPaths,
                                       bool compareCurve) const;

    /// Scoped sibling of revertPending: restore ONLY the timing entries at @p
    /// eventPaths from `committedMotionProfileTree()`, leaving every other
    /// page's staged edits pending. Set and preset FILES are not staged at all
    /// since schema v8, so nothing here restores one.
    /// Refuses (returns false) only with no settings object to read a baseline from. The
    /// caller reverts the shader tree for the same paths separately (the tree is
    /// Settings-owned; see the revertPending() caller contract). @return true
    /// once the restore ran, whether or not any path was in scope.
    bool revertPendingUnder(const QStringList& eventPaths);

    /// True iff any of @p eventPaths differs from its committed timing entry —
    /// the timing half of a per-page dirty check, a value comparison against
    /// committedMotionProfileTree(). The shader-tree half is the same shape of
    /// comparison the caller runs against committedShaderProfileTree().
    bool hasScopedPendingOverrides(const QStringList& eventPaths) const;

    /// Library of user-saved Profile presets. Each entry is a Profile JSON
    /// (`curve`, `duration`, `name`, …) sitting in the same `profiles/`
    /// dir as overrides — distinguished by the `name` field NOT matching
    /// any `ProfilePaths::` constant. Entries with a `curve` starting
    /// with `"spring:"` are spring presets; everything else is easing.
    Q_INVOKABLE QVariantList userPresets() const;

    /// Save @p profileJson under @p name as a user preset. Rejects names
    /// that collide with a built-in `ProfilePaths::` event path so a
    /// preset can't accidentally shadow an override slot. @return true
    /// on a successful write. Emits `userPresetsChanged()`.
    Q_INVOKABLE bool addUserPreset(const QString& name, const QVariantMap& profileJson);

    /// Delete the user preset whose `name` field matches @p name. Will
    /// never delete a leftover override file even when its `name` field happens
    /// to match. @return true on a successful delete. Emits
    /// `userPresetsChanged()`.
    Q_INVOKABLE bool removeUserPreset(const QString& name);

    // ── Motion sets ──────────────────────────────────────────────────

    /// The motion-set store — the `bridge` ShaderSetsPage binds to.
    /// A motion set is a file under
    /// `~/.local/share/plasmazones/motionsets/<slug>.json` carrying both halves
    /// of each event it covers: the timing entry and the pack assignment.
    /// Applying merges, so paths NOT in the set keep what they had. Set files
    /// themselves are immediate CRUD, matching decoration; what a set APPLIES
    /// rides this controller's `setOverride` into config, so it stages like any
    /// other edit and Discard reverts it. The domain closures live in
    /// motionsetdomain.cpp.
    ShaderSetStore* setsBridge() const
    {
        return m_motionSets;
    }

    // ── Shader effects (Phase 6) ─────────────────────────────────────

    /// True when @p path is one of the event paths the daemon's overlay
    /// service actually consumes as a shader-leg surface (osd.show /
    /// .hide and the `popup.<surface>.<show|hide>` family). Other
    /// paths persist a shader assignment but never produce a visible
    /// shader leg — the QML picker hides itself on those rows so the
    /// user gets clear "this control does nothing here" feedback rather
    /// than picking a shader and seeing no change. Single source of
    /// truth lives in @c src/core/types/animationshadersupportedpaths.h —
    /// adding a new shader-leg surface in the daemon means appending
    /// its leg paths there in lockstep.
    Q_INVOKABLE bool supportsShaderLeg(const QString& path) const;

    /// Installed `AnimationShaderEffect`s flattened to a QML-friendly list.
    /// Each row mirrors `animations_controller_detail::effectToMap`:
    /// id / name / description / author / version / category / appliesTo
    /// (QStringList of event-class tokens, empty = universal) / isUserEffect /
    /// parameters (QVariantList of ParameterInfo maps).
    Q_INVOKABLE QVariantList availableShaderEffects() const;

    /// Path-aware variant of @c availableShaderEffects: the same rows, but
    /// FILTERED to the effects whose declared `appliesTo` class can drive the
    /// event @p path. An effect that can't (e.g. the geometry-only window-morph
    /// on a window.appearance.open row, or a desktop effect on a window row) is
    /// omitted from the returned list, so the per-event shader picker only offers
    /// compatible shaders. Each row still carries `dimmed` (always false) and
    /// `dimReason` (always empty) for QML binding compatibility with @c
    /// availableShaderEffects consumers.
    Q_INVOKABLE QVariantList availableShaderEffectsForPath(const QString& path) const;

    /// Just the parameters list for @p effectId — convenience for the
    /// per-event shader-param editor.
    Q_INVOKABLE QVariantList shaderParameters(const QString& effectId) const;

    /// XDG-writable user shader directory path (no side effects). The directory
    /// is created on demand by openUserShaderDirectory() and by the pack
    /// installer, so a caller that only needs the path pays for no filesystem
    /// write.
    /// Internal helper — not exposed to QML; the page surfaces an
    /// "Open Folder" button that calls `openUserShaderDirectory()`
    /// directly rather than displaying the path as a label.
    QString userShaderDirectoryPath() const;

    /// Open the user shader directory in the system file manager,
    /// creating it first if missing.
    Q_INVOKABLE void openUserShaderDirectory();

    /// Install a shader pack from a dropped folder. @p sourceUrl accepts
    /// either a `file://` URL (drag-drop from a file manager) or a bare
    /// absolute path (programmatic callers); both forms are normalised
    /// via `QDir::cleanPath` before use. The source must be a directory
    /// containing a `metadata.json` at its root. The directory is copied
    /// recursively into `userShaderDirectoryPath()/<basename>`; the
    /// registry's filewatcher detects the new pack and emits
    /// `effectsChanged` automatically. Validates that the source exists,
    /// is a non-symlinked directory with a non-symlinked `metadata.json`,
    /// and that the basename does not collide with an existing entry in
    /// the user dir (collision returns false rather than overwriting).
    /// Symlinks anywhere inside the source tree are silently skipped by
    /// the recursive copy. @return true on success.
    Q_INVOKABLE bool installShaderPack(const QString& sourceUrl);

    /// Per-event shader override read.
    /// @return the DIRECT override at this exact path, or an empty map when
    /// there is none. Each key is present only when its optional is engaged,
    /// so all four shapes are distinguishable and a consumer must test for
    /// presence rather than indexing blind:
    ///   - `{}`                                  — no override, inherits both
    ///   - `{ effectId, parameters }`            — owns a pack and its values
    ///   - `{ effectId }`                        — owns a pack; empty string
    ///                                             here is the "None" sentinel
    ///   - `{ parameters }`                      — PARAMS-ONLY: inherits the
    ///                                             ancestor's pack, owns every
    ///                                             parameter value (see
    ///                                             setShaderParametersOnPaths)
    /// The params-only shape is what a shader parameter slider writes on a
    /// path that inherits its pack, so it is the common case, not an edge one.
    Q_INVOKABLE QVariantMap rawShaderProfile(const QString& path) const;

    /// Every DIRECT shader override, keyed by event path, each value the same
    /// map shape `rawShaderProfile()` returns.
    ///
    /// One call rather than a `rawShaderProfile()` per path because the motion
    /// -set snapshot needs the whole picture and runs on every setsChanged:
    /// `shaderProfileTree()` returns the tree BY VALUE, so the per-path form
    /// would parse and copy it once for each of the sixty-odd built-in paths
    /// on the GUI thread.
    QVariantMap allRawShaderProfiles() const;

    /// Walk the parent chain to resolve the effective shader assignment
    /// for @p path. Returns an empty map if no ancestor has one.
    Q_INVOKABLE QVariantMap resolvedShaderProfile(const QString& path) const;

    /// Assign @p effectId (with optional @p parameters) to @p path.
    /// An empty @p effectId writes an explicit "no effect" sentinel at this
    /// path, which BLOCKS inheritance from every ancestor for it and its
    /// descendants. That is deliberately the opposite of
    /// `clearShaderOverride(path)`, which removes the entry so resolution
    /// falls through to the parent again — the sentinel is what makes "I
    /// disabled all popups" stick even when a parent assigns a shader. Emits
    /// `pendingChangesChanged()` whenever the call actually changed state.
    ///
    /// NOT Q_INVOKABLE, nor are the three per-path shader operations below.
    /// QML uses the `*OnPaths` group forms exclusively, and the group-writes
    /// contract depends on that: one tree read and one broadcast per group,
    /// which a delegate-reachable per-path call would defeat by writing N
    /// times. Public for the group writers built on them, and for tests.
    bool setShaderOverride(const QString& path, const QString& effectId, const QVariantMap& parameters);

    /// Remove the shader override at @p path; ancestors take over via
    /// `ShaderProfileTree::resolve` walk-up. Emits
    /// `pendingChangesChanged()` whenever the call actually changed
    /// state.
    bool clearShaderOverride(const QString& path);

    /// Count of shader overrides on paths strictly DEEPER than @p path,
    /// meaning the ones that literally begin `<path>.`.
    /// Used by parent-node cards to surface "N deeper overrides shadow
    /// this parent" — without it, a stale leaf set in a previous
    /// session silently wins the deeper-leaf-overlay merge inside
    /// `ShaderProfileTree::resolve` and the parent's value never
    /// reaches runtime even though the UI control shows it set.
    ///
    /// "Deeper" is a STRING PREFIX, narrower than the cascade the resolver
    /// walks: `parentPath` sends every category root to the bare literal
    /// `global`, which is a prefix of nothing, so passing `global` always
    /// answers 0 however many category overrides exist. Inert today — no card
    /// is built for the global node — but it would have to be revisited first.
    int shaderOverrideDescendantCount(const QString& path) const;

    /// Clear every shader override whose path is strictly DEEPER than
    /// @p path (i.e. paths starting with `<path>.`). Does NOT clear
    /// the override at @p path itself. Returns the number of cleared
    /// entries (0 = nothing to clear). The -1 refusal this used to document
    /// is unreachable: the async-discard gate it named was removed with the
    /// per-event files, and no path in this function returns it.
    /// Persists the batch via a single `setShaderProfileTree`
    /// write, which fires `shaderProfileTreeChanged` once and (via the
    /// constructor's broadcast lambda) one path-agnostic
    /// `shaderProfileChanged()` signal — NOT one per cleared path.
    /// Also emits `pendingChangesChanged()`. Used by parent-node
    /// cards' "Clear shadowing children" affordance.
    int clearShaderOverrideDescendants(const QString& path);

    /// Reverse-lookup: list every event path whose direct shader
    /// override targets @p effectId. Each entry: `{ path, label }` with
    /// @c label produced by @c eventLabel(path). Used by the read-only
    /// shaders browser to surface a "Used in:" line per shader so a
    /// user can tell at a glance which assignments would be affected if
    /// they uninstalled or replaced a pack. Inherited / resolved
    /// references are intentionally NOT included — only direct
    /// overrides count, mirroring the existing tree-walk semantics.
    Q_INVOKABLE QVariantList shaderEffectUsages(const QString& effectId) const;

    /// See the stockSuppressedEvents Q_PROPERTY. Evaluates the effect's
    /// per-event ownership gate (animations enabled, tree-resolved effectId
    /// non-empty, pack applies to the event's contract class) for the
    /// minimize and maximize events — the two whose stock KWin effects the
    /// compositor unloads while a tree pack owns them.
    QStringList stockSuppressedEvents() const;

    /// Test hook: redirect file I/O to @p dir instead of the XDG default.
    /// Pass an empty string to restore the default. Not Q_INVOKABLE — QML
    /// callers must not redirect persistence.
    void setUserProfilesDirOverride(const QString& dir);

Q_SIGNALS:
    /// Emitted whenever this controller's view of a path changed — wider than
    /// "a write succeeded", since it includes an override found ALREADY GONE
    /// between the existence check and the clear. @p path is the affected
    /// event path.
    void overrideChanged(const QString& path);

    /// Emitted on any successful add/removeUserPreset.
    void userPresetsChanged();

    /// Emitted on set/clearShaderOverride, on every write of the tree through
    /// `Settings::setShaderProfileTree` (which Discard's revert goes through),
    /// AND on a reload or settings-profile switch that moves the tree —
    /// `shaderProfileTreeJson` is a NOTIFY property, so `Settings::load()`
    /// re-fires it through `emitChangedNotifyProperties` without any explicit
    /// Q_EMIT. Path-agnostic because diffing the tree to find what moved would
    /// cost more than letting every visible card rebind.
    void shaderProfileChanged(const QString& path);

    /// Re-emit of `AnimationShaderRegistry::effectsChanged` so QML can
    /// rebind without poking at the registry directly.
    void shaderEffectsChanged();

    /// Emitted whenever stockSuppressedEvents may have changed: on any
    /// shader-tree change, registry rescan, or a flip of the animations
    /// master toggle — the three inputs of the ownership gate.
    void stockSuppressedEventsChanged();

    /// Emitted whenever `hasPendingChanges()` may have flipped. The
    /// SettingsController's slot calls `setNeedsSave(true)` when there
    /// are pending changes; emits with `false`-equivalent state on
    /// commit/revert too so the slot can re-evaluate.
    void pendingChangesChanged();

    /// User-facing transient notification request. QML chrome wires
    /// this to `window.showToast()` so a failed shader-pack install
    /// (or a mutator refused mid-discard) surfaces the underlying
    /// reason instead of returning false silently.
    void toastRequested(const QString& text);

public:
    // ── Save / Discard integration (Phase 8) ─────────────────────────
    //
    // Animation edits land in config immediately for live preview, and the
    // standard "Discard" button still reverts this session's changes. Dirty
    // state is VALUE-based: the live timing and shader trees compared against
    // their committed baselines, with no snapshot map to keep in step. Kept in
    // its own block as the dedicated SettingsController integration surface.

    /// True iff there are unsaved changes the user could still discard.
    bool hasPendingChanges() const;

    /// Advance the committed baseline — every change so far is now "saved."
    /// Called from apply(); SettingsController::save() deliberately does NOT
    /// call it (that would double-dispatch, see
    /// settingscontroller_lifecycle.cpp).
    void commitPending();

    /// Re-evaluate the value-based dirty state after an external commit point the
    /// controller cannot observe on its own — chiefly Settings::save()'s
    /// captureBaseline, which advances committedShaderProfileTree() without moving
    /// the live tree (so no shaderProfileTreeChanged fires). SettingsController::
    /// save() calls this right after m_settings.save(); the dirtyChanged forwarder
    /// gates on an actual flip so a no-op refresh is free.
    void refreshDirtyState();

    /// Drop this page's own view of the pending edits and refresh QML.
    ///
    /// Every value this page writes is a config key, so `Settings::load()` in
    /// the caller does the actual reverting — the same path every other
    /// settings page rides. This exists for what Settings cannot do on its
    /// own: re-emit `overrideChanged` / `userPresetsChanged` /
    /// `pendingChangesChanged` and refresh the set store through
    /// `ShaderSetStore::notifyLiveStateChanged()` so an OPEN page rebinds.
    ///
    /// @return true always. The signature is kept because
    /// `SettingsController::load()` shares one shape across pages; there is no
    /// longer a partial-failure mode to report, since nothing here touches the
    /// filesystem.
    bool revertPending();

private:
    /// Flip-gated emitter for stockSuppressedEventsChanged: recomputes the
    /// list and emits only when it actually changed. All three gate inputs
    /// (registry rescan, tree assignment, animations master toggle) route
    /// through here so tree edits that cannot affect the suppression set
    /// stop re-running the conflict-chip bindings.
    void maybeEmitStockSuppressedEventsChanged();

    /// Absolute path of the user's saved-PRESET library directory. Per-event
    /// overrides are config (see `motionTree`); this directory holds only the
    /// named presets the user saves from the curve editor.
    QString userProfilesDir() const;
    QString userMotionSetsDir() const;

    /// The stored per-event timing tree (`Animations/MotionProfileTree`), in
    /// `PhosphorAnimation::ProfileTree`'s serialized shape. Empty when this
    /// controller has no settings object.
    QVariantMap motionTree() const;

    /// Outcome of removeOverride(). `Absent` is not a failure: the desired end
    /// state (no override at this path) already holds.
    enum class OverrideRemoval {
        Removed,
        Absent,
        Failed
    };

    /// The storage half of clearOverride. Emits NOTHING — the caller owns the
    /// path-validity gate and every signal.
    OverrideRemoval removeOverride(const QString& path);

    enum class OverrideWrite {
        Written,
        Unchanged,
        Failed
    };

    /// The storage half of setOverride: guard, then write the entry into the
    /// timing tree. Emits nothing of its own beyond the settings change the
    /// write itself causes; the caller owns `overrideChanged` and the
    /// dirty-state signal.
    OverrideWrite writeOverrideOnly(const QString& path, const QVariantMap& profileJson);

    /// Apply several override edits in ONE tree write.
    ///
    /// @p edits maps a path to the profile to store there; an EMPTY profile
    /// removes that path's override. Every path is assumed already validated by
    /// the caller. Emits nothing — the caller owns `overrideChanged` per path,
    /// and the dirty signal arrives through the settings change this raises.
    ///
    /// One write, not a loop of `writeOverrideOnly`: each of those raises its
    /// own settings change, and every one of those repopulates the process's
    /// profile registry and re-evaluates every card binding. A group clear that
    /// removes some paths and rewrites others is one user action and must cost
    /// one write.
    /// @return true when the write was attempted (false only with no settings).
    bool writeOverridesBatch(const QList<QPair<QString, QVariantMap>>& edits);

    /// Shared body of the three stale-parameter entry points, taking the tree
    /// the caller already read so a group pass costs ONE rebuild.
    bool paramsAreStaleAt(const PhosphorAnimationShaders::ShaderProfileTree& tree, const QString& path) const;

    /// Both boundary checks on a shader effect id: the length/character sanity
    /// check and the registry-membership gate. Shared by `setShaderOverride`
    /// and the group writer `setShaderOverrideOnPaths`, because the group
    /// writer is the only path QML uses and had silently inherited neither.
    /// @param context names the caller in the diagnostics.
    bool acceptableShaderEffectId(const QString& effectId, QLatin1String context) const;

    /// Shared body of the two shader-leg group SETTERS
    /// (setShaderOverrideOnPaths and setShaderParametersOnPaths). Everything
    /// those two have in common lives here: the dedup, the per-path validity
    /// and shader-leg skip, the compare-and-skip against what is already
    /// stored, the write-once epilogue, and the counting rules. What differs is
    /// only how each builds the profile it wants at a path, which is what
    /// @p build supplies.
    ///
    /// The two policies were 40 near-identical lines apart, and the pair has to
    /// stay in step: they share a return contract and a skip rule, and a fix
    /// applied to one of them silently not applying to the other is the failure
    /// this removes.
    ///
    /// @param preflight optional per-call validation. Returning false yields
    /// -1. It belongs here rather than at the caller because the order is
    /// observable: one with no ISettings must still report 0.
    /// @param build receives the profile currently stored at the path and
    /// whether there was one at all, and returns the profile to store.
    /// Returning std::nullopt means "store nothing here": any existing entry is
    /// REMOVED rather than replaced with an empty profile, which is what keeps
    /// a no-op entry out of the tree for the pruner, the diff and the
    /// ancestor's shadowing walk to trip over.
    /// @param context names the caller in the refusal diagnostic.
    /// @return the number of paths holding the requested end state, or -1 when
    /// the preflight refused.
    int applyShaderGroupWrite(const QStringList& rawPaths, QLatin1String context,
                              const std::function<bool()>& preflight,
                              const std::function<std::optional<PhosphorAnimationShaders::ShaderProfile>(
                                  const PhosphorAnimationShaders::ShaderProfile& stored, bool hasStored)>& build);

    /// Shared body of clearAllOverrides / clearOverridesUnder: clears every
    /// override among @p eventPaths in ONE tree write, then emits.
    /// @p context names the caller for the failure log.
    /// @return the number cleared, or -1 when there is no settings object.
    int clearOverridesForPaths(const QStringList& eventPaths, QLatin1String context);

    PhosphorAnimationShaders::AnimationShaderRegistry* m_shaderRegistry = nullptr;
    ISettings* m_settings = nullptr;
    QString m_userProfilesDirOverride; ///< Empty = use XDG default

    // Sub-services owned via QObject-parent: both are constructed with
    // `this` as parent so ~AnimationsPageController tears them down
    // automatically. No manual delete; no QPointer needed.
    AnimationPresetLibrary* m_presets = nullptr;
    ShaderSetStore* m_motionSets = nullptr;
    AnimationPreviewController* m_preview = nullptr;

    /// "The current run of failed merged writes has already been toasted."
    ///
    /// setOverrideMergedOnPaths is reached from a per-pointer-move commit, and
    /// a write that fails does so for a persistent reason, so without this the
    /// same message is re-emitted at drag rate. Set when the failure toast
    /// fires, cleared by the first call in which every path lands.
    bool m_mergedWriteFailureToasted = false;
    /// Last observed value of hasPendingChanges() seen by the
    /// pendingChangesChanged → dirtyChanged forwarder. CLAUDE.md:
    /// "Only emit signals when value actually changes". Several call
    /// sites emit pendingChangesChanged unconditionally; gating the
    /// forward on this cached state keeps the framework's dirty
    /// Q_PROPERTY NOTIFY contract honest.
    bool m_lastHadPendingChanges = false;
    /// Memoised verdict of the value-based tree-dirty compare inside
    /// hasPendingChanges() (each side is a store read + parse + prune, and
    /// the predicate runs several times per mutation at drag rate). Reset by
    /// the shaderProfileTreeChanged lambda (live tree moved) and by
    /// refreshDirtyState() (committed baseline moved).
    mutable std::optional<bool> m_treeDirtyCache;

    /// Persist @p tree, marking the write as this controller's own.
    ///
    /// EVERY motion-tree write from this controller must go through here.
    /// `Settings::setMotionProfileTree` emits synchronously, and the handler
    /// broadcasts a card-wide reload for writes it did not recognise as ours;
    /// a write that bypassed this helper would be misread as external and make
    /// every visible card re-walk its whole chain, at drag rate on the
    /// continuous paths.
    void writeMotionTree(const QVariantMap& tree);

    /// Non-zero while this controller is inside its own motion-tree write.
    ///
    /// `Settings::setMotionProfileTree` emits synchronously, so the
    /// `motionProfileTreeChanged` handler runs nested inside our own write.
    /// The handler broadcasts a card-wide reload for EXTERNAL movers; this
    /// counter is how it tells those apart from our own writes, which already
    /// announce themselves per path and must not defeat the cards' path
    /// filter at drag rate. A counter rather than a bool because the write
    /// helpers can nest.
    int m_selfTreeWriteDepth = 0;
    /// Last emitted stockSuppressedEvents() value; maybeEmitStockSuppressedEventsChanged
    /// gates the NOTIFY on an actual list change so tree edits that cannot
    /// affect the suppression set stop re-running the rule editor's
    /// conflict-chip bindings.
    QStringList m_lastStockSuppressedEvents;
    /// Memoised eventSections() result — taxonomy is static for the
    /// process lifetime so subsequent QML rebinds reuse the same list.
    /// Populated lazily on first call. NOTE: if `ProfilePaths::
    /// allBuiltInPaths()` ever becomes dynamic (e.g. plugin-discovered
    /// event paths), this cache needs a clear() trigger — currently
    /// there's nothing to invalidate it because the source is
    /// compile-time static.
    mutable QVariantList m_eventSectionsCache;
};

} // namespace PlasmaZones
