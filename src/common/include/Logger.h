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
#ifndef LOGGER_H
#define LOGGER_H

#include <map>

#include "Misc.h"
#include <mutex>

class COMMON_EXPORT Logger final
{
    public:
        #ifdef ERROR
            #undef ERROR
        #endif
        enum class Severity { ERROR_FATAL, ERROR, WARNING, NOTICE, DEBUG_MESSAGE };
        using LoggingHandler_t = void (*)(Severity, const std::string&);

        void write(Severity severity, const std::string& message) const;
        void addLoggingHandler(LoggingHandler_t loggingHandler);
        void removeLoggingHandler(LoggingHandler_t loggingHandler);

        static Logger& getSingleton();

    private:
        Logger() = default;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        ~Logger() = default;

        std::map<LoggingHandler_t, size_t> loggingHandlers_;
        mutable std::recursive_mutex loggingHandlersMutex_; // Should just be a normal mutex
        mutable std::recursive_mutex logWriteMutex_; // Same goes for this one
};

#endif
