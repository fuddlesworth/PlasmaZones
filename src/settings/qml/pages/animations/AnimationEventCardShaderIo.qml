// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief The card's shader-axis read and parameter-write bodies.
 *
 * Split out of AnimationEventCard for the same reason AnimationEventCardWriters
 * was: that file is over the project's size ceiling, and this is a coherent
 * slice rather than an arbitrary cut. Everything here concerns ONE axis (the
 * shader) and ONE direction pair — read the resolved state back out of the
 * tree, and write parameter values into it. The timing axis, the group writers
 * and the card's own view state stay where they are.
 *
 * The card keeps a thin forwarder for every function below, so `root._foo(...)`
 * reads the same at every call site: handlers, bindings, and the QML-contract
 * scrape that pins the promotion and latch branches by source text.
 *
 * Note the asymmetry between the two parameter writers. It is deliberate, and
 * it is the thing most likely to be "tidied" into a bug. `_writeShaderParam` is
 * drag-rate, so a refused write is dropped WITHOUT restoring the control:
 * refreshing on every refused tick would drag the slider handle out from under
 * the user. `_writeAllShaderParams` is discrete (randomize, reset), so a
 * refused write DOES restore, because there is no later tick to correct it and
 * the editor has already staged its map onto the card.
 */
QtObject {
    id: shaderIo

    /// The AnimationEventCard these bodies belong to.
    required property var card

    // `effectId` is explicit so callers can snapshot it at user-action
    // time (e.g. when the color dialog opens) rather than reading
    // `card.currentShaderEffectId` at write time. Without that snapshot,
    // a registry refresh that fires while the dialog is open could
    // silently retarget the write at a different effect's param map.
    function _writeShaderParam(effectId, paramId, value) {
        // Refused writes are dropped WITHOUT restoring the control, and that
        // asymmetry with `_writeAllShaderParams` below is deliberate. This path
        // is drag-rate — a parameter slider emits per pointer move — so
        // refreshing here would reassign `currentShaderParams` from the tree on
        // every refused tick and drag the handle back out from under the user.
        // A stale-looking slider for the length of a discard is the better of
        // the two, and the drag's last value is what the first accepted write
        // commits.
        if (!effectId || card._writesRefused)
            return;

        // Bail if the user navigated to a different effect while the
        // dialog (color picker / etc.) was open. The write below carries no
        // effect id, so a late accept can no longer retarget the event at the
        // OLD pack — what it would do instead is land a parameter authored for
        // that pack in the map of the one now showing, where the id means
        // something else or nothing at all. Better to drop the late accept than
        // to write it into state the user explicitly changed.
        if (effectId !== card.currentShaderEffectId)
            return;

        var next = Object.assign({}, card.currentShaderParams || {});
        next[paramId] = value;
        card.currentShaderParams = next;
        // Params-only: never restate the pack. `effectId` above is the RESOLVED
        // id, so on a leaf that inherits its pack, writing it would pin that
        // pack here and sever the cascade. It is still read for the stale-effect
        // guard above, which is the only thing it is good for on this path.
        card._noteWriteResult(card._setShaderParamsOnAll(next));
    }

    /// Whether EVERY write path is already showing @p defaults and owns no
    /// parameters of its own — i.e. whether a Reset would change nothing that
    /// anyone can see, anywhere in the group.
    ///
    /// Asked of the GROUP, not the primary, because the write it gates is a
    /// group write. Deciding from the primary alone would let a Reset skip
    /// while a mirror sat on tuned values, leaving the group split behind a
    /// banner that promises the next change reaches all of it. The sibling
    /// no-op guard in `onShaderEffectActivated` refuses the same shortcut for
    /// the same reason.
    ///
    /// Two tree reads per path, on a discrete button click over a group that is
    /// two paths today. Deliberately not on the drag path.
    function _groupIsAtPackDefaults(defaults) {
        const paths = card._writePaths;
        for (var i = 0; i < paths.length; ++i) {
            const raw = settingsController.animationsPage.rawShaderProfile(paths[i]);
            const own = raw && raw.parameters;
            if (own && Object.keys(own).length > 0)
                return false;
            const resolved = settingsController.animationsPage.resolvedShaderProfile(paths[i]);
            const inherited = (resolved && resolved.parameters) || ({});
            // Overlaid rather than compared directly: parameters reach a resolve
            // only from a stored override, so a path inheriting nothing resolves
            // an EMPTY map while its rows already render the pack's defaults.
            if (!shaderIo._sameParamValues(defaults, Object.assign({}, defaults, inherited)))
                return false;
        }
        return true;
    }

    /// Flat value-equality over two parameter maps. Parameter values are
    /// scalars (numbers, bools, strings, colors), so a key-wise `!==` is the
    /// whole comparison; there is no nested structure to recurse into.
    ///
    /// Strict `!==` means two spellings of the same COLOUR compare unequal — a
    /// pack's schema default is typically `#RRGGBB` while the picker persists
    /// `#AARRGGBB`. That direction is safe: the comparison degrades to "not
    /// equal", and every caller treats not-equal as "write", which is the
    /// conservative arm. Do not add normalisation without re-checking who
    /// depends on the strictness.
    function _sameParamValues(a, b) {
        const left = a || {};
        const right = b || {};
        const leftKeys = Object.keys(left);
        if (leftKeys.length !== Object.keys(right).length)
            return false;
        for (var i = 0; i < leftKeys.length; ++i) {
            const key = leftKeys[i];
            if (!right.hasOwnProperty(key) || left[key] !== right[key])
                return false;
        }
        return true;
    }

    /// Batch write — randomize and reset roll N values that should land as one
    /// `setShaderParametersOnPaths` round-trip rather than N of them.
    ///
    /// The `effectId` check here is a non-empty test in practice, NOT the
    /// stale-effect guard `_writeShaderParam` carries. Both call sites pass
    /// `card.currentShaderEffectId` itself and the editor computes and re-emits
    /// the map synchronously in the same handler, so there is no asynchronous
    /// gap for the id to go stale across. It is kept because the empty case is
    /// real (no pack resolved, nothing to write) and because a future payload
    /// that DOES carry a snapshotted id should find the comparison already
    /// here rather than have to reintroduce it.
    function _writeAllShaderParams(effectId, allParams) {
        if (!effectId || effectId !== card.currentShaderEffectId)
            return;

        // Refused: same reasoning as `_writeShaderParam`, and worse here.
        // Randomize and Reset stage their whole map onto the editor before
        // emitting, so dropping the write silently leaves every parameter row
        // showing a value that was never persisted. Discrete actions have no
        // next tick to correct them either.
        if (card._writesRefused) {
            card.refreshShaderFromTree();
            return;
        }

        // `currentShaderParams` is deliberately NOT re-staged here. It is an
        // alias onto the editor's own `shaderParams`, and the editor assigns
        // the rolled or default map to that property before emitting, so
        // assigning it again would be writing the value it already holds.
        // Params-only, for the reason _writeShaderParam gives.
        card._noteWriteResult(card._setShaderParamsOnAll(allParams));
    }

    function refreshShaderFromTree() {
        var resolved = settingsController.animationsPage.resolvedShaderProfile(card.eventPath);
        var nextEffectId = (resolved && resolved.effectId) ? resolved.effectId : "";
        // Stale-lock clear on effect switch — same-named ids in
        // different shaders are unrelated.
        if (nextEffectId !== card.currentShaderEffectId)
            card.lockedShaderParams = ({});

        card.currentShaderEffectId = nextEffectId;
        card.currentShaderParams = (resolved && resolved.parameters) ? resolved.parameters : ({});
        // Computed HERE, not in refreshFromTree, because it reads the id
        // assigned on the line above. Component.onCompleted runs refreshFromTree
        // FIRST and this second, so computing it there would evaluate it against
        // an empty id on the card's first frame and never revisit it — a card
        // that owns a pack would render the group-ambiguous remove label until
        // something unrelated triggered another timing refresh.
        // Gated, because `_allWritePathsHold` rebuilds the whole shader tree —
        // the same non-cheap read this function's comment below warns about,
        // and this runs at drag rate for every visible card. Its only consumer
        // is the remove control's label, which is unreachable unless some write
        // path owns a pack, so the common case (an event inheriting, or with no
        // pack at all) skips it entirely. `_anyWritePathOwnsShaderPack` is
        // assigned by refreshFromTree, and every caller that moves the shader
        // tree runs this function BEFORE that one, so the value read here is
        // the previous refresh's. That is deliberate and safe, but be precise
        // about how long it lasts: the two flags converge on the next SHADER
        // WRITE, not on the next refresh of any kind. Every shader writer runs
        // this function before the timing one, and the broadcast that write
        // emits is swallowed by the card's own commit latch, so nothing
        // recomputes in between. After adopting or promoting a pack the remove
        // control therefore reads "Remove the shader pack" rather than naming
        // it, until the next shader-tree edit. It only ever shows the
        // CONSERVATIVE label, never a wrong name, which is why it is left
        // alone rather than paid for with another tree read on the drag path.
        card._allWritePathsHoldShownPack = nextEffectId.length > 0 && card._anyWritePathOwnsShaderPack && card._allWritePathsHold(nextEffectId);
        // Recompute deeper-override count on every shader-tree update —
        // the warning banner below depends on it. Only meaningful for
        // parent-node cards but we always refresh so the binding stays
        // consistent.
        //
        // Summed across every write path to match
        // _clearShaderOverrideDescendantsOnAll, whose button clears the
        // mirrors' descendants too. No card today is both a parent node and
        // mirrored, so the mirror legs never contribute in the current config.
        // The sum is kept so a future mirrored parent card reports what the
        // button would actually remove.
        //
        // ONE controller call, not one per path. The per-path Q_INVOKABLE is
        // NOT cheap: it rebuilds the whole shader tree every time (a settings
        // read, a JSON parse and a prune walk, because `rawShaderProfile` is
        // not memoised), so looping it here paid that cost once per write path
        // inside a function that runs at drag rate for every visible card.
        // That is the read-in-a-loop shape the controller's own Group-writes
        // block calls out.
        card._shadowingChildrenCount = settingsController.animationsPage.shaderOverrideDescendantCountForPaths(card._writePaths);
        // Divergence is deliberately NOT recomputed here. refreshFromTree owns
        // it, and every call site of this function calls refreshFromTree
        // alongside it (the three shader group writers' finally blocks and
        // onShaderProfileChanged call it immediately after; Component.onCompleted
        // runs refreshFromTree FIRST and this function second, which is
        // equally fine because refreshFromTree recomputes the divergence
        // itself from live reads, so the order between the two is moot).
        //
        // A future caller that runs this ALONE must call refreshFromTree too
        // or the banner goes stale.
    }
}
