/******************************************************************************\
 * Copyright (c) 2004-2020
 *
 * Author(s):
 *  Aron Vietti
 *
 ******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Health Check
 * This is currently just a TCP socket on the same port as the main Socket
 * That accepts conncetions for the express puprpose of checking if the
 * process is healthy. This helps in cloud environments, like AWS,
 * for load balanaciing.
\******************************************************************************/

#pragma once

#include <QObject>
#include <QTGlobal>
#include <vector>
#ifndef _WIN32
# include <netinet/in.h>
# include <sys/socket.h>
#else
# include <winsock2.h>
#endif

// Forward Declarations
class QThread;

// TCP Socket whose only purpose is to let Health Check monitoring software connect
// to the service to verify it is still functioning.
class CHealthCheckSocket : public QObject
{
    Q_OBJECT

public:
    CHealthCheckSocket(const quint16 iPortNumber);

    virtual ~CHealthCheckSocket();

    // Start listening for incoming conncetions
    void Listen();

    // Accept a connection from the socket
#ifdef _WIN32
    
    SOCKET Accept();
#else
    int Accept();
#endif

    // Close the Socket and all of it's Connections
    void Close();
protected:
    void Init(const quint16 iPortNumber);

#ifdef _WIN32
    SOCKET           TcpSocket;
#else
    int              TcpSocket;
#endif

    // Thread for Accepting and Checking Connections
    class CHealthCheckThread : public QThread
    {
    public:
        CHealthCheckThread(CHealthCheckSocket* pNewSocket = nullptr, QObject* parent = nullptr);

        void Stop();

        void SetSocket(CHealthCheckSocket* pNewSocket);
    protected:
        void run();

        CHealthCheckSocket* pSocket;

        bool bRun;
    };

#ifdef _WIN32
    std::vector<SOCKET>           ConnectionSockets;
#else
    std::vector<int>              ConnectionSockets;
#endif

    CHealthCheckThread AcceptThread;
};