// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configdefaults.h"
#include "configmigration_util.h"

#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorRules/ActionParams.h>
#include <PhosphorRules/ActionTypes.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1String>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace PlasmaZones {

namespace {
// The two per-layout sidecar keys being relocated (the layout-file spellings
// that ZoneJsonKeys used to declare — pinned here because the runtime keys
// are gone) and the OverlayShaderTree JSON field names they land in.
constexpr QLatin1String kSidecarShaderId{"shaderId"};
constexpr QLatin1String kSidecarShaderParams{"shaderParams"};
constexpr QLatin1String kTreeBaseline{"baseline"};
constexpr QLatin1String kTreeOverrides{"overrides"};
constexpr QLatin1String kNodeShaderId{"shaderId"};
constexpr QLatin1String kNodeParameters{"parameters"};
// Records which layout ids this migration has already merged, so a key the
// user REMOVED after a failed sidecar strip cannot be resurrected from the
// stale sidecar on the retry. It is a LIST rather than a bool because the
// strip below is unconditional while the merge is not: a shaderId that reaches
// the sidecar later (a restored backup, a layout file copied from another
// machine) must still be lifted, and only the ids recorded here are ones the
// user could have deleted after a lift.
//
// It lives at the config ROOT, not in the Overlays group, and that placement is
// load-bearing rather than cosmetic. Settings::save() runs purgeStaleKeys,
// whose first pass deletes every undeclared scalar leaf inside a schema-declared
// group — and Overlays IS declared, with OverlayShaderTree as its only declared
// key. A marker inside that group is therefore deleted on the first save, which
// would silently re-arm the resurrection this guard exists to prevent. At the
// root it is invisible to that pass (which visits only schema groups and their
// ancestors) and to the second pass (whose enumerator appends only object-valued
// paths), exactly as the _v4* stashes are. reset() deletes named groups only, so
// it also survives a factory reset — which is correct: the sidecar has already
// been stripped by then, so there is nothing to re-merge into a config the user
// just cleared.
QString liftedMarkerKey()
{
    return ConfigKeys::Legacy::v8SidecarLiftedKey();
}

/// The set of layout ids already merged by a previous run, read from the root.
QSet<QString> liftedMarkerIds(const QJsonObject& root)
{
    QSet<QString> ids;
    const QJsonArray arr = root.value(liftedMarkerKey()).toArray();
    for (const QJsonValue& v : arr) {
        const QString id = v.toString();
        if (!id.isEmpty()) {
            ids.insert(id);
        }
    }
    return ids;
}

/// Remove the two relocated shader keys from every object-valued sidecar
/// entry, dropping entries left empty. Returns true when anything changed.
bool stripShaderKeys(QJsonObject& sidecar)
{
    bool dirty = false;
    const QJsonObject snapshot = sidecar;
    for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        QJsonObject entry = it.value().toObject();
        if (!entry.contains(kSidecarShaderId) && !entry.contains(kSidecarShaderParams)) {
            continue;
        }
        entry.remove(kSidecarShaderId);
        entry.remove(kSidecarShaderParams);
        if (entry.isEmpty()) {
            sidecar.remove(it.key());
        } else {
            sidecar.insert(it.key(), entry);
        }
        dirty = true;
    }
    return dirty;
}
/// The rules half of the overlay relocation: rewrite every OverrideOverlayShader
/// action written against the old shape onto the tree's node shape.
///
/// Before v8 the overlay shader was a property of the layout, and the rule
/// action overrode that property for whatever layout the matched context
/// resolved to. It carried only the shader. In v8 the shader is a node of the
/// OverlayShaderTree, a global default plus per-layout overrides, and the
/// action names the node it overrides in `layoutId` the way the animation
/// action names its event. The faithful translation of an old rule is the
/// GLOBAL node: "this context, every layout" is exactly what "this context,
/// whichever layout is active" meant, so the rewrite stamps `layoutId: ""`
/// and touches nothing else. Assigning the layout that happened to be active
/// at migration time instead would narrow the rule to that one layout and
/// silently stop it applying after the user's next layout switch.
///
/// The rewrite is what makes the migration a migration rather than a
/// tolerated legacy: after it runs, no rule on disk is in the old shape, the
/// descriptor's "absent layoutId means the global node" reading is never
/// exercised by stored data, and a rule file is exactly what the editor would
/// have written. Idempotent by shape (a second run finds nothing lacking the
/// key and writes nothing) and crash-safe (withRuleSet saves atomically or
/// not at all), so it rides the finalize pass on every start like the sidecar
/// lift beside it.
bool relocateOverlayShaderRulesToNodes(const QString& jsonPath)
{
    return withRuleSet(jsonPath, "relocateOverlayShaderRulesToNodes", [](PhosphorRules::RuleSet& ruleSet) {
        bool changed = false;
        // A copy: updateRule replaces entries in the set being walked.
        const QList<PhosphorRules::Rule> rules = ruleSet.rules();
        for (PhosphorRules::Rule rule : rules) {
            bool ruleChanged = false;
            for (PhosphorRules::RuleAction& action : rule.actions) {
                if (action.type != PhosphorRules::ActionType::OverrideOverlayShader
                    || action.params.contains(PhosphorRules::ActionParam::LayoutId)) {
                    continue;
                }
                action.params.insert(QString(PhosphorRules::ActionParam::LayoutId), QString());
                ruleChanged = true;
            }
            if (ruleChanged && ruleSet.updateRule(rule)) {
                changed = true;
            }
        }
        return changed;
    });
}

} // namespace

// v8's overlay-shader half moves zone-overlay shader assignments out of the
// layout-settings sidecar into the config's Overlays/OverlayShaderTree blob,
// and rewrites the rules that addressed the old per-layout property onto the
// tree's node shape (relocateOverlayShaderRulesToNodes above).
// There is no chain step for it: the config root carries nothing to transform,
// and the sidecar lift needs filesystem access and must NOT run on the sparse
// profile deltas the chain also processes (it would stamp the user's live
// assignments into every profile). So it lives here, invoked from
// ensureJsonConfig's finalize pass on every run, the same split as the v4
// layout-settings relocation (relocateLayoutSettings). The version stamp is
// migrateV7ToV8's alone.
bool ConfigMigration::relocateOverlayShaderAssignments(const QString& jsonPath)
{
    // The rules half first, and unconditionally: it does not depend on the
    // sidecar existing (a user can have overlay rules and no per-layout
    // settings at all), and the sidecar lift below must not be able to skip
    // it by returning early. Its own failure is reported through the combined
    // result, never allowed to block the lift.
    const bool rulesOk = relocateOverlayShaderRulesToNodes(jsonPath);

    const QString sidecarPath = ConfigDefaults::layoutSettingsFilePath();
    if (!QFile::exists(sidecarPath)) {
        return rulesOk; // nothing to relocate — fresh install or already clean
    }

    QJsonObject sidecar;
    {
        QFile sf(sidecarPath);
        if (!sf.open(QIODevice::ReadOnly)) {
            qWarning("ConfigMigration: overlay-shader relocation could not read %s — skipping",
                     qPrintable(sidecarPath));
            return true; // unreadable sidecar is the layout store's problem, not a migration failure
        }
        const QByteArray raw = sf.readAll();
        // Cheap steady-state bail: this runs on every startup forever, and
        // once the one-time lift is done the file never carries the shader
        // keys again — skip the JSON parse when the bytes cannot contain
        // them. (kSidecarShaderParams is not a substring of kSidecarShaderId
        // or vice versa, so both are checked.)
        if (!raw.contains(kSidecarShaderId.latin1()) && !raw.contains(kSidecarShaderParams.latin1())) {
            return true;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning("ConfigMigration: overlay-shader relocation skipping unparseable %s", qPrintable(sidecarPath));
            return true;
        }
        sidecar = doc.object();
    }

    // Collect the shader entries to lift. An entry with an empty shaderId is
    // stripped without lifting: it meant "no shader", which is the tree's
    // inherit/baseline default, and any orphaned shaderParams riding such an
    // entry are dropped by design (parameters are meaningless without a
    // shader). Non-UUID keys (the "autotile:<algoId>" entries the pre-v8
    // editor could stamp shader keys onto) are also stripped without lifting:
    // the tree's override paths are layout UUIDs only, so a lifted autotile
    // key could never be resolved and would sit in the config as junk.
    QJsonObject lifted; // uuid → {shaderId, parameters}
    for (auto it = sidecar.constBegin(); it != sidecar.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject entry = it.value().toObject();
        const QString shaderId = entry.value(kSidecarShaderId).toString();
        const QUuid layoutId = QUuid::fromString(it.key());
        if (shaderId.isEmpty() || layoutId.isNull()) {
            continue;
        }
        QJsonObject node;
        node.insert(kNodeShaderId, shaderId);
        const QJsonValue params = entry.value(kSidecarShaderParams);
        if (params.isObject() && !params.toObject().isEmpty()) {
            node.insert(kNodeParameters, params.toObject());
        }
        // Key on the canonical braced spelling rather than the sidecar's own.
        // QUuid::fromString accepts both forms, but every reader asks with
        // QUuid::toString(), so an unbraced key would sit in the tree
        // unresolvable. This also keeps the marker list, the already-present
        // check below and the tree itself on one spelling.
        lifted.insert(layoutId.toString(), node);
    }
    QJsonObject strippedSidecar = sidecar;
    const bool sidecarDirty = stripShaderKeys(strippedSidecar);

    if (!sidecarDirty) {
        // An OPTIMIZATION, not a correctness gate, and worth naming as such so
        // nobody reads it as one. `lifted` cannot be non-empty here (an entry
        // carrying a shaderId is exactly an entry stripShaderKeys finds), so
        // the fall-through would skip the config write, re-read the sidecar,
        // get false from the second strip and return true anyway. This bails
        // one file re-read earlier. It is reached whenever the raw-bytes scan
        // sees the key NAMES somewhere the parse then finds no key for, which
        // testLift_shaderKeyNamesAppearingOnlyAsValuesAreNotAStrip pins.
        return true;
    }

    // Lift into the config root FIRST: the config copy is the authoritative
    // destination, so it must be durably written before the sidecar loses
    // its entries. On a re-run after a sidecar write failure the merge below
    // keeps an already-lifted (possibly since-edited) node — existing tree
    // entries always win over the stale sidecar copy.
    // Nothing liftable (only empty-shaderId or autotile entries carried the
    // keys) skips the whole config write, so the marker is not stamped even
    // though the strip below still runs. That asymmetry is harmless: an empty
    // lift has nothing to resurrect, and once the strip lands the raw-bytes
    // bail at the top short-circuits every later run before it gets here.
    if (!lifted.isEmpty()) {
        if (!QFile::exists(jsonPath)) {
            // No config file yet (interrupted fresh install): leave the
            // sidecar untouched and retry once the config exists.
            return true;
        }
        QFile cf(jsonPath);
        if (!cf.open(QIODevice::ReadOnly)) {
            qWarning("ConfigMigration: overlay-shader relocation could not open %s", qPrintable(jsonPath));
            return false;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(cf.readAll(), &err);
        cf.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning("ConfigMigration: overlay-shader relocation: %s did not parse — aborting lift",
                     qPrintable(jsonPath));
            return false;
        }
        QJsonObject root = doc.object();
        QJsonObject group = groupObjectAtPath(root, ConfigKeys::overlaysGroup());
        // Each layout id is merged from the sidecar at most ONCE. On a retry
        // after a failed sidecar strip the user may have edited OR REMOVED an
        // assignment meanwhile; the config is authoritative, so an id this
        // migration already merged is never taken from the stale sidecar
        // again. An id NOT in the list has never been lifted on this version,
        // so merging it is correct even on a later run — that is what lets a
        // restored backup or a layout file copied in after the first start
        // still reach the tree instead of being stripped away silently.
        const QSet<QString> alreadyMerged = liftedMarkerIds(root);
        QJsonObject tree = group.value(ConfigKeys::overlayShaderTreeKey()).toObject();
        QJsonObject overrides = tree.value(kTreeOverrides).toObject();
        bool treeDirty = false;
        QSet<QString> mergedNow = alreadyMerged;
        for (auto it = lifted.constBegin(); it != lifted.constEnd(); ++it) {
            if (alreadyMerged.contains(it.key())) {
                continue; // merged by an earlier run — the user may have since removed it
            }
            mergedNow.insert(it.key());
            if (overrides.contains(it.key())) {
                continue; // already present (edited copy) — that copy is live
            }
            overrides.insert(it.key(), it.value());
            treeDirty = true;
        }
        // Rewrite only when something actually moved: either the tree gained an
        // override, or the marker list grew. Testing the marker's mere presence
        // here would rewrite config.json on every single startup.
        const bool markerDirty = mergedNow.size() != alreadyMerged.size();
        if (treeDirty || markerDirty) {
            if (treeDirty) {
                tree.insert(kTreeOverrides, overrides);
                group.insert(ConfigKeys::overlayShaderTreeKey(), tree);
                setGroupAtSegments(root, ConfigKeys::overlaysGroup().split(QLatin1Char('.')), group);
            }
            QStringList mergedIds(mergedNow.constBegin(), mergedNow.constEnd());
            mergedIds.sort(); // stable on disk, so an unchanged run rewrites nothing
            root.insert(liftedMarkerKey(), QJsonArray::fromStringList(mergedIds));
            if (!PhosphorConfig::JsonBackend::writeJsonAtomically(jsonPath, root)) {
                qWarning("ConfigMigration: failed to write lifted overlay shader tree to %s", qPrintable(jsonPath));
                return false;
            }
        }
    }

    // Strip the relocated keys from the sidecar. Re-read it FRESH here
    // rather than rewriting the entry-time snapshot: the daemon's runtime
    // LayoutSettingsStore rewrites this file without taking the migration
    // lock, so a snapshot rewrite could clobber a concurrent save (a
    // hiddenFromSelector or autotile toggle landing during this one-shot
    // lift). Stripping from a just-read copy preserves such writes; nothing
    // post-v8 writes shader keys, so re-stripping the fresh copy is safe.
    // A failure here retries on the next run; the existing-entry-wins merge
    // above keeps that safe.
    //
    // NOT pinned by a test, and it cannot be from outside: discriminating the
    // fresh read from the entry-time snapshot needs the sidecar to change
    // BETWEEN them, inside one synchronous call, which a test can only reach
    // through a seam this function does not have and should not grow. Replacing
    // this block with `strippedSidecar` therefore stays green while
    // reintroducing the clobber. Treat the block as load-bearing on the
    // strength of the reasoning above, not on the strength of the suite.
    {
        QFile sf(sidecarPath);
        if (!sf.open(QIODevice::ReadOnly)) {
            // Falling through would write the entry-time snapshot, which is the
            // clobber this fresh read exists to prevent. Retry on the next run.
            qWarning("ConfigMigration: overlay-shader relocation could not re-read %s — deferring strip",
                     qPrintable(sidecarPath));
            return false;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(sf.readAll(), &err);
        sf.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning("ConfigMigration: overlay-shader relocation: %s did not re-parse — deferring strip",
                     qPrintable(sidecarPath));
            return false;
        }
        QJsonObject fresh = doc.object();
        if (!stripShaderKeys(fresh)) {
            return true; // someone else already stripped it
        }
        strippedSidecar = fresh;
    }
    if (!PhosphorConfig::JsonBackend::writeJsonAtomically(sidecarPath, strippedSidecar)) {
        qWarning("ConfigMigration: failed to strip overlay shader keys from %s", qPrintable(sidecarPath));
        return false;
    }
    return true;
}

} // namespace PlasmaZones
