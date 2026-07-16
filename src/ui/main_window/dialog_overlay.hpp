#pragma once
#include "base_dialog.hpp"
#include <QWidget>
#include <QEvent>
#include <QResizeEvent>
#include <QObject>
#include <QMainWindow>

class DialogOverlayManager : public QObject {
public:
    QWidget* overlay;
    QWidget* dialog;

    template <typename T>
    DialogOverlayManager(QMainWindow* mainWindow, T* d) 
        : QObject(mainWindow), dialog(d) 
    {
        overlay = new QWidget(mainWindow);
        overlay->setStyleSheet("background-color: rgba(0, 0, 0, 150);");
        overlay->setGeometry(mainWindow->rect());
        overlay->raise();
        overlay->show();

        dialog->setParent(overlay);
        dialog->setWindowFlags(Qt::Widget);
        
        dialog->setObjectName("dialogOverlayTarget");
        QString shadow = "#dialogOverlayTarget { border: 1px solid #475569; border-radius: 8px; }";
        dialog->setStyleSheet(dialog->styleSheet() + shadow);

        centerDialog();
        dialog->show();

        mainWindow->installEventFilter(this);
        connect(d, &T::finished, this, [this]() {
            overlay->deleteLater();
            this->deleteLater();
        });
    }

    void centerDialog() {
        if (overlay && dialog) {
            dialog->move((overlay->width() - dialog->width()) / 2, 
                         (overlay->height() - dialog->height()) / 2);
        }
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Resize) {
            if (auto* w = qobject_cast<QWidget*>(obj)) {
                overlay->setGeometry(w->rect());
                centerDialog();
            }
        }
        return QObject::eventFilter(obj, event);
    }
};
