/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 ************************************************************************/

#ifndef ASSISTANTPROMPTBLOB_H
#define ASSISTANTPROMPTBLOB_H

#include <QString>

/*!*******************************************************************************************************************
 * \brief Decodes the embedded assistant policy fragment for the system prompt.
 *
 * Returns empty if the blob was tampered with or failed integrity check.
 **********************************************************************************************************************/
QString assistantDecodePolicyFragment();

#endif // ASSISTANTPROMPTBLOB_H
