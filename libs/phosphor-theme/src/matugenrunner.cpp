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
// Flat shape: `{ "primary": "#RRGGBB", "on_primary": "...", ... }`
// Older matugen and any caller that already flattened the modes.
QVariantMap parseFlatColorsObject(const QJsonObject& colors)
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

// Nested per-token shape (matugen v4+):
//   { "primary": { "dark":  { "color": "#RRGGBB" },
//                  "light": { "color": "#RRGGBB" },
//                  "default": { "color": "#RRGGBB" } },
//     "on_primary": { ... },
//     ... }
// Each token has its own mode-keyed object. We pick `mode` (e.g. "dark");
// if the requested mode isn't present we fall back to "default" then
// "light". Tokens that don't follow the shape are silently skipped.
QVariantMap parseNestedColorsObject(const QJsonObject& colors, const QString& mode)
{
    QVariantMap out;
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject perToken = it.value().toObject();

        // Prefer the requested mode; fall back so we don't drop the whole
        // token on a slight schema mismatch.
        QJsonValue modeVal;
        for (const auto& candidate :
             {mode, QStringLiteral("default"), QStringLiteral("dark"), QStringLiteral("light")}) {
            if (perToken.contains(candidate) && perToken.value(candidate).isObject()) {
                modeVal = perToken.value(candidate);
                break;
            }
        }
        if (!modeVal.isObject()) {
            continue;
        }

        const QJsonObject modeObj = modeVal.toObject();
        const auto colorVal = modeObj.value(QLatin1String("color"));
        if (!colorVal.isString()) {
            continue;
        }
        const QColor c(colorVal.toString());
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
    , m_prefer(QStringLiteral("saturation"))
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

QString MatugenRunner::prefer() const
{
    return m_prefer;
}

void MatugenRunner::setPrefer(const QString& prefer)
{
    if (prefer == m_prefer) {
        return;
    }
    m_prefer = prefer;
    Q_EMIT preferChanged();
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
    // `matugen image <wp> --json hex` writes the full color map to stdout.
    // `--prefer <strategy>` is required when matugen would otherwise prompt
    // the user (multiple candidate source colors, non-TTY subprocess —
    // always our situation). An empty prefer means caller opted out, so we
    // skip the flag.
    QStringList args;
    args << QStringLiteral("image") << wallpaperPath << QStringLiteral("--json") << QStringLiteral("hex");
    if (!m_prefer.isEmpty()) {
        args << QStringLiteral("--prefer") << m_prefer;
    }

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

    // Matugen schema flavours seen in the wild:
    //   1. v4+:           `{ "colors": { "<token>": { "dark": {"color":"#..."},
    //                                                  "light": {...},
    //                                                  "default": {...} },
    //                                    ... } }`
    //   2. v3:            `{ "colors": { "dark": { "<token>": "#..." },
    //                                    "light": { ... } } }`
    //   3. single-mode:   `{ "colors": { "<token>": "#..." } }`
    //   4. bare top:      `{ "<mode>": { "<token>": "#..." } }`
    //   5. bare flat:     `{ "<token>": "#..." }`
    // Per-token nesting is detected by sniffing the first non-string value
    // in `colors`; that's enough to discriminate v4+ from v3 reliably.
    QJsonObject candidate;
    if (root.contains(QLatin1String("colors")) && root.value(QLatin1String("colors")).isObject()) {
        candidate = root.value(QLatin1String("colors")).toObject();
    } else if (root.contains(mode) && root.value(mode).isObject()) {
        // `<mode>` keys at root: v3 shape that's been pre-unwrapped.
        return parseFlatColorsObject(root.value(mode).toObject());
    } else {
        // Last resort: assume root *is* the flat color map.
        return parseFlatColorsObject(root);
    }

    // Probe the v3 mode-wrap first: `colors.<mode>` is a token-keyed
    // hex map. If parsing it yields any tokens, that's the schema —
    // pick that result. This intentionally takes precedence over v4+
    // nested parse so that the v3 fixture (which has a `dark` key at
    // this level) doesn't get misidentified as v4+ with a token literally
    // named "dark".
    if (candidate.contains(mode) && candidate.value(mode).isObject()) {
        QVariantMap flat = parseFlatColorsObject(candidate.value(mode).toObject());
        if (!flat.isEmpty()) {
            return flat;
        }
    }

    // v4+ per-token nesting: each token key holds an object with mode
    // sub-keys, each containing `{ "color": "#..." }`.
    QVariantMap nested = parseNestedColorsObject(candidate, mode);
    if (!nested.isEmpty()) {
        return nested;
    }

    // Last resort: treat `colors` itself as a flat token-keyed hex map
    // (older single-mode matugen output).
    return parseFlatColorsObject(candidate);
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
