// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTextEdit;

namespace PlasmaZones {

class MetadataEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MetadataEditorWidget(QWidget* parent = nullptr);
    ~MetadataEditorWidget() override;

    void loadFromJson(const QString& json);
    QString toJson() const;

    bool isModified() const;
    void setModified(bool modified);

Q_SIGNALS:
    void modified();
    void insertUniformRequested(const QString& uniformText);

private:
    void setupUi();
    void connectSignals();
    void onAddParameter();
    void onRemoveParameter();
    void onInsertUniform();
    void markModified();

    static QString computeUniformName(const QString& type, int slot);

    // Shader Info fields
    QLineEdit* m_idEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_categoryCombo = nullptr;
    QLineEdit* m_authorEdit = nullptr;
    QLineEdit* m_versionEdit = nullptr;
    QTextEdit* m_descriptionEdit = nullptr;
    QLabel* m_fragShaderLabel = nullptr;
    QLabel* m_vertShaderLabel = nullptr;
    QCheckBox* m_multipassCheck = nullptr;

    // Parameters table
    QTableWidget* m_paramTable = nullptr;

    bool m_modified = false;
    bool m_loading = false;
    bool m_isExistingShader = false;
    QJsonObject m_originalJson;
};

} // namespace PlasmaZones
