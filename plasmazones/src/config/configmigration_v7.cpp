// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configkeys.h"
#include "configmigration_util.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QSet>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

// ── v6 → v7: the window-movement animation nodes are renamed, and the
//    maximize node is retired ────────────────────────────────────────────────
//
// Every geometry the placement engines commit rides ONE animation node,
// whatever mode placed the window: snapping into a zone, tiling into a slot,
// a scrolling column reflowing, monocle filling the screen. That node was
// still called `snapIn` (and its release leg `snapOut`), a name from when
// snapping was the only mode, so a scrolling-only user who has never drawn a
// zone had every column placement governed by a node called "snapIn". v7
// renames them `placeIn` / `placeOut`.
//
// `window.movement.maximize` is retired with the same move. It had come to
// mean two different things: the KWin-native maximize of a window PlasmaZones
// was not placing, AND an engine placement that happened to set KWin's
// maximize bit on the way (a monocle tile, a column maximized to the edges).
// Deciding which of the two a given placement was turned out to be
// unanswerable on Wayland, where the press, the engine's write and each
// committed echo are a client round trip apart. So a placement always rides
// the placement node, and the native maximize morph rides it too — placeIn
// growing to the maximize area, placeOut restoring from it — which is also
// what a user means by "the animation windows play when they change size".
//
// Schema-migration freeze policy (mirrors migrateV5ToV6): every v6 group/key
// spelling and every path string this step depends on is frozen here as a
// file-scope constant, so the migration's wire-format contract stays stable
// if the live accessors or ProfilePaths constants are renamed again later.

namespace {

// Frozen v6 group and key spellings.
constexpr QLatin1String kV6AnimationsGroup{"Animations"};
constexpr QLatin1String kV6KeyShaderProfileTree{"ShaderProfileTree"};
// ShaderProfileTree::toJson shape: `{ "baseline": {...}, "overrides": [ { "path": "...", "profile": {...} } ] }`.
constexpr QLatin1String kV6KeyOverrides{"overrides"};
constexpr QLatin1String kV6KeyPath{"path"};
// The three retired v6 node paths.
constexpr QLatin1String kV6PathSnapIn{"window.movement.snapIn"};
constexpr QLatin1String kV6PathSnapOut{"window.movement.snapOut"};
constexpr QLatin1String kV6PathMaximize{"window.movement.maximize"};
// Their v7 homes.
constexpr QLatin1String kV7PathPlaceIn{"window.movement.placeIn"};
constexpr QLatin1String kV7PathPlaceOut{"window.movement.placeOut"};

/// The v7 path a v6 override path lands on, or the path itself when it is not
/// one of the three retired ones.
QString v7PathFor(const QString& v6Path)
{
    if (v6Path == kV6PathSnapIn || v6Path == kV6PathMaximize) {
        return QString(kV7PathPlaceIn);
    }
    if (v6Path == kV6PathSnapOut) {
        return QString(kV7PathPlaceOut);
    }
    return v6Path;
}

} // namespace

void ConfigMigration::migrateV6ToV7(QJsonObject& root)
{
    // Defense-in-depth idempotency guard, mirroring the earlier steps.
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 7) {
        return;
    }

    QJsonObject animations = groupObjectAtPath(root, kV6AnimationsGroup);
    const QJsonValue treeValue = animations.value(kV6KeyShaderProfileTree);
    // Only an object-shaped tree is rewritten. A missing tree is the common
    // fresh-config case and needs nothing; a non-object value at that key is
    // not this step's to repair, and is left exactly as found (the loader's
    // own validation owns malformed blobs).
    if (treeValue.isObject()) {
        QJsonObject tree = treeValue.toObject();
        const QJsonArray before = tree.value(kV6KeyOverrides).toArray();

        // Which v7 paths already have a home after the plain renames. The
        // maximize override folds into placeIn only when nothing else will
        // land there: a user who assigned a pack to Maximized meant "animate
        // this with pack X", and placeIn is where that event now lives — but a
        // pack they assigned to the placement node itself is the more general
        // statement and wins, so the maximize entry is dropped rather than
        // clobbering it.
        QSet<QString> occupiedAfterRename;
        for (const QJsonValue& v : before) {
            const QString path = v.toObject().value(kV6KeyPath).toString();
            if (path != kV6PathMaximize) {
                occupiedAfterRename.insert(v7PathFor(path));
            }
        }

        QJsonArray after;
        for (const QJsonValue& v : before) {
            QJsonObject entry = v.toObject();
            const QString path = entry.value(kV6KeyPath).toString();
            if (path == kV6PathMaximize && occupiedAfterRename.contains(QString(kV7PathPlaceIn))) {
                continue;
            }
            const QString renamed = v7PathFor(path);
            if (renamed != path) {
                entry.insert(kV6KeyPath, renamed);
            }
            after.append(entry);
        }

        if (after != before) {
            tree.insert(kV6KeyOverrides, after);
            animations.insert(kV6KeyShaderProfileTree, tree);
            setGroupAtSegments(root, QString(kV6AnimationsGroup).split(QLatin1Char('.')), animations);
        }
    }

    // Stamp the literal, not ConfigSchemaVersion — the historical step's
    // output must stay frozen when the chain grows again.
    root[ConfigKeys::versionKey()] = 7;
}

} // namespace PlasmaZones
