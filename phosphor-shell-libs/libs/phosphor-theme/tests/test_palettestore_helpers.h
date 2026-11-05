// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// Shared scaffolding for the PaletteStore test executables. Three
// translation units consume this header:
//
//   - test_palettestore.cpp           (defaults / applyTokens /
//                                      resetToDefaults core behaviour
//                                      + loadFromJson slots)
//   - test_palettestore_files.cpp     (loadFromFile slots)
//   - test_palettestore_hotreload.cpp (hot-reload slots +
//                                      resetToDefaults_releasesDirectoryWatch)
//
// Each TU defines its own QTest subclass and QTEST_GUILESS_MAIN. The
// helpers here factor out the QTemporaryDir + QFile fixture-write
// pattern most tests rely on so the same `{"tokens": {"primary":
// "#xxxxxx"}}` payload doesn't have to be edited in three places when
// the wrapped-shape contract changes.
//
// Helpers return [[nodiscard]] bool so callers wrap them in QVERIFY at
// the call site; Qt's QVERIFY only returns from the enclosing function,
// so a helper that called QVERIFY internally would silently swallow
// failures and leave the test running against an incomplete fixture
// (same convention used by phosphor-registry's installPluginFixture).

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringLiteral>
#include <QTemporaryDir>

namespace PhosphorThemeTestHelpers {

// Default payload most PaletteStore tests rely on. Wrapped layout
// matching the canonical `{"tokens": {...}}` shape PaletteStore.h
// documents as the matugen-output contract.
inline QByteArray defaultWrappedPayload(const QByteArray& hex = "#112233")
{
    return QByteArray(R"({"tokens": {"primary": ")") + hex + QByteArray(R"("}})");
}

// Write a JSON payload to a fresh file at `path`. Returns false on any
// IO failure so the caller can QVERIFY the result at the call site.
// Truncating-write so callers can reuse the same path across an
// in-place edit without an extra remove() step.
[[nodiscard]] inline bool writeJsonFile(const QString& path, const QByteArray& payload)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const qint64 written = f.write(payload);
    return written == payload.size();
}

// Combine the QTemporaryDir setup + initial-file write that nearly
// every loadFromFile / hot-reload test repeats. `tmp` must outlive
// the returned path (caller owns the QTemporaryDir lifetime). The
// path is built off `tmp.filePath(filename)` so the file lives in
// the temporary directory and is cleaned up when `tmp` goes out of
// scope. Returns false if either the QTemporaryDir is invalid or the
// write fails.
[[nodiscard]] inline bool seedTempJsonFile(QTemporaryDir& tmp, const QString& filename, const QByteArray& payload,
                                           QString& outPath)
{
    if (!tmp.isValid()) {
        return false;
    }
    outPath = tmp.filePath(filename);
    return writeJsonFile(outPath, payload);
}

// Convenience overload using the canonical default payload.
[[nodiscard]] inline bool seedTempJsonFile(QTemporaryDir& tmp, const QString& filename, QString& outPath)
{
    return seedTempJsonFile(tmp, filename, defaultWrappedPayload(), outPath);
}

} // namespace PhosphorThemeTestHelpers
