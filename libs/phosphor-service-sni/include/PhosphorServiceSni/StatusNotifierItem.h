// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceSni/phosphorservicesni_export.h>

#include <QImage>
#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServiceSni {

/// One system-tray item. Owned by StatusNotifierHost. Wraps the DBus
/// proxy + property cache + icon resolution. The host populates these
/// from `RegisteredStatusNotifierItems` and refreshes them in response
/// to per-item NewXxx signals.
class PHOSPHORSERVICESNI_EXPORT StatusNotifierItem : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(StatusNotifierItem)
    Q_PROPERTY(QString id READ id NOTIFY idChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString category READ category NOTIFY categoryChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QImage iconImage READ iconImage NOTIFY iconChanged)
    Q_PROPERTY(QImage overlayIconImage READ overlayIconImage NOTIFY iconChanged)
    Q_PROPERTY(QImage attentionIconImage READ attentionIconImage NOTIFY iconChanged)
    Q_PROPERTY(QString toolTipTitle READ toolTipTitle NOTIFY toolTipChanged)
    Q_PROPERTY(QString toolTipBody READ toolTipBody NOTIFY toolTipChanged)
    Q_PROPERTY(QString menuPath READ menuPath NOTIFY menuPathChanged)
    Q_PROPERTY(bool itemIsMenu READ itemIsMenu NOTIFY menuPathChanged)
    Q_PROPERTY(QString dbusService READ dbusService CONSTANT)
    Q_PROPERTY(QString dbusPath READ dbusPath CONSTANT)
    Q_PROPERTY(bool isValid READ isValid NOTIFY validChanged)
    Q_PROPERTY(int preferredIconSize READ preferredIconSize WRITE setPreferredIconSize NOTIFY iconChanged)

public:
    enum class Status {
        Passive, ///< Item is dormant: could be hidden in an overflow.
        Active, ///< Item is meaningful: show it normally.
        NeedsAttention ///< Use the attention icon + maybe animate.
    };
    Q_ENUM(Status)

    /// `dbusService` is the unique bus name, `dbusPath` the object
    /// path. Watchers produce the canonical "uniqueName/object/path"
    /// concatenation; splitting it into the two args is the host's
    /// job. The item itself only ever sees the split form.
    StatusNotifierItem(const QString& dbusService, const QString& dbusPath, QObject* parent = nullptr);
    ~StatusNotifierItem() override;

    [[nodiscard]] QString id() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString category() const;
    [[nodiscard]] Status status() const;
    [[nodiscard]] QImage iconImage() const;
    [[nodiscard]] QImage overlayIconImage() const;
    [[nodiscard]] QImage attentionIconImage() const;
    [[nodiscard]] QString toolTipTitle() const;
    [[nodiscard]] QString toolTipBody() const;
    [[nodiscard]] QString menuPath() const;
    [[nodiscard]] bool itemIsMenu() const;
    [[nodiscard]] QString dbusService() const;
    [[nodiscard]] QString dbusPath() const;
    [[nodiscard]] bool isValid() const;

    /// Preferred render size in logical pixels. Passed through to the
    /// icon resolver so we don't upscale a 16x16 themed icon when the
    /// panel asks for 24x24 (XDG spec ranks "next-size-up" higher
    /// than "scaled current size"). Q_INVOKABLE so QML delegates can
    /// configure render size on the item directly (e.g. via
    /// `Component.onCompleted: trayItem.setPreferredIconSize(36)`).
    Q_INVOKABLE void setPreferredIconSize(int size);
    [[nodiscard]] int preferredIconSize() const;

public Q_SLOTS:
    void activate(int x, int y);
    void secondaryActivate(int x, int y);
    void contextMenu(int x, int y);
    void scroll(int delta, const QString& orientation);

Q_SIGNALS:
    void idChanged();
    void titleChanged();
    void categoryChanged();
    void statusChanged();
    void iconChanged();
    void toolTipChanged();
    void menuPathChanged();
    void validChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceSni
