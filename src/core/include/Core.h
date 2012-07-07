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
#ifndef CORE_H
#define CORE_H

#include <map>
#include <thread>

#include "Socket.h"
#include "Misc.h"
#include <mutex>

class CORE_EXPORT Core final
{
    public:
        using receiveHandler_t = void (*)(const std::vector<uint8_t>& data);

        void addReceiveHandler(const Socket::ServerOpCode opCode, receiveHandler_t receiveHandler);
        void removeReceiveHandler(const Socket::ServerOpCode opCode, receiveHandler_t receiveHandler);

        void sendPacket(const Socket::ClientOpCode opCode, const std::vector<uint8_t>& data) const;
        void sendCustomPacket(const size_t opCode, const std::vector<uint8_t>& data) const;

        const std::string& getCoreName() const;

        static Core& getSingleton();

    private:
        Core();
        Core(const Core&) = delete;
        Core& operator=(const Core&) = delete;
        ~Core();

        void disconnect_();
        void detach_();

        static void managerListener_(Core* self);

        std::map<Socket::ServerOpCode, std::map<receiveHandler_t, size_t>> receiveHandlers_;
        std::recursive_mutex receiveHandlersMutex_; // Should just be a normal mutex

    #if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
        win32::HANDLE managerListenerThread_;
    #else
        std::thread managerListenerThread_;
    #endif

        Socket::Socket serverSocket_;
        bool isConnected_;

        std::string coreName_;

        static Core singleton;
};

#endif
