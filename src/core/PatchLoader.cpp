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

#include <cassert>

#include "PatchLoader.h"
#include "Socket.h"
#include "Core.h"
#include "Memory.h"

using namespace PatchData;

PatchLoader& PatchLoader::getSingleton()
{
    static PatchLoader singleton;
    return singleton;
}

PatchLoader::PatchLoader()
{
    Core::getSingleton().addReceiveHandler(Socket::ServerOpCode::PATCH_HOOK, patchHookReceiveHandler_);
    Core::getSingleton().addReceiveHandler(Socket::ServerOpCode::PATCH_HOOK_REMOVE, patchHookRemoveReceiveHandler_);
    Core::getSingleton().addReceiveHandler(Socket::ServerOpCode::PATCH_PACK, patchPackReceiveHandler_);
    Core::getSingleton().addReceiveHandler(Socket::ServerOpCode::PATCH_PACK_REMOVE, patchPackRemoveReceiveHandler_);
    Core::getSingleton().addReceiveHandler(Socket::ServerOpCode::PATCH_LIB_LOAD, patchLibraryLoadReceiveHandler_);
    Core::getSingleton().addReceiveHandler(Socket::ServerOpCode::PATCH_LIB_UNLOAD, patchLibraryUnloadReceiveHandler_);
}

PatchLoader::~PatchLoader()
{
    //removeAllPatchPacks_();
    //unregisterAllHooks_();
    //patchLibraryUnloadReceiveHandler_({});
    patcherLibrary_.detach();
}

bool PatchLoader::isHookRegistered(const std::string& name) const noexcept
{
    if (getIteratorToHookNoThrow_(name) != hooks_.end())
        return true;
    return false;
}

bool PatchLoader::isPatchPackLoaded(const std::string& name) const noexcept
{
    if (getIteratorToPatchPackNoThrow_(name) != patchPacks_.end())
        return true;
    return false;
}

bool PatchLoader::isPatchPackEnabled(const std::string& name) const
{
    return getIteratorToPatchPack_(name)->first.info.isCurrentlyEnabled;
}

const std::vector<Hook> PatchLoader::getHooks() const
{
    std::vector<Hook> result;
    result.reserve(patchPacks_.size());
    for (const auto& hook : hooks_)
        result.push_back(hook.first);
    return result;
}

const Hook& PatchLoader::getHook(const std::string& name) const
{
    return getIteratorToHook_(name)->first;
}

const std::vector<PatchPack> PatchLoader::getPatchPacks() const
{
    std::vector<PatchPack> result;
    result.reserve(patchPacks_.size());
    for (const auto& patchPack : patchPacks_)
        result.push_back(patchPack.first);
    return result;
}

const PatchPack& PatchLoader::getPatchPack(const std::string& name) const
{
    return getIteratorToPatchPack_(name)->first;
}

// Private members

void PatchLoader::patchHookReceiveHandler_(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    Hook hook;
    hook.deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));

    getSingleton().registerHook_(hook);
}

void PatchLoader::patchHookRemoveReceiveHandler_(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();
    std::string name = deserialiseIntegralTypeContinuousContainer<std::string>(iterator);

    PatchLoader& patchLoader = getSingleton();
    if (!patchLoader.isHookRegistered(name))
        return;
    patchLoader.unregisterHook_(patchLoader.getIteratorToHook_(name));
}

void PatchLoader::patchPackReceiveHandler_(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();

    PatchPack patchPack;
    patchPack.deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));

    PatchLoader& patchLoader = getSingleton();
    if (!patchLoader.isPatchPackLoaded(patchPack.info.name))
        patchLoader.addPatchPack_(patchPack);
    else
    {
        auto existingPatchPack = patchLoader.getIteratorToPatchPack_(patchPack.info.name);

        if (patchPack.info.isCurrentlyEnabled && !existingPatchPack->first.info.isCurrentlyEnabled)
            patchLoader.enablePatchPack_(*existingPatchPack);
        else if (!patchPack.info.isCurrentlyEnabled && existingPatchPack->first.info.isCurrentlyEnabled)
            patchLoader.disablePatchPack_(*existingPatchPack);
    }
}

void PatchLoader::patchPackRemoveReceiveHandler_(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();
    std::string name = deserialiseIntegralTypeContinuousContainer<std::string>(iterator);

    PatchLoader& patchLoader = getSingleton();
    if (!patchLoader.isPatchPackLoaded(name))
        return;
    patchLoader.removePatchPack_(patchLoader.getIteratorToPatchPack_(name));
}

void PatchLoader::patchLibraryLoadReceiveHandler_(const std::vector<uint8_t>& data)
{
    TRACE("Loading patcher library.");
    PatchLoader& patchLoader = getSingleton();
    if (patchLoader.patcherLibrary_.getIsModuleOpen())
        patchLibraryUnloadReceiveHandler_({});

    auto iterator = data.cbegin();
    std::string libraryFilename = deserialiseIntegralTypeContinuousContainer<std::string>(iterator);

    // Load the library
    patchLoader.patcherLibrary_.load(libraryFilename);

    // Add any saved patches if possible
    for (const auto& savedState : patchLoader.savedPatcherLibraryState_)
    {
        try
        {
            hookPatchFunctions_t& hookPatchFunctions = *(hookPatchFunctions_t*)patchLoader.patcherLibrary_.getSymbol(getHookSafename(savedState.first->first.name) + "_hookPatchFunctions");
            hookPatchFunctionsMutex_t& hookPatchFunctionsMutex = *(hookPatchFunctionsMutex_t*)patchLoader.patcherLibrary_.getSymbol(getHookSafename(savedState.first->first.name) + "_hookPatchFunctionsMutex");
            std::lock_guard<hookPatchFunctionsMutex_t> hookPatchFunctionsLock(hookPatchFunctionsMutex);
            hookPatchFunctions = savedState.second;
        } catch (...)
        {
            // Swallow the exception
        }
        patchLoader.applyHook_(*savedState.first);
    }
}

void PatchLoader::patchLibraryUnloadReceiveHandler_(const std::vector<uint8_t>& /*data*/)
{
    TRACE("Unloading patcher library.");
    PatchLoader& patchLoader = getSingleton();
    if (!patchLoader.patcherLibrary_.getIsModuleOpen())
        return;

    // Save all the patches the hooks in the library calls
    patchLoader.savedPatcherLibraryState_.clear();
    for (auto hook = patchLoader.hooks_.begin(); hook != patchLoader.hooks_.end(); ++hook)
    {
        patchLoader.unapplyHook_(*hook);
        hookPatchFunctions_t& hookPatchFunctions = *(hookPatchFunctions_t*)patchLoader.patcherLibrary_.getSymbol(getHookSafename(hook->first.name) + "_hookPatchFunctions");
        hookPatchFunctionsMutex_t& hookPatchFunctionsMutex = *(hookPatchFunctionsMutex_t*)patchLoader.patcherLibrary_.getSymbol(getHookSafename(hook->first.name) + "_hookPatchFunctionsMutex");
        std::lock_guard<hookPatchFunctionsMutex_t> hookPatchFunctionsLock(hookPatchFunctionsMutex);
        patchLoader.savedPatcherLibraryState_[hook] = hookPatchFunctions;
    }

    // Unload the library
    patchLoader.patcherLibrary_.unload();
}

std::vector<std::pair<Hook, Patcher::PatchGroupId>>::const_iterator PatchLoader::getIteratorToHookNoThrow_(const std::string& name) const noexcept
{
    for (auto hook = hooks_.begin(); hook != hooks_.end(); ++hook)
        if (hook->first.name == name)
            return hook;
    return hooks_.end();
}

std::vector<std::pair<Hook, Patcher::PatchGroupId>>::const_iterator PatchLoader::getIteratorToHook_(const std::string& name) const
{
    auto hook = getIteratorToHookNoThrow_(name);
    if (hook == hooks_.end())
        throw std::logic_error("No hook with that name is registered.");
    return hook;
}

std::vector<std::pair<Hook, Patcher::PatchGroupId>>::iterator PatchLoader::getIteratorToHook_(const std::string& name)
{
    const PatchLoader* self = this;
    auto it = self->getIteratorToHook_(name);
    auto result = hooks_.begin();
    std::advance(result, std::distance<std::vector<std::pair<Hook, Patcher::PatchGroupId>>::const_iterator>(result, it));
    return result;
}

std::vector<std::pair<PatchPack, Patcher::PatchGroupId>>::const_iterator PatchLoader::getIteratorToPatchPackNoThrow_(const std::string& name) const noexcept
{
    for (auto patchPack = patchPacks_.begin(); patchPack != patchPacks_.end(); ++patchPack)
        if (patchPack->first.info.name == name)
            return patchPack;
    return patchPacks_.end();
}

std::vector<std::pair<PatchPack, Patcher::PatchGroupId>>::const_iterator PatchLoader::getIteratorToPatchPack_(const std::string& name) const
{
    auto patchPack = getIteratorToPatchPackNoThrow_(name);
    if (patchPack == patchPacks_.end())
        throw std::logic_error("No patch pack with that name loaded.");
    return patchPack;
}

std::vector<std::pair<PatchPack, Patcher::PatchGroupId>>::iterator PatchLoader::getIteratorToPatchPack_(const std::string& name)
{
    const PatchLoader* self = this;
    auto it = self->getIteratorToPatchPack_(name);
    auto result = patchPacks_.begin();
    std::advance(result, std::distance<std::vector<std::pair<PatchPack, Patcher::PatchGroupId>>::const_iterator>(result, it));
    return result;
}

void PatchLoader::registerHook_(const Hook& hook)
{
    hooks_.push_back(std::make_pair(hook, -1));
    applyHook_(hooks_.back());
}

std::vector<std::pair<Hook, Patcher::PatchGroupId>>::iterator PatchLoader::unregisterHook_(std::vector<std::pair<Hook, Patcher::PatchGroupId>>::iterator hook)
{
    unapplyHook_(*hook);
    return hooks_.erase(hook);
}

void PatchLoader::unregisterAllHooks_()
{
    auto hook = hooks_.begin();
    while (hook != hooks_.end())
        hook = unregisterHook_(hook);
}

void PatchLoader::applyHook_(std::pair<Hook, Patcher::PatchGroupId>& hook)
{
    Patch patch;
    std::vector<uint8_t>* replaceBytes;
    std::set<size_t>* ignoredReplaceBytesRvas;

    // Initialise the patch with the search info
    if (hook.first.getType() == Hook::Type::NAME)
    {
        auto& replaceNamePatch = patch.setType<ReplaceNamePatch>();
        (NameSearch&)replaceNamePatch = hook.first.getTypeData<NameHook>();
        replaceNamePatch.replaceBytes.resize(replaceNamePatch.searchBytes.size(), (uint8_t)-1);
        replaceBytes = &replaceNamePatch.replaceBytes;
        ignoredReplaceBytesRvas = &replaceNamePatch.ignoredReplaceBytesRvas;
    }
    else
    {
        assert(hook.first.getType() == Hook::Type::SEARCH);
        auto& replaceSearchPatch = patch.setType<ReplaceSearchPatch>();
        (Search&)replaceSearchPatch = hook.first.getTypeData<SearchHook>();
        replaceSearchPatch.replaceBytes.resize(replaceSearchPatch.searchBytes.size(), (uint8_t)-1);
        replaceBytes = &replaceSearchPatch.replaceBytes;
        ignoredReplaceBytesRvas = &replaceSearchPatch.ignoredReplaceBytesRvas;
    }

    // Get the address of the hook function wrapper
    uint8_t* hookFunctionWrapper = patcherLibrary_.getSymbol(getHookSafename(hook.first.name) + "_wrapper");

    // Finish the patch by adding in the replace bytes
    (*replaceBytes)[hook.first.hookRva] = (uint8_t)0xe8; // Relative call
    for (size_t b = 0; b < replaceBytes->size(); ++b)
        if (b != hook.first.hookRva)
            ignoredReplaceBytesRvas->insert(b);

    // Add the patch to the patcher queue with a relative address replace for the hook function wrapper
    hook.second = Patcher::getSingleton().addToQueue({{patch, {{hook.first.hookRva + 1, hookFunctionWrapper}}}});
}

void PatchLoader::unapplyHook_(std::pair<Hook, Patcher::PatchGroupId>& hook)
{
    Patcher::getSingleton().undoPatchGroup(hook.second);
    hook.second = (Patcher::PatchGroupId)-1;
}

void PatchLoader::addPatchPack_(const PatchPack& patchPack)
{
    patchPacks_.push_back(std::make_pair(patchPack, -1));
    if (patchPacks_.back().first.info.isCurrentlyEnabled)
    {
        patchPacks_.back().first.info.isCurrentlyEnabled = false;
        enablePatchPack_(patchPacks_.back());
    }
}

std::vector<std::pair<PatchPack, Patcher::PatchGroupId>>::iterator PatchLoader::removePatchPack_(std::vector<std::pair<PatchPack, Patcher::PatchGroupId>>::iterator patchPack)
{
    disablePatchPack_(*patchPack);
    return patchPacks_.erase(patchPack);
}

void PatchLoader::removeAllPatchPacks_()
{
    auto patchPack = patchPacks_.begin();
    while (patchPack != patchPacks_.end())
        patchPack = removePatchPack_(patchPack);
}

void PatchLoader::enablePatchPack_(std::pair<PatchPack, Patcher::PatchGroupId>& patchPack)
{
    if (patchPack.first.info.isCurrentlyEnabled)
        return;

    size_t hookPatchNum = 0;
    std::vector<std::pair<Patch, std::map<size_t, uint8_t*>>> patchGroup;
    for (const auto& patch : patchPack.first.patches)
        switch (patch.getType())
        {
            case Patch::Type::HOOK :
                try
                {
                    hookPatchFunction_t hookPatchFunction = (hookPatchFunction_t)patcherLibrary_.getSymbol(getPatchPackSafename(patchPack.first.info.name) + "_hookPatch" + itos(hookPatchNum));
                    hookPatchFunctions_t& hookPatchFunctions = *(hookPatchFunctions_t*)patcherLibrary_.getSymbol(getHookSafename(patch.getTypeData<HookPatch>().hookName) + "_hookPatchFunctions");
                    hookPatchFunctionsMutex_t& hookPatchFunctionsMutex = *(hookPatchFunctionsMutex_t*)patcherLibrary_.getSymbol(getHookSafename(patch.getTypeData<HookPatch>().hookName) + "_hookPatchFunctionsMutex");
                    std::lock_guard<hookPatchFunctionsMutex_t> hookPatchFunctionsLock(hookPatchFunctionsMutex);
                    hookPatchFunctions[hookPatchFunction] = patchPack.first.info.extraSettings;
                } catch (...)
                {
                    assert(false); // No exceptions should be thrown if the manager did it's job right
                }
                ++hookPatchNum;
                break;

            case Patch::Type::REPLACE_NAME :
                patchGroup.push_back(std::make_pair<const Patch&, std::map<size_t, uint8_t*>>(patch, {}));
                break;

            case Patch::Type::REPLACE_SEARCH :
                patchGroup.push_back(std::make_pair<const Patch&, std::map<size_t, uint8_t*>>(patch, {}));
                break;

            case Patch::Type::BLANK :
                assert(false); // Should have been rejected in the manager stage
        }
    if (!patchGroup.empty())
        patchPack.second = Patcher::getSingleton().addToQueue(patchGroup);
    else
        patchPack.second = (Patcher::PatchGroupId)-1;
    patchPack.first.info.isCurrentlyEnabled = true;
}

void PatchLoader::disablePatchPack_(std::pair<PatchPack, Patcher::PatchGroupId>& patchPack)
{
    if (!patchPack.first.info.isCurrentlyEnabled)
        return;

    if (patchPack.second != (Patcher::PatchGroupId)-1)
        Patcher::getSingleton().undoPatchGroup(patchPack.second);
    size_t hookPatchNum = 0;
    for (const auto& patch : patchPack.first.patches)
        if (patch.getType() == Patch::Type::HOOK)
        {
            try
            {
                hookPatchFunction_t hookPatchFunction = (hookPatchFunction_t)patcherLibrary_.getSymbol(getPatchPackSafename(patchPack.first.info.name) + "_hookPatch" + itos(hookPatchNum));
                hookPatchFunctions_t& hookPatchFunctions = *(hookPatchFunctions_t*)patcherLibrary_.getSymbol(getHookSafename(patch.getTypeData<HookPatch>().hookName) + "_hookPatchFunctions");
                hookPatchFunctionsMutex_t& hookPatchFunctionsMutex = *(hookPatchFunctionsMutex_t*)patcherLibrary_.getSymbol(getHookSafename(patch.getTypeData<HookPatch>().hookName) + "_hookPatchFunctionsMutex");
                std::lock_guard<hookPatchFunctionsMutex_t> hookPatchFunctionsLock(hookPatchFunctionsMutex);
                auto hookPatchFunction_inMap = hookPatchFunctions.find(hookPatchFunction);
                if (hookPatchFunction_inMap != hookPatchFunctions.end())
                    hookPatchFunctions.erase(hookPatchFunction_inMap);
            } catch (...)
            {
                // Swallow the exception
            }
            ++hookPatchNum;
        }
    patchPack.first.info.isCurrentlyEnabled = false;
    patchPack.second = (Patcher::PatchGroupId)-1;
}

std::string PatchLoader::getHookSafename(const std::string& name)
{
    return "hook_" + btos(name);
}

std::string PatchLoader::getPatchPackSafename(const std::string& name)
{
    return "patchpack_" + btos(name);
}
