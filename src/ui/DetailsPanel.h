#pragma once

#include <QWidget>

#include <QVector>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QAction)
QT_FORWARD_DECLARE_CLASS(QToolButton)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QScrollArea)

namespace opentree {

class DetailsPanel : public QWidget {
    Q_OBJECT

public:
    explicit DetailsPanel(QWidget *parent = nullptr);

    void setEntry(const TreeEntry &entry);
    void clear();

private:
    QLabel *createValueLabel(bool multiLine = false);
    void setValue(QLabel *label, const QString &value);
    void updateActionState();

    TreeEntry m_currentEntry;

    QLabel *m_kindBadgeLabel;
    QLabel *m_nameLabel;
    QLabel *m_pathLabel;
    QLabel *m_sizeLabel;
    QLabel *m_allocatedLabel;
    QLabel *m_percentOfParentLabel;
    QLabel *m_filesLabel;
    QLabel *m_foldersLabel;
    QLabel *m_lastModifiedLabel;
    QLabel *m_lastAccessedLabel;
    QLabel *m_creationDateLabel;
    QLabel *m_ownerLabel;
    QLabel *m_attributesLabel;
    QLabel *m_compressionRateLabel;
    QLabel *m_permissionsLabel;
    QLabel *m_pathLengthLabel;
    QAction *m_openAction;
    QAction *m_showInExplorerAction;
    QAction *m_copyPathAction;
    QVector<QLabel *> m_allValueLabels;
};

}
