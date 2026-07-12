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

/// Absolute path to the decoration-sets directory the store writes to,
/// recomputed the way DecorationPageController does. Valid only under
/// QStandardPaths test mode (see initTestCase).
inline QString decorationSetsDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userDecorationSetsSubdir());
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
