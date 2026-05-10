// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

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

public:
    explicit ShellGlobal(QObject* parent = nullptr);
    ~ShellGlobal() override;

    [[nodiscard]] ScreenModel* screens() const;
    void setScreenModel(ScreenModel* model);

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
};

} // namespace PhosphorShell
