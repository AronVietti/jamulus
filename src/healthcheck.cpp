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
\******************************************************************************/

#include "healthcheck.h"
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include "global.h"

/* Socket Helper Functions ****************************************************/

// Handle Socket Errors for Posix and Windows
static void HandleSocketError(int error)
{
    switch (errno)
    {
#ifdef _WIN32
    case WSANOTINITIALISED:
        throw CGenErr("HealthCheck: A successful WSAStartup call must occur before using this function.", "Network Error");

#endif
#ifndef _WIN32
    case EACCES:
#endif
        throw CGenErr("HealthCheck: The address is protected, and the user is not the superuser.", "Network Error");

#ifndef _WIN32
    case EADDRINUSE:
#else
    case WSAEADDRINUSE:
    case WSAEISCONN:
#endif
        throw CGenErr("HealthCheck: The given address is already in use.", "Network Error");


#ifndef _WIN32
    case EBADF:
#else
    case WSAENOTCONN:
    case WSAENETRESET:
    case WSAESHUTDOWN:
#endif
        throw CGenErr("HealthCheck: not a valid file descriptor or is not open for reading.", "Network Error");

#ifndef _WIN32
    case EINVAL:
#else
    case WSAEINVAL:
#endif
        throw CGenErr("HealthCheck: The socket is already bound to an address, or addrlen is wrong, \
            or addr is not a valid address for this socket's domain. ", "Network Error");

#ifndef _WIN32
    case ENOTSOCK:
#else
    case WSAENOTSOCK:
#endif
        throw CGenErr("HealthCheck: The file descriptor sockfd does not refer to a socket.", "Network Error");

#ifndef _WIN32
    case EOPNOTSUPP:
#else
    case WSAEOPNOTSUPP:
#endif
        throw CGenErr("HealthCheck: The socket is not of a type that supports the listen() operation.", "Network Error");

#ifndef _WIN32
    case EFAULT:
#else
    case WSAEFAULT:
#endif
        throw CGenErr("HealthCheck: buf is outside your accessible address space.", "Network Error");

#ifndef _WIN32
    case EINTR:
#else
#endif
        throw CGenErr("HealthCheck: The call was interrupted by a signal before any data was read.", "Network Error");

#ifndef _WIN32
    case EIO:
#else
#endif
        throw CGenErr("HealthCheck: I/O error.  This will happen for example when the process is \
            in a background process group, tries to read from its \
            controlling terminal, and either it is ignoring or blocking \
            SIGTTIN or its process group is orphaned.It may also occur \
            when there is a low - level I / O error while reading from a disk \
            or tape.A further possible cause of EIO on networked \
            filesystems is when an advisory lock had been taken out on the \
            file descriptor and this lock has been lost.See the Lost \
            locks section of fcntl(2) for further details.", "Network Error");

#ifdef _WIN32
    case WSAENOBUFS:
        throw CGenErr("HealthCheck: No buffer space is available.", "Network Error");

    case WSAEMFILE:
        throw CGenErr("HealthCheck: No more socket descriptors are available.", "Network Error");

    case WSAENETDOWN:
        throw CGenErr("HealthCheck: The network subsystem has failed.", "Network Error");

#endif
    default:
        throw CGenErr("HealthCheck: Socket error # " + std::to_string(error), "Network Error");
    }
}

static int GetError()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool IsNonBlockingError(int error)
{
#ifdef _WIN32
    return error == WSAEINPROGRESS || error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

static bool IsDisconnectError(int error)
{
#ifdef _WIN32
    return error == WSAENOTCONN || error == WSAENETRESET || error == WSAESHUTDOWN;
#else
    return error == EBADF;
#endif
}

#ifdef _WIN32
static bool SocketConnected(Socket socket)
#else
static bool SocketConnected(int socket)
#endif
{
    int result = 0;
    char fakeBuffer;

#ifdef _WIN32
    result = recv(socket, &fakeBuffer, 1);
#else
    result = read(socket, &fakeBuffer, 1);
#endif

    // A 0 result means Socket was succesfully disconnected
    if (result == 0)
        return true;
    // An error was returned, handle the error
    if (result == -1)
    {
        // If the error was one of the Non Blocking codes we're still connected
        int error = GetError();
        if (IsNonBlockingError(error))
            return true;
        if (IsDisconnectError(error))
            return false;

        HandleSocketError(error);
    }
}

#ifdef _WIN32
static void CloseSocket(Socket socket)
{
    closesocket(socket);
}
#else
static void CloseSocket(int socket)
{
    close(socket);
}
#endif

/* Classes *******************************************************************/

CHealthCheckSocket::CHealthCheckSocket(const quint16 iPortNumber)
{
    Init(iPortNumber);
}

CHealthCheckSocket::~CHealthCheckSocket()
{
    AcceptThread.Stop();
}

CHealthCheckSocket::Init(const quint16 iPortNumber)
{
#ifdef _WIN32
    // for the Windows socket usage we have to start it up first

// TODO check for error and exit application on error

    WSADATA wsa;
    WSAStartup(MAKEWORD(1, 0), &wsa);
#endif

    TcpSocket = socket(AF_INET, SOCK_STREAM, 0);

    // Try to set this socket as non blocking. This makes it easier to accept and manage
    // connections on a single thread.
    int blocking = 0;

#ifdef _WIN32
    unsigned long mode = 1;
    blocking = ioctlsocket(fd, FIONBIO, &mode) == 0) ? true : false;
#else
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) blocking = flags;
    else
    {
        flags = flags | O_NONBLOCK;
        blocking = fcntl(fd, F_SETFL, flags);
    }
#endif

    if(blocking = -1)
        HandleSocketError(GetError());

    sockaddr_in TcpSocketInAddr;

    TcpSocketInAddr.sin_family = AF_INET;
    TcpSocketInAddr.sin_addr = htonl(INADDR_ANY);
    TcpSocketInAddr.sin_port = htons(iPortNumber);

    int bound = bind(TcpSocket, &TcpSocketInAddr, sizeof(TcpSocketInAddr));

    if (bound == -1)
        HandleSocketError(GetError());
}

CHealthCheckSocket::Listen()
{
    int listening = listen(TcpSocket, 0);

    if (listening == -1)
        HandleSocketError(GetError());

    AcceptThread.SetSocket(this);

    moveToThread(&AcceptThread);
}

#ifdef _WIN32
SOCKET CHealthCheckSocket::Accept()
{
#else
int CHealthCheckSocket::Accept()
{
#endif
    sockaddr addr;
    socklen_t addrlen;
    return accept(TcpSocket, &addr, &addrlen);
}

CHealthCheckSocket::Close()
{
#ifdef _WIN32
    // closesocket will cause recvfrom to return with an error because the
    // socket is closed -> then the thread can safely be shut down
    closesocket(TcpSocket);
#elif defined ( __APPLE__ ) || defined ( __MACOSX )
    // on Mac the general close has the same effect as closesocket on Windows
    close(TcpSocket);
#else
    // on Linux the shutdown call cancels the recvfrom
    shutdown(TcpSocket, SHUT_RDWR);
#endif
}

CHealthCheckSocket::CHealthCheckThread::CHealthCheckThread(CHealthCheckSocket* pNewSocket, QObject* parent)
    : QThread(parent), pSocket(pNewSocket), bRun(false)
{}

void CHealthCheckSocket::CHealthCheckThread::Stop()
{
    bRun = false;

    pSocket->Close();

    for (auto connection = ConnectionSockets.cbegin(); connection != ConnectionSockets.cend(); ++connection)
    {
        CloseSocket(*connection);
        connection = ConnectionSockets.erase(connection);
    }

    wait(5000);
}

void CHealthCheckSocket::CHealthCheckThread::SetSocket(CHealthCheckSocket* pNewSocket)
{
    pSocket = pNewSocket;
}



void CHealthCheckSocket::CHealthCheckThread::run()
{
    bRun = true;

    while (pSocket != nullptr && bRun)
    {
#ifdef _WIN32
        SOCKET newConnection;
#else
        int newConnection;
#endif
        newConnection = pSocket.Accept();

        if (newConnection == -1)
        {
            int error = GetError();
            if (!IsNonBlockingError(error))
            {
                bRun = false;
                connected = false;
                HandleSocketError(error);
                break;
            }
        }
            
        ConnectionSockets.push_back(newConnection);

        // Check existing connections. If they're closed then remove them
        for (auto connection = ConnectionSockets.cbegin(); connection != ConnectionSockets.cend(); ++connection)
        {   
            if (!SocketConnected(*connection))
            {
                CloseSocket(*connection);
                connection = ConnectionSockets.erase(connection);
            }
        }

        // Make sure we don't have too many connections.
        //Disconnect and remove the oldest one if we do.
        if (ConnectionSockets.size() > MAX_NUM_HEALTH_CONNECTIONS)
        {
#ifdef _WIN32
            SOCKET& oldConnection;
#else
            int& oldConnection;
#endif
            oldConnection = ConnectionSockets.front();
            ConnectionSockets.erase(ConnectionSockets.cbegin());
            CloseSocket(oldConnection);
        }

        // Because the socket is set to not block this loop could peg the cpu.
        // Putting in a smal wait to prevent that.
        wait(5);
    }

    bRun = false;
}