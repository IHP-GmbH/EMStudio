/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 ************************************************************************/

#ifndef SECURESTORE_H
#define SECURESTORE_H

#include <QByteArray>
#include <QString>

/*!*******************************************************************************************************************
 * \namespace SecureStore
 * \brief Protect secrets at rest (Windows DPAPI; elsewhere reversible obfuscation only).
 *
 * Used for \c ASSISTANT_API_KEY so Preferences never write the key in cleartext to QSettings.
 **********************************************************************************************************************/
namespace SecureStore {

/*!*******************************************************************************************************************
 * \brief Encrypts \a plain for the current Windows user (DPAPI).
 *
 * \param plain Raw secret bytes.
 * \return Protected blob, or empty on failure / empty input.
 **********************************************************************************************************************/
QByteArray protect(const QByteArray &plain);

/*!*******************************************************************************************************************
 * \brief Decrypts a blob previously returned by \c protect().
 *
 * \param protectedBlob DPAPI (or obfuscated) blob.
 * \return Plain bytes, or empty on failure.
 **********************************************************************************************************************/
QByteArray unprotect(const QByteArray &protectedBlob);

/*!*******************************************************************************************************************
 * \brief UTF-8 string → Base64(DPAPI blob) for QSettings storage.
 *
 * \param plain Cleartext secret.
 * \return Base64 string, or empty if protection failed.
 **********************************************************************************************************************/
QString protectToBase64(const QString &plain);

/*!*******************************************************************************************************************
 * \brief Base64(DPAPI blob) → UTF-8 string for in-memory Preferences use.
 *
 * \param base64 Value loaded from \c ASSISTANT_API_KEY_DPAPI.
 * \return Cleartext secret, or empty on failure.
 **********************************************************************************************************************/
QString unprotectFromBase64(const QString &base64);

} // namespace SecureStore

#endif // SECURESTORE_H
