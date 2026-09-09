// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Overlay sets — the OverlaysPageController half of the shared ShaderSetStore.
//
// A set is a named snapshot of the zone-overlay shader assignments: the global
// default plus the per-layout overrides. All the file machinery (read, write,
// slug, rename, export, import, version gate, coverage, active badge) lives in
// the shared store; this file supplies the four domain closures and the
// directory.
//
// FILE OPERATIONS ARE IMMEDIATE. Save, rename, delete, export and import go
// straight to disk and are NOT staged behind the page's Apply/Discard, exactly
// as decoration sets are. That is enforced structurally rather than by a flag:
// the store contains no staging code at all, so there is nothing for a Discard
// to unwind. Do not reintroduce one — the animation domain grew a
// snapshot/rollback/mutation-guard trio for this and every hook was deleted
// again as unreachable once its state moved into config, because nothing in a
// staging path can restore a file the user has already replaced or deleted.
//
// APPLY is the one operation that stages, and only because of WHAT it writes:
// it calls ISettings::setOverlayShaderTree() once and nothing else, so it rides
// the ordinary settings dirty/apply/discard loop like any other setter and
// Discard undoes it. One write, not one per layout — a write per entry would
// emit overlayShaderTreeChanged per entry, refreshing every assignment card and
// re-running the whole set-row active sweep against each intermediate tree.

#include "overlayspagecontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "core/interfaces/shaderregistry.h"
#include "core/types/overlayshadertree.h"
#include "phosphor_i18n.h"

#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace PlasmaZones {

namespace {

/// On-disk format for an overlay set. Named here so a future bump is one line.
constexpr int kOverlaySetFormatVersion = 1;

// The store's envelope keys. Prefixed because this TU shares a unity blob with
// shadersetstore.cpp, whose anonymous namespace declares the same three names.
constexpr QLatin1String kSetOverridesKey{"overrides"};
constexpr QLatin1String kSetPathKey{"path"};
constexpr QLatin1String kSetProfileKey{"profile"};

/// The reserved entry path standing in for the global default.
///
/// The tree's own convention for the baseline is the EMPTY path, but an entry
/// cannot use it here: the store derives a set's coverage chips from each
/// path's leading section, and an empty path yields an empty section, so a
/// baseline-only set would show no coverage at all. A reserved token that no
/// layout UUID can collide with (a UUID never contains a colon) gives the chip
/// a label and keeps the envelope one shape.
///
/// The store's envelope has a `baseline` key of its own, deliberately unused:
/// every domain refuses it, and a domain with a global default encodes it as an
/// ordinary entry so containment and coverage need no special case.
constexpr QLatin1String kGlobalPath{"overlay:global"};

bool isGlobalPath(const QString& path)
{
    return path == kGlobalPath;
}

/// A layout UUID in the braced form the project uses everywhere, or the
/// reserved global token. The structural test only — whether the layout still
/// EXISTS is a separate question, and deliberately not a refusal (see
/// entryApplicable).
bool isWellFormedPath(const QString& path)
{
    return isGlobalPath(path) || !QUuid::fromString(path).isNull();
}

QJsonObject profileToJson(const OverlayShaderProfile& profile)
{
    // Through the type's own serializer, so a set entry is spelled exactly as
    // the config tree spells it. Building the object by hand here would let the
    // two drift, and a set whose entry differs from the live value only in
    // spelling reads inactive forever.
    return profile.toJson();
}

} // namespace

QString OverlaysPageController::overlaySetsDirectoryPath() const
{
    if (!m_setsDirOverride.isEmpty())
        return m_setsDirOverride;
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + ConfigDefaults::userOverlaySetsSubdir();
}

void OverlaysPageController::setSetsDirOverride(const QString& dir)
{
    m_setsDirOverride = dir;
}

QString OverlaysPageController::setCoverageLabel(const QString& token) const
{
    if (isGlobalPath(token))
        return PhosphorI18n::tr("Global default");
    const QString name = layoutNameFor(token);
    if (!name.isEmpty())
        return name;
    return absentLayoutLabel(token);
}

QString OverlaysPageController::absentLayoutLabel(const QString& layoutId) const
{
    QString bare = layoutId;
    bare.remove(QLatin1Char('{'));
    bare.remove(QLatin1Char('}'));
    const qsizetype dash = bare.indexOf(QLatin1Char('-'));
    // "not on this computer" rather than "deleted": layout ids are per-machine,
    // so an override naming one this machine lacks is far more often a set or a
    // profile from elsewhere than something the user removed.
    return PhosphorI18n::tr("Layout %1 (not on this computer)").arg(dash > 0 ? bare.left(dash) : bare.left(8));
}

void OverlaysPageController::initSetsStore()
{
    ShaderSetStore::Config config;
    config.formatVersion = kOverlaySetFormatVersion;
    config.setsDir = [this]() {
        return overlaySetsDirectoryPath();
    };

    // ── Snapshot ─────────────────────────────────────────────────────────────
    // ONE tree read. availableSets() runs this on the GUI thread for the active
    // badge on every setsChanged, and setsChanged re-fires on every live edit;
    // overlayShaderTree() rebuilds the tree from the config store on each call,
    // so deriving the payload from one copy is the difference between one parse
    // per sweep and one per layout.
    config.snapshot = [this]() {
        QJsonObject root;
        QJsonArray overrides;
        if (m_settings) {
            const OverlayShaderTree tree = m_settings->overlayShaderTree();
            // The baseline leads, so a reader sees the global default first.
            // Captured on PRESENCE, not on emptiness: an empty baseline means
            // "no global overlay", which is a real choice a set may carry.
            if (!tree.baseline().isEmpty()) {
                QJsonObject entry;
                entry.insert(kSetPathKey, QString(kGlobalPath));
                entry.insert(kSetProfileKey, profileToJson(tree.baseline()));
                overrides.append(entry);
            }
            const QStringList layouts = tree.overriddenLayouts();
            for (const QString& layoutId : layouts) {
                QJsonObject entry;
                entry.insert(kSetPathKey, layoutId);
                // NOT gated on the profile being non-empty. An override whose
                // shaderId is empty explicitly suppresses the baseline for that
                // layout — a present-but-empty node is the whole point, and
                // pruning it here would silently drop the user's "no overlay on
                // this layout" from every set. (This is the shape that dropped
                // an explicit empty parameter map from motion sets.)
                entry.insert(kSetProfileKey, profileToJson(tree.directOverride(layoutId)));
                overrides.append(entry);
            }
        }
        root.insert(kSetOverridesKey, overrides);
        return root;
    };

    // ── Validate ─────────────────────────────────────────────────────────────
    // Whole-set: one malformed entry refuses the file, so a set can never be
    // half-committed by a validator that passed what apply then chokes on.
    //
    // Gated on the SAME facts the snapshot can produce, or a set would save,
    // list as a row, and be refused by its own validator on every apply.
    config.validate = [this](const QJsonObject& root) {
        if (root.contains(QLatin1String("baseline")))
            return false; // the global default is an entry, never an envelope key
        const QJsonValue overridesValue = root.value(kSetOverridesKey);
        if (!overridesValue.isArray())
            return false;
        const QJsonArray overrides = overridesValue.toArray();
        if (overrides.isEmpty())
            return false;

        QSet<QString> seen;
        for (const QJsonValue& v : overrides) {
            if (!v.isObject())
                return false;
            const QJsonObject entry = v.toObject();
            const QJsonValue pathValue = entry.value(kSetPathKey);
            if (!pathValue.isString())
                return false;
            const QString path = pathValue.toString();
            if (!isWellFormedPath(path) || seen.contains(path))
                return false;
            seen.insert(path);

            const QJsonValue profileValue = entry.value(kSetProfileKey);
            if (!profileValue.isObject())
                return false;
            const QJsonObject profile = profileValue.toObject();
            // shaderId absent is legal (that is the suppress-the-baseline node);
            // shaderId present but not a string is not.
            const QJsonValue shaderIdValue = profile.value(QLatin1String(OverlayShaderProfile::JsonFieldShaderId));
            if (!shaderIdValue.isUndefined() && !shaderIdValue.isString())
                return false;
            const QJsonValue paramsValue = profile.value(QLatin1String(OverlayShaderProfile::JsonFieldParameters));
            if (!paramsValue.isUndefined() && !paramsValue.isObject())
                return false;

            // An unknown PACK refuses the whole set, matching how motion sets
            // treat a pack this build does not have: the write would refuse it
            // anyway, so accepting it here would mean passing validation and
            // then failing mid-commit. Unlike a layout, a pack id is a global
            // name — a set naming one this machine lacks is a set this machine
            // genuinely cannot honour, not merely one describing other layouts.
            const QString shaderId = shaderIdValue.toString();
            if (!shaderId.isEmpty() && m_shaderRegistry && m_shaderRegistry->shaderInfo(shaderId).isEmpty())
                return false;
        }
        return true;
    };

    // ── Applicability ────────────────────────────────────────────────────────
    // A layout UUID is per-installation, so a set shared between machines names
    // layouts the other has never seen even when it has the same layouts by
    // name. Refusing the set — what decoration and motion do for a path they do
    // not recognise — would make shared overlay sets useless, so apply skips
    // those entries instead. This predicate tells the store the same thing, so
    // an entry apply skipped cannot hold the active badge dark.
    //
    // Held in a local and copied into BOTH closures below, so apply and the
    // badge can never disagree about which entries this machine can honour.
    const auto applicable = [this](const QString& path) {
        if (isGlobalPath(path))
            return true;
        if (!m_layoutRegistry)
            return false;
        const QVector<PhosphorZones::Layout*> layouts = m_layoutRegistry->layouts();
        for (PhosphorZones::Layout* layout : layouts) {
            if (layout && layout->id().toString() == path)
                return true;
        }
        return false;
    };
    config.entryApplicable = applicable;

    // ── Apply ────────────────────────────────────────────────────────────────
    // Merge, not replace: a layout the set does not mention keeps what it has,
    // which is what makes the active badge a containment test rather than an
    // equality one.
    config.apply = [this, applicable](const QJsonObject& root) {
        if (!m_settings)
            return false;
        OverlayShaderTree tree = m_settings->overlayShaderTree();
        const QJsonArray overrides = root.value(kSetOverridesKey).toArray();
        int skipped = 0;
        for (const QJsonValue& v : overrides) {
            const QJsonObject entry = v.toObject();
            const QString path = entry.value(kSetPathKey).toString();
            const OverlayShaderProfile profile = OverlayShaderProfile::fromJson(entry.value(kSetProfileKey).toObject());
            if (isGlobalPath(path)) {
                tree.setBaseline(profile);
                continue;
            }
            if (!applicable(path)) {
                ++skipped;
                continue;
            }
            tree.setOverride(path, profile);
        }
        // ONE write for the whole set — see the file header.
        //
        // Deliberately NOT through writeTreeAnnouncing: that names a single
        // node for the assignment cards to filter on, and a set moves many. The
        // plain setter announces a whole-tree change, which is what this is.
        m_settings->setOverlayShaderTree(tree);
        if (skipped > 0) {
            Q_EMIT toastRequested(
                PhosphorI18n::tr("%n layouts in this set are not on this computer, so they were "
                                 "skipped. Everything else was applied.",
                                 nullptr, skipped));
        }
        return true;
    };

    m_sets = new ShaderSetStore(std::move(config), this);
    // Without this the active badges go stale the moment the user edits an
    // assignment; the store coalesces the burst to one emission per event-loop
    // turn. The signal carries arguments the slot does not take, which Qt
    // allows.
    //
    // This covers the layout catalogue too, even though a layout add or remove
    // changes which entries are applicable without any assignment changing:
    // the constructor forwards ILayoutSourceRegistry::contentsChanged into
    // shaderProfileChanged, so it arrives here. A second direct connection
    // from that signal to this slot used to sit below and was pure
    // duplication.
    connect(this, &OverlaysPageController::shaderProfileChanged, m_sets, &ShaderSetStore::notifyLiveStateChanged);
}

} // namespace PlasmaZones
