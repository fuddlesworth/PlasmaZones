// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_algorithm_copy_suffix.cpp
 * @brief The duplicate suffix is catalog-sourced, so it is external input.
 *
 * AlgorithmService::duplicateAlgorithm() appends a translated " (Copy)" to the
 * source algorithm's name and hands the result to
 * AlgorithmScaffold::rewriteMetadataNameId(), which embeds it in a Luau string
 * literal WITHOUT escaping — its contract is that the caller already ran
 * sanitizeMetadataString(). The source name is sanitized; the suffix has to be
 * too, or a `.qm` under ~/.local/share/locale carrying `"` or `\` in that one
 * message escapes the literal and injects Luau into the copy.
 *
 * The check here is anchored on the whole `name = "…"` line rather than a
 * substring search, so a value that closes its own quote early fails even
 * though the file still "contains" the expected text somewhere.
 */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTest>
#include <QTranslator>

#include "config/settings.h"
#include "core/types/constants.h"
#include "settings/services/algorithmservice.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorFsLoader/WatchedDirectorySet.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// A translation of " (Copy)" that tries to close the Luau string literal it
/// lands in and append a field of its own. Carries both characters
/// sanitizeMetadataString() rewrites: `"` (the literal's own terminator) and
/// `\` (which could otherwise escape one).
const QString kHostileSuffix = QStringLiteral(" \" .. pwn .. \"\\ (Copy)");

/// What kHostileSuffix must come out as: sanitizeMetadataString() maps `"`→`'`
/// and `\`→`/`, one character for one, so the text survives in readable form
/// but can no longer terminate or escape anything.
const QString kNeutralisedSuffix = QStringLiteral(" ' .. pwn .. '/ (Copy)");

/// Answers the one message under test and nothing else. A subclass rather than
/// a compiled `.qm` so the hostile string is visible right here.
class HostileTranslator : public QTranslator
{
public:
    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr,
                      int n = -1) const override
    {
        Q_UNUSED(disambiguation)
        Q_UNUSED(n)
        if (QLatin1String(context) == QLatin1String("plasmazones")
            && QLatin1String(sourceText) == QLatin1String(" (Copy)")) {
            return kHostileSuffix;
        }
        return QString();
    }

    bool isEmpty() const override
    {
        return false;
    }
};

QString sourceScript()
{
    return QStringLiteral(
        "-- SPDX-FileCopyrightText: 2026 fuddlesworth\n"
        "-- SPDX-License-Identifier: GPL-3.0-or-later\n"
        "\n"
        "local pluau = pluau\n"
        "\n"
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"Source\",\n"
        "        id = \"srcalgo\",\n"
        "        description = \"Fixture for the copy-suffix test\",\n"
        "        producesOverlappingZones = false,\n"
        "        supportsMasterCount = false,\n"
        "        supportsSplitRatio = false,\n"
        "        defaultMaxWindows = 4,\n"
        "        minimumWindows = 1,\n"
        "        zoneNumberDisplay = \"all\",\n"
        "        supportsMemory = false,\n"
        "    },\n"
        "\n"
        "    tile = function(ctx)\n"
        "        local area = ctx.area\n"
        "        local count = ctx.windowCount\n"
        "        local early = pluau.guardArea(area, count)\n"
        "        if early then return early end\n"
        "        return pluau.equalColumnsLayout(area, count, ctx.innerGap, ctx.minSizes)\n"
        "    end,\n"
        "}\n");
}

} // namespace

class TestAlgorithmCopySuffix : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// A hostile " (Copy)" translation must not be able to break out of the
    /// `name = "…"` literal the duplicate's metadata rewrite writes it into.
    void testHostileSuffixTranslationCannotEscapeTheLiteral()
    {
        IsolatedConfigGuard guard;

        // XDG_DATA_HOME is the guard's tempdir, so the algorithms subdir under
        // it is BOTH the loader's user directory and duplicateAlgorithm's
        // write destination. Pin XDG_DATA_DIRS at the same root so a real
        // system install's bundled scripts cannot join the scan.
        const QByteArray oldDataDirs = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", guard.dataPath().toUtf8());
        const auto restoreDataDirs = qScopeGuard([&oldDataDirs]() {
            if (oldDataDirs.isEmpty()) {
                qunsetenv("XDG_DATA_DIRS");
            } else {
                qputenv("XDG_DATA_DIRS", oldDataDirs);
            }
        });

        const QString algoDir = guard.dataPath() + QLatin1Char('/') + ScriptedAlgorithmSubdir;
        QVERIFY(QDir().mkpath(algoDir));
        {
            QFile source(algoDir + QStringLiteral("/srcalgo.luau"));
            QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Text));
            QVERIFY(source.write(sourceScript().toUtf8()) > 0);
        }

        PhosphorTiles::AlgorithmRegistry registry(nullptr);
        PhosphorTiles::ScriptedAlgorithmLoader loader(QString(ScriptedAlgorithmSubdir), &registry);
        loader.scanAndRegister(PhosphorFsLoader::LiveReload::Off);
        QVERIFY2(registry.algorithm(QStringLiteral("srcalgo")) != nullptr, "the fixture algorithm failed to register");

        HostileTranslator translator;
        // Held as the base type so the scope guard's lambda carries no
        // anonymous-namespace member (-Wsubobject-linkage under the unity build).
        QTranslator* const installed = &translator;
        QVERIFY(QCoreApplication::installTranslator(installed));
        const auto removeTranslator = qScopeGuard([installed]() {
            QCoreApplication::removeTranslator(installed);
        });

        Settings settings;
        AlgorithmService service(settings, registry, loader);
        QVERIFY(service.duplicateAlgorithm(QStringLiteral("srcalgo")));

        QFile copy(algoDir + QStringLiteral("/srcalgo-copy.luau"));
        QVERIFY2(copy.open(QIODevice::ReadOnly | QIODevice::Text), "the duplicate was not written");
        const QString content = QString::fromUtf8(copy.readAll());

        // Exactly one top-level name field, and its value runs from the opening
        // quote to a closing quote that ends the line's value. Unsanitized, the
        // suffix's own `"` closes the literal early and the rest of the suffix
        // spills out as Luau after it, so the line no longer matches.
        static const QRegularExpression nameLine(QStringLiteral("^\\s*name = \"([^\"\\\\]*)\",$"),
                                                 QRegularExpression::MultilineOption);
        const QRegularExpressionMatch match = nameLine.match(content);
        QVERIFY2(match.hasMatch(),
                 qPrintable(QStringLiteral("the copy's name field is not a well-formed single-line Luau string "
                                           "literal — the suffix escaped it. File:\n")
                            + content));

        // Non-vacuous: the suffix really did reach the name, neutralised rather
        // than dropped. A regression that simply stopped appending a suffix
        // would satisfy the shape check above but fail here.
        QCOMPARE(match.captured(1), QStringLiteral("Source") + kNeutralisedSuffix);
    }
};

// QTEST_MAIN, not GUILESS: Settings::load() reads QGuiApplication::palette()
// to apply the system colour scheme. Nothing here asserts on a screen, so the
// platform plugin in use makes no difference to the result.
QTEST_MAIN(TestAlgorithmCopySuffix)
#include "test_algorithm_copy_suffix.moc"
