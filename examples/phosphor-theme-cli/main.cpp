// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-theme-cli — headless driver for PhosphorTheme.
//
// Wraps MatugenRunner + PaletteStore + TemplateEngine so the matugen
// round-trip and template renderer can be exercised without launching
// the shell. This is the Phase 1.1 example-CLI acceptance test.

#include <PhosphorTheme/MatugenRunner.h>
#include <PhosphorTheme/PaletteStore.h>
#include <PhosphorTheme/PresetPalettes.h>
#include <PhosphorTheme/TemplateEngine.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QObject>
#include <QStandardPaths>
#include <QStringLiteral>
#include <QTextStream>
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
// without breaking the parser.
QByteArray serialisePalette(const QVariantMap& tokens)
{
    QJsonObject jtokens;
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        const QColor c = it.value().value<QColor>();
        if (!c.isValid()) {
            continue;
        }
        jtokens.insert(it.key(), c.name(QColor::HexRgb).toUpper());
    }
    QJsonObject root;
    root.insert(QLatin1String("tokens"), jtokens);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool writeAtomic(const QString& path, const QByteArray& data)
{
    // Write to a sibling temp file and rename — the watcher on the
    // primary path sees one fileChanged event, not a half-written file
    // followed by a truncate.
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString tmp = path + QStringLiteral(".tmp");
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "phosphor-theme-cli: cannot open " << tmp.toStdString()
                  << " for writing: " << f.errorString().toStdString() << '\n';
        return false;
    }
    f.write(data);
    f.close();
    // QFile::rename is atomic on POSIX when src and dst are on the same
    // filesystem — they are, since both are under AppLocalDataLocation.
    if (QFile::exists(path)) {
        QFile::remove(path);
    }
    if (!QFile::rename(tmp, path)) {
        std::cerr << "phosphor-theme-cli: cannot rename " << tmp.toStdString() << " to " << path.toStdString() << '\n';
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
    QCommandLineOption applyOpt(QStringLiteral("apply"), QStringLiteral("Write the result to current.json"));
    QCommandLineOption outOpt(QStringLiteral("out"), QStringLiteral("Write the result to a specific path"),
                              QStringLiteral("path"));
    p.addOption(modeOpt);
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

    PhosphorTheme::MatugenRunner runner;
    runner.setMode(p.value(modeOpt));

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
        QStringLiteral("Rotate through preset palettes — proves the hot-reload pipeline end-to-end"));
    QCommandLineOption intervalOpt(QStringLiteral("interval"),
                                   QStringLiteral("Milliseconds between presets (default 1500)"), QStringLiteral("ms"),
                                   QStringLiteral("1500"));
    QCommandLineOption onceOpt(QStringLiteral("once"),
                               QStringLiteral("Walk presets once and exit instead of looping forever"));
    QCommandLineOption applyOpt(QStringLiteral("apply"),
                                QStringLiteral("Write each preset to current.json (drives a running demo)"));
    QCommandLineOption outOpt(QStringLiteral("out"),
                              QStringLiteral("Write each preset to a specific path (overrides --apply)"),
                              QStringLiteral("path"));
    p.addOption(intervalOpt);
    p.addOption(onceOpt);
    p.addOption(applyOpt);
    p.addOption(outOpt);
    p.process(args);

    const PhosphorTheme::PresetPalettes presets;
    const auto names = presets.names();
    if (names.isEmpty()) {
        std::cerr << "phosphor-theme-cli: no presets available\n";
        return 1;
    }

    bool ok = false;
    const int interval = p.value(intervalOpt).toInt(&ok);
    if (!ok || interval <= 0) {
        std::cerr << "phosphor-theme-cli: --interval must be a positive integer\n";
        return 2;
    }

    const QString outPath = p.isSet(outOpt) ? p.value(outOpt) : p.isSet(applyOpt) ? defaultPalettePath() : QString();
    const bool runOnce = p.isSet(onceOpt);

    // Counter survives across timer ticks via lambda capture-by-ref.
    auto* idx = new int(0);

    auto applyNext = [&, idx]() {
        const auto name = names.at(*idx % names.size());
        const auto tokens = presets.byName(name);
        std::cerr << "phosphor-theme-cli: applying preset " << name.toStdString() << " (" << tokens.size()
                  << " tokens)\n";
        if (!outPath.isEmpty()) {
            if (!writeAtomic(outPath, serialisePalette(tokens))) {
                std::cerr << "phosphor-theme-cli: write failed; aborting cycle\n";
                QCoreApplication::exit(1);
                return;
            }
        }
        ++(*idx);
        if (runOnce && *idx >= names.size()) {
            QCoreApplication::quit();
        }
    };

    // Fire one immediately so the user sees retinting on tick zero rather
    // than after a full interval of staring at the unchanged window.
    QTimer::singleShot(0, applyNext);

    auto* loopTimer = new QTimer();
    loopTimer->setInterval(interval);
    QObject::connect(loopTimer, &QTimer::timeout, applyNext);
    loopTimer->start();

    const int rc = QCoreApplication::exec();
    delete loopTimer;
    delete idx;
    return rc;
}

void printUsage()
{
    std::cerr << R"(phosphor-theme-cli — drive the PhosphorTheme library headlessly.

USAGE
  phosphor-theme-cli set-wallpaper <image> [--mode dark|light] [--apply | --out <path>]
  phosphor-theme-cli dump [--source <palette.json>]
  phosphor-theme-cli render-template <template> <out> [--palette <palette.json>]
  phosphor-theme-cli cycle [--interval ms] [--once] [--apply | --out <path>]

Without --apply or --out, set-wallpaper prints the new palette JSON to stdout.
cycle without --apply or --out only logs each preset name — pair it with
--apply (or --out path) to drive a running phosphor-theme-demo and watch
the whole window retint every <interval> ms.

dump and render-template default to the canonical built-in palette
(plus any active current.json under ~/.local/share/phosphor/palettes/).
)";
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
