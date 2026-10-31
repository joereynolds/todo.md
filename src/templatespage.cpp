#include "templatespage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

TemplatesPage::TemplatesPage(QTreeView *view, QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 0);
    auto make = [this, bar](const QString &label, const QString &name, const QString &tip, void (TemplatesPage::*sig)()) {
        auto *b = new QPushButton(label, this);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::TabFocus); // keep focus (and the shortcuts) on the list
        connect(b, &QPushButton::clicked, this, sig);
        bar->addWidget(b);
        return b;
    };
    m_new = make(tr("New template…"), QStringLiteral("new"), tr("Create an empty template"), &TemplatesPage::newRequested);
    m_rename = make(tr("Rename…"), QStringLiteral("rename"), tr("Rename the selected template"),
                    &TemplatesPage::renameRequested);
    m_delete = make(tr("Delete template"), QStringLiteral("delete"), tr("Delete the selected template"),
                    &TemplatesPage::deleteRequested);
    bar->addSpacing(12);
    m_title = new QLabel(this);
    m_title->setEnabled(false); // dim
    bar->addWidget(m_title);
    bar->addStretch();
    m_open = make(tr("Open in $EDITOR"), QStringLiteral("open"), tr("Edit the template file in your editor"),
                  &TemplatesPage::openRequested);
    m_insert = make(tr("Insert into tasks"), QStringLiteral("insert"),
                    tr("Copy the selected template into the Tasks tab, after the selected task there (or into todo.md)"),
                    &TemplatesPage::insertRequested);
    layout->addLayout(bar);

    m_hint = new QLabel(this);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setWordWrap(true);
    m_hint->setEnabled(false);
    m_hint->hide();
    layout->addWidget(m_hint, 1);
    layout->addWidget(view, 1);
    setCurrentTemplate(QString());
}

QPushButton *TemplatesPage::button(const QString &name) const
{
    return findChild<QPushButton *>(name);
}

void TemplatesPage::setCurrentTemplate(const QString &name)
{
    const bool has = !name.isEmpty();
    m_rename->setEnabled(has);
    m_delete->setEnabled(has);
    m_insert->setEnabled(has);
    m_open->setEnabled(has);
    m_title->setText(has ? tr("Selected: %1").arg(name) : QString());
}

void TemplatesPage::setEmpty(bool empty, bool haveDirectory)
{
    m_new->setEnabled(haveDirectory);
    m_hint->setText(haveDirectory ? tr("No templates yet.\n\nClick New template…, or select tasks in the Tasks tab "
                                       "and use Save as template… from the right-click menu.")
                                  : tr("Add a directory first (Ctrl+O)."));
    m_hint->setVisible(empty);
    if (QWidget *view = layout()->itemAt(2)->widget())
        view->setVisible(!empty);
}
