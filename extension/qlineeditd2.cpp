#include "qlineeditd2.h"

//****************************************************************************************
// QLineEditD2::mouseDoubleClickEvent
//****************************************************************************************
void QLineEditD2::mouseDoubleClickEvent(QMouseEvent * e)
{

    // ate it !
    // QLineEdit::mouseDoubleClickEvent(e);
	emit mouseDoubleClickSignal(e);
}
