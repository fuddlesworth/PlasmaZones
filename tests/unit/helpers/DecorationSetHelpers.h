// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file DecorationSetHelpers.h
 * @brief Shared fixtures for the decoration-set test binaries.
 *
 * test_decoration_sets covers the round-trip behaviour and
 * test_decoration_sets_validation covers the refusal paths. They share these so
 * there is exactly ONE definition of the sandbox guard: a second copy would be a
 * second place for it to rot, and it is the only thing standing between a lost
 * setTestModeEnabled and the user's real saved sets.
 */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include "config/configdefaults.h"

using namespace PlasmaZones;

/// Absolute path to this BINARY's decoration-sets sandbox, under the
/// QStandardPaths test-mode tree. Suffixed with the application name because
/// two binaries share the round-trip / validation split, ctest runs them in
/// parallel (-j in CI), and a directory keyed only on the user would have each
/// binary's init() wipe the other's in-flight files. Every test must point its
/// controller here via setSetsDirOverride().
inline QString decorationSetsDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userDecorationSetsSubdir() + QLatin1Char('-')
                           + QCoreApplication::applicationName());
}

/// Write @p root to @p path as a hand-crafted set file.
inline void writeSetFile(const QString& path, const QJsonObject& root)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    const QByteArray bytes = QJsonDocument(root).toJson();
    QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
    f.close();
}

/// A minimal valid decoration-set payload covering `window.tiled`.
inline QJsonObject validSetPayload(const QString& name, int version = 1)
{
    QJsonObject profile;
    profile.insert(QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")});
    QJsonObject entry;
    entry.insert(QStringLiteral("path"), QStringLiteral("window.tiled"));
    entry.insert(QStringLiteral("profile"), profile);

    QJsonObject root;
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("overrides"), QJsonArray{entry});
    return root;
}

/// Recursively clear the sets directory, but ONLY inside the QStandardPaths
/// sandbox. The guard lives here rather than in initTestCase because QtTest
/// still runs cleanupTestCase after initTestCase fails: a future edit that
/// dropped setTestModeEnabled would otherwise delete the user's real saved sets
/// on its way out.
inline void wipeSetsDir()
{
    const QString dir = decorationSetsDir();
    if (!dir.contains(QLatin1String("qttest"))) {
        qWarning("refusing to wipe a sets directory outside the test sandbox");
        return;
    }
    QDir(dir).removeRecursively();
}
