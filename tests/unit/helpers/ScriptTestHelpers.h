// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFile>
#include <QString>
#include <QTemporaryDir>

namespace PlasmaZones {
namespace TestHelpers {

/**
 * @brief Helper to write a temporary JS script file
 * @param dir Temporary directory to write into
 * @param filename Name of the script file
 * @param content Script content
 * @return Full path to the written file, or empty string on failure
 */
inline QString writeTempScript(QTemporaryDir& dir, const QString& filename, const QString& content)
{
    QString path = dir.path() + QStringLiteral("/") + filename;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    f.write(content.toUtf8());
    f.close();
    return path;
}

/**
 * @brief Helper to write a .js script file in a given directory path
 * @param dirPath Directory path to write into
 * @param filename Name of the script file
 * @param content Script content
 * @return Full path to the written file, or empty string on failure
 */
inline QString writeScript(const QString& dirPath, const QString& filename, const QString& content)
{
    QString path = dirPath + QStringLiteral("/") + filename;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    f.write(content.toUtf8());
    f.close();
    return path;
}

} // namespace TestHelpers
} // namespace PlasmaZones
