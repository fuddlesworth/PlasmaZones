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
    const QString content = QTextStream(&file).readAll();
    if (content.isEmpty()) {
        qCCritical(lcAutotile) << "Builtin JS resource is empty:" << name;
    }
    return content;
}

QString distributeWithGapsJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("distributeWithGaps.js"));
    return s;
}

QString distributeWithMinSizesJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("distributeWithMinSizes.js"));
    return s;
}

QString solveTwoPartJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("solveTwoPart.js"));
    return s;
}

QString solveThreeColumnJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("solveThreeColumn.js"));
    return s;
}

QString cumulativeMinDimsJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("computeCumulativeMinDims.js"));
    return s;
}

QString gracefulDegradationJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("appendGracefulDegradation.js"));
    return s;
}

QString dwindleLayoutJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("dwindleLayout.js"));
    return s;
}

QString extractMinDimsJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("extractMinDims.js"));
    return s;
}

QString interleaveStacksJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("interleaveStacks.js"));
    return s;
}

QString applyPerWindowMinSizeJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("applyPerWindowMinSize.js"));
    return s;
}

QString extractRegionMaxMinJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("extractRegionMaxMin.js"));
    return s;
}

QString fillAreaJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("fillArea.js"));
    return s;
}

QString masterStackLayoutJs()
{
    static const QString s = loadBuiltinJs(QStringLiteral("masterStackLayout.js"));
    return s;
}

} // namespace ScriptedHelpers
} // namespace PlasmaZones
