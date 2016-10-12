//
//  cppsocket
//

#include <cstring>
#ifdef _MSC_VER
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include "Log.h"
#include "Socket.h"
#include "Network.h"

namespace cppsocket
{
    static uint8_t TEMP_BUFFER[65536];

    Socket::Socket(Network& pNetwork, socket_t pSocketFd):
        network(pNetwork), socketFd(pSocketFd)
    {
        network.addSocket(*this);
    }

    Socket::~Socket()
    {
        network.removeSocket(*this);

        if (socketFd != INVALID_SOCKET)
        {
#ifdef _MSC_VER
            if (closesocket(socketFd) < 0)
#else
            if (::close(socketFd) < 0)
#endif
            {
                int error = getLastError();
                Log(Log::Level::ERROR) << "Failed to close socket, error: " << error;
            }
            else
            {
                Log(Log::Level::INFO) << "Socket closed";
            }
        }
    }

    Socket::Socket(Socket&& other):
        network(other.network),
        socketFd(other.socketFd),
        ready(other.ready),
        blocking(other.blocking),
        ipAddress(other.ipAddress),
        port(other.port),
        readCallback(std::move(other.readCallback)),
        closeCallback(std::move(other.closeCallback)),
        inData(std::move(other.inData)),
        outData(std::move(other.outData))
    {
        network.addSocket(*this);

        other.socketFd = INVALID_SOCKET;
        other.ready = false;
        other.blocking = true;
        other.ipAddress = 0;
        other.port = 0;
    }

    Socket& Socket::operator=(Socket&& other)
    {
        close();

        socketFd = other.socketFd;
        ready = other.ready;
        blocking = other.blocking;
        ipAddress = other.ipAddress;
        port = other.port;
        readCallback = std::move(other.readCallback);
        closeCallback = std::move(other.closeCallback);
        inData = std::move(other.inData);
        outData = std::move(other.outData);

        other.socketFd = INVALID_SOCKET;
        other.ready = false;
        other.blocking = true;
        other.ipAddress = 0;
        other.port = 0;

        return *this;
    }

    bool Socket::close()
    {
        ready = false;

        if (socketFd != INVALID_SOCKET)
        {
#ifdef _MSC_VER
            int result = closesocket(socketFd);
#else
            int result = ::close(socketFd);
#endif
            socketFd = INVALID_SOCKET;

            if (result < 0)
            {
                int error = getLastError();
                Log(Log::Level::ERROR) << "Failed to close socket, error: " << error;
                return false;
            }
            else
            {
                Log(Log::Level::INFO) << "Socket closed";
            }

        }

        return true;
    }

    void Socket::update(float)
    {
    }

    bool Socket::startRead()
    {
        if (socketFd == INVALID_SOCKET)
        {
            Log(Log::Level::ERROR) << "Can not start reading, invalid socket";
            return false;
        }

        ready = true;

        return true;
    }

    void Socket::setReadCallback(const std::function<void(const std::vector<uint8_t>&)>& newReadCallback)
    {
        readCallback = newReadCallback;
    }

    void Socket::setCloseCallback(const std::function<void()>& newCloseCallback)
    {
        closeCallback = newCloseCallback;
    }

    bool Socket::setBlocking(bool newBlocking)
    {
        blocking = newBlocking;

        if (socketFd != INVALID_SOCKET)
        {
            return setFdBlocking(newBlocking);
        }

        return true;
    }

    bool Socket::createSocketFd()
    {
        socketFd = socket(AF_INET, SOCK_STREAM, 0);

        if (socketFd == INVALID_SOCKET)
        {
            int error = getLastError();
            Log(Log::Level::ERROR) << "Failed to create socket, error: " << error;
            return false;
        }

        if (!blocking)
        {
            if (!setFdBlocking(false))
            {
                return false;
            }
        }

        return true;
    }

    bool Socket::setFdBlocking(bool block)
    {
        if (socketFd == INVALID_SOCKET)
        {
            return false;
        }

#ifdef _MSC_VER
        unsigned long mode = block ? 0 : 1;
        if (ioctlsocket(socketFd, FIONBIO, &mode) != 0)
        {
            return false;
        }
#else
        int flags = fcntl(socketFd, F_GETFL, 0);
        if (flags < 0) return false;
        flags = block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

        if (fcntl(socketFd, F_SETFL, flags) != 0)
        {
            return false;
        }
#endif

        return true;
    }

    bool Socket::send(std::vector<uint8_t> buffer)
    {
        if (!ready)
        {
            return false;
        }

        outData.insert(outData.end(), buffer.begin(), buffer.end());

        return true;
    }

    bool Socket::read()
    {
        int size = static_cast<int>(recv(socketFd, reinterpret_cast<char*>(TEMP_BUFFER), sizeof(TEMP_BUFFER), 0));

        if (size < 0)
        {
            int error = getLastError();

            if (error == EAGAIN ||
#ifdef _MSC_VER
                error == WSAEWOULDBLOCK ||
#endif
                error == EWOULDBLOCK)
            {
                Log(Log::Level::WARNING) << "Nothing to read from socket";
                return true;
            }
            else if (error == ECONNRESET)
            {
                Log(Log::Level::INFO) << "Connection reset by peer";
                disconnected();
                return false;
            }
            else
            {
                Log(Log::Level::ERROR) << "Failed to read from socket, error: " << error;
                disconnected();
                return false;
            }
        }
        else if (size == 0)
        {
            disconnected();

            return true;
        }

        Log(Log::Level::VERBOSE) << "Socket received " << size << " bytes";

        inData.insert(inData.end(), TEMP_BUFFER, TEMP_BUFFER + size);

        if (!inData.empty())
        {
            if (readCallback)
            {
                readCallback(inData);
            }

            inData.clear();
        }

        return true;
    }

    bool Socket::write()
    {
        if (ready && !outData.empty())
        {
#ifdef _MSC_VER
            int dataSize = static_cast<int>(outData.size());
#else
            size_t dataSize = outData.size();
#endif
            int size = static_cast<int>(::send(socketFd, reinterpret_cast<const char*>(outData.data()), dataSize, 0));

            if (size < 0)
            {
                int error = getLastError();
                if (error != EAGAIN &&
#ifdef _MSC_VER
                    error != WSAEWOULDBLOCK &&
#endif
                    error != EWOULDBLOCK)
                {
                    Log(Log::Level::ERROR) << "Failed to send data, error: " << error;

                    outData.clear();

                    return false;
                }
            }
            else if (size != static_cast<int>(outData.size()))
            {
                Log(Log::Level::VERBOSE) << "Socket did not send all data, sent " << size << " out of " << outData.size() << " bytes";
            }
            else if (size)
            {
                Log(Log::Level::VERBOSE) << "Socket sent " << size << " bytes";
            }

            if (size > 0)
            {
                outData.erase(outData.begin(), outData.begin() + size);
            }
        }

        return true;
    }

    bool Socket::disconnected()
    {
        if (ready)
        {
            Log(Log::Level::INFO) << "Socket disconnected";

            ready = false;

            if (closeCallback)
            {
                closeCallback();
            }
        }

        return true;
    }
}
