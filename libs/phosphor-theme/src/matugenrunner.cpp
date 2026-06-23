// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/MatugenRunner.h>

#include <QColor>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QProcess>
#include <QString>
#include <QTimer>

// Token-mapping policy. Matugen emits Material 3 token names. Most of
// them align with Phosphor's snake_case palette directly. Tokens we
// don't expose in TokenNames pass through unchanged. Examples include
// surface_dim, scrim, and the inverse_* triad. PaletteStore stores
// them and Theme.qml ignores them. Phosphor extensions absent from
// matugen keep their previous value thanks to PaletteStore's merge
// semantics. Those extensions are brand_stop_0 through brand_stop_3
// and the ANSI status colors. No name remapping happens here. If
// matugen ever diverges, add a lookup table at the top of
// `parse*ColorsObject`.

namespace PhosphorTheme {

namespace {

// Human-readable mapping for QProcess error codes. The raw enum integer
// is opaque to end users ("matugen process error: 0" is meaningless);
// this gives the status bar a string a maintainer can act on without
// looking up the enum.
QString processErrorString(QProcess::ProcessError err)
{
    switch (err) {
    case QProcess::FailedToStart:
        return QStringLiteral("failed to start (binary not on PATH or not executable)");
    case QProcess::Crashed:
        return QStringLiteral("crashed");
    case QProcess::Timedout:
        return QStringLiteral("timed out");
    case QProcess::WriteError:
        return QStringLiteral("write error");
    case QProcess::ReadError:
        return QStringLiteral("read error");
    case QProcess::UnknownError:
        return QStringLiteral("unknown error");
    }
    return QStringLiteral("error %1").arg(static_cast<int>(err));
}

// Flat shape: `{ "primary": "#RRGGBB", "on_primary": "...", ... }`.
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

void MatugenRunner::run(const QUrl& wallpaperUrl)
{
    if (!wallpaperUrl.isLocalFile()) {
        Q_EMIT failed(wallpaperUrl.toString(), QStringLiteral("wallpaper URL is not a local file"));
        return;
    }
    run(wallpaperUrl.toLocalFile());
}

void MatugenRunner::run(const QString& wallpaperPath)
{
    if (!QFileInfo::exists(wallpaperPath)) {
        Q_EMIT failed(wallpaperPath, QStringLiteral("wallpaper does not exist"));
        return;
    }

    // Normalise to an absolute path before handing to matugen. A relative
    // path like "-weird.jpg" would otherwise be parsed by clap as a flag.
    // QFileInfo::absoluteFilePath always returns a `/`-prefixed string for
    // any existing local file, so flag injection at the subprocess
    // boundary is structurally impossible. We don't use canonicalFilePath
    // because symlink resolution would surprise users who deliberately
    // pass a symlinked wallpaper.
    const QString absolutePath = QFileInfo(wallpaperPath).absoluteFilePath();

    // Drop any in-flight run before starting a new one. Wallpaper
    // changes can arrive faster than matugen finishes. The most recent
    // request always wins. cancelInflight() does the disconnect-and-kill
    // without blocking the GUI thread.
    if (m_process) {
        cancelInflight();
    }

    m_pendingWallpaper = absolutePath;
    m_process = std::make_unique<QProcess>(this);

    // `matugen image <wp> --json hex` writes the full color map to stdout.
    // `--prefer <strategy>` is required when matugen would otherwise prompt
    // the user. Matugen runs non-interactively because we invoke it as a
    // subprocess with no TTY. Without this flag it refuses to choose
    // between candidate source colors. An empty prefer means the caller
    // opted out so we skip the flag. We don't probe the binary version.
    // If `--json hex` is rejected the run fails with a clear stderr
    // message. That's better than silently falling back to an
    // undocumented schema.
    QStringList args;
    args << QStringLiteral("image") << absolutePath << QStringLiteral("--json") << QStringLiteral("hex");
    if (!m_prefer.isEmpty()) {
        args << QStringLiteral("--prefer") << m_prefer;
    }

    QProcess* proc = m_process.get();

    connect(proc, &QProcess::errorOccurred, this, [this, proc, absolutePath](QProcess::ProcessError err) {
        // The error may arrive before or after `finished`. Guard
        // against double-emit when both fire. FailedToStart only
        // emits errorOccurred. Crashed emits both.
        if (m_process.get() != proc) {
            return;
        }
        Q_EMIT failed(absolutePath, QStringLiteral("matugen %1").arg(processErrorString(err)));
        disposeProcess();
        emitRunningChangedIfTransitioned();
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, absolutePath](int exitCode, QProcess::ExitStatus status) {
                // A concurrent cancel() may have replaced or reset m_process
                // between the queued `finished` signal being posted and us
                // running. Compare identity rather than truthiness.
                if (m_process.get() != proc) {
                    return;
                }
                const auto stderrTail = QString::fromUtf8(proc->readAllStandardError()).trimmed().right(512);
                const auto stdoutBytes = proc->readAllStandardOutput();
                // Drop our owning reference first so isRunning() reads false
                // by the time consumers react to the signals below.
                disposeProcess();

                if (status != QProcess::NormalExit || exitCode != 0) {
                    const auto message = stderrTail.isEmpty()
                        ? QStringLiteral("matugen exited %1 with no stderr output").arg(exitCode)
                        : QStringLiteral("matugen exited %1: %2").arg(exitCode).arg(stderrTail);
                    Q_EMIT failed(absolutePath, message);
                    emitRunningChangedIfTransitioned();
                    return;
                }
                const auto tokens = parseMatugenJson(stdoutBytes, m_mode);
                if (tokens.isEmpty()) {
                    Q_EMIT failed(absolutePath, QStringLiteral("matugen output had no usable colors"));
                    emitRunningChangedIfTransitioned();
                    return;
                }
                Q_EMIT paletteReady(tokens, absolutePath);
                emitRunningChangedIfTransitioned();
            });

    proc->start(m_matugenBinary, args);
    emitRunningChangedIfTransitioned();
}

void MatugenRunner::cancel()
{
    if (!m_process) {
        return;
    }
    cancelInflight();
    emitRunningChangedIfTransitioned();
}

void MatugenRunner::emitRunningChangedIfTransitioned()
{
    const bool currently = isRunning();
    if (currently != m_lastEmittedRunning) {
        m_lastEmittedRunning = currently;
        Q_EMIT runningChanged();
    }
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
    // hex map. If parsing it yields any tokens, that's the schema ,
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
    // Destructor-only path. We MUST block here. deleteLater would miss
    // the upcoming event loop teardown. Qt would then warn and possibly
    // leak the QProcess if the child were still running when *this
    // died. GUI-thread callers use cancelInflight() instead.
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
    disposeProcess();
}

void MatugenRunner::cancelInflight()
{
    // Async cancel for the GUI hot path. waitForFinished() on the GUI
    // thread blocks the event loop for up to 1s per pending process,
    // freezing the UI when a user rapidly cycles wallpapers. We
    // disconnect this from the QProcess so its lingering signals
    // become no-ops in the lambdas via the proc-identity guard, then
    // terminate the child. disposeProcess wires the QProcess up to
    // self-destruct once the OS reports it finished, so we never call
    // ~QProcess while the child is still running.
    if (!m_process) {
        return;
    }
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
    }
    disposeProcess();
}

void MatugenRunner::disposeProcess()
{
    if (!m_process) {
        return;
    }
    // Release ownership and disconnect this slot chain so a queued
    // signal already in flight resolves to a no-op. The proc-identity
    // check in the lambdas catches the case where m_process has been
    // replaced. The released QProcess is self-managed from here on. It
    // self-destructs when QProcess::finished fires from the OS. A
    // safety-net singleShot of 5 seconds also schedules deleteLater so
    // the object cannot leak even if the child somehow never reports
    // finished (orphaned by zombie reaping, kernel bug). ~QProcess
    // would otherwise warn and block when called before the child
    // exited.
    //
    // Reparenting to nullptr is critical. If we leave the released
    // QProcess parented to `this`, ~MatugenRunner reclaims it as a
    // QObject child and deletes it directly. That brings back the
    // exact warn-and-block we orphaned the process to avoid.
    QProcess* released = m_process.release();
    released->setParent(nullptr);
    released->disconnect(this);
    connect(released, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), released, &QObject::deleteLater);
    QTimer::singleShot(5000, released, &QObject::deleteLater);
}

} // namespace PhosphorTheme
