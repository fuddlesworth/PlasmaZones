// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The offline pack validator's authoring-model detection and include-root
// resolution. Detection decides which validator arm a pack is sent to, and the
// wrong arm produces confident diagnostics about nothing (a demand for
// `zone.vert` from an animation pack), so the shared/ marker lookup is pinned
// here in every layout a pack ships in: beside its helpers in a source tree,
// and on its own in a user data dir with the helpers installed elsewhere.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "helpers/XdgEnvGuard.h"
#include "packvalidatortesthelpers.h"
#include "shadervalidate/packvalidatorcommon.h"

using PlasmaZones::ShaderValidate::detectPackModel;
using PlasmaZones::ShaderValidate::PackModel;
using PlasmaZones::ShaderValidate::packSharedRoots;

class TestPackModelDetection : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// The authoring model is DETECTED from the pack's sibling shared/ dir.
    ///
    /// This is the guard on a diagnostic that used to be actively misleading:
    /// the model was a flag with --overlay silently the default, so validating
    /// an animation pack without remembering --animation ran the ZONE checks
    /// over it and reported two errors ("rename it to zone.vert", "Include not
    /// found: common.glsl") that described nothing wrong with the pack.
    ///
    /// Each marker is asserted through a pack laid out the way the real trees
    /// are (a `shared/` dir beside the pack, not inside it), because the lookup
    /// is relative to the pack's PARENT — a detector that searched the pack dir
    /// itself would find nothing and silently fall back to overlay for
    /// everything, which is the exact bug this replaced.
    void authoringModelIsDetectedFromTheSharedMarker()
    {
        struct Case
        {
            const char* marker;
            PackModel expected;
        };
        const QList<Case> cases = {
            {"animation_uniforms.glsl", PackModel::Animation},
            {"surface_uniforms.glsl", PackModel::Surface},
            {"pointer_uniforms.glsl", PackModel::Pointer},
            {"common.glsl", PackModel::Overlay},
        };

        for (const Case& c : cases) {
            QTemporaryDir tmp;
            QVERIFY(tmp.isValid());
            const QString sharedDir = tmp.filePath(QStringLiteral("shared"));
            QVERIFY(QDir().mkpath(sharedDir));
            QFile marker(sharedDir + QLatin1Char('/') + QLatin1String(c.marker));
            QVERIFY(marker.open(QIODevice::WriteOnly));
            marker.close();

            const QString pack = tmp.filePath(QStringLiteral("some-pack"));
            QVERIFY(QDir().mkpath(pack));

            const std::optional<PackModel> got = detectPackModel(pack);
            QVERIFY2(got.has_value(), c.marker);
            QVERIFY2(*got == c.expected, c.marker);
        }
    }

    /// An INSTALLED pack detects from the XDG data chain, not just its sibling.
    ///
    /// This is the layout every third-party and user pack actually ships in:
    /// the pack lands in `~/.local/share/plasmazones/<family>/<id>` while the
    /// shared helpers stay in the system prefix, so the pack has NO sibling
    /// `shared/` at all. A sibling-only lookup therefore failed on exactly the
    /// packs the tool exists to check, reporting the pack as undetected and
    /// then every one of its includes as missing. Widening to the family's XDG
    /// roots keeps this a marker lookup — the same roots the runtime resolves
    /// includes against — rather than a guess from metadata.
    /// EVERY family, not just one. The widening is gated on a hardcoded list of
    /// family directory names, so a table over a single family proves nothing
    /// about the others: the pointer family shipped with its marker wired into
    /// the detector but its directory name missing from that gate, and a
    /// single-family slot here passed throughout. Every installed pointer pack
    /// was undetectable in consequence. Adding a family means adding a row.
    void anInstalledPackDetectsThroughTheXdgChain()
    {
        struct Family
        {
            const char* dir;
            const char* marker;
            PackModel expected;
        };
        const QList<Family> families = {
            {"animations", "animation_uniforms.glsl", PackModel::Animation},
            {"surface", "surface_uniforms.glsl", PackModel::Surface},
            {"pointer", "pointer_uniforms.glsl", PackModel::Pointer},
            {"overlays", "common.glsl", PackModel::Overlay},
        };

        for (const Family& family : families) {
            QTemporaryDir sysRoot; // stands in for /usr/share
            QTemporaryDir userRoot; // stands in for ~/.local/share
            QVERIFY2(sysRoot.isValid(), family.dir);
            QVERIFY2(userRoot.isValid(), family.dir);

            const QString familyPath = QStringLiteral("plasmazones/") + QLatin1String(family.dir);

            // The helpers, installed once into the system prefix.
            const QString sharedDir = sysRoot.filePath(familyPath + QStringLiteral("/shared"));
            QVERIFY2(QDir().mkpath(sharedDir), family.dir);
            QFile marker(sharedDir + QLatin1Char('/') + QLatin1String(family.marker));
            QVERIFY2(marker.open(QIODevice::WriteOnly), family.dir);
            marker.close();

            // The pack, installed on its own with no sibling shared/.
            const QString pack = userRoot.filePath(familyPath + QStringLiteral("/some-pack"));
            QVERIFY2(QDir().mkpath(pack), family.dir);
            QVERIFY2(!QDir(userRoot.filePath(familyPath + QStringLiteral("/shared"))).exists(), family.dir);

            // The guard restores the variables to UNSET when they were unset,
            // which a save/qputenv pair cannot: an empty XDG_DATA_HOME means
            // "use ~/.local/share" to QStandardPaths, so a hand-rolled restore
            // would leave every later slot in this process reading whatever
            // the machine has installed.
            XdgEnvGuard guard;
            qputenv("XDG_DATA_DIRS", sysRoot.path().toUtf8());
            qputenv("XDG_DATA_HOME", userRoot.path().toUtf8());

            const std::optional<PackModel> got = detectPackModel(pack);
            QVERIFY2(got.has_value(), family.dir);
            QVERIFY2(*got == family.expected, family.dir);
        }
    }

    /// A pack outside the installed `plasmazones/<family>/<id>` layout is a
    /// self-contained tree and gets its sibling shared/ as its only include
    /// root, whatever the XDG chain holds.
    ///
    /// The source tree is the case that matters: with the family's helpers
    /// also installed under /usr/share, a header missing from
    /// `data/animations/shared` used to resolve from the installed copy, so
    /// the bundled gate went green on a developer machine and red in CI. The
    /// XDG widening exists for the installed layout (the slot above and the
    /// one below) and must not apply to any other.
    void aSourceTreeLayoutIsSelfContained()
    {
        QTemporaryDir sysRoot;
        QTemporaryDir tree;
        QVERIFY(sysRoot.isValid());
        QVERIFY(tree.isValid());

        // An installed copy of the helpers, which must NOT be consulted.
        const QString installedShared = sysRoot.filePath(QStringLiteral("plasmazones/animations/shared"));
        QVERIFY(QDir().mkpath(installedShared));
        QFile installedMarker(installedShared + QStringLiteral("/animation_uniforms.glsl"));
        QVERIFY(installedMarker.open(QIODevice::WriteOnly));
        installedMarker.close();

        // A source-tree layout: `<tree>/animations/shared` beside the pack,
        // under a family-named parent but NOT under a `plasmazones/` data
        // root, which is what distinguishes it from the installed layout.
        const QString siblingShared = tree.filePath(QStringLiteral("animations/shared"));
        QVERIFY(QDir().mkpath(siblingShared));
        QFile siblingMarker(siblingShared + QStringLiteral("/animation_uniforms.glsl"));
        QVERIFY(siblingMarker.open(QIODevice::WriteOnly));
        siblingMarker.close();
        const QString pack = tree.filePath(QStringLiteral("animations/some-pack"));
        QVERIFY(QDir().mkpath(pack));

        XdgEnvGuard guard;
        qputenv("XDG_DATA_DIRS", sysRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", tree.filePath(QStringLiteral("nowhere")).toUtf8());

        const QStringList roots = packSharedRoots(pack);
        QCOMPARE(roots, QStringList{QDir::cleanPath(siblingShared)});

        // The same pack given with a trailing slash derives the same sibling:
        // the CLI normalises its arguments, but the exported function is what
        // the tests and any library caller reach, and QFileInfo would
        // otherwise read `<pack>/` as its own parent.
        QCOMPARE(packSharedRoots(pack + QLatin1Char('/')), roots);
    }

    /// An installed pack with a PARTIAL sibling shared/ (a user override of
    /// one header beside the system copy of the rest) keeps the XDG chain:
    /// the runtime resolves that layout against every root's shared/, so the
    /// validator must too, or a documented-supported override fails every
    /// include the override dir does not carry. The sibling still comes first,
    /// so the override shadows the system copy as it does at runtime.
    void anInstalledPackWithAPartialSiblingKeepsTheXdgChain()
    {
        QTemporaryDir sysRoot;
        QTemporaryDir userRoot;
        QVERIFY(sysRoot.isValid());
        QVERIFY(userRoot.isValid());

        const QString installedShared = sysRoot.filePath(QStringLiteral("plasmazones/animations/shared"));
        QVERIFY(QDir().mkpath(installedShared));
        QFile installedMarker(installedShared + QStringLiteral("/animation_uniforms.glsl"));
        QVERIFY(installedMarker.open(QIODevice::WriteOnly));
        installedMarker.close();

        // The user's override dir carries the marker header and nothing else.
        const QString userShared = userRoot.filePath(QStringLiteral("plasmazones/animations/shared"));
        QVERIFY(QDir().mkpath(userShared));
        QFile userMarker(userShared + QStringLiteral("/animation_uniforms.glsl"));
        QVERIFY(userMarker.open(QIODevice::WriteOnly));
        userMarker.close();
        const QString pack = userRoot.filePath(QStringLiteral("plasmazones/animations/some-pack"));
        QVERIFY(QDir().mkpath(pack));

        XdgEnvGuard guard;
        qputenv("XDG_DATA_DIRS", sysRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", userRoot.path().toUtf8());

        const QStringList roots = packSharedRoots(pack);
        QCOMPARE(roots.size(), 2);
        QCOMPARE(roots.first(), QDir::cleanPath(userShared));
        QCOMPARE(roots.last(), QDir::cleanPath(installedShared));
    }

    /// The pack's OWN sibling shared/ always comes first in the include roots,
    /// so a pack that has one can never be shadowed by an installed copy of
    /// the helpers, and a pack that has none still gets a well-defined first
    /// entry.
    void theSiblingSharedDirIsAlwaysTheFirstIncludeRoot()
    {
        const QString pack = QStringLiteral(P_SOURCE_DIR "/data/animations/fade");
        if (!QDir(pack).exists()) {
            QSKIP("data/animations not found — running outside source tree");
        }
        const QStringList roots = packSharedRoots(pack);
        // The source tree carries its marker, so it is self-contained: the
        // sibling is not only first, it is the ONLY root.
        QCOMPARE(roots, QStringList{QDir::cleanPath(QStringLiteral(P_SOURCE_DIR "/data/animations/shared"))});

        // A pack outside the canonical layout gets the sibling root and nothing
        // else: there is no family name to resolve against, so no XDG lookup
        // happens and the behaviour is exactly what it was before.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString orphan = tmp.filePath(QStringLiteral("orphan-pack"));
        QVERIFY(QDir().mkpath(orphan));
        QCOMPARE(packSharedRoots(orphan).size(), 1);
    }

    /// A pack tree with no shared/ marker is reported as UNDETECTED rather than
    /// guessed at. The caller turns that into the overlay fallback plus a
    /// message telling the author to pass a flag, which is honest; silently
    /// picking a model would put back the wrong-validator diagnostics.
    void aPackWithNoSharedMarkerIsNotDetected()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pack = tmp.filePath(QStringLiteral("orphan-pack"));
        QVERIFY(QDir().mkpath(pack));
        QVERIFY(!detectPackModel(pack).has_value());

        // A shared/ dir that exists but holds none of the four markers is the
        // same answer, not a crash or an accidental match on the first entry.
        const QString sharedDir = tmp.filePath(QStringLiteral("shared"));
        QVERIFY(QDir().mkpath(sharedDir));
        QFile stray(sharedDir + QStringLiteral("/unrelated.glsl"));
        QVERIFY(stray.open(QIODevice::WriteOnly));
        stray.close();
        QVERIFY(!detectPackModel(pack).has_value());
    }

    /// End to end over the REAL trees: every bundled pack must detect as the
    /// family it actually belongs to. The synthetic cases above pin the lookup
    /// rule, but only this catches a tree being reorganised (or a marker header
    /// renamed) out from under it, which would send a whole directory to the
    /// wrong validator.
    void everyBundledTreeDetectsAsItsOwnFamily()
    {
        struct Tree
        {
            const char* path;
            PackModel expected;
        };
        const QList<Tree> trees = {
            {P_SOURCE_DIR "/data/animations", PackModel::Animation},
            {P_SOURCE_DIR "/data/surface", PackModel::Surface},
            {P_SOURCE_DIR "/data/pointer", PackModel::Pointer},
            {P_SOURCE_DIR "/data/overlays", PackModel::Overlay},
        };

        // `continue`, not QSKIP: QSKIP returns from the whole test function, so
        // one absent tree would abandon the others silently — and a tree
        // going missing because it was renamed is exactly what this slot is
        // meant to catch. A run with no tree at all is skipped after the loop.
        // Within a tree, a pack that fails is recorded and the walk goes on,
        // so one bad pack does not hide the next.
        int treesSeen = 0;
        QStringList wrong;
        for (const Tree& tree : trees) {
            QDir root(QLatin1String(tree.path));
            if (!root.exists()) {
                continue;
            }
            ++treesSeen;
            int checked = 0;
            const QStringList subdirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString& sub : subdirs) {
                const QString packDir = root.filePath(sub);
                // Only real packs; `shared/` is a sibling helper dir, not a pack.
                if (!QFile::exists(packDir + QStringLiteral("/metadata.json"))) {
                    continue;
                }
                const std::optional<PackModel> got = detectPackModel(packDir);
                if (!got.has_value() || *got != tree.expected) {
                    wrong << packDir;
                }
                ++checked;
            }
            QVERIFY2(checked > 0, tree.path);
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QLatin1String(", "))));
        if (treesSeen == 0) {
            QSKIP("no bundled pack tree found — running outside source tree");
        }
    }
};

QTEST_MAIN(TestPackModelDetection)
#include "test_pack_model_detection.moc"
