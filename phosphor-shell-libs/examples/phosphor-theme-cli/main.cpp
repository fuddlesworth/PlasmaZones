// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-theme-cli, headless driver for PhosphorTheme.
//
// Wraps MatugenRunner + PaletteStore + TemplateEngine so the matugen
// round-trip and template renderer can be exercised without launching
// the shell. This is the Phase 1.1 example-CLI acceptance test.

#include <PhosphorTheme/MatugenRunner.h>
#include <PhosphorTheme/PaletteStore.h>
#include <PhosphorTheme/TemplateEngine.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <iostream>

namespace {

QString defaultPaletteDir()
{
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("palettes"));
}

QString defaultPalettePath()
{
    return QDir(defaultPaletteDir()).filePath(QStringLiteral("current.json"));
}

// Serialise a token map back to the on-disk JSON shape PaletteStore reads.
// Uses the wrapped `{ "tokens": { ... } }` form so future metadata
// (source wallpaper path, generation timestamp) has a place to live
// without breaking the parser. Tokens with non-opaque alpha serialize
// in `#AARRGGBB` form so round-trip preserves transparency. Opaque
// tokens stay in the shorter `#RRGGBB` form for readability.
QByteArray serialisePalette(const QVariantMap& tokens)
{
    QJsonObject jtokens;
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        const QColor c = it.value().value<QColor>();
        if (!c.isValid()) {
            std::cerr << "phosphor-theme-cli: dropping non-color token '" << it.key().toStdString()
                      << "' from serialised palette\n";
            continue;
        }
        const QString hex = c.alpha() == 255 ? c.name(QColor::HexRgb).toUpper() : c.name(QColor::HexArgb).toUpper();
        jtokens.insert(it.key(), hex);
    }
    QJsonObject root;
    root.insert(QLatin1String("tokens"), jtokens);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool writeAtomic(const QString& path, const QByteArray& data)
{
    // QSaveFile writes to a sibling temp file and commits via rename(2)
    // on POSIX, which atomically replaces the destination. A reader (or
    // a QFileSystemWatcher) between commits sees either the old file or
    // the new file, never an absent or half-written one, and the
    // watcher fires exactly once per commit.
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "phosphor-theme-cli: cannot open " << path.toStdString()
                  << " for writing: " << f.errorString().toStdString() << '\n';
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        std::cerr << "phosphor-theme-cli: commit failed for " << path.toStdString() << ": "
                  << f.errorString().toStdString() << '\n';
        return false;
    }
    return true;
}

int runSetWallpaper(const QStringList& args)
{
    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("Run matugen on a wallpaper image"));
    p.addPositionalArgument(QStringLiteral("wallpaper"), QStringLiteral("Image path"));
    QCommandLineOption modeOpt({QStringLiteral("m"), QStringLiteral("mode")}, QStringLiteral("dark|light"),
                               QStringLiteral("mode"), QStringLiteral("dark"));
    QCommandLineOption preferOpt(
        QStringLiteral("prefer"),
        QStringLiteral("matugen candidate-color preference (default 'saturation'). One of "
                       "darkness|lightness|saturation|less-saturation|value|closest-to-fallback"),
        QStringLiteral("strategy"), QStringLiteral("saturation"));
    QCommandLineOption applyOpt(QStringLiteral("apply"), QStringLiteral("Write the result to current.json"));
    QCommandLineOption outOpt(QStringLiteral("out"), QStringLiteral("Write the result to a specific path"),
                              QStringLiteral("path"));
    p.addOption(modeOpt);
    p.addOption(preferOpt);
    p.addOption(applyOpt);
    p.addOption(outOpt);
    p.process(args);

    // QCommandLineParser strips the program name (subArgs[0]) so the
    // positional list contains only the remaining args. Wallpaper is at
    // index 0.
    const auto positional = p.positionalArguments();
    if (positional.isEmpty()) {
        std::cerr << "usage: phosphor-theme-cli set-wallpaper <image>\n";
        return 2;
    }
    const auto wallpaper = positional.at(0);

    const auto mode = p.value(modeOpt);
    if (mode != QLatin1String("dark") && mode != QLatin1String("light")) {
        std::cerr << "phosphor-theme-cli: --mode must be 'dark' or 'light' (got '" << mode.toStdString() << "')\n";
        return 2;
    }

    PhosphorTheme::MatugenRunner runner;
    runner.setMode(mode);
    runner.setPrefer(p.value(preferOpt));

    int exitCode = 0;
    QObject::connect(&runner, &PhosphorTheme::MatugenRunner::paletteReady,
                     [&](const QVariantMap& tokens, const QString& wp) {
                         const auto json = serialisePalette(tokens);
                         if (p.isSet(outOpt)) {
                             if (!writeAtomic(p.value(outOpt), json)) {
                                 exitCode = 1;
                             }
                         } else if (p.isSet(applyOpt)) {
                             if (!writeAtomic(defaultPalettePath(), json)) {
                                 exitCode = 1;
                             }
                         } else {
                             std::cout.write(json.constData(), json.size());
                             std::cout.put('\n');
                         }
                         std::cerr << "phosphor-theme-cli: applied palette from " << wp.toStdString() << " ("
                                   << tokens.size() << " tokens)\n";
                         QCoreApplication::quit();
                     });
    QObject::connect(&runner, &PhosphorTheme::MatugenRunner::failed, [&](const QString& wp, const QString& reason) {
        std::cerr << "phosphor-theme-cli: failed on " << wp.toStdString() << ": " << reason.toStdString() << '\n';
        exitCode = 1;
        QCoreApplication::quit();
    });

    QTimer::singleShot(0, [&]() {
        runner.run(wallpaper);
    });
    QCoreApplication::exec();
    return exitCode;
}

int runDump(const QStringList& args)
{
    QCommandLineParser p;
    QCommandLineOption srcOpt(QStringLiteral("source"),
                              QStringLiteral("Palette JSON to load (defaults to current.json)"),
                              QStringLiteral("path"));
    p.addOption(srcOpt);
    p.process(args);

    PhosphorTheme::PaletteStore store;
    QObject::connect(&store, &PhosphorTheme::PaletteStore::loadError, [](const QString& path, const QString& reason) {
        std::cerr << "phosphor-theme-cli: failed to load " << path.toStdString() << ", " << reason.toStdString()
                  << '\n';
    });
    if (p.isSet(srcOpt)) {
        if (!store.loadFromFile(p.value(srcOpt))) {
            return 1;
        }
    } else if (QFile::exists(defaultPalettePath())) {
        store.loadFromFile(defaultPalettePath());
    }
    // else: built-in defaults are already loaded.

    const auto json = serialisePalette(store.palette());
    std::cout.write(json.constData(), json.size());
    std::cout.put('\n');
    return 0;
}

int runRenderTemplate(const QStringList& args)
{
    QCommandLineParser p;
    p.addPositionalArgument(QStringLiteral("template"), QStringLiteral("Input template path"));
    p.addPositionalArgument(QStringLiteral("out"), QStringLiteral("Output path"));
    QCommandLineOption paletteOpt(QStringLiteral("palette"), QStringLiteral("Palette JSON to render against"),
                                  QStringLiteral("path"));
    p.addOption(paletteOpt);
    p.process(args);

    const auto positional = p.positionalArguments();
    if (positional.size() < 2) {
        std::cerr << "usage: phosphor-theme-cli render-template <template> <out>\n";
        return 2;
    }
    const auto templatePath = positional.at(0);
    const auto outPath = positional.at(1);

    PhosphorTheme::PaletteStore store;
    QObject::connect(&store, &PhosphorTheme::PaletteStore::loadError, [](const QString& path, const QString& reason) {
        std::cerr << "phosphor-theme-cli: failed to load " << path.toStdString() << ", " << reason.toStdString()
                  << '\n';
    });
    if (p.isSet(paletteOpt)) {
        if (!store.loadFromFile(p.value(paletteOpt))) {
            return 1;
        }
    } else if (QFile::exists(defaultPalettePath())) {
        store.loadFromFile(defaultPalettePath());
    }

    if (!PhosphorTheme::TemplateEngine::renderFile(templatePath, outPath, store.palette())) {
        return 1;
    }
    std::cerr << "phosphor-theme-cli: rendered " << templatePath.toStdString() << " -> " << outPath.toStdString()
              << '\n';
    return 0;
}

int runCycle(const QStringList& args)
{
    QCommandLineParser p;
    p.setApplicationDescription(
        QStringLiteral("Rotate through palette JSON files in a directory, proves the hot-reload pipeline"));
    p.addPositionalArgument(QStringLiteral("dir"), QStringLiteral("Directory of *.json palette files"));
    QCommandLineOption intervalOpt(QStringLiteral("interval"),
                                   QStringLiteral("Milliseconds between palettes (default 1500)"), QStringLiteral("ms"),
                                   QStringLiteral("1500"));
    QCommandLineOption onceOpt(QStringLiteral("once"),
                               QStringLiteral("Walk the directory once and exit instead of looping forever"));
    QCommandLineOption applyOpt(QStringLiteral("apply"),
                                QStringLiteral("Write each palette to current.json (drives a running demo)"));
    QCommandLineOption outOpt(QStringLiteral("out"),
                              QStringLiteral("Write each palette to a specific path (overrides --apply)"),
                              QStringLiteral("path"));
    p.addOption(intervalOpt);
    p.addOption(onceOpt);
    p.addOption(applyOpt);
    p.addOption(outOpt);
    p.process(args);

    const auto positional = p.positionalArguments();
    if (positional.isEmpty()) {
        std::cerr << "usage: phosphor-theme-cli cycle <dir> [--interval ms] [--once] [--apply | --out <path>]\n";
        return 2;
    }

    // Enumerate *.json files in alphabetical order so successive runs
    // produce the same cycle order (deterministic for screenshots / demos).
    const QDir dir(positional.at(0));
    if (!dir.exists()) {
        std::cerr << "phosphor-theme-cli: directory does not exist: " << positional.at(0).toStdString() << '\n';
        return 1;
    }
    const auto entries = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (entries.isEmpty()) {
        std::cerr << "phosphor-theme-cli: no *.json files in " << positional.at(0).toStdString() << '\n';
        return 1;
    }

    bool ok = false;
    const int interval = p.value(intervalOpt).toInt(&ok);
    if (!ok || interval <= 0) {
        std::cerr << "phosphor-theme-cli: --interval must be a positive integer\n";
        return 2;
    }

    QString outPath;
    if (p.isSet(outOpt)) {
        outPath = p.value(outOpt);
    } else if (p.isSet(applyOpt)) {
        outPath = defaultPalettePath();
    }
    const bool runOnce = p.isSet(onceOpt);

    // Require an output destination unless the user asked for a single
    // pass. Without --apply / --out the loop would just print token
    // counts forever and never drive a running demo, which is the only
    // documented use case for the looping mode.
    if (outPath.isEmpty() && !runOnce) {
        std::cerr
            << "phosphor-theme-cli: cycle in loop mode requires --apply or --out (or pass --once for a dry run)\n";
        return 2;
    }

    int idx = 0;
    QTimer loopTimer;
    loopTimer.setInterval(interval);

    auto applyNext = [entries, outPath, runOnce, &idx, &loopTimer]() {
        const auto& entry = entries.at(idx % entries.size());
        // Validate by loading through PaletteStore. A broken JSON file
        // logs an error and skips to the next tick rather than crashing
        // the cycle.
        PhosphorTheme::PaletteStore store;
        if (!store.loadFromFile(entry.absoluteFilePath())) {
            std::cerr << "phosphor-theme-cli: skipping " << entry.fileName().toStdString()
                      << " (not a valid palette JSON)\n";
        } else {
            std::cerr << "phosphor-theme-cli: applying " << entry.fileName().toStdString() << " ("
                      << store.palette().size() << " tokens)\n";
            if (!outPath.isEmpty()) {
                if (!writeAtomic(outPath, serialisePalette(store.palette()))) {
                    std::cerr << "phosphor-theme-cli: write failed; aborting cycle\n";
                    loopTimer.stop();
                    QCoreApplication::exit(1);
                    return;
                }
            }
        }
        ++idx;
        if (runOnce && idx >= entries.size()) {
            // Stop the timer before quitting. Otherwise a queued timeout
            // re-enters applyNext after the QCoreApplication::quit()
            // event is posted. That tick would land first and apply
            // the first file a second time.
            loopTimer.stop();
            QCoreApplication::quit();
        }
    };

    QObject::connect(&loopTimer, &QTimer::timeout, applyNext);
    // Fire one immediately so the user sees retinting on tick zero
    // rather than after a full interval.
    QTimer::singleShot(0, applyNext);
    loopTimer.start();

    return QCoreApplication::exec();
}

void printUsage()
{
    // Compute the active palette directory at runtime so the help text
    // matches what dump/render-template actually consult. Hardcoding the
    // path drifts whenever QStandardPaths resolves differently for the
    // current org/app names.
    const auto palettePathHint = defaultPalettePath();
    std::cerr << "phosphor-theme-cli, drive the PhosphorTheme library headlessly.\n\n"
              << "USAGE\n"
              << "  phosphor-theme-cli set-wallpaper <image> [--mode dark|light] [--prefer <strategy>] "
                 "[--apply | --out <path>]\n"
              << "  phosphor-theme-cli dump [--source <palette.json>]\n"
              << "  phosphor-theme-cli render-template <template> <out> [--palette <palette.json>]\n"
              << "  phosphor-theme-cli cycle <dir> [--interval ms] [--once] [--apply | --out <path>]\n\n"
              << "Without --apply or --out, set-wallpaper prints the new palette JSON to stdout.\n"
              << "cycle iterates *.json files in <dir> alphabetically; without --apply or\n"
              << "--out it just logs each file. Pair it with --apply (or --out <path>) to\n"
              << "drive a running phosphor-theme-demo and watch the whole window retint\n"
              << "every <interval> ms.\n\n"
              << "dump and render-template default to the canonical built-in palette\n"
              << "(plus any active current.json under " << palettePathHint.toStdString() << ").\n";
}

} // namespace

int main(int argc, char* argv[])
{
    // MatugenRunner uses QProcess. QCoreApplication is enough for that
    // and for QColor parsing; QGuiApplication isn't needed here since
    // there's no UI thread.
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor"));

    const auto args = QCoreApplication::arguments();
    if (args.size() < 2) {
        printUsage();
        return 2;
    }
    const auto cmd = args.at(1);

    QStringList subArgs;
    subArgs << args.at(0);
    for (qsizetype i = 2; i < args.size(); ++i) {
        subArgs << args.at(i);
    }

    if (cmd == QLatin1String("set-wallpaper")) {
        return runSetWallpaper(subArgs);
    }
    if (cmd == QLatin1String("dump")) {
        return runDump(subArgs);
    }
    if (cmd == QLatin1String("render-template")) {
        return runRenderTemplate(subArgs);
    }
    if (cmd == QLatin1String("cycle")) {
        return runCycle(subArgs);
    }
    if (cmd == QLatin1String("-h") || cmd == QLatin1String("--help") || cmd == QLatin1String("help")) {
        printUsage();
        return 0;
    }
    std::cerr << "phosphor-theme-cli: unknown subcommand '" << cmd.toStdString() << "'\n\n";
    printUsage();
    return 2;
}
