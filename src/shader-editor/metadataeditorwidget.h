// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

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
    void markModified();

    void onAddParameter();
    void onRemoveParameter();
    void onInsertUniform();
    void onMoveParameterUp();
    void onMoveParameterDown();
    void showParameterContextMenu(const QPoint& pos);
    void addParameterRow(const QJsonObject& param);

    static QString formatDefaultValue(const QJsonObject& param);

    // Shader Info fields
    QLabel* m_idLabel = nullptr;
    QLineEdit* m_idEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_categoryEdit = nullptr;
    QLineEdit* m_authorEdit = nullptr;
    QLineEdit* m_versionEdit = nullptr;
    QLineEdit* m_descriptionEdit = nullptr;
    QCheckBox* m_multipassCheck = nullptr;

    // Parameters
    QTreeWidget* m_paramTree = nullptr;
    QMenu* m_paramContextMenu = nullptr;

    bool m_modified = false;
    bool m_loading = false;
    bool m_isExistingShader = false;
    QJsonObject m_originalJson;
};

} // namespace PlasmaZones
