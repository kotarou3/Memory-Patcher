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

#include <set>
#include <stdexcept>

#include <cstring>
#include <cassert>

#include "Patcher.h"
#include "Core.h"
#include "Memory.h"

using namespace PatchData;

Patcher& Patcher::getSingleton()
{
    static Patcher singleton;
    return singleton;
}

Patcher::Patcher():
    isRequestStop_(false),
    isRunning_(false)
{
}

Patcher::~Patcher()
{
#if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
    win32::Sleep(10); //TODO: See Core.cpp
#else
    if (isRunning_)
        stopPatcherThreadAndWait();
    if (patcherThread_.joinable())
        patcherThread_.join();
#endif
}

void Patcher::startPatcherThread()
{
    if (isRunning_)
        return;
    isRequestStop_ = false;
#if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
    patcherThread_ = (win32::HANDLE)win32::_beginthread((void (*)(void*))patcher_, 0, this);
#else
    patcherThread_ = std::move(std::thread(patcher_, this));
#endif
}

void Patcher::stopPatcherThread()
{
    if (!isRunning_)
        return;
    isRequestStop_ = true;
}

void Patcher::stopPatcherThreadAndWait()
{
    stopPatcherThread();
    while (isRunning_)
    #if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
        win32::Sleep(100);
    #else
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    #endif
}

Patcher::PatchGroupId Patcher::addToQueue(const std::vector<std::pair<PatchData::Patch, std::map<size_t, uint8_t*>>>& patchGroup,
                                          std::time_t secondsToTry,
                                          Patcher::patchGroupCallback_t patchGroupFailureCallback,
                                          Patcher::patchGroupCallback_t patchGroupSuccessCallback)
{
    // Check for an empty `patchGroup'
    if (patchGroup.empty())
        throw std::logic_error("`patchGroup' cannot be empty.");

    // Check if the patches given are either a replace name or replace search patch
    // And that no RVA of the relative address replaces are outside the range of the
    // patches replace bytes
    for (const auto& patch : patchGroup)
    {
        switch (patch.first.getType())
        {
            case Patch::Type::REPLACE_NAME :
                if (!patch.second.empty() && patch.second.crend()->first + 4 > patch.first.getTypeData<ReplaceNamePatch>().replaceBytes.size())
                {
                    TRACE(patch.second.crend()->first + 4 << " > " << patch.first.getTypeData<ReplaceNamePatch>().replaceBytes.size());
                    throw std::logic_error("Relative address replaces RVAs + 4 must be less than the patch's replace bytes.");
                }
                break;


            case Patch::Type::REPLACE_SEARCH :
                if (!patch.second.empty() && patch.second.crend()->first + 4 > patch.first.getTypeData<ReplaceSearchPatch>().replaceBytes.size())
                    throw std::logic_error("Relative address replaces RVAs + 4 must be less than the patch's replace bytes.");
                break;

            default:
                throw std::logic_error("Patches passed must only be of the replace name or replace search types.");
        }
        size_t previousRelativeAddressReplaceRva = -4;
        for (const auto& relativeAddressReplace : patch.second)
            if (relativeAddressReplace.first < previousRelativeAddressReplaceRva + 4)
                throw std::logic_error("Relative address replaces RVAs must be at least 4 bytes apart.");
            else
                previousRelativeAddressReplaceRva = relativeAddressReplace.first;
    }

    // Prepare the patch group structure
    PatchGroup patchGroup_;
    for (const auto& patch : patchGroup)
    {
        PatchGroup::Patch patch_;
        patch_.patch = patch.first;
        patch_.relativeAddressReplaces = patch.second;
        patchGroup_.patches.push_back(patch_);
    }
    patchGroup_.secondsToTry = secondsToTry;
    patchGroup_.timeAdded = std::time(nullptr);
    patchGroup_.patchGroupFailureCallback = patchGroupFailureCallback;
    patchGroup_.patchGroupSuccessCallback = patchGroupSuccessCallback;
    patchGroup_.isTimedOut = false;
    patchGroup_.isPatchesSuccessful = false;
    PatchGroupId patchGroupId = getNextAvailablePatchGroupId_();

    // Add it!
    std::lock_guard<std::recursive_mutex> patchGroupsLock(patchGroupsMutex_);
    patchGroups_[patchGroupId] = patchGroup_;
    patchGroupQueue_.push_back(patchGroups_.find(patchGroupId));
    TRACE("Added patch group #" << patchGroupId);
    return patchGroupId;
}

void Patcher::undoPatchGroup(Patcher::PatchGroupId id)
{
    std::lock_guard<std::recursive_mutex> patchGroupsLock(patchGroupsMutex_);
    auto patchGroup = patchGroups_.find(id);
    if (patchGroup == patchGroups_.end())
        throw std::logic_error("No such patch group exists.");

    // Check if the patch group is already patched or not (or timed out)
    if (patchGroup->second.isPatchesSuccessful)
        // Yes, so we restore the original bytes in this function
        for (const auto& patch : patchGroup->second.patches)
            for (const auto& resultAndOriginalBytes : patch.resultsAndOriginalBytes)
                Memory::safeCopy(resultAndOriginalBytes.second, resultAndOriginalBytes.first);
    else if (!patchGroup->second.isTimedOut)
        // No (and not timed out), so we just remove it from the queue
        for (auto patchGroupInQueue = patchGroupQueue_.begin(); patchGroupInQueue != patchGroupQueue_.end(); ++patchGroupInQueue)
            if (*patchGroupInQueue == patchGroup)
            {
                patchGroupQueue_.erase(patchGroupInQueue);
                break;
            }
    // If the patch group timed out, it will be already have been removed from the queue, but not patched, so we don't need to do anything

    // Erase it from the patch groups
    patchGroups_.erase(patchGroup);
}

// Private members

void Patcher::patcher_(Patcher* self)
{
    self->isRunning_ = true;
    while (!self->isRequestStop_)
    {
        {
            std::lock_guard<std::recursive_mutex> patchGroupsLock(self->patchGroupsMutex_);
            for (size_t amountInQueue = self->patchGroupQueue_.size(); amountInQueue > 0; --amountInQueue)
            {
                // Pop a patch group off the queue
                auto patchGroup = self->patchGroupQueue_.front();
                self->patchGroupQueue_.pop_front();

                // Check if time is up for the patch group
                if (patchGroup->second.secondsToTry != -1 &&
                    patchGroup->second.timeAdded + patchGroup->second.secondsToTry > std::time(nullptr))
                {
                    patchGroup->second.isTimedOut = true;
                    if (patchGroup->second.patchGroupFailureCallback != nullptr)
                        patchGroup->second.patchGroupFailureCallback(patchGroup->first);
                    continue;
                }

                bool isSuccessfulPatchGroup = true;
                try
                {
                    // Go through every patch in the group and check if they all can be patched
                    for (auto& patch : patchGroup->second.patches)
                    {
                        std::set<uint8_t*> searchResults;
                        if (patch.patch.getType() == Patch::Type::REPLACE_NAME)
                            searchResults = patch.patch.getTypeData<ReplaceNamePatch>().doSearch();
                        else if (patch.patch.getType() == Patch::Type::REPLACE_SEARCH)
                            searchResults = patch.patch.getTypeData<ReplaceSearchPatch>().doSearch();
                        else
                            assert(false); // Any other patch type should have been blocked at the adding process!

                        if (searchResults.empty())
                        {
                            isSuccessfulPatchGroup = false;
                            break;
                        }

                        for (const auto& searchResult : searchResults)
                            patch.resultsAndOriginalBytes[searchResult] = {};
                    }

                    if (isSuccessfulPatchGroup)
                        // If all patches in the group can be patched, start saving the original bytes and patching!
                        for (auto& patch : patchGroup->second.patches)
                        {
                            std::vector<uint8_t> replaceBytes;
                            std::set<size_t> ignoredReplaceBytesRvas;
                            if (patch.patch.getType() == Patch::Type::REPLACE_NAME)
                            {
                                replaceBytes = patch.patch.getTypeData<ReplaceNamePatch>().replaceBytes;
                                ignoredReplaceBytesRvas = patch.patch.getTypeData<ReplaceNamePatch>().ignoredReplaceBytesRvas;
                            }
                            else if (patch.patch.getType() == Patch::Type::REPLACE_SEARCH)
                            {
                                replaceBytes = patch.patch.getTypeData<ReplaceSearchPatch>().replaceBytes;
                                ignoredReplaceBytesRvas = patch.patch.getTypeData<ReplaceSearchPatch>().ignoredReplaceBytesRvas;
                            }
                            else
                                assert(false);

                            for (auto& resultAndOriginalBytes : patch.resultsAndOriginalBytes)
                            {
                                // Check if the memory location is readable and writable (And make it so if not)
                                bool isProtectionChanged = false;
                                auto segments = Memory::queryPage(resultAndOriginalBytes.first, replaceBytes.size());
                                for (const auto& segment : segments)
                                    if (!segment.isReadable || !segment.isWritable)
                                    {
                                        Memory::PageInfo newSegment = segment;
                                        newSegment.isReadable = true;
                                        newSegment.isWritable = true;
                                        Memory::changePageProtection(newSegment);
                                        isProtectionChanged = true;
                                    }

                                // Copy the original bytes
                                resultAndOriginalBytes.second.resize(replaceBytes.size());
                                std::memcpy(&resultAndOriginalBytes.second[0], resultAndOriginalBytes.first, replaceBytes.size());

                                // Write the new bytes
                                for (size_t b = 0; b < replaceBytes.size(); ++b)
                                {
                                    auto relativeAddressReplace = patch.relativeAddressReplaces.find(b);
                                    if (relativeAddressReplace != patch.relativeAddressReplaces.end())
                                    {
                                        size_t relativeAddress = relativeAddressReplace->second - (resultAndOriginalBytes.first + b + 4);
                                        std::memcpy(resultAndOriginalBytes.first + b, (char*)&relativeAddress, 4);
                                        b += 3;
                                        continue;
                                    }
                                    if (ignoredReplaceBytesRvas.count(b) > 0)
                                        continue;
                                    resultAndOriginalBytes.first[b] = replaceBytes[b];
                                }

                                // Restore the page(s) protection if it was changed
                                if (isProtectionChanged)
                                    for (const auto& segment : segments)
                                        Memory::changePageProtection(segment);
                            }
                        }
                    else
                        // Otherwise, remove any results
                        for (auto& patch : patchGroup->second.patches)
                            patch.resultsAndOriginalBytes.clear();
                }
                catch (...)
                {
                    isSuccessfulPatchGroup = false;
                }

                // If the patch group wasn't successful, push it back on to the queue
                if (!isSuccessfulPatchGroup)
                    self->patchGroupQueue_.push_back(patchGroup);
                else
                {
                    // Otherwise, mark it as successful
                    patchGroup->second.isPatchesSuccessful = true;
                    if (patchGroup->second.patchGroupSuccessCallback != nullptr)
                        patchGroup->second.patchGroupSuccessCallback(patchGroup->first);
                    TRACE("Patch #" << patchGroup->first  << " success!");
                }
            }
        }

    #if !defined(_GLIBCXX_HAS_GTHREADS) && defined(_WIN32)
        win32::Sleep(100);
    #else
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    #endif
    }
    self->isRunning_ = false;
}

Patcher::PatchGroupId Patcher::getNextAvailablePatchGroupId_() const
{
    // FIXME: Scan for the next available patch group id rather than just keep on incrementing
    static PatchGroupId nextAvailablePatchGroupId = 0;
    if (nextAvailablePatchGroupId == (PatchGroupId)-1)
        throw std::logic_error("Limit on patch groups reached.");
    return nextAvailablePatchGroupId++;
}
