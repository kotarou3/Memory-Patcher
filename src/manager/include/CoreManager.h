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

#pragma once
#ifndef COREMANAGER_H
#define COREMANAGER_H

#include <string>
#include <vector>
#include <map>
#include <utility>
#include <thread>
#include <mutex>

#include <stdint.h>

#ifdef _WIN32
namespace win32
{
    #include <winsock2.h>
    #include <windows.h>
}
#else
namespace posix
{
    #include <unistd.h>
    using ::pid_t; // We shouldn't need to do this, but <string> causes everything in <pthreads.h> to be included in the global namespace
}
#endif

#include "Socket.h"
#include "Misc.h"

class MANAGER_EXPORT CoreManager final
{
    public:
        using CoreId = uint8_t;
        using receiveHandler_t = void (*)(CoreId, const std::vector<uint8_t>& data);
        using ProcessId =
        #ifdef _WIN32
            win32::PROCESS_INFORMATION;
        #else
            posix::pid_t;
        #endif

        CoreId startCore();
        void endCoreConnection(const CoreId coreId);
        void endCore(const CoreId coreId);
        std::vector<CoreId> getConnectedCores() const;

        void addReceiveHandler(const Socket::ClientOpCode opCode, receiveHandler_t receiveHandler);
        void removeReceiveHandler(const Socket::ClientOpCode opCode, receiveHandler_t receiveHandler);

        void sendPacket(const Socket::ServerOpCode opCode, const std::vector<uint8_t>& data) const;
        void sendCustomPacket(const size_t opCode, const std::vector<uint8_t>& data) const;
        void sendPacketTo(const CoreId coreId, const Socket::ServerOpCode opCode, const std::vector<uint8_t>& data) const;
        void sendCustomPacketTo(const CoreId coreId, const size_t opCode, const std::vector<uint8_t>& data) const;

        static CoreManager& getSingleton();

    private:
        CoreManager();
        CoreManager(const CoreManager&) = delete;
        CoreManager& operator=(const CoreManager&) = delete;
        ~CoreManager();

        void initQuitSockets_();

        Socket::Socket startConnectCore_() const;
        ProcessId startCore_(const std::string& applicationName, const std::string& parameters, const std::string& coreName);
        CoreId finishConnectCore_(ProcessId pid, Socket::Socket listenSocket, const std::string& coreName);

        void endAllCoreConnections_();

        static void logReceiveHandler_(CoreId coreId, const std::vector<uint8_t>& data);

        static void coreListener_(CoreManager* self);

        std::map<Socket::ClientOpCode, std::map<receiveHandler_t, size_t>> receiveHandlers_;
        std::map<CoreId, std::pair<ProcessId, Socket::Socket>> cores_;
        std::recursive_mutex receiveHandlersMutex_; // Should be just a normal mutex
        mutable std::recursive_mutex coresMutex_;

    #if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
        win32::HANDLE coreListenerThread_;
    #else
        std::thread coreListenerThread_;
    #endif
        Socket::Socket listenerThreadServerSocket_, listenerThreadClientSocket_; // Communication between listener thread and main thread

        CoreId getNextAvailableCoreId_() const;

        static CoreManager singleton;
};

#endif
