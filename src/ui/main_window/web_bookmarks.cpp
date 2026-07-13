// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"

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

bool editWebAddress(QWidget* parent,
                    const QString& title,
                    const InternalWebBookmark& initial,
                    InternalWebBookmark* result)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setWindowIcon(appIcon());
    dialog.setMinimumWidth(440);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* aliasEdit = new QLineEdit(initial.alias, &dialog);
    aliasEdit->setPlaceholderText(QStringLiteral("Name shown in the menu"));
    auto* urlEdit = new QLineEdit(initial.url, &dialog);
    urlEdit->setPlaceholderText(QStringLiteral("http://host:port/path"));
    form->addRow(QStringLiteral("Name"), aliasEdit);
    form->addRow(QStringLiteral("Address"), urlEdit);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto* save = buttons->button(QDialogButtonBox::Save);
    auto updateSave = [aliasEdit, urlEdit, save] {
        save->setEnabled(!aliasEdit->text().trimmed().isEmpty()
                         && !normalizedWebUrl(urlEdit->text()).isEmpty());
    };
    QObject::connect(aliasEdit, &QLineEdit::textChanged, &dialog, updateSave);
    QObject::connect(urlEdit, &QLineEdit::textChanged, &dialog, updateSave);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        result->alias = aliasEdit->text().trimmed();
        result->url = normalizedWebUrl(urlEdit->text());
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    updateSave();
    return dialog.exec() == QDialog::Accepted;
}

bool editSavedWebAddresses(QWidget* parent, QVector<InternalWebBookmark>* bookmarks)
{
    if (!bookmarks || bookmarks->isEmpty()) {
        return false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Edit"));
    dialog.setWindowIcon(appIcon());
    dialog.setMinimumWidth(680);

    constexpr int kNameRole = Qt::UserRole;
    constexpr int kAddressRole = Qt::UserRole + 1;
    auto* mainLayout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
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
    for (const auto& bookmark : std::as_const(*bookmarks)) {
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
    auto* nameEdit = new QLineEdit(&dialog);
    auto* addressEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(QStringLiteral("Name"));
    addressEdit->setPlaceholderText(QStringLiteral("Address"));
    editLayout->addWidget(new QLabel(QStringLiteral("Name"), &dialog));
    editLayout->addWidget(nameEdit, 1);
    editLayout->addWidget(new QLabel(QStringLiteral("Address"), &dialog));
    editLayout->addWidget(addressEdit, 3);
    mainLayout->addLayout(editLayout);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto* save = buttons->button(QDialogButtonBox::Save);
    auto updateSave = [&] {
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
        save->setEnabled(valid);
    };
    auto loadCurrent = [&] {
        const QSignalBlocker nameBlocker(nameEdit);
        const QSignalBlocker addressBlocker(addressEdit);
        const QListWidgetItem* item = list->currentItem();
        nameEdit->setEnabled(item != nullptr);
        addressEdit->setEnabled(item != nullptr);
        nameEdit->setText(item ? item->data(kNameRole).toString() : QString());
        addressEdit->setText(item ? item->data(kAddressRole).toString() : QString());
    };
    auto updateCurrent = [&] {
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
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog, loadCurrent);
    QObject::connect(nameEdit, &QLineEdit::textChanged, &dialog, updateCurrent);
    QObject::connect(addressEdit, &QLineEdit::textChanged, &dialog, updateCurrent);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        bookmarks->clear();
        bookmarks->reserve(list->count());
        for (int i = 0; i < list->count(); ++i) {
            const QListWidgetItem* item = list->item(i);
            bookmarks->push_back({item->data(kNameRole).toString().trimmed(),
                                  normalizedWebUrl(item->data(kAddressRole).toString())});
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    mainLayout->addWidget(buttons);
    list->setCurrentRow(0);
    loadCurrent();
    updateSave();
    return dialog.exec() == QDialog::Accepted;
}

bool selectWebAddressToRemove(QWidget* parent,
                              const QVector<InternalWebBookmark>& bookmarks,
                              QVector<int>* selectedIndexes)
{
    if (bookmarks.isEmpty()) {
        return false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Remove"));
    dialog.setWindowIcon(appIcon());
    dialog.setMinimumSize(560, 280);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("Select addresses to remove:"), &dialog));
    auto* list = new QListWidget(&dialog);
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
        auto* text = new QLabel(bookmark.alias + QStringLiteral("  —  ") + bookmark.url, row);
        text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        text->setToolTip(bookmark.url);
        auto* checkBox = new BookmarkCheckBox(row);
        checkBox->setToolTip(QStringLiteral("Select for removal"));
        rowLayout->addWidget(text, 1);
        rowLayout->addWidget(checkBox, 0, Qt::AlignRight | Qt::AlignVCenter);
        list->setItemWidget(item, row);
        checkBoxes.push_back(checkBox);
    }
    layout->addWidget(list, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* remove = buttons->addButton(QStringLiteral("Remove"), QDialogButtonBox::DestructiveRole);
    remove->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    auto updateRemove = [checkBoxes, remove] {
        bool anyChecked = false;
        for (const auto* checkBox : checkBoxes) {
            if (checkBox->isChecked()) {
                anyChecked = true;
                break;
            }
        }
        remove->setEnabled(anyChecked);
    };
    for (auto* checkBox : std::as_const(checkBoxes)) {
        QObject::connect(checkBox, &QCheckBox::toggled, &dialog, updateRemove);
    }
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        selectedIndexes->clear();
        for (int i = 0; i < checkBoxes.size(); ++i) {
            if (checkBoxes.at(i)->isChecked()) {
                selectedIndexes->push_back(i);
            }
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    return dialog.exec() == QDialog::Accepted;
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
    InternalWebBookmark added;
    if (!editWebAddress(this, QStringLiteral("Add Web Address"), {}, &added)) {
        return;
    }
    QVector<InternalWebBookmark> pages = savedInternalWebPages();
    for (int i = pages.size() - 1; i >= 0; --i) {
        if (pages.at(i).url == added.url) {
            pages.removeAt(i);
        }
    }
    pages.prepend(added);
    saveInternalWebPages(pages);
    refreshInternalWebMenu();
}

void MainWindow::editInternalWebPage()
{
    QVector<InternalWebBookmark> pages = savedInternalWebPages();
    if (pages.isEmpty() || !editSavedWebAddresses(this, &pages)) {
        return;
    }
    saveInternalWebPages(pages);
    refreshInternalWebMenu();
}

void MainWindow::removeInternalWebPage()
{
    QVector<InternalWebBookmark> pages = savedInternalWebPages();
    if (pages.isEmpty()) {
        return;
    }
    QVector<int> indexes;
    if (!selectWebAddressToRemove(this, pages, &indexes) || indexes.isEmpty()) {
        return;
    }
    std::sort(indexes.begin(), indexes.end(), std::greater<int>());
    for (const int index : std::as_const(indexes)) {
        pages.removeAt(index);
    }
    saveInternalWebPages(pages);
    refreshInternalWebMenu();
}

void MainWindow::refreshInternalWebMenu()
{
    if (!internalWebMenu_) {
        return;
    }
    internalWebMenu_->clear();
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
    connect(add, &QAction::triggered, this, [this] {
        QTimer::singleShot(0, this, &MainWindow::addInternalWebPage);
    });
    connect(edit, &QAction::triggered, this, [this] {
        QTimer::singleShot(0, this, &MainWindow::editInternalWebPage);
    });
    connect(remove, &QAction::triggered, this, [this] {
        QTimer::singleShot(0, this, &MainWindow::removeInternalWebPage);
    });
    edit->setEnabled(!pages.isEmpty());
    remove->setEnabled(!pages.isEmpty());
}
