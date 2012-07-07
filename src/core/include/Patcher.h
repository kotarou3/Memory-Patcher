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
#ifndef PATCHER_H
#define PATCHER_H

#include <chrono>
#include <thread>
#include <vector>
#include <map>
#include <list>
#include <utility>

#include <ctime>

#include "Patch.h"
#include <mutex>

#ifdef _WIN32
namespace win32
{
    #include <windows.h>
    #include <process.h>
}
#endif

class CORE_EXPORT Patcher final
{
    public:
        using PatchGroupId = size_t;
        using patchGroupCallback_t = void (*)(PatchGroupId);

        void startPatcherThread();
        void stopPatcherThread();
        void stopPatcherThreadAndWait();

        PatchGroupId addToQueue(const std::vector<std::pair<PatchData::Patch, std::map<size_t, uint8_t*>>>& patchGroup,
                                std::time_t secondsToTry = -1,
                                patchGroupCallback_t patchGroupFailureCallback = nullptr,
                                patchGroupCallback_t patchGroupSuccessCallback = nullptr);
        void undoPatchGroup(PatchGroupId id);

        static Patcher& getSingleton();

    private:
        Patcher();
        Patcher(const Patcher&) = delete;
        Patcher& operator=(const Patcher&) = delete;
        ~Patcher();

        static void patcher_(Patcher* self);

    #if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
        win32::HANDLE patcherThread_;
    #else
        std::thread patcherThread_;
    #endif
        bool isRequestStop_;
        bool isRunning_;

        PatchGroupId getNextAvailablePatchGroupId_() const;

        class PatchGroup
        {
            public:
                class Patch
                {
                    public:
                        PatchData::Patch patch;
                        std::map<size_t, uint8_t*> relativeAddressReplaces;
                        std::map<uint8_t*, std::vector<uint8_t>> resultsAndOriginalBytes;
                };
                std::vector<Patch> patches;

                std::time_t secondsToTry;
                std::time_t timeAdded;
                patchGroupCallback_t patchGroupFailureCallback;
                patchGroupCallback_t patchGroupSuccessCallback;
                bool isTimedOut;
                bool isPatchesSuccessful;
        };
        std::map<PatchGroupId, PatchGroup> patchGroups_;
        std::list<std::map<PatchGroupId, PatchGroup>::iterator> patchGroupQueue_;
        std::recursive_mutex patchGroupsMutex_; // FIXME: Should be just a regular mutex
};

#endif
