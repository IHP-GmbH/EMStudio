#ifndef QLINEEDITD2_H
#define QLINEEDITD2_H

#include <QLineEdit>

class QLineEditD2 : public QLineEdit
{
    Q_OBJECT
public:
    QLineEditD2(QWidget *parent = 0) : QLineEdit(parent) {}

signals:
    void mouseDoubleClickSignal(QMouseEvent * e);
protected:
    void mouseDoubleClickEvent(QMouseEvent * e);
private slots:
    //void mouseDoubleClickEvent();
private:
};

#endif
