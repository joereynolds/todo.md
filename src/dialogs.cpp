#include "dialogs.h"
#include "querybuilder.h"

#include <QButtonGroup>
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

namespace {
enum DueChoice { DueNone, DueToday, DueTomorrow, DueWeek, DueMonth, DuePick };
const char *const kDueNames[] = {"none", "today", "tomorrow", "week", "month", "pick"};
} // namespace

AddTaskDialog::AddTaskDialog(const QString &title, const QString &prompt, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(title);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(prompt, this));
    m_text = new QLineEdit(this);
    m_text->setMinimumWidth(380);
    layout->addWidget(m_text);

    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    row->addWidget(new QLabel(tr("Due:"), this));
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    const QDate today = QDate::currentDate();
    const std::pair<QString, QDate> choices[] = {
        {tr("&None"), QDate()},
        {tr("To&day"), today},
        {tr("To&morrow"), today.addDays(1)},
        {tr("Next &week"), today.addDays(7)},
        {tr("Next m&onth"), today.addMonths(1)},
        {tr("&Pick…"), QDate()},
    };
    for (int id = 0; id < 6; ++id) {
        auto *b = new QPushButton(choices[id].first, this);
        b->setCheckable(true);
        b->setAutoDefault(false); // Enter in the text box accepts the dialog
        b->setFocusPolicy(Qt::TabFocus);
        b->setObjectName(QString::fromLatin1(kDueNames[id]));
        if (choices[id].second.isValid())
            b->setToolTip(choices[id].second.toString(QStringLiteral("ddd d MMM yyyy")));
        m_group->addButton(b, id);
        row->addWidget(b);
        if (id == DuePick)
            connect(b, &QPushButton::clicked, this, &AddTaskDialog::pick);
        else
            connect(b, &QPushButton::clicked, this, [this, id, d = choices[id].second] { choose(id, d); });
    }
    row->addStretch();
    layout->addLayout(row);
    m_dueLabel = new QLabel(this);
    m_dueLabel->setEnabled(false); // dim
    layout->addWidget(m_dueLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    choose(DueNone, QDate());
    m_text->setFocus();
}

QString AddTaskDialog::text() const
{
    return m_text->text();
}

QPushButton *AddTaskDialog::dueButton(const QString &name) const
{
    return findChild<QPushButton *>(name);
}

void AddTaskDialog::choose(int id, const QDate &date)
{
    m_choice = id;
    m_due = date;
    m_group->button(id)->setChecked(true);
    m_dueLabel->setText(date.isValid() ? tr("Due %1").arg(date.toString(QStringLiteral("dddd d MMMM yyyy")))
                                       : tr("No due date"));
}

void AddTaskDialog::pick()
{
    DueDateDialog dlg(m_due.isValid() ? m_due : QDate::currentDate(), 1, this);
    dlg.setWindowTitle(tr("Pick a due date"));
    if (dlg.exec() == QDialog::Accepted && dlg.date().isValid())
        choose(DuePick, dlg.date());
    else
        m_group->button(m_choice)->setChecked(true); // cancelled: keep the previous choice
    m_text->setFocus();
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

ViewDialog::ViewDialog(const QString &title, const QString &name, const QString &query,
                       std::function<int(const QString &)> countFor, std::function<bool(const QString &)> nameTaken,
                       TagIndex *tags, QWidget *parent)
    : QDialog(parent), m_countFor(std::move(countFor)), m_nameTaken(std::move(nameTaken))
{
    setWindowTitle(title);
    setMinimumWidth(560);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(name, this);
    m_name->setPlaceholderText(tr("e.g. This week, Media"));
    m_query = new QLineEdit(query, this);
    m_query->setPlaceholderText(tr("e.g. due:overdue|due:week   or   #music|#video|#photography"));
    form->addRow(tr("&Name:"), m_name);
    form->addRow(tr("&Filter:"), m_query);
    layout->addLayout(form);

    if (tags) {
        // Click to build the filter; the text above follows, and typing there updates the chips.
        m_builder = new QueryBuilder(tags, this);
        m_builder->setQuery(query);
        layout->addWidget(m_builder);
        connect(m_builder, &QueryBuilder::queryChanged, m_query, &QLineEdit::setText);
        connect(m_query, &QLineEdit::textEdited, m_builder, &QueryBuilder::setQuery);
    }

    auto *help = new QLabel(tr("Or type terms yourself. Spaces mean <b>and</b>, <b>|</b> means <b>or</b>:<br>"
                               "<code>#music|#video</code> &nbsp; <code>due:overdue|due:week</code> &nbsp; "
                               "<code>~&lt;=15m</code> &nbsp; <code>is:doing</code> &nbsp; <code>done:week</code> &nbsp; "
                               "plain words match the text or file path."),
                            this);
    help->setWordWrap(true);
    help->setTextFormat(Qt::RichText);
    help->setEnabled(false); // dim
    layout->addWidget(help);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_ok->setText(tr("&Save"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_name, &QLineEdit::textChanged, this, &ViewDialog::validate);
    connect(m_query, &QLineEdit::textChanged, this, &ViewDialog::validate);
    validate();
    (name.isEmpty() ? m_name : m_query)->setFocus();
}

QString ViewDialog::name() const
{
    return m_name->text().simplified();
}

QString ViewDialog::query() const
{
    return m_query->text().simplified();
}

void ViewDialog::validate()
{
    QString problem;
    if (name().isEmpty())
        problem = tr("Give the view a name.");
    else if (m_nameTaken && m_nameTaken(name()))
        problem = tr("There is already a view called \"%1\".").arg(name());
    else if (query().isEmpty())
        problem = tr("Enter a filter.");
    m_ok->setEnabled(problem.isEmpty());
    if (!problem.isEmpty())
        m_status->setText(problem);
    else if (m_countFor)
        m_status->setText(tr("%n task(s) match right now (done and cancelled only if the filter asks for them).", nullptr,
                             m_countFor(query())));
    else
        m_status->clear();
}
