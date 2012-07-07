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

#include <stdexcept>

#include "Logger.h"
#include "Core.h"

Logger& Logger::getSingleton()
{
    static Logger singleton;
    return singleton;
}

void Logger::write(Severity severity, const std::string& message) const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralType(data, severity);
    serialiseIntegralTypeContainer(data, message);

    std::lock_guard<std::recursive_mutex> logWriteLock(logWriteMutex_);
    Core::getSingleton().sendPacket(Socket::ClientOpCode::LOG, data);

    std::lock_guard<std::recursive_mutex> loggingHandlersLock(loggingHandlersMutex_);
    for (auto& loggingHandler : loggingHandlers_)
        loggingHandler.first(severity, message);
}

void Logger::addLoggingHandler(LoggingHandler_t loggingHandler)
{
    std::lock_guard<std::recursive_mutex> loggingHandlersLock(loggingHandlersMutex_);
    ++loggingHandlers_[loggingHandler];
}

void Logger::removeLoggingHandler(LoggingHandler_t loggingHandler)
{
    std::lock_guard<std::recursive_mutex> loggingHandlersLock(loggingHandlersMutex_);
    if (loggingHandlers_.count(loggingHandler) < 1)
        throw std::logic_error("No such logging handler exists.");
    --loggingHandlers_[loggingHandler];
    if (loggingHandlers_[loggingHandler] < 1)
        loggingHandlers_.erase(loggingHandler);
}
