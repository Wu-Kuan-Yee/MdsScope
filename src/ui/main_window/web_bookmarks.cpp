// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "base_dialog.hpp"

#include <QDialogButtonBox>
#include <QSignalBlocker>

namespace {
class BookmarkCheckBox final : public QCheckBox {
public:
    explicit BookmarkCheckBox(QWidget* parent = nullptr)
        : QCheckBox(parent)
    {
        setFixedSize(24, 24);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF box(3.5, 3.5, 17.0, 17.0);
        const QColor border = isChecked() ? palette().color(QPalette::Highlight)
                                          : palette().color(QPalette::Text);
        const QColor fill = isChecked() ? palette().color(QPalette::Highlight)
                                        : palette().color(QPalette::Base);
        painter.setPen(QPen(border, hasFocus() ? 2.0 : 1.4));
        painter.setBrush(fill);
        painter.drawRoundedRect(box, 3.0, 3.0);

        if (isChecked()) {
            QPainterPath check;
            check.moveTo(7.0, 12.0);
            check.lineTo(10.2, 15.0);
            check.lineTo(17.2, 8.3);
            painter.setPen(QPen(palette().color(QPalette::HighlightedText),
                                2.2,
                                Qt::SolidLine,
                                Qt::RoundCap,
                                Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(check);
        }
    }
};

QString normalizedWebUrl(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        return {};
    }
    if (!value.contains(QStringLiteral("://"))) {
        value.prepend(QStringLiteral("http://"));
    }
    QUrl url(value, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        return {};
    }
    if (url.path().isEmpty()) {
        url.setPath(QStringLiteral("/"));
    }
    return url.toString(QUrl::FullyEncoded);
}

QString defaultWebAlias(const QString& value)
{
    const QUrl url(value);
    QString alias = url.host();
    if (url.port() > 0) {
        alias += ':' + QString::number(url.port());
    }
    if (!url.path().isEmpty() && url.path() != QStringLiteral("/")) {
        alias += url.path();
    }
    return alias.isEmpty() ? value : alias;
}

void editWebAddress(QWidget* parent, const QString& title, const InternalWebBookmark& initial, std::function<void(const InternalWebBookmark&)> onAccept)
{
    auto* _dialog = new BaseDialog(parent);
    _dialog->setWindowTitle(title);
    _dialog->setWindowIcon(appIcon());
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    _dialog->setMinimumWidth(440);
#endif

    auto* layout = new QVBoxLayout(_dialog);
    auto* form = new QFormLayout;
    auto* aliasEdit = new QLineEdit(initial.alias, _dialog);
    aliasEdit->setPlaceholderText(QStringLiteral("Name shown in the menu"));
    auto* urlEdit = new QLineEdit(initial.url, _dialog);
    urlEdit->setPlaceholderText(QStringLiteral("http://host:port/path"));
    form->addRow(QStringLiteral("Name"), aliasEdit);
    form->addRow(QStringLiteral("Address"), urlEdit);
    layout->addLayout(form);

    auto* saveBtn = new QPushButton(QStringLiteral("Save"), _dialog);
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"), _dialog);
    auto updateSave = [aliasEdit, urlEdit, saveBtn] {
        saveBtn->setEnabled(!aliasEdit->text().trimmed().isEmpty()
                            && !normalizedWebUrl(urlEdit->text()).isEmpty());
    };
    QObject::connect(aliasEdit, &QLineEdit::textChanged, _dialog, updateSave);
    QObject::connect(urlEdit, &QLineEdit::textChanged, _dialog, updateSave);
    QObject::connect(saveBtn, &QPushButton::clicked, _dialog, [=] {
        InternalWebBookmark res;
        res.alias = aliasEdit->text().trimmed();
        res.url = normalizedWebUrl(urlEdit->text());
        onAccept(res);
        _dialog->hide();
    });
    QObject::connect(cancelBtn, &QPushButton::clicked, _dialog, [=] {
        _dialog->hide();
    });
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);
    updateSave();
#ifdef Q_OS_IOS
    _dialog->setAttribute(Qt::WA_StyledBackground, true);
    _dialog->setStyleSheet(_dialog->styleSheet() + QStringLiteral("BaseDialog { background: palette(window); border: 1px solid palette(mid); border-radius: 8px; }"));
    _dialog->adjustSize();
    if (parent) {
        _dialog->move((parent->width() - _dialog->width()) / 2, (parent->height() - _dialog->height()) / 2);
    }
    _dialog->raise();
    _dialog->show();
#else
    _dialog->open();
#endif
}

void editSavedWebAddresses(QWidget* parent, const QVector<InternalWebBookmark>& bookmarks, std::function<void(const QVector<InternalWebBookmark>&)> onAccept)
{
    if (bookmarks.isEmpty()) {
        return;
    }

    auto* _dialog = new BaseDialog(parent);
    _dialog->setWindowTitle(QStringLiteral("Edit"));
    _dialog->setWindowIcon(appIcon());
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    _dialog->setMinimumWidth(680);
#endif

    constexpr int kNameRole = Qt::UserRole;
    constexpr int kAddressRole = Qt::UserRole + 1;
    auto* mainLayout = new QVBoxLayout(_dialog);
    auto* list = new QListWidget(_dialog);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setDragDropOverwriteMode(false);
    list->setAutoScroll(true);
    list->setAutoScrollMargin(24);
    list->setDropIndicatorShown(true);
    list->setAlternatingRowColors(true);
    list->setTextElideMode(Qt::ElideMiddle);
    list->setSpacing(2);
    list->viewport()->setCursor(Qt::OpenHandCursor);
    for (const auto& bookmark : bookmarks) {
        auto* item = new QListWidgetItem(bookmark.alias + QStringLiteral("  —  ") + bookmark.url, list);
        item->setData(kNameRole, bookmark.alias);
        item->setData(kAddressRole, bookmark.url);
        item->setToolTip(bookmark.url);
        item->setSizeHint(QSize(0, 30));
    }
    const int visibleRows = std::max(list->count() + 1, 2);
    list->setFixedHeight(visibleRows * 32 + 4);
    mainLayout->addWidget(list);

    auto* editLayout = new QHBoxLayout;
    editLayout->setSpacing(6);
    auto* nameEdit = new QLineEdit(_dialog);
    auto* addressEdit = new QLineEdit(_dialog);
    nameEdit->setPlaceholderText(QStringLiteral("Name"));
    addressEdit->setPlaceholderText(QStringLiteral("Address"));
    editLayout->addWidget(new QLabel(QStringLiteral("Name"), _dialog));
    editLayout->addWidget(nameEdit, 1);
    editLayout->addWidget(new QLabel(QStringLiteral("Address"), _dialog));
    editLayout->addWidget(addressEdit, 3);
    mainLayout->addLayout(editLayout);

    auto* saveBtn = new QPushButton(QStringLiteral("Save"), _dialog);
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"), _dialog);
    auto updateSave = [=] {
        QSet<QString> urls;
        bool valid = true;
        for (int i = 0; i < list->count(); ++i) {
            const QListWidgetItem* item = list->item(i);
            const QString url = normalizedWebUrl(item->data(kAddressRole).toString());
            if (item->data(kNameRole).toString().trimmed().isEmpty()
                || url.isEmpty()
                || urls.contains(url)) {
                valid = false;
                break;
            }
            urls.insert(url);
        }
        saveBtn->setEnabled(valid);
    };
    auto loadCurrent = [=] {
        const QSignalBlocker nameBlocker(nameEdit);
        const QSignalBlocker addressBlocker(addressEdit);
        const QListWidgetItem* item = list->currentItem();
        nameEdit->setEnabled(item != nullptr);
        addressEdit->setEnabled(item != nullptr);
        nameEdit->setText(item ? item->data(kNameRole).toString() : QString());
        addressEdit->setText(item ? item->data(kAddressRole).toString() : QString());
    };
    auto updateCurrent = [=] {
        QListWidgetItem* item = list->currentItem();
        if (!item) {
            return;
        }
        const QString name = nameEdit->text().trimmed();
        const QString address = addressEdit->text().trimmed();
        item->setData(kNameRole, name);
        item->setData(kAddressRole, address);
        item->setText(name + QStringLiteral("  —  ") + address);
        item->setToolTip(address);
        updateSave();
    };
    QObject::connect(list, &QListWidget::currentItemChanged, _dialog, loadCurrent);
    QObject::connect(nameEdit, &QLineEdit::textChanged, _dialog, updateCurrent);
    QObject::connect(addressEdit, &QLineEdit::textChanged, _dialog, updateCurrent);
    QObject::connect(saveBtn, &QPushButton::clicked, _dialog, [=] {
        QVector<InternalWebBookmark> res;
        res.reserve(list->count());
        for (int i = 0; i < list->count(); ++i) {
            const QListWidgetItem* item = list->item(i);
            res.push_back({item->data(kNameRole).toString().trimmed(),
                                  normalizedWebUrl(item->data(kAddressRole).toString())});
        }
        onAccept(res);
        _dialog->hide();
    });
    QObject::connect(cancelBtn, &QPushButton::clicked, _dialog, [=] {
        _dialog->hide();
    });
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
    list->setCurrentRow(0);
    loadCurrent();
    updateSave();
#ifdef Q_OS_IOS
    _dialog->setAttribute(Qt::WA_StyledBackground, true);
    _dialog->setStyleSheet(_dialog->styleSheet() + QStringLiteral("BaseDialog { background: palette(window); border: 1px solid palette(mid); border-radius: 8px; }"));
    _dialog->adjustSize();
    if (parent) {
        _dialog->move((parent->width() - _dialog->width()) / 2, (parent->height() - _dialog->height()) / 2);
    }
    _dialog->raise();
    _dialog->show();
#else
    _dialog->open();
#endif
}

void selectWebAddressToRemove(QWidget* parent, const QVector<InternalWebBookmark>& bookmarks, std::function<void(const QVector<int>&)> onAccept)
{
    if (bookmarks.isEmpty()) {
        return;
    }

    auto* _dialog = new BaseDialog(parent);
    _dialog->setWindowTitle(QStringLiteral("Remove"));
    _dialog->setWindowIcon(appIcon());
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    _dialog->setMinimumSize(560, 280);
#endif

    auto* layout = new QVBoxLayout(_dialog);
    layout->addWidget(new QLabel(QStringLiteral("Select addresses to remove:"), _dialog));
    auto* list = new QListWidget(_dialog);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QVector<BookmarkCheckBox*> checkBoxes;
    checkBoxes.reserve(bookmarks.size());
    for (const auto& bookmark : bookmarks) {
        auto* item = new QListWidgetItem(list);
        item->setToolTip(bookmark.url);
        item->setSizeHint(QSize(0, 34));
        auto* row = new QWidget(list);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(6, 2, 6, 2);
        rowLayout->setSpacing(8);
        #if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        const QString fullText = bookmark.alias + QStringLiteral("  —  ") + bookmark.url;
        const QFontMetrics fm(row->font());
        const QString displayText = fm.elidedText(fullText, Qt::ElideMiddle, 320);
        #else
        const QString displayText = bookmark.alias + QStringLiteral("  —  ") + bookmark.url;
        #endif
        auto* text = new QLabel(displayText, row);
        text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        text->setToolTip(bookmark.url);
        auto* checkBox = new BookmarkCheckBox(row);
        checkBox->setAttribute(Qt::WA_TransparentForMouseEvents, true); // Let row clicks pass through
        rowLayout->addWidget(text, 1);
        rowLayout->addWidget(checkBox, 0, Qt::AlignRight | Qt::AlignVCenter);
        list->setItemWidget(item, row);
        checkBoxes.push_back(checkBox);
    }
    layout->addWidget(list, 1);
    
    QObject::connect(list, &QListWidget::itemClicked, list, [=](QListWidgetItem* item) {
        int row = list->row(item);
        if (row >= 0 && row < checkBoxes.size()) {
            checkBoxes[row]->setChecked(!checkBoxes[row]->isChecked());
        }
    });

    auto* removeBtn = new QPushButton(QStringLiteral("Remove"), _dialog);
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"), _dialog);
    removeBtn->setEnabled(false);
    auto updateRemove = [checkBoxes, removeBtn] {
        bool anyChecked = false;
        for (const auto* checkBox : checkBoxes) {
            if (checkBox->isChecked()) {
                anyChecked = true;
                break;
            }
        }
        removeBtn->setEnabled(anyChecked);
    };
    for (auto* checkBox : std::as_const(checkBoxes)) {
        QObject::connect(checkBox, &QCheckBox::toggled, _dialog, updateRemove);
    }
    QObject::connect(removeBtn, &QPushButton::clicked, _dialog, [=] {
        QVector<int> res;
        for (int i = 0; i < checkBoxes.size(); ++i) {
            if (checkBoxes.at(i)->isChecked()) {
                res.push_back(i);
            }
        }
        onAccept(res);
        _dialog->hide();
    });
    QObject::connect(cancelBtn, &QPushButton::clicked, _dialog, [=] {
        _dialog->hide();
    });
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(removeBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);
#ifdef Q_OS_IOS
    _dialog->setAttribute(Qt::WA_StyledBackground, true);
    _dialog->setStyleSheet(_dialog->styleSheet() + QStringLiteral("BaseDialog { background: palette(window); border: 1px solid palette(mid); border-radius: 8px; }"));
    _dialog->adjustSize();
    if (parent) {
        _dialog->move((parent->width() - _dialog->width()) / 2, (parent->height() - _dialog->height()) / 2);
    }
    _dialog->raise();
    _dialog->show();
#else
    _dialog->open();
#endif
}

}

QVector<InternalWebBookmark> MainWindow::savedInternalWebPages() const
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    QVector<InternalWebBookmark> cleaned;
    const int count = settings.beginReadArray(QStringLiteral("web/bookmarks"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        const QString url = normalizedWebUrl(settings.value(QStringLiteral("url")).toString());
        QString alias = settings.value(QStringLiteral("alias")).toString().trimmed();
        if (alias.isEmpty()) {
            alias = defaultWebAlias(url);
        }
        const bool duplicate = std::any_of(cleaned.cbegin(), cleaned.cend(), [&url](const auto& bookmark) {
            return bookmark.url == url;
        });
        if (!url.isEmpty() && !duplicate) {
            cleaned.push_back({alias, url});
        }
    }
    settings.endArray();

    if (cleaned.isEmpty()) {
        const QStringList legacy = settings.value(QStringLiteral("web/urls")).toStringList();
        for (const QString& entry : legacy) {
            const QString url = normalizedWebUrl(entry);
            if (!url.isEmpty()) {
                cleaned.push_back({defaultWebAlias(url), url});
            }
        }
    }
    return cleaned;
}

void MainWindow::saveInternalWebPages(const QVector<InternalWebBookmark>& bookmarks) const
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    settings.remove(QStringLiteral("web/bookmarks"));
    settings.beginWriteArray(QStringLiteral("web/bookmarks"), bookmarks.size());
    for (int i = 0; i < bookmarks.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("alias"), bookmarks.at(i).alias);
        settings.setValue(QStringLiteral("url"), bookmarks.at(i).url);
    }
    settings.endArray();
    settings.remove(QStringLiteral("web/urls"));
}

void MainWindow::addInternalWebPage()
{
    editWebAddress(this, QStringLiteral("Add Web Address"), {}, [this](const InternalWebBookmark& added) {
        QVector<InternalWebBookmark> pages = savedInternalWebPages();
        for (int i = pages.size() - 1; i >= 0; --i) {
            if (pages.at(i).url == added.url) {
                pages.removeAt(i);
            }
        }
        pages.prepend(added);
        saveInternalWebPages(pages);
        refreshInternalWebMenu();
    });
}

void MainWindow::editInternalWebPage()
{
    QVector<InternalWebBookmark> pages = savedInternalWebPages();
    if (pages.isEmpty()) {
        return;
    }
    editSavedWebAddresses(this, pages, [this](const QVector<InternalWebBookmark>& res) {
        saveInternalWebPages(res);
        refreshInternalWebMenu();
    });
}

void MainWindow::removeInternalWebPage()
{
    QVector<InternalWebBookmark> pages = savedInternalWebPages();
    if (pages.isEmpty()) {
        return;
    }
    selectWebAddressToRemove(this, pages, [this, pages](const QVector<int>& indexes) {
        if (indexes.isEmpty()) return;
        QVector<InternalWebBookmark> res = pages;
        QVector<int> sorted = indexes;
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        for (const int index : std::as_const(sorted)) {
            res.removeAt(index);
        }
        saveInternalWebPages(res);
        refreshInternalWebMenu();
    });
}

void MainWindow::refreshInternalWebMenu()
{
    if (!internalWebMenu_) return;

    // First call (startup): full build.
    if (internalWebMenu_->actions().isEmpty()) {
        const QVector<InternalWebBookmark> pages = savedInternalWebPages();
        if (pages.isEmpty()) {
            QAction* empty = internalWebMenu_->addAction(QStringLiteral("No Saved Web Addresses"));
            empty->setEnabled(false);
        } else {
            const QFontMetrics fm(internalWebMenu_->font());
            for (const auto& bookmark : pages) {
                QString label = fm.elidedText(bookmark.alias, Qt::ElideRight, 440);
                label.replace('&', QStringLiteral("&&"));
                QAction* action = internalWebMenu_->addAction(label);
                action->setToolTip(bookmark.url);
                connect(action, &QAction::triggered, this, [this, url = bookmark.url] {
                    QTimer::singleShot(0, this, [this, url] { openInternalWebPage(url); });
                });
            }
        }
        internalWebMenu_->addSeparator();
        QAction* add = internalWebMenu_->addAction(QStringLiteral("Add..."));
        QAction* edit = internalWebMenu_->addAction(QStringLiteral("Edit..."));
        QAction* remove = internalWebMenu_->addAction(QStringLiteral("Remove..."));
        connect(add, &QAction::triggered, this, &MainWindow::addInternalWebPage);
        connect(edit, &QAction::triggered, this, &MainWindow::editInternalWebPage);
        connect(remove, &QAction::triggered, this, &MainWindow::removeInternalWebPage);
        edit->setEnabled(!pages.isEmpty());
        remove->setEnabled(!pages.isEmpty());
        return;
    }

    // Subsequent calls: incremental update without clear() or delete.
    const QList<QAction*> actions = internalWebMenu_->actions();
    QAction* separator = nullptr;
    for (QAction* a : actions) {
        if (a->isSeparator()) {
            separator = a;
            break;
        }
    }
    if (!separator) return;

    // Hide + remove old bookmark actions.
    for (QAction* a : actions) {
        if (a == separator) break;
        a->setVisible(false);
        internalWebMenu_->removeAction(a);
    }

    const QVector<InternalWebBookmark> pages = savedInternalWebPages();
    const QFontMetrics fm(internalWebMenu_->font());
    for (int i = pages.size() - 1; i >= 0; --i) {
        const auto& bookmark = pages[i];
        QString label = fm.elidedText(bookmark.alias, Qt::ElideRight, 440);
        label.replace('&', QStringLiteral("&&"));
        auto* action = new QAction(label, internalWebMenu_);
        action->setToolTip(bookmark.url);
        connect(action, &QAction::triggered, this, [this, url = bookmark.url] {
            QTimer::singleShot(0, this, [this, url] { openInternalWebPage(url); });
        });
        internalWebMenu_->insertAction(separator, action);
    }

    if (pages.isEmpty()) {
        auto* empty = new QAction(QStringLiteral("No Saved Web Addresses"), internalWebMenu_);
        empty->setEnabled(false);
        internalWebMenu_->insertAction(separator, empty);
    }

    // Update Edit/Remove enabled state.
    for (QAction* a : internalWebMenu_->actions()) {
        if (a->text() == QStringLiteral("Edit..."))
            a->setEnabled(!pages.isEmpty());
        else if (a->text() == QStringLiteral("Remove..."))
            a->setEnabled(!pages.isEmpty());
    }
}
