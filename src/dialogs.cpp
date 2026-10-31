#include "dialogs.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
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

namespace {
const std::pair<QChar, const char *> kUnits[] = {
    {u'm', QT_TRANSLATE_NOOP("EtaDialog", "minutes")}, {u'h', QT_TRANSLATE_NOOP("EtaDialog", "hours")},
    {u'd', QT_TRANSLATE_NOOP("EtaDialog", "days")},    {u'w', QT_TRANSLATE_NOOP("EtaDialog", "weeks")},
    {u'y', QT_TRANSLATE_NOOP("EtaDialog", "years")},
};
} // namespace

EtaDialog::EtaDialog(const QString &current, int count, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Set ETA (%n task(s))", nullptr, count));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("How long it might take:"), this));

    auto *row = new QHBoxLayout;
    m_amount = new QSpinBox(this);
    m_amount->setRange(1, 9999);
    m_amount->setMinimumWidth(90);
    m_unit = new QComboBox(this);
    for (const auto &[unit, label] : kUnits)
        m_unit->addItem(tr(label), unit);
    row->addWidget(m_amount);
    row->addWidget(m_unit, 1);
    layout->addLayout(row);

    auto *quick = new QHBoxLayout;
    const std::pair<int, QChar> presets[] = {{2, u'm'}, {15, u'm'}, {30, u'm'}, {1, u'h'}, {2, u'h'}, {1, u'd'}};
    for (const auto &[amount, unit] : presets) {
        auto *b = new QPushButton(QStringLiteral("%1%2").arg(amount).arg(unit), this);
        b->setAutoDefault(false);
        b->setToolTip(tr("Set %1%2 and close").arg(amount).arg(unit));
        connect(b, &QPushButton::clicked, this, [this, amount, unit] {
            set(amount, unit);
            m_result = value();
            accept();
        });
        quick->addWidget(b);
    }
    layout->addLayout(quick);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *remove = buttons->addButton(tr("&Remove ETA"), QDialogButtonBox::DestructiveRole);
    connect(remove, &QPushButton::clicked, this, [this] {
        m_result.clear();
        accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        m_result = value();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Start from the current ETA, if it parses.
    set(30, u'm');
    if (current.size() >= 2) {
        bool ok = false;
        const int n = current.chopped(1).toInt(&ok);
        if (ok && n > 0)
            set(n, current.back());
    }
    m_amount->setFocus();
    m_amount->selectAll();
}

void EtaDialog::set(int amount, QChar unit)
{
    m_amount->setValue(amount);
    const int i = m_unit->findData(unit);
    if (i >= 0)
        m_unit->setCurrentIndex(i);
}

QString EtaDialog::value() const
{
    return QString::number(m_amount->value()) + m_unit->currentData().toChar();
}
