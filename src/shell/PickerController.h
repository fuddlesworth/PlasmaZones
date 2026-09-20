// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
namespace PhosphorLayer {
class IScreenProvider;
}
namespace PhosphorShellApp {
// Appearance's navigation and modal state survive a QML geometry reload.
// Both appearance and the existing picker IPC commands open this workspace.
class PickerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString openScreen READ openScreen NOTIFY openScreenChanged)
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)
    Q_PROPERTY(QString page MEMBER m_page NOTIFY pageChanged)
    Q_PROPERTY(QString chosenWidget MEMBER m_chosenWidget NOTIFY chosenWidgetChanged)
    Q_PROPERTY(QString selectedScreen MEMBER m_selectedScreen NOTIFY selectedScreenChanged)
    Q_PROPERTY(bool linked MEMBER m_linked NOTIFY linkedChanged)
    Q_PROPERTY(bool desktopPreview MEMBER m_desktopPreview NOTIFY desktopPreviewChanged)
    Q_PROPERTY(bool closePending READ closePending NOTIFY closePendingChanged)
    Q_PROPERTY(QVariantMap scrollPositions MEMBER m_scrollPositions NOTIFY scrollPositionsChanged)
public:
    explicit PickerController(PhosphorLayer::IScreenProvider* screens, QObject* parent = nullptr);
    ~PickerController() override;
    QString openScreen() const
    {
        return m_openScreen;
    }
    QVariantList screens() const;
    bool closePending() const
    {
        return m_closePending;
    }
    Q_INVOKABLE void show(const QString& screenName = QString());
    Q_INVOKABLE void toggle(const QString& screenName = QString());
    Q_INVOKABLE void hide();
    Q_INVOKABLE void discard();
    Q_INVOKABLE bool apply(bool close = false);
    Q_INVOKABLE void keepEditing();
Q_SIGNALS:
    void openScreenChanged();
    void screensChanged();
    void pageChanged();
    void chosenWidgetChanged();
    void selectedScreenChanged();
    void linkedChanged();
    void desktopPreviewChanged();
    void closePendingChanged();
    void scrollPositionsChanged();

private:
    QString resolveScreen(const QString& screenName) const;
    void setOpenScreen(const QString& screenName);
    PhosphorLayer::IScreenProvider* m_screens;
    QString m_openScreen;
    QString m_page = QStringLiteral("wallpaper");
    QString m_chosenWidget = QStringLiteral("workspaces");
    QString m_selectedScreen;
    bool m_linked = false;
    bool m_desktopPreview = false;
    bool m_closePending = false;
    QVariantMap m_scrollPositions;
};
}
