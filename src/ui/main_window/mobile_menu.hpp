#pragma once
#include <QDialog>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTimer>
#include <QFrame>
#include <QLabel>
#include <QScrollArea>

class MobileContextMenu : public QDialog {
public:
    explicit MobileContextMenu(const QString& title, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        // Do not use WA_DeleteOnClose! It destroys the view before iOS animation finishes, causing a crash.
        connect(this, &QDialog::finished, this, [this]() {
        });
        
        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);
        
        auto* container = new QFrame(this);
        container->setStyleSheet("QFrame { background: #ffffff; border: 1px solid #d1d5db; border-radius: 12px; }");
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(4);
        
        if (!title.isEmpty()) {
            auto* titleLabel = new QLabel(title, container);
            titleLabel->setAlignment(Qt::AlignCenter);
            titleLabel->setStyleSheet("font-weight: bold; color: #6b7280; padding: 4px; border: none;");
            layout->addWidget(titleLabel);
            
            auto* line = new QFrame(container);
            line->setFrameShape(QFrame::HLine);
            line->setStyleSheet("background: #e5e7eb; border: none; max-height: 1px;");
            layout->addWidget(line);
        }
        
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
        
        contentWidget_ = new QWidget(scroll);
        contentWidget_->setStyleSheet("background: transparent; border: none;");
        contentLayout_ = new QVBoxLayout(contentWidget_);
        contentLayout_->setContentsMargins(0, 0, 0, 0);
        contentLayout_->setSpacing(2);
        
        scroll->setWidget(contentWidget_);
        layout->addWidget(scroll);
        
        mainLayout->addWidget(container);
    }
    
    void addAction(const QString& text, const std::function<void()>& slot) {
        auto* btn = new QPushButton(text, contentWidget_);
        btn->setStyleSheet(
            "QPushButton { padding: 12px; font-size: 16px; border: none; background: transparent; color: #111827; text-align: center; }"
            "QPushButton:pressed { background: #f3f4f6; border-radius: 6px; }"
        );
        connect(btn, &QPushButton::clicked, this, [this, slot]() {
            accept();
            QTimer::singleShot(500, this->parent(), slot);
        });
        contentLayout_->addWidget(btn);
    }
    
    void addSeparator() {
        auto* line = new QFrame(contentWidget_);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("background: #e5e7eb; border: none; max-height: 1px; margin: 4px 0px;");
        contentLayout_->addWidget(line);
    }

private:
    QWidget* contentWidget_;
    QVBoxLayout* contentLayout_;
};
