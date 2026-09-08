// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pointer-set domain closures for the shared ShaderSetStore. A pointer set is
// a named snapshot of the user's pointer chain (the ordered pack list with its
// parameter overrides), persisted as one JSON file under
// ~/.local/share/plasmazones/pointersets. The chain is config-backed, so apply
// writes ONE value through ISettings::setPointerChain and rides the normal
// dirty / apply / discard staging flow with no extra snapshot plumbing.
//
// The chain is a single flat value rather than a path-keyed tree, so a set
// carries exactly ONE entry, at the literal path "pointer". That synthetic
// path is what keeps the store's generic coverage summary and its
// active-detection working unchanged across all three set domains.
//
// The generic store handles the envelope (name / description / version), the
// coverage summary, and every file operation.

#include "pointerpagecontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "settings/stores/shadersetstore.h"

#include <PhosphorPointer/PointerProfile.h>

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QStandardPaths>

namespace PlasmaZones {

namespace {

constexpr QLatin1String kBaselineKey{"baseline"};
constexpr QLatin1String kOverridesKey{"overrides"};
constexpr QLatin1String kPathKey{"path"};
constexpr QLatin1String kProfileKey{"profile"};

/// The one synthetic path a pointer set carries. There is no pointer path
/// taxonomy — the user has a single chain — so this literal exists purely so
/// the store's generic coverage and containment logic have something to key on.
constexpr QLatin1String kPointerPath{"pointer"};

/// Current on-disk pointer-set format. The store stamps it on save and refuses
/// a NEWER file on apply / import.
constexpr int kSetFormatVersion = 1;

/// Validate + stage the chain carried by @p root. Whole-set discipline: any
/// malformed part refuses the set rather than committing partial state. Shared
/// by validate (the import / apply gate) and apply (the commit), so the two can
/// never drift apart on what counts as a valid set.
/// @return false when the set is malformed or would stage an empty chain.
bool stagePointerChain(const QJsonObject& root, PhosphorPointerShaders::PointerProfile* out)
{
    using PhosphorPointerShaders::PointerProfile;

    *out = PointerProfile{};
    // The pointer domain has no baseline concept at all: there is one chain and
    // no walk-up resolve to fall back to. A baseline arriving through an import
    // could therefore never be seen, applied or cleared. Refuse it at the
    // boundary, exactly as the motion and decoration domains do.
    if (root.contains(kBaselineKey)) {
        qCWarning(lcConfig) << "pointerset: rejecting a set that carries a baseline";
        return false;
    }
    // A present-but-non-array `overrides` would otherwise read as "no
    // overrides" and let a file import and apply with its whole payload
    // silently dropped.
    if (root.contains(kOverridesKey) && !root.value(kOverridesKey).isArray()) {
        qCWarning(lcConfig) << "pointerset: rejecting a set whose overrides are not an array";
        return false;
    }
    const QJsonArray overrides = root.value(kOverridesKey).toArray();
    // Exactly one entry: the chain is one value, so a set carrying none covers
    // nothing and a set carrying two contradicts itself about what the chain is.
    if (overrides.size() != 1) {
        qCWarning(lcConfig) << "pointerset: a set must carry exactly one entry, found" << overrides.size();
        return false;
    }
    const QJsonValue first = overrides.at(0);
    if (!first.isObject()) {
        qCWarning(lcConfig) << "pointerset: non-object entry in set";
        return false;
    }
    const QJsonObject entry = first.toObject();
    const QString path = entry.value(kPathKey).toString();
    if (path != kPointerPath) {
        qCWarning(lcConfig) << "pointerset: rejecting unknown path" << path;
        return false;
    }
    if (!entry.value(kProfileKey).isObject()) {
        qCWarning(lcConfig) << "pointerset: profile is not an object";
        return false;
    }
    // Judge the PARSED profile, not the raw object. fromJson ignores unknown
    // and wrong-typed keys, so `{"layers": "comet"}` (a string where an array
    // belongs) is a non-empty object that still parses to an empty chain.
    // Staging that would make the set READ as covering the pointer while
    // changing nothing the user can see.
    const PointerProfile parsed = PointerProfile::fromJson(entry.value(kProfileKey).toObject());
    if (parsed.isEmpty()) {
        qCWarning(lcConfig) << "pointerset: profile carries no layers, refusing the set";
        return false;
    }
    *out = parsed;
    return true;
}

} // namespace

QString PointerPageController::pointerSetsDirectoryPath() const
{
    if (!m_setsDirOverride.isEmpty()) {
        return m_setsDirOverride;
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userPointerSetsSubdir());
}

void PointerPageController::setSetsDirOverride(const QString& dir)
{
    m_setsDirOverride = dir;
}

void PointerPageController::initSetsStore()
{
    using PhosphorPointerShaders::PointerProfile;

    ShaderSetStore::Config config;
    config.formatVersion = kSetFormatVersion;
    config.setsDir = [this]() {
        return pointerSetsDirectoryPath();
    };

    // ── Snapshot: serialise the live chain, and NOTHING else. In particular a
    //    set never carries the `Pointer/Enabled` master switch. A set is a
    //    look; whether the user wants pointer effects on at all is a separate
    //    preference, and applying a saved look must not silently switch the
    //    feature off (or on) behind them.
    //
    //    An empty chain snapshots to an empty payload, which the store treats
    //    as "nothing to save" and refuses — the set would otherwise be a no-op
    //    that the validator then rejects forever.
    config.snapshot = [this]() -> QJsonObject {
        if (!m_settings) {
            return QJsonObject{};
        }
        const PointerProfile chain = m_settings->pointerChain();
        if (chain.isEmpty()) {
            return QJsonObject{};
        }
        QJsonObject entry;
        entry.insert(kPathKey, QString(kPointerPath));
        entry.insert(kProfileKey, chain.toJson());

        QJsonObject root;
        root.insert(kOverridesKey, QJsonArray{entry});
        return root;
    };

    config.validate = [](const QJsonObject& root) -> bool {
        PointerProfile parsed;
        return stagePointerChain(root, &parsed);
    };

    // ── Apply: REPLACE the whole chain. There is a single value here, so there
    //    is nothing to merge into.
    config.apply = [this](const QJsonObject& root) -> bool {
        if (!m_settings) {
            return false;
        }
        PointerProfile parsed;
        if (!stagePointerChain(root, &parsed)) {
            return false;
        }
        m_settings->setPointerChain(parsed);
        return true;
    };

    // config.entrySatisfied is deliberately left unset. The default is exact
    // equality, which is right here: apply replaces the whole value, so after
    // applying, live equals the set and anything else means it is not applied.

    m_sets = new ShaderSetStore(std::move(config), this);

    // The `active` badge is derived from live state, so it goes stale when the
    // user edits the chain on the Chain page.
    connect(this, &PointerPageController::chainChanged, m_sets, &ShaderSetStore::notifyLiveStateChanged);
}

} // namespace PlasmaZones
