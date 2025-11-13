#include "fileedit.h"

#include <QLabel>
#include <QPalette>
#include <QPainter>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFocusEvent>
#include <QComboBox>
#include <QColorDialog>

//*******************************************************************************
// FileEdit::FileEdit
//*******************************************************************************
FileEdit::FileEdit(QWidget *parent, TYPE type)
    : QWidget(parent),
      type(type)
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    theLineEdit = new QLineEditD2(this);    
    theLineEdit->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred));

    if(type == COLOR) {
        iconBox = new QLabel(this);
        //iconBox->setMaximumHeight(17);
        //iconBox->setStyleSheet("QComboBox::drop-down { padding: 2px 1px 1px 1px; width: 0px; border-style: none}");
        iconBox->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred));
        //iconBox->view()->setTextElideMode(Qt::ElideRight);
        //iconBox->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        layout->addWidget(iconBox);
    }

    button = new QToolButton(this);
    button->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred));
    button->setText(QLatin1String("..."));
    layout->addWidget(theLineEdit);
    layout->addWidget(button);
    theLineEdit->setFocusProxy(this);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);

    if(type == PASSWORD) {
        button->setVisible(false);
        theLineEdit->setEchoMode(QLineEdit::Password);
    }

    connect(theLineEdit, SIGNAL(textEdited(const QString &)),
                this, SIGNAL(filePathChanged(const QString &)));

    connect(theLineEdit, SIGNAL(mouseDoubleClickSignal(QMouseEvent *)),
		this, SLOT(mouseDoubleClickSlot(QMouseEvent *)));

    connect(button, SIGNAL(clicked()),
                this, SLOT(buttonClicked()));
}

//*******************************************************************************
// FileEdit::~FileEdit
//*******************************************************************************
/*void FileEdit::~FileEdit()
{
    if(iconBox) delete iconBox;
    if(theLineEdit) delete theLineEdit;
    if(button) delete button;
}*/

//*******************************************************************************
// FileEdit::mouseDoubleClickSlot
//*******************************************************************************
void FileEdit::mouseDoubleClickSlot(QMouseEvent *)
{
	buttonClicked();
}

//*******************************************************************************
// FileEdit::setIconColor
//*******************************************************************************
void FileEdit::setIconColor(const QColor &color)
{
    QPixmap pix = QPixmap( 17, 17 );
    pix.fill(color);
    QPainter paint(&pix);
    paint.setPen( QPen( QBrush(Qt::black), 1 ) );
    paint.drawRect( 0, 0, 16, 16 );

    iconBox->setPixmap(pix);
}

//*******************************************************************************
// FileEdit::getLineEditPalette
//*******************************************************************************
QPalette FileEdit::getNetlistPalette(QString path)
{
    QPalette pal;
    if(type != COLOR && type != PASSWORD) {
        if(QFileInfo(path).isReadable()) {
            pal.setColor(QPalette::Text, Qt::darkGreen);
        }
        else {
            pal.setColor(QPalette::Text, Qt::red);
        }
    }
    else if(type != COLOR) {
        pal.setColor(QPalette::Text, QColor("black"));
    }
    else {
        QColor colorId = QColor(path);
        if(!colorId.isValid()) {
            colorId = QColor("black");
        }
        pal.setColor(QPalette::Text, colorId);
    }

    return pal;
}

//*******************************************************************************
// FileEdit::buttonClicked
//*******************************************************************************
void FileEdit::buttonClicked()
{
    QString filePath;
    if( type == FOLDER )
    {
        filePath = QFileDialog::getExistingDirectory(this, tr("Choose a folder"), theLineEdit->text() );
    }
    else if( type == FILE )
    {
        filePath = QFileDialog::getOpenFileName(this, tr("Choose a file"), theLineEdit->text(), theFilter);
    }
    else if( type == COLOR )
    {
        QColor colorId = QColorDialog::getColor();
        if(colorId.isValid()) {
            setIconColor(colorId);
            filePath = colorId.name();
        }
        else {
            return;
        }
    }

    if (filePath.isNull())
        return;

    theLineEdit->setText(filePath);

    if(type != COLOR ) {
        theLineEdit->setPalette(getNetlistPalette(filePath));
    }
    else {
        QPalette pal;
        pal.setColor(QPalette::Text, QColor(filePath));
        theLineEdit->setPalette(pal);
    }
    emit filePathChanged(filePath);
}

//*******************************************************************************
// FileEdit::focusInEvent
//*******************************************************************************
void FileEdit::focusInEvent(QFocusEvent *e)
{
    theLineEdit->event(e);
    if (e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason) {
        theLineEdit->selectAll();
    }
    QWidget::focusInEvent(e);
}

//*******************************************************************************
// FileEdit::focusOutEvent
//*******************************************************************************
void FileEdit::focusOutEvent(QFocusEvent *e)
{
    theLineEdit->event(e);
    QWidget::focusOutEvent(e);
}

//*******************************************************************************
// FileEdit::keyPressEvent
//*******************************************************************************
void FileEdit::keyPressEvent(QKeyEvent *e)
{
    theLineEdit->event(e);
}

//*******************************************************************************
// FileEdit::keyReleaseEvent
//*******************************************************************************
void FileEdit::keyReleaseEvent(QKeyEvent *e)
{
    theLineEdit->event(e);
}

//*******************************************************************************
// FileEdit::setKeywords
//*******************************************************************************
void FileEdit::setKeywords(const QString &keys)
{
    m_keywords = keys;
}



