// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFile>
#include <QString>
#include <QTemporaryDir>

namespace PlasmaZones {
namespace TestHelpers {

/**
 * @brief Helper to write a script file (e.g. `.luau`) in a given directory path
 * @param dirPath Directory path to write into
 * @param filename Name of the script file
 * @param content Script content
 * @return Full path to the written file, or empty string on failure
 *
 * `[[nodiscard]]`, and the write itself is checked, matching the three sibling
 * helpers in the LGPL test trees. Several callers assert that an id was NOT
 * registered; without a checked write and a checked result those assertions pass
 * vacuously when the file was never created, which is the same failure the
 * script under test is supposed to produce.
 */
[[nodiscard]] inline QString writeScript(const QString& dirPath, const QString& filename, const QString& content)
{
    QString path = dirPath + QStringLiteral("/") + filename;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    const QByteArray payload = content.toUtf8();
    if (f.write(payload) != payload.size())
        return QString();
    f.close();
    return path;
}

/**
 * @brief Helper to write a temporary Luau script file
 * @param dir Temporary directory to write into
 * @param filename Name of the script file
 * @param content Script content
 * @return Full path to the written file, or empty string on failure
 *
 * `[[nodiscard]]` and the write is checked, matching `writeScript` above. Several
 * callers assert that a script FAILED to load or was NOT registered; without a
 * checked write and a checked result, those assertions pass vacuously when the
 * file was never created — indistinguishable from the rejection they mean to pin.
 */
[[nodiscard]] inline QString writeTempScript(QTemporaryDir& dir, const QString& filename, const QString& content)
{
    // Byte-identical semantics to writeScript above; delegate so the checked
    // write logic lives once.
    return writeScript(dir.path(), filename, content);
}

} // namespace TestHelpers
} // namespace PlasmaZones
