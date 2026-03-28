// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithmJsBuiltins.h"
#include "core/logging.h"
#include <QFile>
#include <QString>
#include <QTextStream>

namespace PlasmaZones {
namespace ScriptedHelpers {

/**
 * @brief Load a JS builtin from a Qt resource file
 *
 * All builtin JS helpers are stored as real .js files under
 * src/autotile/algorithms/builtins/ and compiled into the binary
 * via builtins.qrc. This lets them benefit from syntax highlighting,
 * linting, and standard JS tooling — unlike the previous approach of
 * encoding JS as C++ QStringLiteral concatenations.
 */
static QString loadBuiltinJs(const QString& name)
{
    QFile file(QStringLiteral(":/builtins/") + name);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCCritical(lcAutotile) << "Failed to load builtin JS resource:" << name;
        return QString();
    }
    return QTextStream(&file).readAll();
}

QString distributeWithGapsJs()
{
    return loadBuiltinJs(QStringLiteral("distributeWithGaps.js"));
}

QString distributeWithMinSizesJs()
{
    return loadBuiltinJs(QStringLiteral("distributeWithMinSizes.js"));
}

QString solveTwoPartJs()
{
    return loadBuiltinJs(QStringLiteral("solveTwoPart.js"));
}

QString solveThreeColumnJs()
{
    return loadBuiltinJs(QStringLiteral("solveThreeColumn.js"));
}

QString cumulativeMinDimsJs()
{
    return loadBuiltinJs(QStringLiteral("computeCumulativeMinDims.js"));
}

QString gracefulDegradationJs()
{
    return loadBuiltinJs(QStringLiteral("appendGracefulDegradation.js"));
}

QString dwindleLayoutJs()
{
    return loadBuiltinJs(QStringLiteral("dwindleLayout.js"));
}

QString extractMinDimsJs()
{
    return loadBuiltinJs(QStringLiteral("extractMinDims.js"));
}

} // namespace ScriptedHelpers
} // namespace PlasmaZones
