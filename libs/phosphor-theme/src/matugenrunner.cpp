// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/MatugenRunner.h>

#include <PhosphorTheme/IThemeService.h>

#include <QColor>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QProcess>
#include <QString>
#include <QStringLiteral>

namespace PhosphorTheme {

namespace {

// Matugen emits Material 3 token names. Most align with Phosphor's
// snake_case palette already, but the M3 spec has tokens that don't
// appear in our default set (surface_dim, surface_bright, inverse_*,
// scrim, shadow, etc.). We pass those through unchanged — PaletteStore's
// merge semantics ignore unknown tokens by adding them rather than
// rejecting, which is fine for consumers that want the full M3 surface.
//
// The reverse case — Phosphor tokens absent from matugen output — is
// also fine: PaletteStore preserves the prior (default) value for any
// key not in the new map. That's how brand_stop_0..3 and the ANSI
// status colors survive a matugen run.
//
// Returned key set is exactly what matugen emitted, no remapping yet.
// Future hook: if matugen schema diverges, add the lookup table here.
QVariantMap parseColorsObject(const QJsonObject& colors)
{
    QVariantMap out;
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
        if (!it.value().isString()) {
            continue;
        }
        const QColor c(it.value().toString());
        if (!c.isValid()) {
            continue;
        }
        out.insert(it.key(), c);
    }
    return out;
}

} // namespace

MatugenRunner::MatugenRunner(QObject* parent)
    : QObject(parent)
    , m_matugenBinary(QStringLiteral("matugen"))
    , m_mode(QStringLiteral("dark"))
{
}

MatugenRunner::~MatugenRunner()
{
    // QProcess's destructor warns + kills if a child is still running;
    // explicit teardown keeps the log clean and the wait deterministic.
    teardownProcess();
}

QString MatugenRunner::matugenBinary() const
{
    return m_matugenBinary;
}

void MatugenRunner::setMatugenBinary(const QString& binary)
{
    if (binary == m_matugenBinary) {
        return;
    }
    m_matugenBinary = binary;
    Q_EMIT matugenBinaryChanged();
}

QString MatugenRunner::mode() const
{
    return m_mode;
}

void MatugenRunner::setMode(const QString& mode)
{
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    Q_EMIT modeChanged();
}

bool MatugenRunner::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void MatugenRunner::run(const QString& wallpaperPath)
{
    if (!QFileInfo::exists(wallpaperPath)) {
        Q_EMIT failed(wallpaperPath, QStringLiteral("wallpaper does not exist"));
        return;
    }

    // Drop any in-flight run before starting a new one. Wallpaper
    // changes can arrive faster than matugen finishes (e.g. user
    // mashes the cycle button), and we always want the most recent
    // request to win.
    if (isRunning()) {
        cancel();
    }

    m_pendingWallpaper = wallpaperPath;
    m_process = std::make_unique<QProcess>(this);

    // `matugen image <wallpaper> --json hex` writes the full color map
    // to stdout in a stable schema. `--json hex` is the flag in modern
    // matugen builds; older builds use `--json` alone. We don't probe
    // the binary version — if `--json hex` is rejected the run fails
    // with a clear stderr message, which is better than silently
    // falling back to an undocumented schema.
    QStringList args;
    args << QStringLiteral("image") << wallpaperPath << QStringLiteral("--json") << QStringLiteral("hex");

    connect(m_process.get(), &QProcess::errorOccurred, this, [this, wallpaperPath](QProcess::ProcessError err) {
        Q_EMIT failed(wallpaperPath, QStringLiteral("matugen process error: %1").arg(static_cast<int>(err)));
        Q_EMIT runningChanged();
    });

    connect(m_process.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, wallpaperPath](int exitCode, QProcess::ExitStatus status) {
                Q_EMIT runningChanged();
                if (!m_process) {
                    return;
                }
                if (status != QProcess::NormalExit || exitCode != 0) {
                    const auto stderrTail = QString::fromUtf8(m_process->readAllStandardError()).trimmed().right(512);
                    Q_EMIT failed(wallpaperPath, QStringLiteral("matugen exited %1: %2").arg(exitCode).arg(stderrTail));
                    return;
                }
                const auto stdoutBytes = m_process->readAllStandardOutput();
                const auto tokens = parseMatugenJson(stdoutBytes, m_mode);
                if (tokens.isEmpty()) {
                    Q_EMIT failed(wallpaperPath, QStringLiteral("matugen output had no usable colors"));
                    return;
                }
                Q_EMIT paletteReady(tokens, wallpaperPath);
            });

    m_process->start(m_matugenBinary, args);
    Q_EMIT runningChanged();
}

void MatugenRunner::cancel()
{
    teardownProcess();
    Q_EMIT runningChanged();
}

QVariantMap MatugenRunner::parseMatugenJson(const QByteArray& json, const QString& mode) const
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    const auto root = doc.object();

    // Matugen schema flavours seen in practice:
    //   1. `{ "colors": { "dark": { ... }, "light": { ... } } }`     - newer
    //   2. `{ "colors": { "primary": "...", "on_primary": "..." } }` - older / single-mode
    //   3. `{ "dark": { ... }, "light": { ... } }`                   - bare top-level
    // We try each in the order they're most likely, picking the
    // requested `mode` when both variants are present.
    QJsonObject colors;
    if (root.contains(QLatin1String("colors")) && root.value(QLatin1String("colors")).isObject()) {
        const auto colorsObj = root.value(QLatin1String("colors")).toObject();
        if (colorsObj.contains(mode) && colorsObj.value(mode).isObject()) {
            colors = colorsObj.value(mode).toObject();
        } else {
            colors = colorsObj;
        }
    } else if (root.contains(mode) && root.value(mode).isObject()) {
        colors = root.value(mode).toObject();
    } else {
        // Last resort: assume root *is* the color map.
        colors = root;
    }

    return parseColorsObject(colors);
}

void MatugenRunner::teardownProcess()
{
    if (!m_process) {
        return;
    }
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(500)) {
            m_process->kill();
            m_process->waitForFinished(500);
        }
    }
    m_process.reset();
}

} // namespace PhosphorTheme
