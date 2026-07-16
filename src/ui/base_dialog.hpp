#pragma once

#include <qglobal.h>

#ifdef Q_OS_IOS

#include <QWidget>

class BaseDialog : public QWidget {
    Q_OBJECT
public:
    explicit BaseDialog(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags()) 
        : QWidget(parent, f) {}

public slots:
    virtual void accept() { emit accepted(); emit finished(1); hide(); }
    virtual void reject() { emit rejected(); emit finished(0); hide(); }

public:
    void setModal(bool) {}
    
signals:
    void accepted();
    void rejected();
    void finished(int result);
};

#else

#include <QDialog>
using BaseDialog = QDialog;

#endif
