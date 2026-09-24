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

#include "securestore.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <wincrypt.h>
#endif

namespace SecureStore {

#ifdef Q_OS_WIN

/*!*******************************************************************************************************************
 * \brief Encrypts \a plain for the current Windows user (DPAPI), or obfuscates on other platforms.
 *
 * \param plain Raw secret bytes.
 * \return Protected blob, or empty on failure.
 **********************************************************************************************************************/
QByteArray protect(const QByteArray &plain)
{
    if (plain.isEmpty())
        return {};

    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));
    in.cbData = static_cast<DWORD>(plain.size());

    DATA_BLOB out;
    ZeroMemory(&out, sizeof(out));

    if (!CryptProtectData(&in,
                          L"EMStudioAssistant",
                          nullptr,
                          nullptr,
                          nullptr,
                          0,
                          &out)) {
        return {};
    }

    QByteArray enc(reinterpret_cast<const char *>(out.pbData),
                   static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return enc;
}

/*!*******************************************************************************************************************
 * \brief Decrypts a blob previously returned by \c protect().
 *
 * \param protectedBlob DPAPI / obfuscated blob.
 * \return Plain bytes, or empty on failure.
 **********************************************************************************************************************/
QByteArray unprotect(const QByteArray &protectedBlob)
{
    if (protectedBlob.isEmpty())
        return {};

    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(protectedBlob.constData()));
    in.cbData = static_cast<DWORD>(protectedBlob.size());

    DATA_BLOB out;
    ZeroMemory(&out, sizeof(out));

    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        return {};

    QByteArray plain(reinterpret_cast<const char *>(out.pbData),
                     static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return plain;
}

#else

// Non-Windows: reversible obfuscation only (not strong crypto). Prefer OS keyring later.

/*!*******************************************************************************************************************
 * \brief Obfuscates \a plain (non-Windows fallback; XOR, not strong crypto).
 **********************************************************************************************************************/
QByteArray protect(const QByteArray &plain)
{
    QByteArray out = plain;
    for (int i = 0; i < out.size(); ++i)
        out[i] = char(uchar(out.at(i)) ^ uchar(0xA5 + (i % 17)));
    return out;
}

/*!*******************************************************************************************************************
 * \brief Reverses \c protect() obfuscation (non-Windows).
 **********************************************************************************************************************/
QByteArray unprotect(const QByteArray &protectedBlob)
{
    return protect(protectedBlob); // XOR is symmetric
}

#endif

/*!*******************************************************************************************************************
 * \brief UTF-8 string to Base64(DPAPI) for QSettings.
 **********************************************************************************************************************/
QString protectToBase64(const QString &plain)
{
    const QByteArray enc = protect(plain.toUtf8());
    if (enc.isEmpty() && !plain.isEmpty())
        return {};
    return QString::fromLatin1(enc.toBase64());
}

/*!*******************************************************************************************************************
 * \brief Base64(DPAPI) to UTF-8 string for in-memory use.
 **********************************************************************************************************************/
QString unprotectFromBase64(const QString &base64)
{
    if (base64.trimmed().isEmpty())
        return {};
    const QByteArray blob = QByteArray::fromBase64(base64.toLatin1());
    const QByteArray plain = unprotect(blob);
    return QString::fromUtf8(plain);
}

} // namespace SecureStore
