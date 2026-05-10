// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>
#include <PhosphorShell/WallpaperService.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

namespace PhosphorShell {

class PersistentProperties;
class ScreenModel;

// Exposed to QML as a context property (`PhosphorShell`) by ShellEngine —
// not registered as a QML singleton. Q_INVOKABLE methods and Q_PROPERTYs
// are still accessible through the context-property reference.
class PHOSPHORSHELL_EXPORT ShellGlobal : public QObject
{
    Q_OBJECT

    // NOTIFY (not CONSTANT) — `screens` is set lazily by ShellEngine after
    // engine construction (setScreenModel). A CONSTANT property would let
    // QML cache the initial nullptr forever even after the model arrives.
    Q_PROPERTY(ScreenModel* screens READ screens NOTIFY screensChanged)

    /// Desktop wallpaper image source for shader backgrounds. CONSTANT
    /// because the service itself is constructed in ShellGlobal's ctor
    /// and lives for the engine's lifetime — only its `image` property
    /// changes, and that has its own NOTIFY.
    Q_PROPERTY(WallpaperService* wallpaper READ wallpaper CONSTANT)

public:
    explicit ShellGlobal(QObject* parent = nullptr);
    ~ShellGlobal() override;

    [[nodiscard]] ScreenModel* screens() const;
    void setScreenModel(ScreenModel* model);

    [[nodiscard]] WallpaperService* wallpaper() const;

    Q_INVOKABLE [[nodiscard]] QObject* singleton(const QString& reloadId) const;
    void registerSingleton(const QString& reloadId, PersistentProperties* props);
    void clearSingletons();

Q_SIGNALS:
    void screensChanged();

private:
    // QPointer so external destruction (engine reload) doesn't leave us
    // dangling.
    QPointer<ScreenModel> m_screens;
    QHash<QString, QPointer<PersistentProperties>> m_singletons;
    // Owned by `this` — process-wide info but conveniently scoped to
    // the engine's lifetime so a hot-reload cleanly tears down the
    // file watcher and pending async loads with the rest of the shell.
    WallpaperService* m_wallpaper = nullptr;
};

} // namespace PhosphorShell
