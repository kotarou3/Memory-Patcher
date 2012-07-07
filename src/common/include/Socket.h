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
#ifndef SOCKET_H
#define SOCKET_H

// TODO: Rewrite this file or use a socket library

#include <string>

#include <cstring>

#include <stdint.h>

#ifdef _WIN32
namespace win32
{
    #include <winsock2.h>
}
#endif

#define LISTEN_PORT (int16_t)(('C' + 'o') * ('r' + 'e') / 2)

namespace Socket
{
    using Socket =
    #ifdef _WIN32
        win32::SOCKET;
    #else
        int;
    #endif

    enum class ServerOpCode
    {
        CONNECT_OK, DISCONNECT, DETACH,
        PLUGIN, PLUGIN_REMOVE,
        PATCH_PACK, PATCH_PACK_REMOVE,
        PATCH_HOOK, PATCH_HOOK_REMOVE,
        PATCH_LIB_LOAD, PATCH_LIB_UNLOAD,
        CUSTOM
    };
    enum class ClientOpCode { CONNECT, DISCONNECT, READY, LOG, CUSTOM };

    class ServerHeader final
    {
        public:
            ServerOpCode opCode;
            size_t dataSize;
    };
    class ClientHeader final
    {
        public:
            ClientOpCode opCode;
            size_t dataSize;
    };

    class CustomPacket final
    {
        public:
            size_t opCode;
            size_t dataSize;
            uint8_t data[];
    };
}

#endif
