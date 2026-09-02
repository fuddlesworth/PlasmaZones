// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configdefaults.h"
#include "configmigration_util.h"

#include <PhosphorConfig/JsonBackend.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1String>
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
} // namespace

void ConfigMigration::migrateV6ToV7(QJsonObject& root)
{
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 7) {
        return;
    }
    // v7 moves zone-overlay shader assignments out of the layout-settings
    // sidecar into the config's Snapping.OverlayShaders/OverlayShaderTree
    // blob. The config root itself carries nothing to transform — the
    // sidecar lift needs filesystem access and must NOT run on the sparse
    // profile deltas this chain also processes (it would stamp the user's
    // live assignments into every profile), so it lives in
    // relocateOverlayShaderAssignments, invoked from ensureJsonConfig's
    // finalize pass on every run — the same split as the v4 layout-settings
    // relocation (relocateLayoutSettings). All this step does is stamp.
    root[ConfigKeys::versionKey()] = 7;
}

bool ConfigMigration::relocateOverlayShaderAssignments(const QString& jsonPath)
{
    const QString sidecarPath = ConfigDefaults::layoutSettingsFilePath();
    if (!QFile::exists(sidecarPath)) {
        return true; // nothing to relocate — fresh install or already clean
    }

    QJsonObject sidecar;
    {
        QFile sf(sidecarPath);
        if (!sf.open(QIODevice::ReadOnly)) {
            qWarning("ConfigMigration: overlay-shader relocation could not read %s — skipping",
                     qPrintable(sidecarPath));
            return true; // unreadable sidecar is the layout store's problem, not a migration failure
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(sf.readAll(), &err);
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
    // shader). Non-UUID keys (the "autotile:<algoId>" entries the pre-v7
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
        if (shaderId.isEmpty() || QUuid::fromString(it.key()).isNull()) {
            continue;
        }
        QJsonObject node;
        node.insert(kNodeShaderId, shaderId);
        const QJsonValue params = entry.value(kSidecarShaderParams);
        if (params.isObject() && !params.toObject().isEmpty()) {
            node.insert(kNodeParameters, params.toObject());
        }
        lifted.insert(it.key(), node);
    }
    QJsonObject strippedSidecar = sidecar;
    const bool sidecarDirty = stripShaderKeys(strippedSidecar);

    if (!sidecarDirty) {
        return true; // fully idempotent — nothing left to move
    }

    // Lift into the config root FIRST: the config copy is the authoritative
    // destination, so it must be durably written before the sidecar loses
    // its entries. On a re-run after a sidecar write failure the merge below
    // keeps an already-lifted (possibly since-edited) node — existing tree
    // entries always win over the stale sidecar copy.
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
        QJsonObject group = groupObjectAtPath(root, ConfigKeys::snappingOverlayShadersGroup());
        QJsonObject tree = group.value(ConfigKeys::overlayShaderTreeKey()).toObject();
        QJsonObject overrides = tree.value(kTreeOverrides).toObject();
        bool treeDirty = false;
        for (auto it = lifted.constBegin(); it != lifted.constEnd(); ++it) {
            if (overrides.contains(it.key())) {
                continue; // already lifted on an earlier run — that copy is live
            }
            overrides.insert(it.key(), it.value());
            treeDirty = true;
        }
        if (treeDirty) {
            tree.insert(kTreeOverrides, overrides);
            group.insert(ConfigKeys::overlayShaderTreeKey(), tree);
            setGroupAtSegments(root, ConfigKeys::snappingOverlayShadersGroup().split(QLatin1Char('.')), group);
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
    // post-v7 writes shader keys, so re-stripping the fresh copy is safe.
    // A failure here retries on the next run; the existing-entry-wins merge
    // above keeps that safe.
    {
        QFile sf(sidecarPath);
        if (sf.open(QIODevice::ReadOnly)) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(sf.readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject fresh = doc.object();
                if (!stripShaderKeys(fresh)) {
                    return true; // someone else already stripped it
                }
                strippedSidecar = fresh;
            }
        }
    }
    if (!PhosphorConfig::JsonBackend::writeJsonAtomically(sidecarPath, strippedSidecar)) {
        qWarning("ConfigMigration: failed to strip overlay shader keys from %s", qPrintable(sidecarPath));
        return false;
    }
    return true;
}

} // namespace PlasmaZones
