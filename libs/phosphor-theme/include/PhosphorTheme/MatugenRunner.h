// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/phosphortheme_export.h>

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QProcess;

namespace PhosphorTheme {

// Spawns the external `matugen` binary on a wallpaper path, captures its
// JSON output, parses it into a `token name -> QColor` map, and emits
// `paletteReady` for the caller (typically a PaletteStore) to apply.
//
// MatugenRunner deliberately does NOT couple to PaletteStore directly:
// the runner emits the parsed map, and the caller decides whether to
// merge into the live palette, persist it to disk, or both. This keeps
// the matugen integration testable without spinning up a full theme
// service, and lets the CLI (which only wants the map) reuse the runner
// without dragging in QML.
//
// Failure cases:
//   - matugen not on PATH                       → `failed` with reason
//   - wallpaper path missing                    → `failed`
//   - matugen exits non-zero                    → `failed` with stderr tail
//   - JSON unparseable / unexpected schema      → `failed` with reason
//
// All failure paths leave any concurrent run state cleaned up.
class PHOSPHORTHEME_EXPORT MatugenRunner : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // matugen invocation: `matugenBinary` is resolved on PATH unless an
    // absolute path is given. Defaults to "matugen".
    Q_PROPERTY(QString matugenBinary READ matugenBinary WRITE setMatugenBinary NOTIFY matugenBinaryChanged)

    // Dark / light variant selection — matugen emits both; we pick the
    // requested one before exposing. Default: "dark".
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)

    // True while a `run()` call is in flight. Bind UI busy indicators here.
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)

public:
    explicit MatugenRunner(QObject* parent = nullptr);
    ~MatugenRunner() override;

    [[nodiscard]] QString matugenBinary() const;
    void setMatugenBinary(const QString& binary);

    [[nodiscard]] QString mode() const;
    void setMode(const QString& mode);

    [[nodiscard]] bool isRunning() const;

    // Spawn matugen on `wallpaperPath`. Emits `paletteReady` on success,
    // `failed` on any error path. A second `run` while one is already in
    // flight cancels the previous run before starting.
    Q_INVOKABLE void run(const QString& wallpaperPath);

    // Cancel an in-flight run. Safe to call when nothing is running.
    Q_INVOKABLE void cancel();

    // Parse a matugen JSON byte array directly — no subprocess. Exposed
    // for tests and for shells that drive matugen via a different
    // mechanism (e.g. an existing CLI pipeline). Returns the token map;
    // empty on parse failure (no signal fires).
    [[nodiscard]] QVariantMap parseMatugenJson(const QByteArray& json, const QString& mode) const;

Q_SIGNALS:
    void matugenBinaryChanged();
    void modeChanged();
    void runningChanged();

    // The matugen invocation completed and yielded a non-empty token map.
    // The map has Phosphor-canonical snake_case keys; any matugen names
    // that don't round-trip have already been remapped.
    void paletteReady(const QVariantMap& tokens, const QString& wallpaperPath);

    // Anything that prevented `paletteReady` from firing — missing
    // binary, non-zero exit, parse error. `reason` is human-readable.
    void failed(const QString& wallpaperPath, const QString& reason);

private:
    void teardownProcess();

    QString m_matugenBinary;
    QString m_mode;
    QString m_pendingWallpaper;
    std::unique_ptr<QProcess> m_process;
};

} // namespace PhosphorTheme
