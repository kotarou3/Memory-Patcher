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
#ifndef PATCHMANAGER_H
#define PATCHMANAGER_H

#include <string>
#include <vector>
#include <memory>

#include "Hook.h"
#include "Patch.h"
#include "Misc.h"
#include "CoreManager.h"

class MANAGER_EXPORT PatchManager final
{
    public:
        void registerHook(const PatchData::Hook& hook);
        void unregisterHook(const std::string& name, bool isNoNotifyCores = false);
        void unregisterAllHooks(bool isNoNotifyCores = false);
        bool isHookRegistered(const std::string& name) const noexcept;

        void addPatchPack(const PatchData::PatchPack& patchPack);
        void removePatchPack(const std::string& name, bool isNoNotifyCores = false);
        void removeAllPatchPacks(bool isNoNotifyCores = false);
        bool isPatchPackLoaded(const std::string& name) const noexcept;

        void enablePatchPack(const std::string& name);
        void enableAllPatchPacks();
        void disablePatchPack(const std::string& name, bool isNoNotifyCores = false);
        void disableAllPatchPacks(bool isNoNotifyCores = false);
        bool isPatchPackEnabled(const std::string& name) const;

        const std::vector<PatchData::Hook> getHooks() const;
        const PatchData::Hook& getHook(const std::string& name) const;
        const std::vector<PatchData::PatchPack>& getPatchPacks() const;
        const PatchData::PatchPack& getPatchPack(const std::string& name) const;

        void setPatchPackExtraSettingValue(const std::string& name, const std::string& extraSettingLabel, const std::string& value);
        void restorePatchPackExtraSettingDefaults(const std::string& name);
        void restoreAllPatchPackExtraSettingDefaults();

        std::string compileHooksAndPatchPacks() const;
        void updateCoreAboutHook(const CoreManager::CoreId coreId, const std::string& name) const;
        void updateCoresAboutHook(const std::string& name) const;
        void updateCoreAboutAllHooks(const CoreManager::CoreId coreId) const;
        void updateCoresAboutAllHooks() const;
        void updateCoreAboutPatchPack(const CoreManager::CoreId coreId, const std::string& name) const;
        void updateCoresAboutPatchPack(const std::string& name) const;
        void updateCoreAboutAllPatchPacks(const CoreManager::CoreId coreId) const;
        void updateCoresAboutAllPatchPacks() const;

        static PatchManager& getSingleton();
        static bool getIsSingletonInitialised();

    private:
        PatchManager() = default;
        PatchManager(const PatchManager&) = delete;
        PatchManager& operator=(const PatchManager&) = delete;
        ~PatchManager();

        class Hook_ final
        {
            public:
                PatchData::Hook hook;
                std::vector<std::vector<PatchData::PatchPack>::iterator> dependantPatchPacks; // Iterators to the dependant patch packs
        };

        std::vector<Hook_>::const_iterator getIteratorToHookNoThrow_(const std::string& name) const noexcept;
        std::vector<Hook_>::const_iterator getIteratorToHook_(const std::string& name) const;
        std::vector<Hook_>::iterator getIteratorToHook_(const std::string& name);
        std::vector<PatchData::PatchPack>::const_iterator getIteratorToPatchPackNoThrow_(const std::string& name) const noexcept;
        std::vector<PatchData::PatchPack>::const_iterator getIteratorToPatchPack_(const std::string& name) const;
        std::vector<PatchData::PatchPack>::iterator getIteratorToPatchPack_(const std::string& name);

        std::vector<PatchData::PatchPack>::iterator removePatchPack_(std::vector<PatchData::PatchPack>::iterator patchPack, bool isNoNotifyCores = false);

        std::vector<Hook_>::iterator unregisterHook_(std::vector<Hook_>::iterator hook, bool isNoNotifyCores = false);

        void enablePatchPack_(PatchData::PatchPack& patchPack);
        void disablePatchPack_(PatchData::PatchPack& patchPack, bool isNoNotifyCores = false);

        void setPatchPackExtraSettingValue_(PatchData::PatchPack& patchPack, const std::string& extraSettingLabel, const std::string& value);
        void restorePatchPackExtraSettingDefaults_(PatchData::PatchPack& patchPack);

        void updateCoreAboutHook_(const CoreManager::CoreId coreId, const Hook_& hook) const;
        void updateCoresAboutHook_(const Hook_& hook) const;
        void updateCoreAboutPatchPack_(const CoreManager::CoreId coreId, const PatchData::PatchPack& patchPack) const;
        void updateCoresAboutPatchPack_(const PatchData::PatchPack& patchPack) const;

        void checkSingletonAndIsInitialised_() const;

        std::vector<Hook_> hooks_;
        std::vector<PatchData::PatchPack> patchPacks_;

        static PatchManager singleton;
        static bool isSingletonInitialised;
};

#endif
