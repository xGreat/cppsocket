//
//  cppsocket
//

#include <algorithm>
#include <iostream>
#include <map>
#include <chrono>
#ifdef _MSC_VER
#define NOMINMAX
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#endif
#include "Network.h"
#include "Socket.h"

namespace cppsocket
{
    Network::Network()
    {
#ifdef _MSC_VER
        static bool ready = false;
        if (!ready)
        {
            WORD sockVersion = MAKEWORD(2, 2);
            WSADATA wsaData;
            int error = WSAStartup(sockVersion, &wsaData);
            if (error != 0)
            {
                std::cerr << "WSAStartup failed, error: " << error << std::endl;
                return;
            }

            if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
            {
                std::cerr << "Incorrect Winsock version" << std::endl;
                WSACleanup();
                return;
            }

            ready = true;
        }
#endif

        previousTime = std::chrono::steady_clock::now();
    }

    bool Network::update()
    {
        auto currentTime = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - previousTime);

        float delta = diff.count() / 1000000000.0f;
        previousTime = currentTime;

        std::vector<pollfd> pollFds;
        pollFds.reserve(sockets.size());

        std::map<socket_t, std::reference_wrapper<Socket>> socketMap;

        for (const auto& s : sockets)
        {
            Socket& socket = s.get();

            if (socket.socketFd != INVALID_SOCKET)
            {
                pollfd pollFd;
                pollFd.fd = socket.socketFd;
                pollFd.events = POLLIN | POLLOUT;

                pollFds.push_back(pollFd);

                socketMap.insert(std::pair<socket_t, std::reference_wrapper<Socket>>(socket.socketFd, socket));
            }
        }

        if (!pollFds.empty())
        {
#ifdef _MSC_VER
            if (WSAPoll(pollFds.data(), static_cast<ULONG>(pollFds.size()), 0) < 0)
#else
            if (poll(pollFds.data(), static_cast<nfds_t>(pollFds.size()), 0) < 0)
#endif
            {
                int error = getLastError();
                std::cerr << "Poll failed, error: " << error << std::endl;
                return false;
            }

            for (uint32_t i = 0; i < pollFds.size(); ++i)
            {
                pollfd pollFd = pollFds[i];

                auto iter = socketMap.find(pollFd.fd);

                if (iter != socketMap.end())
                {
                    Socket& socket = iter->second;

                    if (pollFd.revents & POLLIN)
                    {
                        socket.read();
                    }

                    if (pollFd.revents & POLLOUT)
                    {
                        socket.write();
                    }

                    socket.update(delta);
                }
            }
        }

        return true;
    }

    void Network::addSocket(Socket& socket)
    {
        sockets.push_back(socket);
    }

    void Network::removeSocket(Socket& socket)
    {
        std::vector<std::reference_wrapper<Socket>>::iterator i = std::find_if(sockets.begin(), sockets.end(), [&socket](const std::reference_wrapper<Socket>& sock) { return &socket == &sock.get(); });

        if (i != sockets.end())
        {
            sockets.erase(i);
        }
    }
}
