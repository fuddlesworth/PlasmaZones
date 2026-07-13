// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Decoration-set domain closures for the shared ShaderSetStore. A set is a
// named snapshot of the decoration profile tree (per-surface direct
// overrides), persisted as one JSON file under
// ~/.local/share/plasmazones/decorationsets. Unlike motion sets (which
// snapshot per-path override FILES), the decoration tree is config-backed, so
// apply mutates ONE tree and persists it through
// ISettings::setDecorationProfileTree — dirty / apply / discard ride the normal
// settings staging flow with no extra snapshot plumbing. The generic store
// handles the envelope (name / description / version), the coverage summary,
// and every file operation.

#include "decorationpagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/isettings.h"
#include "../core/logging.h"
#include "shadersetstore.h"

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorSurface/DecorationSupportedPaths.h>

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QStringList>

namespace PlasmaZones {

namespace {

constexpr QLatin1String kBaselineKey{"baseline"};
constexpr QLatin1String kOverridesKey{"overrides"};
constexpr QLatin1String kPathKey{"path"};
constexpr QLatin1String kProfileKey{"profile"};

/// Current on-disk decoration-set format. The store stamps it on save and
/// refuses a NEWER file on apply / import.
constexpr int kSetFormatVersion = 1;

struct StagedEntry
{
    QString path;
    PhosphorSurfaceShaders::DecorationProfile profile;
};

/// Validate + stage every entry in @p root. Whole-set discipline: one
/// malformed entry rejects the set rather than committing partial state.
/// Shared by validate (the import / apply gate) and apply (the commit), so the
/// two can never drift apart on what counts as a valid entry.
/// @return false when any entry is malformed, or when the set covers nothing.
bool stageEntries(const QJsonObject& root, QList<StagedEntry>* staged)
{
    using PhosphorSurfaceShaders::DecorationProfile;

    staged->clear();
    // Decoration sets do NOT carry a baseline, for the same reason the Decoration
    // nav has no General surface page: there is no meaningful global default (see
    // settingscontroller_pageregistration.cpp — borders and title bars are
    // window-only, and the daemon surfaces default to no decoration). The tree
    // still HAS a baseline, reachable over D-Bus, but no settings page binds it, so
    // a baseline in a set file could only ever ARRIVE through an import and could
    // then never be seen or cleared. Refuse it at the boundary, as motion does.
    if (root.contains(kBaselineKey)) {
        qCWarning(lcConfig) << "decorationset: rejecting a set that carries a baseline";
        return false;
    }
    // Whole-set discipline, same as the baseline above: a present-but-non-array
    // `overrides` would otherwise read as "no overrides" and let a file import and
    // apply with every override it claims silently dropped.
    if (root.contains(kOverridesKey) && !root.value(kOverridesKey).isArray()) {
        qCWarning(lcConfig) << "decorationset: rejecting a set whose overrides are not an array";
        return false;
    }
    const QJsonArray overrides = root.value(kOverridesKey).toArray();
    staged->reserve(overrides.size());
    for (const QJsonValue& v : overrides) {
        if (!v.isObject()) {
            qCWarning(lcConfig) << "decorationset: non-object entry in set";
            return false;
        }
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(kPathKey).toString();
        // Membership in the supported surface taxonomy is the same rule the
        // controller's setters enforce. It also rejects empty and
        // traversal-attempting paths.
        if (path.isEmpty() || !PhosphorSurfaceShaders::decorationSurfaceSupported(path)) {
            qCWarning(lcConfig) << "decorationset: rejecting unknown surface path" << path;
            return false;
        }
        if (!entry.value(kProfileKey).isObject()) {
            qCWarning(lcConfig) << "decorationset: profile for" << path << "is not an object";
            return false;
        }
        // Judge the PARSED profile, not the raw object. fromJson ignores unknown
        // and wrong-typed keys, so `{"chain": "border"}` (a string where an array
        // belongs) is a non-empty object that still parses to an all-inherit
        // profile. Staging that would make the surface READ as overridden while
        // changing nothing the user can see. An engaged-but-empty field is fine
        // and is NOT this case: `chain: []` ("explicitly no packs") parses to an
        // engaged optional. This is the same rule the snapshot applies when it
        // skips an override whose toJson() is empty.
        const DecorationProfile profile = DecorationProfile::fromJson(entry.value(kProfileKey).toObject());
        if (profile == DecorationProfile{}) {
            qCWarning(lcConfig) << "decorationset: profile for" << path << "engages no field, refusing the set";
            return false;
        }
        staged->push_back({path, profile});
    }
    return !staged->isEmpty();
}

} // namespace

QString DecorationPageController::decorationSetsDirectoryPath() const
{
    if (!m_setsDirOverride.isEmpty()) {
        return m_setsDirOverride;
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userDecorationSetsSubdir());
}

void DecorationPageController::setSetsDirOverride(const QString& dir)
{
    m_setsDirOverride = dir;
}

// NOTE: under GCC, this TU is compiled with -fno-lto -Wno-maybe-uninitialized in every
// consuming target (src/settings/CMakeLists.txt for plasmazones-settings, and
// tests/unit/CMakeLists.txt, whose one directory-scoped call covers every
// decoration-controller test target in that directory — source-file
// properties are directory-scoped, so each directory sets its own). GCC 16's
// LTO pass emits false-positive -Wmaybe-uninitialized warnings against the
// staged->push_back() in stageEntries() above.
void DecorationPageController::initSetsStore()
{
    using PhosphorSurfaceShaders::DecorationProfile;
    using PhosphorSurfaceShaders::DecorationProfileTree;

    ShaderSetStore::Config config;
    config.formatVersion = kSetFormatVersion;
    config.setsDir = [this]() {
        return decorationSetsDirectoryPath();
    };

    // ── Snapshot: serialise the live tree's direct overrides. NOT the baseline:
    //    no settings page can reach it, so a set carrying one could never be
    //    undone through the UI (see stageEntries). Paths are sorted so the output
    //    is stable across saves, which keeps a set file diffable.
    config.snapshot = [this]() -> QJsonObject {
        if (!m_settings) {
            return QJsonObject{};
        }
        const DecorationProfileTree& tree = this->tree();

        QJsonObject root;
        QJsonArray overrides;
        QStringList paths = tree.overriddenPaths();
        paths.sort();
        for (const QString& p : paths) {
            // Skip an override that engages no field. A tree can hold one
            // (config.json is hand-editable and fromJson stores every entry), and
            // emitting it would write a set file that stageEntries then refuses
            // forever.
            const QJsonObject profileJson = tree.directOverride(p).toJson();
            if (profileJson.isEmpty()) {
                continue;
            }
            QJsonObject entry;
            entry.insert(kPathKey, p);
            entry.insert(kProfileKey, profileJson);
            overrides.append(entry);
        }
        root.insert(kOverridesKey, overrides);
        return root;
    };

    config.validate = [](const QJsonObject& root) -> bool {
        QList<StagedEntry> staged;
        return stageEntries(root, &staged);
    };

    // ── Apply: merge into ONE tree and persist once. Surfaces the set does
    //    not cover keep their current overrides (motion-set semantics).
    config.apply = [this](const QJsonObject& root) -> bool {
        if (!m_settings) {
            return false;
        }
        QList<StagedEntry> staged;
        if (!stageEntries(root, &staged)) {
            return false;
        }
        DecorationProfileTree tree = this->tree(); // a COPY: the closure mutates it
        for (const StagedEntry& e : staged) {
            tree.setOverride(e.path, e.profile);
        }
        m_settings->setDecorationProfileTree(tree);
        return true;
    };

    m_sets = new ShaderSetStore(std::move(config), this);

    // The `active` badge is derived from live state, so it goes stale when
    // the user edits a chain anywhere else on the Decoration pages.
    connect(this, &DecorationPageController::profilesChanged, m_sets, &ShaderSetStore::notifyLiveStateChanged);
}

} // namespace PlasmaZones
