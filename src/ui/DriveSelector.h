#pragma once

#include <QWidget>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QMenu)

namespace opentree {

// Thin used/free bar used by the drive selector and its drop-down.
class CapacityBar : public QWidget {
    Q_OBJECT

public:
    explicit CapacityBar(QWidget *parent = nullptr);

    void setValues(qint64 total, qint64 free);
    QSize sizeHint() const override;
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qint64 m_total = 0;
    qint64 m_free = 0;
};

// Toolbar widget showing the current root's volume plus a drop-down of all ready
// drives with their own capacity bars.
class DriveSelector : public QWidget {
    Q_OBJECT

public:
    explicit DriveSelector(QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    QString rootPath() const;

signals:
    void driveActivated(const QString &path);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void showMenu();
    void refreshDisplay();

    QLabel *m_label;
    CapacityBar *m_bar;
    QMenu *m_menu;
    QString m_rootPath;
};

}
