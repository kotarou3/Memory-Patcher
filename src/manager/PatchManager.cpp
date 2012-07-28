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

#include <iterator>
#include <stdexcept>

#include "PatchManager.h"
#include "PatchCompiler.h"
#include "PluginManager.h"
#include "SettingsManager.h"
#include "Socket.h"

using namespace PatchData;

PatchManager& PatchManager::getSingleton()
{
    static PatchManager singleton;
    return singleton;
}

void PatchManager::registerHook(const Hook& hook)
{
    if (hook.name.empty())
        throw std::logic_error("The hook name cannot be empty.");

    if (isHookRegistered(hook.name))
        throw std::logic_error("A hook with the same name is already registered.");

    hook.checkValid();
    switch (hook.getType())
    {
        case Hook::Type::NAME :
            for (const auto& hook_ : hooks_)
                if (hook_.hook.getType() == Hook::Type::NAME)
                    hook.getTypeData<NameHook>().checkOverlapWith(hook_.hook.getTypeData<NameHook>());
            break;

        default:
            break;
    }

    Hook_ hook_;
    hook_.hook = hook;
    hooks_.push_back(hook_);

    // Tell the cores
    updateCoresAboutHook_(hook_);
}

void PatchManager::unregisterHook(const std::string& name)
{
    unregisterHook_(getIteratorToHook_(name));
}

void PatchManager::unregisterAllHooks()
{
    auto hook = hooks_.begin();
    while (hook != hooks_.end())
        hook = unregisterHook_(hook);
}

bool PatchManager::isHookRegistered(const std::string& name) const noexcept
{
    if (getIteratorToHookNoThrow_(name) != hooks_.end())
        return true;
    return false;
}

void PatchManager::addPatchPack(const PatchPack& patchPack)
{
    if (patchPack.info.name.empty())
        throw std::logic_error("The patch pack name cannot be empty.");
    for (const auto& patchPack_ : patchPacks_)
        if (patchPack.info.name == patchPack_.info.name)
            throw std::logic_error("A patch with the same name already exists.");

    for (const auto& requiredPlugin : patchPack.requiredPlugins)
        if (!PluginManager::getSingleton().isLoaded(requiredPlugin))
            throw std::logic_error("Required plugin `" + requiredPlugin + "\' is not loaded.");

    std::vector<Hook_*> hooksUsed;
    for (const auto& patch : patchPack.patches)
    {
        patch.checkValid();
        switch (patch.getType())
        {
            case Patch::Type::HOOK :
                hooksUsed.push_back(&(*getIteratorToHook_(patch.getTypeData<HookPatch>().hookName)));
                break;

            case Patch::Type::REPLACE_NAME :
                for (const auto& patchPack_ : patchPacks_)
                    for (const auto& patch_ : patchPack_.patches)
                        if (patch_.getType() == Patch::Type::REPLACE_NAME)
                            patch.getTypeData<ReplaceNamePatch>().checkOverlapWith(patch_.getTypeData<ReplaceNamePatch>());
                break;

            default:
                break;
        }
    }

    patchPacks_.push_back(patchPack);
    auto copiedPatchPack = patchPacks_.end();
    copiedPatchPack--;

    // TODO: Read from settings instead of using default values
    copiedPatchPack->info.isCurrentlyEnabled = false;
    restorePatchPackExtraSettingDefaults_(*copiedPatchPack);
    if (copiedPatchPack->info.isDefaultEnabled)
        enablePatchPack_(*copiedPatchPack);

    for (auto& hookUsed : hooksUsed)
        hookUsed->dependantPatchPacks.push_back(copiedPatchPack);
}

void PatchManager::removePatchPack(const std::string& name)
{
    removePatchPack_(getIteratorToPatchPack_(name));
}

void PatchManager::removeAllPatchPacks()
{
    auto patchPack = patchPacks_.begin();
    while (patchPack != patchPacks_.end())
        patchPack = removePatchPack_(patchPack);
}

bool PatchManager::isPatchPackLoaded(const std::string& name) const noexcept
{
    if (getIteratorToPatchPackNoThrow_(name) != patchPacks_.end())
        return true;
    return false;
}

void PatchManager::enablePatchPack(const std::string& name)
{
    enablePatchPack_(*getIteratorToPatchPack_(name));
}

void PatchManager::enableAllPatchPacks()
{
    for (auto& patchPack : patchPacks_)
        enablePatchPack_(patchPack);
}

void PatchManager::disablePatchPack(const std::string& name)
{
    disablePatchPack_(*getIteratorToPatchPack_(name));
}

void PatchManager::disableAllPatchPacks()
{
    for (auto& patchPack : patchPacks_)
        disablePatchPack_(patchPack);
}

bool PatchManager::isPatchPackEnabled(const std::string& name) const
{
    return getIteratorToPatchPack_(name)->info.isCurrentlyEnabled;
}

const std::vector<Hook> PatchManager::getHooks() const
{
    std::vector<Hook> result;
    result.reserve(hooks_.size());
    for (const auto& hook : hooks_)
        result.push_back(hook.hook);
    return result;
}

const Hook& PatchManager::getHook(const std::string& name) const
{
    return getIteratorToHook_(name)->hook;
}

const std::vector<PatchPack>& PatchManager::getPatchPacks() const
{
    return patchPacks_;
}

const PatchPack& PatchManager::getPatchPack(const std::string& name) const
{
    return *getIteratorToPatchPack_(name);
}

void PatchManager::setPatchPackExtraSettingValue(const std::string& name, const std::string& extraSettingLabel, const std::string& value)
{
    setPatchPackExtraSettingValue_(*getIteratorToPatchPack_(name), extraSettingLabel, value);
}

void PatchManager::restorePatchPackExtraSettingDefaults(const std::string& name)
{
    restorePatchPackExtraSettingDefaults_(*getIteratorToPatchPack_(name));
}

void PatchManager::restoreAllPatchPackExtraSettingDefaults()
{
    for (auto& patchPack : patchPacks_)
        restorePatchPackExtraSettingDefaults_(patchPack);
}

std::string PatchManager::compileHooksAndPatchPacks() const
{
    std::string output;
    output.reserve(1024);
    try
    {
        bool isAllSkipped = true;
        for (const auto& hook : hooks_)
        {
            output += "Compiling hook " + hook.hook.name + "...\n";
            bool isSkipped;
            output += PatchCompiler::compileHook(hook.hook, isSkipped);
            if (isSkipped)
                output += "Skipped.\n";
            else
                isAllSkipped = false;
        }
        for (auto& patchPack : patchPacks_)
        {
            output += "Compiling patch pack " + patchPack.info.name + "...\n";
            bool isSkipped;
            output += PatchCompiler::compilePatchPack(patchPack, isSkipped);
            if (isSkipped)
                output += "Skipped.\n";
            else
                isAllSkipped = false;
        }
        output += "Linking...\n";
        if (isAllSkipped)
            output += "Skipped.\n";
        else
        {
            CoreManager::getSingleton().sendPacket(Socket::ServerOpCode::PATCH_LIB_UNLOAD, {});
            output += PatchCompiler::linkObjects();
            std::string patchesFilename = SettingsManager::getSingleton().get("core.patchesLibrary");
            std::vector<uint8_t> data;
            serialiseIntegralTypeContinuousContainer(data, patchesFilename);
            CoreManager::getSingleton().sendPacket(Socket::ServerOpCode::PATCH_LIB_LOAD, data);
        }
    }
    catch (const std::exception& e)
    {
        output += e.what();
        throw std::runtime_error("Failed to compile hooks and patch packs. Output:\n" + output);
    }
    return output;
}

void PatchManager::updateCoreAboutHook(const CoreManager::CoreId coreId, const std::string& name) const
{
    updateCoreAboutHook_(coreId, *getIteratorToHook_(name));
}

void PatchManager::updateCoresAboutHook(const std::string& name) const
{
    updateCoresAboutHook_(*getIteratorToHook_(name));
}

void PatchManager::updateCoreAboutAllHooks(const CoreManager::CoreId coreId) const
{
    for (const auto& hook : hooks_)
        updateCoreAboutHook_(coreId, hook);
}

void PatchManager::updateCoresAboutAllHooks() const
{
    for (const auto& hook : hooks_)
        updateCoresAboutHook_(hook);
}

void PatchManager::updateCoreAboutPatchPack(const CoreManager::CoreId coreId, const std::string& name) const
{
    updateCoreAboutPatchPack_(coreId, *getIteratorToPatchPack_(name));
}

void PatchManager::updateCoresAboutPatchPack(const std::string& name) const
{
    updateCoresAboutPatchPack_(*getIteratorToPatchPack_(name));
}

void PatchManager::updateCoreAboutAllPatchPacks(const CoreManager::CoreId coreId) const
{
    for (const auto& patchPack : patchPacks_)
        updateCoreAboutPatchPack_(coreId, patchPack);
}

void PatchManager::updateCoresAboutAllPatchPacks() const
{
    for (const auto& patchPack : patchPacks_)
        updateCoresAboutPatchPack_(patchPack);
}


// Private Members

std::vector<PatchManager::Hook_>::const_iterator PatchManager::getIteratorToHookNoThrow_(const std::string& name) const noexcept
{
    for (auto hook = hooks_.begin(); hook != hooks_.end(); ++hook)
        if (hook->hook.name == name)
            return hook;
    return hooks_.end();
}

std::vector<PatchManager::Hook_>::const_iterator PatchManager::getIteratorToHook_(const std::string& name) const
{
    auto hook = getIteratorToHookNoThrow_(name);
    if (hook == hooks_.end())
        throw std::logic_error("No hook with that name is registered.");
    return hook;
}

std::vector<PatchManager::Hook_>::iterator PatchManager::getIteratorToHook_(const std::string& name)
{
    const PatchManager* self = this;
    auto it = self->getIteratorToHook_(name);
    auto result = hooks_.begin();
    std::advance(result, std::distance<std::vector<PatchManager::Hook_>::const_iterator>(result, it));
    return result;
}

std::vector<PatchPack>::const_iterator PatchManager::getIteratorToPatchPackNoThrow_(const std::string& name) const noexcept
{
    for (auto patchPack = patchPacks_.begin(); patchPack != patchPacks_.end(); ++patchPack)
        if (patchPack->info.name == name)
            return patchPack;
    return patchPacks_.end();
}

std::vector<PatchPack>::const_iterator PatchManager::getIteratorToPatchPack_(const std::string& name) const
{
    auto patchPack = getIteratorToPatchPackNoThrow_(name);
    if (patchPack == patchPacks_.end())
        throw std::logic_error("No patch pack with that name loaded.");
    return patchPack;
}

std::vector<PatchPack>::iterator PatchManager::getIteratorToPatchPack_(const std::string& name)
{
    const PatchManager* self = this;
    auto it = self->getIteratorToPatchPack_(name);
    auto result = patchPacks_.begin();
    std::advance(result, std::distance<std::vector<PatchPack>::const_iterator>(result, it));
    return result;
}

std::vector<PatchManager::Hook_>::iterator PatchManager::unregisterHook_(std::vector<PatchManager::Hook_>::iterator hook)
{
    // Remove all patches that use the hook
    auto dependantPatchPackIterator = hook->dependantPatchPacks.begin();
    while (dependantPatchPackIterator != hook->dependantPatchPacks.end())
    {
        auto dependantPatchPack = *dependantPatchPackIterator;
        dependantPatchPackIterator = hook->dependantPatchPacks.erase(dependantPatchPackIterator);
        removePatchPack_(dependantPatchPack);
    }

    // Tell the cores
    std::vector<uint8_t> data;
    data.reserve(hook->hook.name.size());
    serialiseIntegralTypeContinuousContainer(data, hook->hook.name);
    CoreManager::getSingleton().sendPacket(Socket::ServerOpCode::PATCH_HOOK_REMOVE, data);

    // Remove the hook
    return hooks_.erase(hook);
}

std::vector<PatchPack>::iterator PatchManager::removePatchPack_(std::vector<PatchPack>::iterator patchPack)
{
    // TODO: Save to settings
    for (const auto& patch : patchPack->patches)
        if (patch.getType() == Patch::Type::HOOK)
        {
            auto hook = getIteratorToHook_(patch.getTypeData<HookPatch>().hookName);
            for (auto dependantPatchPack = hook->dependantPatchPacks.begin(); dependantPatchPack != hook->dependantPatchPacks.end(); ++dependantPatchPack)
                if (*dependantPatchPack == patchPack)
                {
                    hook->dependantPatchPacks.erase(dependantPatchPack);
                    break;
                }
        }
    disablePatchPack_(*patchPack);

    // Tell the cores
    std::vector<uint8_t> data;
    data.reserve(patchPack->info.name.size());
    serialiseIntegralTypeContinuousContainer(data, patchPack->info.name);
    CoreManager::getSingleton().sendPacket(Socket::ServerOpCode::PATCH_PACK_REMOVE, data);

    return patchPacks_.erase(patchPack);
}

void PatchManager::enablePatchPack_(PatchPack& patchPack)
{
    patchPack.info.isCurrentlyEnabled = true;
    updateCoresAboutPatchPack_(patchPack);
}

void PatchManager::disablePatchPack_(PatchPack& patchPack)
{
    patchPack.info.isCurrentlyEnabled = false;
    updateCoresAboutPatchPack_(patchPack);
}

void PatchManager::setPatchPackExtraSettingValue_(PatchPack& patchPack, const std::string& extraSettingLabel, const std::string& value)
{
    getExtraSettingByLabel(patchPack.info.extraSettings, extraSettingLabel).currentValue = value;
}

void PatchManager::restorePatchPackExtraSettingDefaults_(PatchPack& patchPack)
{
    for (auto& extraSetting : patchPack.info.extraSettings)
        extraSetting.currentValue = extraSetting.defaultValue;
}

void PatchManager::updateCoreAboutHook_(const CoreManager::CoreId coreId, const PatchManager::Hook_& hook) const
{
    compileHooksAndPatchPacks();

    std::vector<uint8_t> data;
    data.reserve(1024);
    serialiseIntegralTypeContinuousContainer(data, hook.hook.serialise());

    CoreManager::getSingleton().sendPacketTo(coreId, Socket::ServerOpCode::PATCH_HOOK, data);
}

void PatchManager::updateCoresAboutHook_(const PatchManager::Hook_& hook) const
{
    compileHooksAndPatchPacks();

    std::vector<uint8_t> data;
    data.reserve(1024);
    serialiseIntegralTypeContinuousContainer(data, hook.hook.serialise());

    CoreManager::getSingleton().sendPacket(Socket::ServerOpCode::PATCH_HOOK, data);
}

void PatchManager::updateCoreAboutPatchPack_(const CoreManager::CoreId coreId, const PatchData::PatchPack& patchPack) const
{
    compileHooksAndPatchPacks();

    std::vector<uint8_t> data;
    data.reserve(1024);
    serialiseIntegralTypeContinuousContainer(data, patchPack.serialise());

    CoreManager::getSingleton().sendPacketTo(coreId, Socket::ServerOpCode::PATCH_PACK, data);
}

void PatchManager::updateCoresAboutPatchPack_(const PatchData::PatchPack& patchPack) const
{
    compileHooksAndPatchPacks();

    std::vector<uint8_t> data;
    data.reserve(1024);
    serialiseIntegralTypeContinuousContainer(data, patchPack.serialise());

    CoreManager::getSingleton().sendPacket(Socket::ServerOpCode::PATCH_PACK, data);
}
