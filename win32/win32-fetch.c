/*
    Gpredict: Real-time satellite tracking and orbit prediction program

    Copyright (C)  2001-2017  Alexandru Csete, OZ9AEC.
    Copyright (C)  2017 Phil Ashby, 2E0IPX.

    Comments, questions and bugreports should be submitted via
    http://sourceforge.net/projects/gpredict/
    More details can be found at the project home page:

            http://gpredict.oz9aec.net/

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, visit http://www.fsf.org/


    Fetch URL content to a specified handle using Wininet as no libcurl
    dev libs available for Win32/64.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <stdio.h>
#include "win32-fetch.h"

#define BUFLEN 8192

int win32_fetch(char *url, FILE *file, char *proxy, char *ua)
{
    HINTERNET hInt, hUrl;
    char buf[BUFLEN];
    DWORD nRead = 0;
    BOOL ok = TRUE;

    if (proxy != NULL)
        hInt = InternetOpen(ua, INTERNET_OPEN_TYPE_PROXY, proxy, "<local>", 0);
    else
        hInt = InternetOpen(ua, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (NULL == hInt)
        return 0x80000000 | (int)GetLastError();

    /* Don't sit forever on a host that accepts the TCP connection and then
     * sends nothing (typical of an IP filter). */
    {
        DWORD timeout = 10000;
        InternetSetOption(hInt, INTERNET_OPTION_CONNECT_TIMEOUT,
                          &timeout, sizeof(timeout));
        timeout = 30000;
        InternetSetOption(hInt, INTERNET_OPTION_RECEIVE_TIMEOUT,
                          &timeout, sizeof(timeout));
        InternetSetOption(hInt, INTERNET_OPTION_SEND_TIMEOUT,
                          &timeout, sizeof(timeout));
    }

    hUrl = InternetOpenUrl(hInt, url, NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (NULL == hUrl)
    {
        InternetCloseHandle(hInt);
        return 0x40000000 | (int)GetLastError();
    }

    while ((ok = InternetReadFile(hUrl, buf, BUFLEN, &nRead)) && nRead > 0)
        fwrite(buf, nRead, 1, file);
    if (!ok)
    {
        int err = 0x40000000 | (int)GetLastError();
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInt);
        return err;
    }

    {
        DWORD status = 0;
        DWORD len = sizeof(status);
        DWORD index = 0;

        if (HttpQueryInfoA(hUrl,
                           HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                           &status, &len, &index) &&
            status >= 400)
        {
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInt);
            return (int) status;
        }
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInt);
    return 0;
}

