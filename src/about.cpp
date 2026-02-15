#include "about.h"
#include "ui_about.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QPixmap>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::AboutDialog)
{
    m_ui->setupUi(this);
    initUi();

    resize(360, sizeHint().height());

    connect(m_ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

AboutDialog::~AboutDialog()
{
    delete m_ui;
}

void AboutDialog::initUi()
{
    m_ui->lblLogo->setPixmap(QPixmap(":/logo"));

    m_ui->lblVersion->setText(
        QCoreApplication::applicationVersion().isEmpty()
            ? QStringLiteral("dev")
            : QCoreApplication::applicationVersion()
        );

    m_ui->lblQt->setText(QString::fromLatin1(qVersion()));

#ifdef QT_DEBUG
    const QString buildType = "Debug";
#else
    const QString buildType = "Release";
#endif

    m_ui->lblBuild->setText(
        QString("%1 | %2")
            .arg(buildType,
                 QDateTime::currentDateTime().toString(Qt::ISODate))
        );
}
