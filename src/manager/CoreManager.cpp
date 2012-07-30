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

#include <chrono>
#include <algorithm>
#include <stdexcept>

#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cerrno>

#ifdef _WIN32
namespace win32
{
    #include <io.h>
    #include <fcntl.h>

    #include <ws2tcpip.h>
    #define SHUT_RDWR SD_BOTH

    #include <process.h>

    #ifdef STILL_ACTIVE
        #undef STILL_ACTIVE // The original STILL_ACTIVE macro includes a cast, so we remove it
        #define STILL_ACTIVE 259
    #endif
}
#else
namespace posix
{
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/socket.h>
    //#include <sys/select.h> // Can't seem to put select.h in a namespace because of standard headers including it
    #include <netinet/in.h>
    #include <signal.h>

    using ::strdup; // POSIX function that lives in string.h
}
    #include "stringToArgcArgv.h"

    extern char **environ;
#endif

#include "CoreManager.h"
#include "PluginManager.h"
#include "PatchManager.h"
#include "SettingsManager.h"
#include "Logger.h"

#ifdef _WIN32
    using win32::socket;
    using win32::setsockopt;
    using win32::bind;
    using win32::listen;
    using win32::accept;
    using win32::connect;
    using win32::select;
    using win32::send;
    using win32::recv;
    using win32::shutdown;

    using win32::htons;
    using win32::htonl;

    using win32::sockaddr;
    using win32::sockaddr_in;
    //using win32::timeval; // <chrono> pollutes the global namespace with this...
    using win32::fd_set;

    // Need to import these so the fd_set macros work
    using win32::u_int;
    using win32::SOCKET;
#else
    using posix::socket;
    using posix::setsockopt;
    using posix::bind;
    using posix::listen;
    using posix::accept;
    using posix::connect;
    //using posix::select;
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
    //using posix::timeval;
    //using posix::fd_set;
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

CoreManager& CoreManager::getSingleton()
{
    static CoreManager singleton;
    return singleton;
}

CoreManager::CoreManager()
{
#ifdef _WIN32
    win32::WSADATA wsaData;
    win32::WSAStartup((2 << 8) + 2, &wsaData);
#endif
    initQuitSockets_();
    addReceiveHandler(Socket::ClientOpCode::LOG, logReceiveHandler_);
}

CoreManager::~CoreManager()
{
    endAllCoreConnections_();
#if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
    win32::Sleep(10); //TODO: Hook on to ExitProcess so the listener thread can exit cleanly
#else
    if (coreListenerThread_.joinable())
        coreListenerThread_.join();
#endif
    close(listenerThreadServerSocket_);
    close(listenerThreadClientSocket_);
#ifdef _WIN32
    win32::WSACleanup();
#endif
}

CoreManager::CoreId CoreManager::startCore()
{
    std::string applicationName = SettingsManager::getSingleton().get("CoreManager.applicationName");
    std::string parameters = SettingsManager::getSingleton().get("CoreManager.applicationParameters");
    std::string libraryPath = SettingsManager::getSingleton().get("CoreManager.libraryPath");
    std::string coreName = "lib" + SettingsManager::getSingleton().get("CoreManager.coreLibrary");
#ifdef _WIN32
    coreName += ".dll";
#else
    coreName += ".so";
#endif

    Socket::Socket listenSocket = startConnectCore_();
    ProcessId pid;
    try
    {
        pid = startCore_(applicationName, parameters, libraryPath, coreName);
    }
    catch (std::exception e)
    {
        close(listenSocket);
        throw;
    }
    return finishConnectCore_(pid, listenSocket, coreName);
}

void CoreManager::endCoreConnection(const CoreId coreId)
{
    std::lock_guard<std::recursive_mutex> coresLock(coresMutex_);
    if (cores_.count(coreId) == 0)
        throw std::logic_error("Invalid core id.");
    Socket::ServerHeader disconnect = { Socket::ServerOpCode::DISCONNECT, 0 };
    send(cores_[coreId].second, (char*)&disconnect, sizeof(Socket::ServerHeader), 0);
    send(listenerThreadClientSocket_, (char*)&coreId, sizeof(coreId), 0);
}

void CoreManager::endCore(const CoreId coreId)
{
    std::lock_guard<std::recursive_mutex> coresLock(coresMutex_);
    if (cores_.count(coreId) == 0)
        throw std::logic_error("Invalid core id.");
    Socket::ServerHeader detach =  { Socket::ServerOpCode::DETACH, 0 };
    send(cores_[coreId].second, (char*)&detach, sizeof(Socket::ServerHeader), 0);
    send(listenerThreadClientSocket_, (char*)&coreId, sizeof(coreId), 0);
}

std::vector<CoreManager::CoreId> CoreManager::getConnectedCores() const
{
    std::lock_guard<std::recursive_mutex> coresLock(coresMutex_);
    std::vector<CoreId> result;
    result.reserve(cores_.size());
    for (const auto& core : cores_)
        result.push_back(core.first);
    return result;
}

void CoreManager::addReceiveHandler(const Socket::ClientOpCode opCode, receiveHandler_t receiveHandler)
{
    std::lock_guard<std::recursive_mutex> receiveHandlersLock(receiveHandlersMutex_);
    ++receiveHandlers_[opCode][receiveHandler];
}

void CoreManager::removeReceiveHandler(const Socket::ClientOpCode opCode, receiveHandler_t receiveHandler)
{
    std::lock_guard<std::recursive_mutex> receiveHandlersLock(receiveHandlersMutex_);
    if (receiveHandlers_[opCode].count(receiveHandler) < 1)
        throw std::logic_error("No such receive handler exists.");
    --receiveHandlers_[opCode][receiveHandler];
    if (receiveHandlers_[opCode][receiveHandler] < 1)
        receiveHandlers_[opCode].erase(receiveHandler);
}

void CoreManager::sendPacket(const Socket::ServerOpCode opCode, const std::vector<uint8_t>& data) const
{
    std::lock_guard<std::recursive_mutex> coresLock(coresMutex_);
    for (const auto& core : cores_)
            sendPacketTo(core.first, opCode, data);
}

void CoreManager::sendCustomPacket(const size_t opCode, const std::vector<uint8_t>& data) const
{
    std::lock_guard<std::recursive_mutex> coresLock(coresMutex_);
    for (const auto& core : cores_)
            sendCustomPacketTo(core.first, opCode, data);
}

void CoreManager::sendPacketTo(const CoreId coreId, const Socket::ServerOpCode opCode, const std::vector<uint8_t>& data) const
{
    std::lock_guard<std::recursive_mutex> coresLock(coresMutex_);
    if (cores_.count(coreId) == 0)
        throw std::logic_error("Invalid core id.");
    Socket::ServerHeader header = { opCode, data.size() };
    TRACE("Sending packet to core #" << (int)coreId << ": code: " << (int)header.opCode << "; length: " << header.dataSize);
    send(cores_.at(coreId).second, (char*)&header, sizeof(Socket::ServerHeader), 0);
    send(cores_.at(coreId).second, (char*)&data[0], data.size(), 0);
}

void CoreManager::sendCustomPacketTo(const CoreId coreId, const size_t opCode, const std::vector<uint8_t>& data) const
{
    Socket::CustomPacket* packet = (Socket::CustomPacket*)std::malloc(sizeof(Socket::CustomPacket) + data.size());
    packet->opCode = opCode;
    packet->dataSize = data.size();
    std::memcpy(&packet->data, &data[0], data.size());
    std::vector<uint8_t> newdata((uint8_t*)packet, (uint8_t*)(&packet->data + data.size()));
    std::free(packet);
    sendPacketTo(coreId, Socket::ServerOpCode::CUSTOM, newdata);
}

// Private members

void CoreManager::initQuitSockets_()
{
    Socket::Socket listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int on = true;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(int));
    sockaddr_in sockInfo = {0};
    sockInfo.sin_family = AF_INET;
    sockInfo.sin_port = htons(LISTEN_PORT + 1);
    sockInfo.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(listenSocket, (sockaddr*)&sockInfo, sizeof(sockaddr_in));
    listen(listenSocket, SOMAXCONN);

    Socket::Socket clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(clientSocket, (sockaddr*)&sockInfo, sizeof(sockaddr_in));

    Socket::Socket serverSocket = accept(listenSocket, nullptr, nullptr);
	close(listenSocket);
#ifndef _WIN32
    posix::fcntl(serverSocket, F_SETFD, posix::fcntl(serverSocket, F_GETFD) | FD_CLOEXEC);
    posix::fcntl(clientSocket, F_SETFD, posix::fcntl(clientSocket, F_GETFD) | FD_CLOEXEC);
#endif
    listenerThreadServerSocket_ = serverSocket;
    listenerThreadClientSocket_ = clientSocket;
}

Socket::Socket CoreManager::startConnectCore_() const
{
    Socket::Socket listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int on = true;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(int));
#ifndef _WIN32
    posix::fcntl(listenSocket, F_SETFD, posix::fcntl(listenSocket, F_GETFD) | FD_CLOEXEC);
#endif
    sockaddr_in sockInfo = {0};
    sockInfo.sin_family = AF_INET;
    sockInfo.sin_port = htons(LISTEN_PORT);
    sockInfo.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(listenSocket, (sockaddr*)&sockInfo, sizeof(sockaddr_in));
    listen(listenSocket, SOMAXCONN);
    return listenSocket;
}

CoreManager::ProcessId CoreManager::startCore_(const std::string& applicationName, const std::string& parameters, const std::string& libraryPath, const std::string& coreName)
{
#ifdef _WIN32
    // Create the process as suspended
    win32::STARTUPINFO si = {0};
    ProcessId pid = {0};
    if (!win32::CreateProcess(applicationName.c_str(), &(applicationName + " " + parameters)[0], nullptr, nullptr, false, CREATE_SUSPENDED, nullptr, nullptr, &si, &pid))
        throw std::runtime_error("Could not create process: " + strErrorWin32(win32::GetLastError()));

    // Allocate and initialise memory in the other process with the core name
    std::string corePathfile = libraryPath + "/" + coreName; // FIXME: Should be full path
    void* coreName_otherProcess = win32::VirtualAllocEx(pid.hProcess, nullptr, MAX_PATH, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    win32::WriteProcessMemory(pid.hProcess, coreName_otherProcess, corePathfile.c_str(), corePathfile.size(), nullptr);

    // Create wrapper code around the LoadLibrary call so it returns the error code on exit because we don't need to know the core address
    char callLoadLibrary[] = {
                                 0x68, -1, -1, -1, -1,  // push xxxxxxxxh (coreName_otherProcess)
                                 0xe8, -1, -1, -1, -1,  // call +xxxxxxxxh (LoadLibraryA)
                                 0x85, 0xc0,            // test eax, eax
                                 0x74, 0x05,            // jz   short +5
                                 0x31, 0xc0,            // xor  eax, eax
                                 0xc2, 0x04, 0x00,      // retn 4
                                 0xe8, -1, -1, -1, -1,  // call +xxxxxxxxh (GetLastError)
                                 0xc2, 0x04, 0x00       // retn 4
                             };

    // Allocate memory in the other process for the wrapper code
    void* callLoadLibrary_otherProcess = win32::VirtualAllocEx(pid.hProcess, nullptr, sizeof(callLoadLibrary), MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);

    // Initialise the wrapper code with correct addresses
    *(char**)(callLoadLibrary + 1) = (char*)coreName_otherProcess;
    *(char**)(callLoadLibrary + 6) = (char*)win32::GetProcAddress(win32::GetModuleHandle("kernel32.dll"), "LoadLibraryA") - ((size_t)callLoadLibrary_otherProcess + 10);
    *(char**)(callLoadLibrary + 20) = (char*)win32::GetProcAddress(win32::GetModuleHandle("kernel32.dll"), "GetLastError") - ((size_t)callLoadLibrary_otherProcess + 25);

    // Write the wrapper code to the other process and call it
    win32::WriteProcessMemory(pid.hProcess, callLoadLibrary_otherProcess, callLoadLibrary, sizeof(callLoadLibrary), nullptr);
    win32::HANDLE loadLibrary_otherProcess = win32::CreateRemoteThread(pid.hProcess, nullptr, 0, (win32::LPTHREAD_START_ROUTINE)callLoadLibrary_otherProcess, nullptr, 0, nullptr);

    // Test if LoadLibrary failed or not
    win32::WaitForSingleObject(loadLibrary_otherProcess, 50); // Give it some time to fail, if it's gonna fail
    uint32_t lastError;
    win32::GetExitCodeThread(loadLibrary_otherProcess, (win32::DWORD*)&lastError);
    if (lastError != STILL_ACTIVE) // The thread will never exit normally under 50ms because it blocks on a recv
        throw std::runtime_error("Could not inject core: " + strErrorWin32(lastError));
#else
    int pipes[2];
    posix::pipe(pipes);
    posix::fcntl(pipes[0], F_SETFD, posix::fcntl(pipes[0], F_GETFD) | FD_CLOEXEC);
    posix::fcntl(pipes[1], F_SETFD, posix::fcntl(pipes[1], F_GETFD) | FD_CLOEXEC);
    ProcessId pid = posix::fork();
    if (pid == 0)
    {
        posix::close(pipes[0]);
        int argc;
        char** _argv;
        try
        {
            stringToArgcArgv(applicationName + parameters, &argc, &_argv);
        }
        catch (std::exception e)
        {
            throw std::logic_error("Error reading parameters: " + std::string(e.what()));
        }
        // stringToArgcArgv doesn't output the ending NULL so
        // we have to manually add it on
        char* argv[argc + 1];
        for (int i = 0; i < argc; ++i)
            argv[i] = _argv[i];
        argv[argc] = NULL;

        // Get the current environment
        std::vector<char*> _envp;
        std::string LD_LIBRARY_PATH;
        std::string LD_PRELOAD;
        for (size_t i = 0; environ[i] != nullptr; ++i)
            if (std::strstr(environ[i], "LD_LIBRARY_PATH") == environ[i])
                LD_LIBRARY_PATH = std::strchr(environ[i], '=');
            else if (std::strstr(environ[i], "LD_PRELOAD") == environ[i])
                LD_PRELOAD = std::strchr(environ[i], '=');
            else
                _envp.push_back(environ[i]);

        // Update LD_LIBRARY_PATH to contain the path to the core
        if (LD_LIBRARY_PATH.empty())
            LD_LIBRARY_PATH = libraryPath;
        else if ((":" + LD_LIBRARY_PATH + ":").find(":" + libraryPath + ":") == std::string::npos)
            LD_LIBRARY_PATH += ":" + libraryPath;

        // Update LD_PRELOAD to load the core
        if (LD_PRELOAD.empty())
            LD_PRELOAD = coreName;
        else
            LD_PRELOAD += " " + coreName;

        // Ready the environment to be passed to exec
        char* envp[_envp.size() + 3];
        for (size_t i = 0; i < _envp.size(); ++i)
            envp[i] = _envp[i];
        envp[_envp.size()] = posix::strdup(("LD_LIBRARY_PATH=" + LD_LIBRARY_PATH).c_str());
        envp[_envp.size() + 1] = posix::strdup(("LD_PRELOAD=" + LD_PRELOAD).c_str());
        envp[_envp.size() + 2] = nullptr;

        // Screw memory deallocation. We are exec'ing!
        posix::execvpe(applicationName.c_str(), argv, envp);
        // We shouldn't get here if execvpe was successful
        posix::write(pipes[1], &errno, sizeof(errno)); // Write error to parent
        posix::close(pipes[1]);
        std::abort();
    }
    posix::close(pipes[1]);
    if (pid < 0)
    {
        posix::close(pipes[0]);
        throw std::runtime_error("Could not fork: " + strError(errno));
    }
    int err;
    int amountRead = posix::read(pipes[0], &err, sizeof(err));
    posix::close(pipes[0]);
    if (amountRead > 0)
        throw std::runtime_error("Could not execvpe: " + strError(err));
#endif
    return pid;
}

CoreManager::CoreId CoreManager::finishConnectCore_(ProcessId pid, Socket::Socket listenSocket, const std::string& coreName)
{
    // Attempt to connect to the core
    fd_set listenSet;
    FD_ZERO(&listenSet);
    FD_SET(listenSocket, &listenSet);
    timeval timeoutWait = { 5, 0 }; // 5 second timeout
    if (!select(listenSocket + 1, &listenSet, nullptr, nullptr, &timeoutWait))
    {
    #ifdef _WIN32
        win32::TerminateProcess(pid.hProcess, 1);
    #else
        posix::kill(pid, SIGKILL);
    #endif
        throw std::runtime_error("Could not connect to core: Timeout.");
    }
    Socket::Socket coreConnection = accept(listenSocket, nullptr, nullptr);
	close(listenSocket);
#ifndef _WIN32
    posix::fcntl(coreConnection, F_SETFD, posix::fcntl(coreConnection, F_GETFD) | FD_CLOEXEC);
#endif

    // Shake hands with the core
    Socket::ClientOpCode clientOpCode;
    recv(coreConnection, (char*)&clientOpCode, sizeof(Socket::ClientOpCode), 0);
    if (clientOpCode != Socket::ClientOpCode::CONNECT) // FIXME: Should have some magic string too
    {
		close(coreConnection);
        throw std::runtime_error("Could not connect to core: Invalid handshake.");
    }
    Socket::ServerOpCode serverOpCode = Socket::ServerOpCode::CONNECT_OK;
    send(coreConnection, (char*)&serverOpCode, sizeof(Socket::ServerOpCode), 0);

    // Send the core name to the core
    std::vector<uint8_t> coreNameData;
    serialiseIntegralTypeContinuousContainer(coreNameData, coreName);
    std::vector<uint8_t>::size_type coreNameDataSize = coreNameData.size();
    send(coreConnection, (char*)&coreNameDataSize, sizeof(std::vector<uint8_t>::size_type), 0);
    send(coreConnection, (char*)&coreNameData[0], coreNameDataSize, 0);

    // Wait for the core to be ready
    do
        recv(coreConnection, (char*)&clientOpCode, sizeof(Socket::ClientOpCode), 0);
    while (clientOpCode != Socket::ClientOpCode::READY);

    // Add it to the connect cores list
    std::lock_guard<std::recursive_mutex> coresLock(coresMutex_);
    CoreId coreId = getNextAvailableCoreId_();
    cores_[coreId] = std::make_pair(pid, coreConnection);

    // Start the listener thread if it isn't running, or notify of new core if it is
    if (cores_.size() == 1)
    #if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
        coreListenerThread_ = (win32::HANDLE)win32::_beginthread((void(*)(void*))coreListener_, 0, (void*)this);
    #else
        coreListenerThread_ = std::move(std::thread(coreListener_, this));
    #endif
    else
    {
        CoreId notify = 0;
        send(listenerThreadClientSocket_, (char*)&notify, sizeof(CoreId), 0);
    }

    // Give information to the core
    PluginManager::getSingleton().updateCoreAboutAll(coreId);
    PatchManager::getSingleton().updateCoreAboutAllHooks(coreId);
    PatchManager::getSingleton().updateCoreAboutAllPatchPacks(coreId);

#ifdef _WIN32
    TRACE("Resuming main thread...");
    win32::ResumeThread(pid.hThread);
#endif
    return coreId;
}

void CoreManager::endAllCoreConnections_()
{
    std::lock_guard<std::recursive_mutex> coresLock(coresMutex_);
    for (const auto& core : cores_)
        endCoreConnection(core.first);
}

void CoreManager::logReceiveHandler_(CoreId coreId, const std::vector<uint8_t>& data)
{
    auto iterator = data.begin();
    Logger::Severity severity = deserialiseIntegralType<Logger::Severity>(iterator);
    std::string message = deserialiseIntegralTypeContinuousContainer<std::string>(iterator);

    message = "From Core #" + itos(coreId) + ": " + message;
    Logger::getSingleton().write(severity, message);
}

void CoreManager::coreListener_(CoreManager* self)
{
    while (true)
    {
        {
            std::lock_guard<std::recursive_mutex> coresLock(self->coresMutex_);
            if (self->cores_.empty())
                break;
        }
        fd_set listenOn;
        FD_ZERO(&listenOn);
        FD_SET(self->listenerThreadServerSocket_, &listenOn);
        Socket::Socket maxFd = self->listenerThreadServerSocket_;
        {
            std::lock_guard<std::recursive_mutex> coresLock(self->coresMutex_);
            for (const auto& core : self->cores_)
            {
                FD_SET(core.second.second, &listenOn);
                if (core.second.second > maxFd)
                    maxFd = core.second.second;
            }
        }
        select(maxFd + 1, &listenOn, nullptr, nullptr, nullptr);

        std::lock_guard<std::recursive_mutex> coresLock(self->coresMutex_);

        // Any messages from the main thread?
        if (FD_ISSET(self->listenerThreadServerSocket_, &listenOn))
        {
            // Any non-zero integer received means to disconnect that core
            CoreId coreToQuit;
            recv(self->listenerThreadServerSocket_, (char*)&coreToQuit, sizeof(CoreId), 0);
            if (coreToQuit != 0)
            {
			    close(self->cores_[coreToQuit].second);
			    self->cores_.erase(coreToQuit);
		    }
		    // But a zero received is used to break this thread out of select(), usually used to notify of a new core
        }

        // Incoming requests from cores?
        for (auto core = self->cores_.begin(); core != self->cores_.end(); )
        {
            if (FD_ISSET(core->second.second, &listenOn))
            {
                Socket::ClientHeader header;
                int amountRead = recv(core->second.second, (char*)&header, sizeof(Socket::ClientHeader), 0);
            #ifdef _WIN32
                if (amountRead == SOCKET_ERROR ||
            #else
                if (amountRead <= 0 ||
            #endif
                    header.opCode == Socket::ClientOpCode::DISCONNECT)
                {
                    // Connection ended unexpectedly or core quit
                    auto tmp = *core;
                    self->cores_.erase(core->first);
                    close(tmp.second.second);
                    core = self->cores_.upper_bound(tmp.first);
                    continue;
                }

                std::vector<uint8_t> data;
                data.resize(header.dataSize);
                recv(core->second.second, (char*)&data[0], header.dataSize, 0);

                std::lock_guard<std::recursive_mutex> receiveHandlersLock(self->receiveHandlersMutex_);
                for (auto& receiveHandler : self->receiveHandlers_[header.opCode])
                    receiveHandler.first(core->first, data);
            }
            ++core;
        }
    }
}

CoreManager::CoreId CoreManager::getNextAvailableCoreId_() const
{
    // This function doesn't scan for a next available id so if an core unexpectedly quits,
    // attempting to send any packets to that core will result in an exception. If this
    // function did scan for the next available id, then the packet might be accidently be
    // sent to the wrong core.
    static CoreId nextAvailableCoreId = 1;
    if (nextAvailableCoreId == 0)
        throw std::logic_error("Limit on cores reached.");
    return nextAvailableCoreId++;
}
