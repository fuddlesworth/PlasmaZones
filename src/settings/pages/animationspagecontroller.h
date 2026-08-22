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
}

namespace PlasmaZones {

class AnimationPresetLibrary;
class ISettings;

/// Q_PROPERTY surface for the "Animations" settings page.
///
/// Edits per-event motion-profile overrides and surfaces the built-in
/// `PhosphorAnimation::ProfilePaths` taxonomy as a section-grouped list
/// for the QML drilldown.
///
/// ## Persistence model
///
/// Per-event overrides live as one Profile JSON file per path under
/// `~/.local/share/plasmazones/profiles/`. The daemon's existing
/// `PhosphorAnimation::ProfileLoader` watches that dir and pushes files
/// into `PhosphorProfileRegistry` automatically — no daemon code changes.
/// User-wins-over-shipped semantics are already wired by the loader's
/// owner-tag partitioning. The settings app has its own bootstrap-owned
/// loader (see `src/settings/main.cpp`); both watchers respond to the
/// same dir, so a write here updates QML thumbnails in-process AND
/// live-updates the daemon at runtime.
///
/// ## Effective-value resolution
///
/// `resolvedProfile()` walks the path's parent chain and, at each level,
/// reads this controller's own override file FIRST, falling back to the
/// process-wide `PhosphorProfileRegistry::defaultRegistry()` for levels
/// with no user file (shipped profiles, and whatever the hosting process
/// has registered), then fills in library defaults. Disk leads because
/// the registry is fed by a watcher on the same directory this class
/// writes, and that watcher is debounced — see
/// `setProfileStoreRefresher()` for the other half of keeping the two in
/// step. With no registry published (unit tests without a bootstrap) the
/// walk is purely file-backed, which is self-consistent.
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
    qreal springOmegaMin() const
    {
        return 1.0;
    }
    qreal springOmegaMax() const
    {
        return 40.0;
    }
    qreal springZetaMin() const
    {
        return 0.1;
    }
    qreal springZetaMax() const
    {
        return 4.0;
    }

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

    /// True iff a user override file exists for @p path. Returns false
    /// for any @p path that is not a built-in event path (rejecting
    /// traversal attempts).
    Q_INVOKABLE bool hasOverride(const QString& path) const;

    /// Per-path override file content as a QVariantMap, normalised exactly as
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
    /// An empty map therefore means EITHER no override file exists OR no field
    /// in it survived. Callers that must tell those apart ask `hasOverride()`,
    /// which tests the file itself.
    Q_INVOKABLE QVariantMap rawProfile(const QString& path) const;

    /// Effective Profile for @p path: walks the parent chain reading this
    /// controller's own override file at each level, falling back to the
    /// process-wide registry where no user file exists, and fills any
    /// still-missing fields with `Profile::Default*` constants. Always
    /// returns a populated map. See the class docs for why disk leads.
    Q_INVOKABLE QVariantMap resolvedProfile(const QString& path) const;

    /// Write @p profileJson as the user override at @p path. The map
    /// follows `Profile::toJson()` shape (curve / duration / minDistance /
    /// sequenceMode / staggerInterval / presetName); a top-level `name`
    /// field is added automatically. Emits `overrideChanged(path)` on
    /// success. Rejects any @p path that isn't a built-in event path —
    /// path traversal (`../etc/passwd`) and arbitrary names cannot reach
    /// the disk.
    /// @return true on a successful disk write.
    Q_INVOKABLE bool setOverride(const QString& path, const QVariantMap& profileJson);

    /// Delete the override file at @p path. Emits `overrideChanged(path)`
    /// when a file actually existed and was removed. Same path validation
    /// as `setOverride`. @return true when a file was removed.
    Q_INVOKABLE bool clearOverride(const QString& path);

    /// Clear every per-event override file (each built-in event path falls back
    /// to its built-in default). Backs the settings app's per-page "Reset to
    /// defaults" for the animation pages: each cleared file is snapshotted like
    /// a normal edit, so the change stages and a subsequent Discard restores it.
    /// The shader tree, animation Profile blob, and window filtering are separate
    /// Settings keys the caller resets alongside this.
    /// @return the number of override files actually removed, or -1 when the
    /// reset did not complete: either it was REFUSED because an async discard
    /// owns the snapshot map (every override file is still on disk), or some
    /// files could not be removed (a partial reset). The page toasts the reason
    /// in both cases. A caller must not treat -1 as "nothing to clear".
    int clearAllOverrides();

    /// Scoped sibling of clearAllOverrides, called from the per-page kebab AND
    /// from the event card's Override toggle (its OFF branch clears the card's
    /// whole write-path group in one batch). Q_INVOKABLE for that second
    /// caller: clear only the
    /// override files at @p eventPaths (one settings page's own event-path
    /// subtree), leaving every other page's overrides untouched. Same snapshot /
    /// return-code contract as clearAllOverrides (-1 = refused or partial). @p
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
    // drop its own per-path snapshot caches: `rawProfile` is memoised (see
    // `cachedDiskProfile`), the merge reads every path before its first write
    // so the memo is not invalidated out from under it, and
    // `divergentPathCount` reads the shader tree once for the whole group. The
    // shader tree itself is NOT memoised — `rawShaderProfile` rebuilds it on
    // every call — which is precisely why anything here must read it once and
    // pass it down rather than call that accessor in a loop. And each is now
    // directly testable without driving QML.
    //
    // What deliberately stays in the card: the `_committing` /
    // `_committingShader` re-entrancy latches and the refresh that follows a
    // group write. Those are view state about which signals the card should
    // ignore while its own write is in flight, and they mean nothing here.
    //
    // None of these caps `paths.size()`, and none needs to. Each DEDUPLICATES
    // its list on entry (distinctPaths). Invalid entries differ per writer:
    // clearFieldOnPaths, divergentPathCount, the four shader group writers
    // (setShaderOverrideOnPaths, setShaderParametersOnPaths,
    // clearShaderOverrideOnPaths, clearShaderOverrideDescendantsOnPaths) and
    // the group readers (shaderOverrideDescendantCountForPaths,
    // anyPathOwnsShaderPack, anyPathSupportsShaderLeg) all SKIP a non-built-in
    // path, while setOverrideMergedOnPaths forwards it to writeOverrideFileOnly,
    // which rejects it, so that path is absent from the returned count and the
    // call toasts — a caller bug surfaces instead of being silently dropped.
    // allPathsHoldShaderEffect is the one that neither skips nor counts: it
    // RETURNS FALSE on an invalid path, because "every path holds this id"
    // cannot be true of a path that cannot hold anything. Either way the WORK is
    // bounded by `ProfilePaths::allBuiltInPaths()` rather than by the
    // caller's list — a repeat costs nothing and an unrecognised entry costs
    // one lookup, never a disk read or a shader-tree rebuild. The dedup
    // matters because QML builds a group as `[eventPath].concat(mirrorPaths)`
    // and does not dedupe. (The scoped reverts declared elsewhere in this
    // header — clearOverridesUnder / clearOverridesForPaths — do NOT dedupe;
    // they are safe against duplicates anyway because the second visit to a
    // path classifies as Absent and is skipped.)

    /// Merge @p fields into the stored override at every path in @p rawPaths and
    /// write each result back.
    ///
    /// Merged over each path's OWN stored profile rather than replacing it, so
    /// fields the caller does not mention (minDistance, sequenceMode,
    /// staggerInterval, presetName) survive. A motion set can write those to a
    /// leaf, and a caller that replaced the whole map would drop them the
    /// moment the user nudged Duration.
    ///
    /// @p curveFromCommit distinguishes the two things a caller can mean about
    /// the curve, which a plain map cannot express. An INVALID or NULL QVariant
    /// (QML `undefined` or `null`) means "the user did not touch the curve":
    /// each path keeps its own, so a path that owns one keeps it and a path that
    /// inherits stays inheriting. A valid non-null string means the user edited
    /// the curve and it travels to every path. Never decide a curve on the
    /// user's behalf by passing the resolved one here.
    ///
    /// @return the number of paths written, or -1 if the call was refused
    /// because an async discard owns the snapshot map (it toasts). Matches the
    /// sentinel every other group writer here uses, and the distinction is
    /// load-bearing rather than cosmetic: this used to return a plain bool that
    /// went false for a refusal, for a caller-bug path AND for a genuine disk
    /// failure alike. A caller that stops re-issuing a write on a refusal —
    /// which the card does, to keep a drag from toasting per pointer move —
    /// then also stopped on a disk failure it could have recovered from. A
    /// partial failure now TOASTS and reports the count that did land, exactly
    /// as clearFieldOnPaths does. -1 means only "nothing was attempted".
    ///
    /// A path whose merged object already matches disk comes back Unchanged and
    /// is NOT counted, so a call where every path was already in the desired
    /// state returns 0. That is the clearFieldOnPaths convention, and the
    /// OPPOSITE of the shader group writers, which count an already-satisfied
    /// path as written. Test success with `>= 0`, never with `> 0`.
    Q_INVOKABLE int setOverrideMergedOnPaths(const QStringList& rawPaths, const QVariantMap& fields,
                                             const QVariant& curveFromCommit);

    /// Remove ONE field (`"curve"` or `"duration"`) from the stored override at
    /// every path in @p rawPaths, returning that field to inheritance while the
    /// other timing field and the motion-set fields stay put. A path whose
    /// override becomes empty has its file DELETED rather than left as an empty
    /// object: the two resolve identically, but the card's toggle and the
    /// pending-changes walk both key on file existence.
    /// @return the number of paths actually changed, or -1 on refusal. Paths
    /// that did not carry the field are skipped, so 0 means the field was
    /// already inherited everywhere and nothing needed doing. -1 is reserved
    /// for "nothing was attempted": @p field is not one this owns, or an
    /// async discard holds the snapshot map (which toasts). A PARTIAL write
    /// failure toasts and returns the count that DID change — returning -1
    /// there collapsed the editor under the cursor of the user who had just
    /// clicked inside it. A caller must not read -1 as "there was nothing to
    /// clear".
    Q_INVOKABLE int clearFieldOnPaths(const QStringList& rawPaths, const QString& field);

    /// True when ANY path in @p rawPaths takes a shader leg. A group mutation must
    /// gate on this rather than on the primary path alone: a mirror that does
    /// support a leg would otherwise keep its shader override across a toggle
    /// off, and `divergentPathCount` would then report a divergence no control
    /// on the card could clear.
    Q_INVOKABLE bool anyPathSupportsShaderLeg(const QStringList& paths) const;

    /// True iff every shader-capable path in @p rawPaths already carries
    /// @p effectId as its DIRECT shader override. Non-supporting paths are
    /// SKIPPED, mirroring setShaderOverrideOnPaths (which never writes to
    /// them), so a mixed group can report true right after a successful group
    /// write. The empty string is the engaged-empty "None" sentinel, which is
    /// a real stored value and distinct from carrying no override at all.
    Q_INVOKABLE bool allPathsHoldShaderEffect(const QStringList& paths, const QString& effectId) const;

    /// Set @p effectId (with @p parameters) as the shader override on every
    /// path in @p rawPaths that can host a shader leg. Non-supporting paths are
    /// SKIPPED rather than attempted: `setShaderOverride` would reject them
    /// anyway, and skipping keeps the warning out of the log for a call that
    /// was never going to land.
    /// The whole group is applied to ONE tree read and written back ONCE, so a
    /// card cannot observe a half-written group, and a drag over a shader
    /// parameter costs one settings write per tick rather than one per path.
    /// @return the number of paths written, or -1 if the call was refused
    /// because an async discard owns the tree (it toasts).
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
    /// It rides the cascade on the PACK axis only. `ShaderProfile::overlay`
    /// replaces the whole parameter map rather than merging keys, so whatever
    /// this stores becomes the complete parameter set at that path and stops
    /// following the ancestor's parameter edits. That is the documented model,
    /// not a defect here, and the descendant's card discloses it through the
    /// shader row's ownership caption.
    ///
    /// An EMPTY @p parameters means "own no parameter values", and what that
    /// resolves to depends on what else the path owns. The two outcomes are
    /// deliberate and differ from setShaderOverrideOnPaths, which treats an
    /// empty map as "leave `parameters` unset" and still stores an entry:
    ///   - path owns a pack (engaged `effectId`, sentinel included): the
    ///     parameters are stripped and the entry stays, pack intact.
    ///   - path owns only parameters: nothing would remain engaged, so the
    ///     ENTRY IS REMOVED rather than stored empty. Storing an empty profile
    ///     would leave a no-op override that the pruner, the diff and the
    ///     ancestor's shadowing walk all have to reason about.
    /// So this is also the vehicle for "revert my parameters to inherited".
    ///
    /// @return the number of paths that hold the requested end state, which
    /// counts a path whose stored value already matched and a path that had
    /// nothing to clear, not just the ones mutated. -1, and only -1, means
    /// nothing was attempted because an async discard owns the tree (it
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
    /// @return the summed count; 0 when there is no ISettings. Never negative:
    /// this reads and cannot be refused.
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
    /// @return the number of paths whose override was removed, or -1 if the
    /// call was refused because an async discard owns the tree (it toasts).
    Q_INVOKABLE int clearShaderOverrideOnPaths(const QStringList& rawPaths);

    /// Clear the shader overrides BELOW every path in @p rawPaths.
    /// @return the total number cleared, or -1 if any path refused (the
    /// "async discard in flight" sentinel). Stops at the first refusal: the
    /// in-flight gate cannot change between iterations, and the controller
    /// toasts per refused call. A -1 is never summed in, which would make a
    /// refusal indistinguishable from a smaller successful clear, and the
    /// caller gates its own feedback on telling the two apart.
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
    /// on permanently. The motion-set fields never count, because the merged
    /// writer preserves each path's own rather than converging them.
    ///
    /// The shader axis is compared only for paths that can host a shader leg. A
    /// non-supporting path always stores nothing on that axis, so comparing its
    /// permanently-empty leg against a supporting path's real one would report
    /// a divergence over an axis nothing could converge.
    Q_INVOKABLE int divergentPathCount(const QString& primaryPath, const QStringList& rawMirrorPaths,
                                       bool compareCurve) const;

    /// Scoped sibling of revertPending: restore ONLY the snapshotted override
    /// files at @p eventPaths from their pre-edit content, leaving every other
    /// page's staged file edits (and any preset / motion-set snapshots) pending.
    /// Refuses (returns false) while an async discard owns the snapshot map. The
    /// caller reverts the shader tree for the same paths separately (the tree is
    /// Settings-owned; see the revertPending() caller contract). @return true
    /// when every in-scope snapshot restored (or there were none).
    bool revertPendingUnder(const QStringList& eventPaths);

    /// True iff any of @p eventPaths carries a staged (snapshotted) override-file
    /// edit — the file half of a per-page dirty check. The shader-tree half is a
    /// value comparison the caller runs against committedShaderProfileTree().
    bool hasScopedPendingFiles(const QStringList& eventPaths) const;

    /// Library of user-saved Profile presets. Each entry is a Profile JSON
    /// (`curve`, `duration`, `name`, …) sitting in the same `profiles/`
    /// dir as overrides — distinguished by the `name` field NOT matching
    /// any `ProfilePaths::` constant. Entries with a `curve` starting
    /// with `"spring:"` are spring presets; everything else is easing.
    Q_INVOKABLE QVariantList userPresets() const;

    /// Save @p profileJson under @p name as a user preset. Rejects names
    /// that collide with a built-in `ProfilePaths::` event path so a
    /// preset can't accidentally shadow an override slot. @return true
    /// on a successful disk write. Emits `userPresetsChanged()`.
    Q_INVOKABLE bool addUserPreset(const QString& name, const QVariantMap& profileJson);

    /// Delete the user preset whose `name` field matches @p name. Will
    /// never delete an override file even when its `name` field happens
    /// to match. @return true on a successful delete. Emits
    /// `userPresetsChanged()`.
    Q_INVOKABLE bool removeUserPreset(const QString& name);

    // ── Motion sets ──────────────────────────────────────────────────

    /// The motion-set store — the `bridge` ShaderSetsPage binds to.
    /// Motion sets snapshot the per-event override FILES under
    /// `~/.local/share/plasmazones/motionsets/<slug>.json`. Applying
    /// merges: overrides at paths NOT in the set are preserved. Writes ride
    /// this controller's `setOverride`, so each one snapshots pre-edit
    /// content and Discard restores it. The domain closures live in
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
    /// previewPath / parameters (QVariantList of ParameterInfo maps).
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
    Q_INVOKABLE bool setShaderOverride(const QString& path, const QString& effectId, const QVariantMap& parameters);

    /// Remove the shader override at @p path; ancestors take over via
    /// `ShaderProfileTree::resolve` walk-up. Emits
    /// `pendingChangesChanged()` whenever the call actually changed
    /// state.
    Q_INVOKABLE bool clearShaderOverride(const QString& path);

    /// Count of shader overrides on paths strictly DEEPER than @p path
    /// (i.e. paths whose first component up to a `.` matches @p path).
    /// Used by parent-node cards to surface "N deeper overrides shadow
    /// this parent" — without it, a stale leaf set in a previous
    /// session silently wins the deeper-leaf-overlay merge inside
    /// `ShaderProfileTree::resolve` and the parent's value never
    /// reaches runtime even though the UI control shows it set.
    Q_INVOKABLE int shaderOverrideDescendantCount(const QString& path) const;

    /// Clear every shader override whose path is strictly DEEPER than
    /// @p path (i.e. paths starting with `<path>.`). Does NOT clear
    /// the override at @p path itself. Returns the number of cleared
    /// entries (0 = nothing to clear), or -1 when refused while an
    /// async discard owns the tree (the page toasts the reason).
    /// Persists the batch via a single `setShaderProfileTree`
    /// write, which fires `shaderProfileTreeChanged` once and (via the
    /// constructor's broadcast lambda) one path-agnostic
    /// `shaderProfileChanged()` signal — NOT one per cleared path.
    /// Also emits `pendingChangesChanged()`. Used by parent-node
    /// cards' "Clear shadowing children" affordance.
    Q_INVOKABLE int clearShaderOverrideDescendants(const QString& path);

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

    /// Injected by the composition root: brings the process's profile
    /// registry back in line with this controller's directory
    /// SYNCHRONOUSLY. Run on every path that can REMOVE an override file,
    /// ahead of the matching `overrideChanged`, once per batch.
    ///
    /// The registry is fed by a `ProfileLoader` watching the same
    /// directory through a 50 ms debounce. The registry itself does catch
    /// up on its own once that debounce lands; what does not is the PAGE,
    /// whose only re-read happens synchronously inside the
    /// `overrideChanged` handler. Without this it samples the pre-delete
    /// state and never re-samples, which is the per-field revert
    /// link that appeared to do nothing until the app was reopened.
    ///
    /// Removals only. A write is already covered by `resolvedProfile`
    /// reading the user files ahead of the registry, and writes arrive at
    /// slider rate during a duration drag, where a synchronous directory
    /// rescan per tick would be far too expensive. The batch revert paths
    /// run it whenever their batch MAY have removed a file rather than
    /// tracking removals individually: a pure-write batch pays one
    /// redundant rescan per click, which is not worth threading a
    /// per-file "was deleted" flag through the discard worker to avoid.
    ///
    /// Unset (the default, and every unit test) leaves the controller
    /// purely file-backed, which is already self-consistent.
    ///
    /// Deliberately one-sided: no teardown path clears it. Safety rests on
    /// the QPointer the composition root captures inside the callable, which
    /// logs and no-ops once the loader is gone rather than degrading
    /// silently.
    void setProfileStoreRefresher(std::function<void()> refresher);

    /// Forget the cached contents of every user override file.
    ///
    /// `resolvedProfile` reads those files off disk and memoises the result
    /// (see `cachedDiskProfile`), and every mutation this controller performs
    /// drops the cache itself. This is the entry point for the OTHER writer:
    /// the profiles directory is a filesystem boundary a user can edit by hand,
    /// and the page even offers to open it. The composition root connects
    /// `ProfileLoader::profilesChanged` here so a hand-edited file is picked up
    /// on the next read instead of serving the pre-edit value for the rest of
    /// the session. Emits `overrideChanged(QString())` — the tree-wide reload
    /// broadcast — so an OPEN page re-reads too, rather than only the next one.
    ///
    /// The broadcast is suppressed while this controller's own
    /// `refreshProfileStore()` is on the stack, because the loader's rescan
    /// re-enters here synchronously and the caller is already about to emit the
    /// precise per-path signals. External writers are the case this exists for.
    ///
    /// One consequence of that suppression is worth knowing. `rescanNow()`
    /// cancels the loader's pending debounce, so an external edit landing
    /// inside the debounce window of one of our own writes is folded into the
    /// same rescan and loses its broadcast. It never loses its invalidation
    /// (the memo drop below happens before the early return), so no stale value
    /// is ever served. An already-open card at an unrelated path just stays
    /// visually stale until something else rebinds it.
    ///
    /// Cheap and idempotent: worst case the next read re-parses a few hundred
    /// bytes per level.
    void forgetCachedOverrideFiles();

Q_SIGNALS:
    /// Emitted on any successful set/clearOverride, and — with an EMPTY path —
    /// from `forgetCachedOverrideFiles` as a tree-wide reload broadcast when
    /// somebody outside this controller wrote to the profiles directory. A
    /// consumer must therefore treat an empty path as "re-read everything"
    /// rather than indexing into it. Otherwise @p path is the
    /// affected event path.
    void overrideChanged(const QString& path);

    /// Emitted on any successful add/removeUserPreset.
    void userPresetsChanged();

    /// Emitted on set/clearShaderOverride AND on any full settings
    /// reload (`ISettings::shaderProfileTreeChanged`). The path-agnostic
    /// emission on reload is the cheapest way to refresh every visible
    /// event card after Discard / Settings::load() — diffing the tree
    /// would be more expensive than just rebinding.
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
    // Animation edits write to disk immediately for live preview, but we
    // still want the standard "Discard" button to revert this session's
    // changes. The controller keeps a per-file snapshot of pre-edit
    // content (or "did not exist" sentinel); commit clears it, revert
    // restores files from it. Kept in its own block as the dedicated
    // SettingsController integration surface.

    /// True iff there are unsaved changes the user could still discard.
    bool hasPendingChanges() const;

    /// Forget the snapshot — every change so far is now "saved." Called from
    /// apply(); SettingsController::save() deliberately does NOT call it (that
    /// would double-dispatch, see settingscontroller_lifecycle.cpp).
    void commitPending();

    /// Re-evaluate the value-based dirty state after an external commit point the
    /// controller cannot observe on its own — chiefly Settings::save()'s
    /// captureBaseline, which advances committedShaderProfileTree() without moving
    /// the live tree (so no shaderProfileTreeChanged fires). SettingsController::
    /// save() calls this right after m_settings.save(); the dirtyChanged forwarder
    /// gates on an actual flip so a no-op refresh is free.
    void refreshDirtyState();

    /// Restore every file in the snapshot to its pre-edit state and
    /// clear the snapshot. Called from `SettingsController::load()`
    /// (Discard). Emits `overrideChanged`/`userPresetsChanged`/
    /// `pendingChangesChanged` and refreshes the set store through
    /// `ShaderSetStore::notifyLiveStateChanged()` so QML refreshes.
    /// (`shaderProfileChanged` arrives separately, from the caller's
    /// own `Settings::load()`.)
    /// Failures (e.g. permission errors during file restore) are
    /// retained in the snapshot so a subsequent revert can retry.
    /// @return true when every snapshotted FILE was restored. This is NOT "the
    /// page is clean": the shader TREE half of the dirty state is the caller's
    /// job (see the CALLER CONTRACT block above), so a page whose only remaining
    /// dirtiness is a diverged tree returns true here while `hasPendingChanges()`
    /// is still true. False has two causes, and a caller that goes on to declare
    /// the state clean (an import, a defaults reset) must honour both, or it
    /// strands pre-edit snapshots that a later Discard writes back over the new
    /// state:
    ///   * the revert was REFUSED outright, because an asyncRevertPending() worker
    ///     holds the snapshot map, or
    ///   * some file could not be restored (permission drift) and was RETAINED for
    ///     a retry.
    /// The Discard path itself may ignore the result: ApplicationController's
    /// discardAllAsync dispatches this page's async revert before the settings
    /// domain calls load(), so hitting the guard there means the async worker
    /// already owns the restore.
    bool revertPending();

    /// Async sibling of revertPending — runs the QSaveFile restore
    /// loop on a QtConcurrent worker so a Discard with dozens of
    /// snapshotted profile paths doesn't stall the GUI thread for
    /// dozens of disk round-trips. Emits the inherited
    /// `discardResult(ok, error)` signal on completion (back on the
    /// GUI thread) so a future chrome footer can surface
    /// "discarding..." state. Same retain-on-failure semantics as
    /// revertPending — partial failures stay in m_pendingFileSnapshots
    /// for a subsequent retry.
    Q_INVOKABLE void asyncRevertPending();

    /// True while an asyncRevertPending() worker owns the snapshot map. A caller
    /// that gets false from revertPending() uses this to tell the two causes
    /// apart: the worker is mid-restore and will finish the job (and re-dirty the
    /// page itself if it has to retain a file), versus a restore that actually
    /// failed and left the page dirty.
    bool asyncRevertInFlight() const;

private:
    /// Flip-gated emitter for stockSuppressedEventsChanged: recomputes the
    /// list and emits only when it actually changed. All three gate inputs
    /// (registry rescan, tree assignment, animations master toggle) route
    /// through here so tree edits that cannot affect the suppression set
    /// stop re-running the conflict-chip bindings.
    void maybeEmitStockSuppressedEventsChanged();

    QString userProfilesDir() const;
    QString userMotionSetsDir() const;
    QString profileFilePath(const QString& path) const;

    /// Capture @p filePath's current content into the snapshot if not
    /// already snapshotted. Called by every file-mutating method just
    /// before the write/delete so revert can put it back.
    /// @return @c true if the snapshot is in place after the call (or
    /// was already there); @c false if the file exists but couldn't be
    /// read for snapshot capture (e.g. mid-session permission drift).
    /// Callers that proceed with a write on a @c false return will
    /// permanently lose pre-edit content — the controller's direct
    /// callers (setOverride / clearOverride) bail in that case.
    bool snapshotFileIfFirst(const QString& filePath);
    /// Whether a snapshot drop announces the dirty-state flip it causes.
    /// `Defer` is for a caller mutating several files in a row: an
    /// intermediate flip is not the batch's outcome, and announcing it
    /// leaves the page believing whatever the LAST intermediate state
    /// happened to be. Such a caller owns one emit for the net flip.
    enum class SnapshotDropSignal {
        Emit,
        Defer
    };

    /// Undo a snapshotFileIfFirst() staging when the write it was taken for
    /// never landed. No-op unless the file on disk still matches the staged
    /// content exactly, so a snapshot guarding an earlier successful edit is
    /// left alone. Handed to the sub-services as a callable.
    /// @return true when the entry was dropped (and, under
    /// `SnapshotDropSignal::Emit`, pendingChangesChanged was emitted if that
    /// flipped hasPendingChanges()).
    bool dropFileSnapshotIfUnchanged(const QString& filePath,
                                     SnapshotDropSignal signalPolicy = SnapshotDropSignal::Emit);

    /// Run the injected profile-store refresher, if any. No-op otherwise.
    /// Not const: it exists to mutate process-global state (a directory
    /// rescan that rewrites the registry partition and fans out signals).
    void refreshProfileStore();

    /// Outcome of removeOverrideFile(). `Absent` is not a failure: the
    /// desired end state (no override file at this path) already holds,
    /// which is what the pre-split code reported by re-testing existence
    /// after a false return.
    enum class OverrideFileRemoval {
        Removed,
        Absent,
        Failed
    };

    /// The file half of clearOverride: snapshot, delete, settle the staged
    /// entry. Emits NOTHING — the snapshot drop defers its dirty-state
    /// signal to the caller — and does not refresh the profile store.
    /// The caller owns the in-flight gate (`m_asyncRevertInFlight`), the
    /// path validity gate, the refresh, and every signal.
    ///
    /// Split out so the bulk clears can delete every file, refresh ONCE,
    /// and then emit — the batch shape revertPending already uses. Going
    /// through clearOverride in a loop instead would rescan (and re-parse)
    /// the whole profiles directory once per deleted file.
    OverrideFileRemoval removeOverrideFile(const QString& path);

    enum class OverrideFileWrite {
        Written,
        Unchanged,
        Failed
    };

    /// The file half of setOverride: guard, snapshot, write, settle the staged
    /// entry. Emits NOTHING and does NOT invalidate the disk memo — the caller
    /// owns the invalidate and every signal. Split out so the group writer can
    /// write every path, invalidate the memo ONCE, then emit — otherwise each
    /// per-path setOverride dropped the whole memo and forced every sibling
    /// card to re-walk resolvedProfile uncached, at drag rate. setOverride is a
    /// thin wrapper: this core, then invalidate + drop + emit for the one path.
    OverrideFileWrite writeOverrideFileOnly(const QString& path, const QVariantMap& profileJson);

    /// Both boundary checks on a shader effect id: the length/character sanity
    /// check and the registry-membership gate. Shared by `setShaderOverride` and
    /// the group writer `setShaderOverrideOnPaths`, because the group writer is
    /// the only path QML uses and had silently inherited neither.
    /// @param context names the caller in the diagnostics.
    bool acceptableShaderEffectId(const QString& effectId, QLatin1String context) const;

    /// Shared body of the two shader-leg group SETTERS
    /// (setShaderOverrideOnPaths and setShaderParametersOnPaths). Everything
    /// those two have in common lives here: the dedup, the async-discard
    /// refusal and its toast, the per-path validity and shader-leg skip, the
    /// compare-and-skip against what is already stored, the write-once
    /// epilogue, and the counting rules. What differs is only how each builds
    /// the profile it wants at a path, which is what @p build supplies.
    ///
    /// The two policies were 40 near-identical lines apart, and the pair has to
    /// stay in step: they share a return contract, a refusal sentinel and a
    /// skip rule, and a fix applied to one of them silently not applying to the
    /// other is the failure this removes.
    ///
    /// @param preflight optional per-call validation, run AFTER the refusal
    /// gates so those keep precedence. Returning false yields -1. It belongs
    /// here rather than at the caller because the order is observable: an
    /// invalid input during an async discard must still toast the discard, and
    /// one with no ISettings must still report 0.
    /// @param build receives the profile currently stored at the path and
    /// whether there was one at all, and returns the profile to store.
    /// Returning std::nullopt means "store nothing here": any existing entry is
    /// REMOVED rather than replaced with an empty profile, which is what keeps
    /// a no-op entry out of the tree for the pruner, the diff and the
    /// ancestor's shadowing walk to trip over.
    /// @param context names the caller in the refusal diagnostic.
    /// @return the number of paths holding the requested end state, or -1 when
    /// an async discard owns the tree.
    int applyShaderGroupWrite(const QStringList& rawPaths, QLatin1String context,
                              const std::function<bool()>& preflight,
                              const std::function<std::optional<PhosphorAnimationShaders::ShaderProfile>(
                                  const PhosphorAnimationShaders::ShaderProfile& stored, bool hasStored)>& build);

    /// Shared body of clearAllOverrides / clearOverridesUnder: clears every
    /// override file among @p eventPaths, refreshes the profile store once,
    /// then emits. @p context names the caller for the failure log.
    ///
    /// The caller owns the `m_asyncRevertInFlight` gate and its refusal
    /// toast, exactly as it does for removeOverrideFile above.
    /// @return the number cleared, or -1 if any removal failed.
    int clearOverridesForPaths(const QStringList& eventPaths, QLatin1String context);

    /// Sanitised contents of the user override file at @p path, cached.
    ///
    /// `resolvedProfile` walks up to four ancestor levels and reads each
    /// level's override file off disk. It backs a QML binding that every card
    /// in scope re-evaluates on each `overrideChanged`, and `setOverride` emits
    /// one of those per write path per slider tick, so without a cache a
    /// duration drag costs (visible cards x chain depth) synchronous file opens
    /// and JSON parses per tick on the GUI thread. One read per path per
    /// mutation instead.
    ///
    /// Empty map for "no override file here", which is also what a file that
    /// fails every validity check normalises to — both mean the same thing to
    /// the caller (fall through to the registry), so they share a cache slot.
    QVariantMap cachedDiskProfile(const QString& path) const;

    /// Drop every `cachedDiskProfile` entry. Called from each path that writes,
    /// deletes, or re-reads override files: `setOverride`, `removeOverrideFile`
    /// (and so, transitively, every batch clear), `refreshProfileStore` (and so
    /// every revert), the profiles-directory test override, and the public
    /// `forgetCachedOverrideFiles` for external edits. Clearing wholesale
    /// rather than per path is deliberate: the map holds at most one small
    /// entry per event path, and a partial invalidation is one missed call site
    /// away from serving a stale inherited value, which is the exact class of
    /// bug this controller's disk-first read exists to fix.
    void invalidateDiskProfileCache() const;

    /// Non-zero while this controller's own `refreshProfileStore()` is running.
    /// The loader's `rescanNow()` emits `profilesChanged` synchronously on that
    /// stack, and `forgetCachedOverrideFiles` is wired to it — so without this
    /// the controller would answer its own rescan with a tree-wide reload
    /// broadcast, on top of the precise per-path signals the caller is already
    /// about to send. A counter rather than a bool: no call site nests a
    /// refresh inside another today (every call site is a single top-level call), but
    /// a counter cannot be left stuck by a nesting one added later, and it
    /// costs nothing over a bool.
    int m_selfDrivenRescanDepth = 0;

    /// Event path -> sanitised override-file contents. See `cachedDiskProfile`.
    /// Mutated only from the GUI thread, like `m_pendingFileSnapshots`. It is
    /// `mutable` and written from a `const` accessor, so that contract is worth
    /// stating: the async discard worker must never reach it.
    mutable QHash<QString, QVariantMap> m_diskProfileCache;

    PhosphorAnimationShaders::AnimationShaderRegistry* m_shaderRegistry = nullptr;
    ISettings* m_settings = nullptr;
    QString m_userProfilesDirOverride; ///< Empty = use XDG default
    std::function<void()> m_profileStoreRefresher; ///< See setProfileStoreRefresher

    // Sub-services owned via QObject-parent: both are constructed with
    // `this` as parent so ~AnimationsPageController tears them down
    // automatically. No manual delete; no QPointer needed.
    AnimationPresetLibrary* m_presets = nullptr;
    ShaderSetStore* m_motionSets = nullptr;

    /// Pre-edit file contents keyed by absolute path. `std::nullopt`
    /// means "the file did not exist before this session." Mutated only
    /// from the GUI thread. Shader-tree edits don't go through this
    /// snapshot — they ride the standard Settings::load() Q_PROPERTY
    /// re-emit path like every other settings page. During a worker
    /// run, two copies exist briefly (live + captured): the worker
    /// owns a value-captured snapshot, while this member continues to
    /// reflect GUI-thread state. Never alias them by reference —
    /// merging is done by key set in the worker's finished handler.
    QHash<QString, std::optional<QByteArray>> m_pendingFileSnapshots;
    /// In-flight guard for asyncRevertPending — set true on dispatch
    /// and cleared in the QFutureWatcher::finished handler. A second
    /// asyncRevertPending invocation while a worker is running would
    /// run on stale captured state AND the second worker would
    /// overwrite the first's retained map, producing inconsistent
    /// disk state. Mirrors ApplicationController::m_applying.
    bool m_asyncRevertInFlight = false;

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
