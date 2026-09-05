// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

namespace PhosphorLayer {
class IScreenProvider;
}

namespace PhosphorShellApp {

// The picker strip's open state, published to QML as the PickerRegistry
// context property. There is one strip per output (a PerScreenPanels
// delegate, built in a context where shell.qml's ids do not resolve), so
// which one is open has to live somewhere every delegate can read. The
// `picker` IpcTarget drives show / toggle / hide; an empty screen name
// means the focused output, else the primary.
//
// `targetCount` is the fan-out figure the strip prints: how many shell
// surfaces take the palette on Apply, from the surfaces shell.qml mounts
// per output and the ones it shares across them.
class PickerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString openScreen READ openScreen NOTIFY openScreenChanged)
    Q_PROPERTY(int targetCount READ targetCount NOTIFY openScreenChanged)

public:
    explicit PickerController(PhosphorLayer::IScreenProvider* screens, QObject* parent = nullptr);
    ~PickerController() override;

    [[nodiscard]] QString openScreen() const;
    [[nodiscard]] int targetCount() const;

    Q_INVOKABLE void show(const QString& screenName = QString());
    Q_INVOKABLE void toggle(const QString& screenName = QString());
    Q_INVOKABLE void hide();

    // What shell.qml mounts: per output the bar, the OSD overlay, the
    // toast overlay, the polkit dim and the picker strip; shared, the
    // launcher, the control center and the power menu.
    static constexpr int kSurfacesPerScreen = 5;
    static constexpr int kSharedSurfaces = 3;

Q_SIGNALS:
    void openScreenChanged();

private:
    [[nodiscard]] QString resolveScreen(const QString& screenName) const;
    void setOpenScreen(const QString& screenName);

    PhosphorLayer::IScreenProvider* m_screens;
    QString m_openScreen;
};

} // namespace PhosphorShellApp
