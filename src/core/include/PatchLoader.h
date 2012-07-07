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
#ifndef PATCHLOADER_H
#define PATCHLOADER_H

#include <string>
#include <vector>
#include <utility>

#include "Misc.h"
#include "Hook.h"
#include "Patch.h"
#include "Patcher.h"
#include "Module.h"
#include "HookFunctions.h"
#include <mutex>

class CORE_EXPORT PatchLoader final
{
    public:
        bool isHookRegistered(const std::string& name) const noexcept;
        bool isPatchPackLoaded(const std::string& name) const noexcept;
        bool isPatchPackEnabled(const std::string& name) const;

        const std::vector<PatchData::Hook> getHooks() const;
        const PatchData::Hook& getHook(const std::string& name) const;
        const std::vector<PatchData::PatchPack> getPatchPacks() const;
        const PatchData::PatchPack& getPatchPack(const std::string& name) const;

        static PatchLoader& getSingleton();

    private:
        PatchLoader();
        PatchLoader(const PatchLoader&) = delete;
        PatchLoader& operator=(const PatchLoader&) = delete;
        ~PatchLoader();

        static void patchHookReceiveHandler_(const std::vector<uint8_t>& data);
        static void patchHookRemoveReceiveHandler_(const std::vector<uint8_t>& data);
        static void patchPackReceiveHandler_(const std::vector<uint8_t>& data);
        static void patchPackRemoveReceiveHandler_(const std::vector<uint8_t>& data);
        static void patchLibraryLoadReceiveHandler_(const std::vector<uint8_t>& data);
        static void patchLibraryUnloadReceiveHandler_(const std::vector<uint8_t>& data);

        std::vector<std::pair<PatchData::Hook, Patcher::PatchGroupId>>::const_iterator getIteratorToHookNoThrow_(const std::string& name) const noexcept;
        std::vector<std::pair<PatchData::Hook, Patcher::PatchGroupId>>::const_iterator getIteratorToHook_(const std::string& name) const;
        std::vector<std::pair<PatchData::Hook, Patcher::PatchGroupId>>::iterator getIteratorToHook_(const std::string& name);
        std::vector<std::pair<PatchData::PatchPack, Patcher::PatchGroupId>>::const_iterator getIteratorToPatchPackNoThrow_(const std::string& name) const noexcept;
        std::vector<std::pair<PatchData::PatchPack, Patcher::PatchGroupId>>::const_iterator getIteratorToPatchPack_(const std::string& name) const;
        std::vector<std::pair<PatchData::PatchPack, Patcher::PatchGroupId>>::iterator getIteratorToPatchPack_(const std::string& name);

        void registerHook_(const PatchData::Hook& hook);
        std::vector<std::pair<PatchData::Hook, Patcher::PatchGroupId>>::iterator unregisterHook_(std::vector<std::pair<PatchData::Hook, Patcher::PatchGroupId>>::iterator hook);
        void unregisterAllHooks_();

        void applyHook_(std::pair<PatchData::Hook, Patcher::PatchGroupId>& hook);
        void unapplyHook_(std::pair<PatchData::Hook, Patcher::PatchGroupId>& hook);

        void addPatchPack_(const PatchData::PatchPack& patchPack);
        std::vector<std::pair<PatchData::PatchPack, Patcher::PatchGroupId>>::iterator removePatchPack_(std::vector<std::pair<PatchData::PatchPack, Patcher::PatchGroupId>>::iterator patchPack);
        void removeAllPatchPacks_();

        void enablePatchPack_(std::pair<PatchData::PatchPack, Patcher::PatchGroupId>& patchPack);
        void disablePatchPack_(std::pair<PatchData::PatchPack, Patcher::PatchGroupId>& patchPack);

        static std::string getHookSafename(const std::string& name);
        static std::string getPatchPackSafename(const std::string& name);

        Module patcherLibrary_;
        using hookPatchFunctions_t = std::map<hookPatchFunction_t, ExtraSettings>;
        using hookPatchFunctionsMutex_t = std::recursive_mutex;
        std::map<std::vector<std::pair<PatchData::Hook, Patcher::PatchGroupId>>::iterator, hookPatchFunctions_t> savedPatcherLibraryState_;

        std::vector<std::pair<PatchData::Hook, Patcher::PatchGroupId>> hooks_;
        std::vector<std::pair<PatchData::PatchPack, Patcher::PatchGroupId>> patchPacks_;
};

/* TODO list:
    NameHook
    SearchHook
    HookPatch
*/

#endif
