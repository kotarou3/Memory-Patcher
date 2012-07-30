/*
    This file is part of Memory Patcher.

    Memory Patcher is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Memory Patcher is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with Memory Patcher. If not, see <http://www.gnu.org/licenses/>.
*/

#include <string>
#include <vector>
#include <stdexcept>

#include <cstring>
#include <cstdlib>
#include <cerrno>

#include <stdint.h>

#ifdef _WIN32
namespace win32
{
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define SHUT_RDWR SD_BOTH

    #include <windows.h>
    #include <process.h>

    #ifdef STILL_ACTIVE
        #undef STILL_ACTIVE // The original STILL_ACTIVE macro includes a cast, so we remove it
        #define STILL_ACTIVE (259)
    #endif
}
#else
namespace posix
{
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>

    // Bunch of POSIX functions defined in string.h
    using ::getenv;
    using ::setenv;
}
#endif

#include "Core.h"
#include "PluginLoader.h"
#include "PatchLoader.h"
#include "Patcher.h"
#include "Logger.h"

#ifdef _WIN32
    using win32::socket;
    using win32::connect;
    using win32::send;
    using win32::recv;
    using win32::shutdown;

    using win32::htons;
    using win32::htonl;

    using win32::sockaddr;
    using win32::sockaddr_in;
#else
    using posix::socket;
    using posix::connect;
    using posix::send;
    using posix::recv;
    using posix::shutdown;

    using posix::SOCK_STREAM;
    using posix::IPPROTO_TCP;
    using posix::SHUT_RDWR;

    using posix::htons;
    using posix::htonl;

    using posix::sockaddr;
    using posix::sockaddr_in;
    using posix::in_addr_t;
#endif

namespace
{
	void close(Socket::Socket socket)
	{
        shutdown(socket, SHUT_RDWR);
	#ifdef _WIN32
		win32::closesocket(socket);
	#else
		posix::close(socket);
	#endif
	}

}

Core Core::singleton;
Core& Core::getSingleton()
{
    return singleton;
}

Core::Core():
    isConnected_(false)
{
    // Start up the IPC
#ifdef _WIN32
    win32::WSADATA wsaData;
    win32::WSAStartup((2 << 8) + 2, &wsaData);
#endif
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in sockInfo = {0};
    sockInfo.sin_family = AF_INET;
    sockInfo.sin_port = htons(LISTEN_PORT);
    sockInfo.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(serverSocket_, (sockaddr*)&sockInfo, sizeof(sockaddr_in))
    #ifdef _WIN32
        == SOCKET_ERROR)
    #else
        < 0)
    #endif
    {
        close(serverSocket_);
    #ifdef _WIN32
        win32::WSACleanup();
    #endif
        throw std::runtime_error("Could not connect to manager: " +
        #ifdef _WIN32
            strErrorWin32(win32::WSAGetLastError()));
        #else
            strError(/*std::*/errno));
        #endif
    }

    // Shake hands with the manager
    Socket::ClientOpCode connect = Socket::ClientOpCode::CONNECT;
    send(serverSocket_, (char*)&connect, sizeof(Socket::ClientOpCode), 0);
    Socket::ServerOpCode reply;
    recv(serverSocket_, (char*)&reply, sizeof(Socket::ServerOpCode), 0);
    if (reply != Socket::ServerOpCode::CONNECT_OK)
    {
        close(serverSocket_);
    #ifdef _WIN32
        win32::WSACleanup();
    #endif
        throw std::runtime_error("Could not connect to manager: Invalid handshake.");
    }
    isConnected_ = true;

    // Receive the core name from the manager
    std::vector<uint8_t> coreNameData;
    std::vector<uint8_t>::size_type coreNameDataSize;
    recv(serverSocket_, (char*)&coreNameDataSize, sizeof(std::vector<uint8_t>::size_type), 0);
    coreNameData.resize(coreNameDataSize);
    recv(serverSocket_, (char*)&coreNameData[0], coreNameDataSize, 0);
    auto coreNameDataIterator = coreNameData.cbegin();
    deserialiseIntegralTypeContinuousContainer(coreNameDataIterator, coreName_);

#ifndef _WIN32
    // Clear the core name from LD_PRELOAD
    std::string LD_PRELOAD = posix::getenv("LD_PRELOAD");
    LD_PRELOAD.replace(LD_PRELOAD.find(coreName_), coreName_.size(), "");
    posix::setenv("LD_PRELOAD", LD_PRELOAD.c_str(), true);
#endif

    // Initialise the important other classes.
    // getSingleton() in them are configure to construct the singleton on first call
    PluginLoader::getSingleton();
    Patcher::getSingleton().startPatcherThread();
    PatchLoader::getSingleton();

    // Start the listener thread
#if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
    managerListenerThread_ = (win32::HANDLE)win32::_beginthread((void (*)(void*))managerListener_, 0, this);
#else
    managerListenerThread_ = std::move(std::thread(managerListener_, this));
#endif

    // Tell the manager we are ready!
    Socket::ClientOpCode ready = Socket::ClientOpCode::READY;
    send(serverSocket_, (char*)&ready, sizeof(Socket::ClientOpCode), 0);
}

Core::~Core()
{
    disconnect_();
#if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
    win32::Sleep(10); //TODO: Hook on to ExitProcess so the listener thread can exit cleanly
#else
    if (managerListenerThread_.joinable())
        managerListenerThread_.join();
#endif
#ifdef _WIN32
    win32::WSACleanup();
#endif
}

void Core::addReceiveHandler(const Socket::ServerOpCode opCode, receiveHandler_t receiveHandler)
{
    std::lock_guard<std::recursive_mutex> receiveHandlersLock(receiveHandlersMutex_);
    ++receiveHandlers_[opCode][receiveHandler];
}

void Core::removeReceiveHandler(const Socket::ServerOpCode opCode, receiveHandler_t receiveHandler)
{
    std::lock_guard<std::recursive_mutex> receiveHandlersLock(receiveHandlersMutex_);
    if (receiveHandlers_[opCode].count(receiveHandler) < 1)
        throw std::logic_error("No such receive handler exists.");
    --receiveHandlers_[opCode][receiveHandler];
    if (receiveHandlers_[opCode][receiveHandler] < 1)
        receiveHandlers_[opCode].erase(receiveHandler);
}

void Core::sendPacket(const Socket::ClientOpCode opCode, const std::vector<uint8_t>& data) const
{
    if (!isConnected_)
        return;

    static std::recursive_mutex sendMutex; // Should be just a normal mutex
    Socket::ClientHeader header = { opCode, data.size() };
    std::lock_guard<std::recursive_mutex> sendLock(sendMutex);
    send(serverSocket_, (char*)&header, sizeof(Socket::ClientHeader), 0);
    send(serverSocket_, (char*)&data[0], data.size(), 0);
}

void Core::sendCustomPacket(const size_t opCode, const std::vector<uint8_t>& data) const
{
    Socket::CustomPacket* packet = (Socket::CustomPacket*)std::malloc(sizeof(Socket::CustomPacket) + data.size());
    packet->opCode = opCode;
    packet->dataSize = data.size();
    std::memcpy(&packet->data, &data[0], data.size());
    std::vector<uint8_t> newdata((uint8_t*)packet, (uint8_t*)(&packet->data + data.size()));
    std::free(packet);
    sendPacket(Socket::ClientOpCode::CUSTOM, newdata);
}

const std::string& Core::getCoreName() const
{
    return coreName_;
}

// Private Members

void Core::disconnect_()
{
    if (!isConnected_)
        return;
    isConnected_ = false;
    sendPacket(Socket::ClientOpCode::DISCONNECT, {});
    close(serverSocket_);
}

void Core::detach_()
{
    disconnect_();
    // TODO
}

void Core::managerListener_(Core* self)
{
    while (true)
    {
        Socket::ServerHeader serverHeader;
        int amountRead = recv(self->serverSocket_, (char*)&serverHeader, sizeof(Socket::ServerHeader), 0);
    #ifdef _WIN32
        if (amountRead == SOCKET_ERROR ||
    #else
        if (amountRead <= 0 ||
    #endif
            serverHeader.opCode == Socket::ServerOpCode::DISCONNECT)
        {
            // Connection ended unexpectedly, disconnect_() called or asked to disconnect by manager
            self->disconnect_();
            break;
        }
        else if (serverHeader.opCode == Socket::ServerOpCode::DETACH)
        {
            // Or did the manager ask us to detach?
            self->detach_();
            break;
        }

        std::vector<uint8_t> data;
        data.resize(serverHeader.dataSize);
        recv(self->serverSocket_, (char*)&data[0], serverHeader.dataSize, 0);
        try
        {
            std::lock_guard<std::recursive_mutex> receiveHandlersLock(self->receiveHandlersMutex_);
            for (auto& receiveHandler : self->receiveHandlers_[serverHeader.opCode])
                receiveHandler.first(data);
        }
        catch (std::exception& e)
        {
            Logger::getSingleton().write(Logger::Severity::ERROR, e.what());
        }
    }
}
