// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Decoration-set domain closures for the shared ShaderSetStore. A set is a
// named snapshot of the decoration profile tree (baseline + per-surface direct
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
bool stageEntries(const QJsonObject& root, QList<StagedEntry>* staged, bool* hasBaseline)
{
    using PhosphorSurfaceShaders::DecorationProfile;

    staged->clear();
    // An EMPTY baseline object is not a baseline. The snapshot side omits an
    // empty one (it carries no engaged field), so treating `"baseline": {}`
    // from a hand-edited or foreign file as a real baseline would apply an
    // all-inherit profile over whatever the user had — a silent wipe.
    *hasBaseline = !root.value(kBaselineKey).toObject().isEmpty();
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
            qCWarning(lcConfig) << "decorationset: missing profile object for" << path;
            return false;
        }
        staged->push_back({path, DecorationProfile::fromJson(entry.value(kProfileKey).toObject())});
    }
    return !staged->isEmpty() || *hasBaseline;
}

} // namespace

QString DecorationPageController::decorationSetsDirectoryPath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userDecorationSetsSubdir());
}

// NOTE: this TU is compiled with -fno-lto -Wno-maybe-uninitialized in every
// consuming target (src/settings/CMakeLists.txt for plasmazones-settings, and
// tests/unit/CMakeLists.txt, whose one directory-scoped call covers both
// test_decorationpagecontroller and test_decoration_sets — source-file
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

    // ── Snapshot: serialise the live tree (baseline + direct overrides).
    //    Paths are sorted so the output is deterministic, which the store
    //    relies on when it compares this snapshot against a saved set to
    //    decide which one is active.
    config.snapshot = [this]() -> QJsonObject {
        if (!m_settings) {
            return QJsonObject{};
        }
        const DecorationProfileTree tree = m_settings->decorationProfileTree();

        QJsonObject root;
        // The baseline is part of the look — capture it when it carries any
        // engaged field (an empty profile serialises to an empty object).
        const QJsonObject baselineJson = tree.baseline().toJson();
        if (!baselineJson.isEmpty()) {
            root.insert(kBaselineKey, baselineJson);
        }
        QJsonArray overrides;
        QStringList paths = tree.overriddenPaths();
        paths.sort();
        for (const QString& p : paths) {
            QJsonObject entry;
            entry.insert(kPathKey, p);
            entry.insert(kProfileKey, tree.directOverride(p).toJson());
            overrides.append(entry);
        }
        root.insert(kOverridesKey, overrides);
        return root;
    };

    config.validate = [](const QJsonObject& root) -> bool {
        QList<StagedEntry> staged;
        bool hasBaseline = false;
        return stageEntries(root, &staged, &hasBaseline);
    };

    // ── Apply: merge into ONE tree and persist once. Surfaces the set does
    //    not cover keep their current overrides (motion-set semantics).
    config.apply = [this](const QJsonObject& root) -> bool {
        if (!m_settings) {
            return false;
        }
        QList<StagedEntry> staged;
        bool hasBaseline = false;
        if (!stageEntries(root, &staged, &hasBaseline)) {
            return false;
        }
        DecorationProfileTree tree = m_settings->decorationProfileTree();
        if (hasBaseline) {
            tree.setBaseline(DecorationProfile::fromJson(root.value(kBaselineKey).toObject()));
        }
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
