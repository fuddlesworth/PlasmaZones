// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shaderpackageio.h"

#include <QDialog>

class QQuickWidget;

namespace PlasmaZones {

class NewPackageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewPackageDialog(QWidget* parent = nullptr);
    ~NewPackageDialog() override;

    QString shaderName() const;
    QString shaderId() const;
    ShaderFeatures selectedFeatures() const;
    QString category() const;
    QString description() const;
    QString author() const;

    // Exposed to QML
    Q_INVOKABLE QString sanitizeId(const QString& name) const;
    Q_INVOKABLE QString fixedFontFamily() const;

private:
    void setupUi();

    QQuickWidget* m_quickWidget = nullptr;
};

} // namespace PlasmaZones
