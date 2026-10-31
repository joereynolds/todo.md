#include "dialogs.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

DueDateDialog::DueDateDialog(const QDate &initial, int count, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Set due date (%n task(s))", nullptr, count));
    auto *layout = new QVBoxLayout(this);

    m_calendar = new QCalendarWidget(this);
    m_calendar->setGridVisible(true);
    m_calendar->setFirstDayOfWeek(Qt::Monday);
    m_calendar->setSelectedDate(initial.isValid() ? initial : QDate::currentDate());
    connect(m_calendar, &QCalendarWidget::activated, this, &QDialog::accept);
    layout->addWidget(m_calendar);

    auto *quick = new QHBoxLayout;
    const QDate today = QDate::currentDate();
    const QDate monday = today.addDays(8 - today.dayOfWeek());
    const std::pair<QString, QDate> shortcuts[] = {
        {tr("&Today"), today},
        {tr("To&morrow"), today.addDays(1)},
        {tr("Next &Monday"), monday},
        {tr("+1 &week"), today.addDays(7)},
        {tr("+1 m&onth"), today.addMonths(1)},
    };
    for (const auto &[label, date] : shortcuts) {
        auto *b = new QPushButton(label, this);
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, [this, d = date] {
            m_calendar->setSelectedDate(d);
            accept();
        });
        quick->addWidget(b);
    }
    layout->addLayout(quick);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    m_calendar->setFocus();
}

QDate DueDateDialog::date() const
{
    return m_calendar->selectedDate();
}

ReplaceDialog::ReplaceDialog(int count, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Replace in %n task(s)", nullptr, count));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_find = new QLineEdit(this);
    m_replace = new QLineEdit(this);
    m_regex = new QCheckBox(tr("Regular expression (\\1 for groups)"), this);
    m_case = new QCheckBox(tr("Case sensitive"), this);
    form->addRow(tr("Find:"), m_find);
    form->addRow(tr("Replace with:"), m_replace);
    form->addRow(QString(), m_regex);
    form->addRow(QString(), m_case);
    layout->addLayout(form);

    auto *hint = new QLabel(tr("Matches the raw task text after \"- [ ] \", dates included.\n"
                               "With regex, find \"$\" to append or \"^\" to prepend."),
                            this);
    hint->setEnabled(false);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    setMinimumWidth(420);
}

QString ReplaceDialog::find() const { return m_find->text(); }
QString ReplaceDialog::replacement() const { return m_replace->text(); }
bool ReplaceDialog::regex() const { return m_regex->isChecked(); }
bool ReplaceDialog::caseSensitive() const { return m_case->isChecked(); }
