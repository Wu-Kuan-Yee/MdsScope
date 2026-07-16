#pragma once
#include "base_dialog.hpp"
#include <QWidget>
#include <QEvent>
#include <QResizeEvent>
#include <QObject>
#include <QPointer>
#include <QTimer>

class DialogOverlayManager : public QObject {
public:
    QPointer<QWidget> overlay;
    QPointer<QWidget> dialog;

    template <typename T>
    DialogOverlayManager(QMainWindow* mainWindow, T* d) 
        : QObject(mainWindow), dialog(d) 
    {
        overlay = new QWidget(mainWindow);
        overlay->setObjectName("dialogOverlayBackground");
        overlay->setAttribute(Qt::WA_StyledBackground, true);
        overlay->setStyleSheet("#dialogOverlayBackground { background-color: rgba(0, 0, 0, 150); }");
        overlay->setGeometry(mainWindow->rect());
        overlay->raise();
        overlay->show();

        dialog->setParent(overlay);
        dialog->setWindowFlags(Qt::Widget);
        dialog->setAttribute(Qt::WA_StyledBackground, true);
        
        dialog->setObjectName("dialogOverlayTarget");
        QString shadow = "#dialogOverlayTarget { border: 1px solid palette(mid); border-radius: 8px; }";
        dialog->setStyleSheet(dialog->styleSheet() + shadow);

        dialog->adjustSize();
        centerDialog();
        dialog->show();

        mainWindow->installEventFilter(this);
        connect(d, &T::finished, this, [this]() {
            if (overlay) {
                overlay->hide();
                // QTimer::singleShot(2000, overlay, &QObject::deleteLater);
            }
            // QTimer::singleShot(2000, this, &QObject::deleteLater);
        });
    }

    void centerDialog() {
        if (overlay && dialog) {
            int maxWidth = std::max(100, overlay->width() - 32);
            int maxHeight = std::max(100, overlay->height() - 64);
            dialog->setMaximumSize(maxWidth, maxHeight);
            dialog->adjustSize(); // Re-adjust based on new constraints
            dialog->move((overlay->width() - dialog->width()) / 2, 
                         (overlay->height() - dialog->height()) / 2);
        }
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Resize) {
            if (auto* w = qobject_cast<QWidget*>(obj)) {
                if (overlay) {
                    overlay->setGeometry(w->rect());
                    centerDialog();
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }
};
