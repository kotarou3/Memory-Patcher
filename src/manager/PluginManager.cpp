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

#include <algorithm>
#include <utility>
#include <stdexcept>

#include "PluginManager.h"
#include "ManagerPlugin.h"

PluginManager& PluginManager::getSingleton()
{
    static PluginManager singleton;
    return singleton;
}

PluginManager::~PluginManager()
{
    auto plugin = plugins_.begin();
    while (plugin != plugins_.end())
        plugin = remove_(plugin, true);
}

void PluginManager::add(const std::string& pathfile)
{
    Module module;
    try
    {
        module.load(pathfile);
    }
    catch (std::runtime_error& e)
    {
        throw std::runtime_error("Could not load plugin `" + pathfile + "': " + e.what());
    }

    createInstance_t createInstance;
    try
    {
        createInstance = (createInstance_t)module.getSymbol("createInstance");
    }
    catch (std::runtime_error& e)
    {
        throw std::runtime_error("Could not find symbol `createInstance' in `" + pathfile + "': " + e.what());
    }

    Plugin_ plugin;
    plugin.module = std::move(module);
    plugin.plugin = createInstance();
    plugin.info = plugin.plugin->getInfo();

    if (plugin.info.name.empty())
        throw std::logic_error("The plugin name cannot be empty.");
    else if (isLoaded(plugin.info.name))
        throw std::logic_error("The plugin is already loaded.");

    for (const auto& interfaceHeader : plugin.plugin->getInterfaceHeaders())
        interfaceHeaders_.add(interfaceHeader);
    plugins_.push_back(std::move(plugin));
    auto& movedPlugin = plugins_.back();

    // TODO: Get it from settings
    movedPlugin.info.isCurrentlyEnabled = false;
    restoreExtraSettingDefaults_(movedPlugin);
    if (movedPlugin.info.isDefaultEnabled)
        enable_(movedPlugin);

    // Tell the cores about it
    updateCoresAbout_(movedPlugin);
}

void PluginManager::remove(const std::string& name)
{
    remove_(getIteratorToPlugin_(name));
}

void PluginManager::removeAll()
{
    auto plugin = plugins_.begin();
    while (plugin != plugins_.end())
        plugin = remove_(plugin);
}

bool PluginManager::isLoaded(const std::string& name) const noexcept
{
    if (getIteratorToPluginNoThrow_(name) != plugins_.end())
        return true;
    return false;
}

void PluginManager::enable(const std::string& name)
{
    enable_(*getIteratorToPlugin_(name));
}

void PluginManager::enableAll()
{
    for (auto& plugin : plugins_)
        enable_(plugin);
}

void PluginManager::disable(const std::string& name)
{
    disable_(*getIteratorToPlugin_(name));
}

void PluginManager::disableAll()
{
    for (auto& plugin : plugins_)
        disable_(plugin);
}

bool PluginManager::isEnabled(const std::string& name) const
{
    return getIteratorToPlugin_(name)->info.isCurrentlyEnabled;
}

const std::vector<Info> PluginManager::getPluginsInfo() const
{
    std::vector<Info> retval;
    for (const auto& plugin : plugins_)
        retval.push_back(plugin.info);
    return retval;
}

const Info& PluginManager::getPluginInfo(const std::string& name) const
{
    return getIteratorToPlugin_(name)->info;
}

void PluginManager::setExtraSettingValue(const std::string& name, const std::string& extraSettingLabel, const std::string& value)
{
    setExtraSettingValue_(*getIteratorToPlugin_(name), extraSettingLabel, value);
}

void PluginManager::restoreExtraSettingDefaults(const std::string& name)
{
    restoreExtraSettingDefaults_(*getIteratorToPlugin_(name));
}

void PluginManager::restoreAllExtraSettingDefaults()
{
    for (auto& plugin : plugins_)
        restoreExtraSettingDefaults_(plugin);
}

void PluginManager::updateCoreAbout(const CoreManager::CoreId coreId, const std::string& name) const
{
    updateCoreAbout_(coreId, *getIteratorToPlugin_(name));
}

void PluginManager::updateCoresAbout(const std::string& name) const
{
    updateCoresAbout_(*getIteratorToPlugin_(name));
}

void PluginManager::updateCoreAboutAll(const CoreManager::CoreId coreId) const
{
    for (const auto& plugin : plugins_)
        updateCoreAbout_(coreId, plugin);
}

void PluginManager::updateCoresAboutAll() const
{
    for (const auto& plugin : plugins_)
        updateCoresAbout_(plugin);
}

// Private members

std::vector<uint8_t> PluginManager::Plugin_::serialise() const
{
    std::vector<uint8_t> data;
    data.reserve(1024);

    serialiseIntegralTypeContinuousContainer(data, module.getFile());
    serialiseIntegralTypeContinuousContainer(data, module.getPath());
    serialiseIntegralTypeContinuousContainer(data, info.serialise());
    return data;
}

std::vector<PluginManager::Plugin_>::const_iterator PluginManager::getIteratorToPluginNoThrow_(const std::string& name) const noexcept
{
    for (auto plugin_ = plugins_.begin(); plugin_ != plugins_.end(); ++plugin_)
        if (plugin_->info.name == name)
            return plugin_;
    return plugins_.end();
}

std::vector<PluginManager::Plugin_>::const_iterator PluginManager::getIteratorToPlugin_(const std::string& name) const
{
    auto result = getIteratorToPluginNoThrow_(name);
    if (result == plugins_.end())
        throw std::logic_error("No plugin with that name is loaded.");
    return result;
}

std::vector<PluginManager::Plugin_>::iterator PluginManager::getIteratorToPlugin_(const std::string& name)
{
    const PluginManager* self = this;
    auto it = self->getIteratorToPlugin_(name);
    auto result = plugins_.begin();
    std::advance(result, std::distance<std::vector<PluginManager::Plugin_>::const_iterator>(result, it));
    return result;
}

std::vector<PluginManager::Plugin_>::iterator PluginManager::remove_(std::vector<Plugin_>::iterator plugin, bool isExiting)
{
    // TODO: Save to settings
    disable_(*plugin, isExiting);
    for (const auto& interfaceHeader : plugin->plugin->getInterfaceHeaders())
        interfaceHeaders_.remove(interfaceHeader);

    if (!isExiting)
    {
        // Tell the cores about it
        std::vector<uint8_t> data;
        data.reserve(plugin->info.name.size());
        serialiseIntegralTypeContinuousContainer(data, plugin->info.name);
        CoreManager::getSingleton().sendPacket(Socket::ServerOpCode::PLUGIN_REMOVE, data);
    }

    return plugins_.erase(plugin);
}

void PluginManager::enable_(Plugin_& plugin)
{
    if (plugin.info.isCurrentlyEnabled)
        return;
    try
    {
        plugin.plugin->onEnable();
        plugin.info.isCurrentlyEnabled = true;
        plugin.plugin->onSettingChange(plugin.info);
        updateCoresAbout_(plugin);
    }
    catch (std::exception& e)
    {
        throw std::runtime_error("Could not enable plugin: " + std::string(e.what()));
    }
}

void PluginManager::disable_(Plugin_& plugin, bool isExiting)
{
    if (!plugin.info.isCurrentlyEnabled)
        return;
    try
    {
        plugin.plugin->onDisable(isExiting);
    } catch (...) {} // Gobble any exceptions
    plugin.info.isCurrentlyEnabled = false;
    plugin.plugin->onSettingChange(plugin.info);
    if (!isExiting)
        updateCoresAbout_(plugin);
}

void PluginManager::setExtraSettingValue_(Plugin_& plugin, const std::string& extraSettingLabel, const std::string& value)
{
    getExtraSettingByLabel(plugin.info.extraSettings, extraSettingLabel).currentValue = value;
    plugin.plugin->onSettingChange(plugin.info);
    updateCoresAbout_(plugin);
}

void PluginManager::restoreExtraSettingDefaults_(Plugin_& plugin)
{
    for (auto& extraSetting : plugin.info.extraSettings)
        extraSetting.currentValue = extraSetting.defaultValue;
    plugin.plugin->onSettingChange(plugin.info);
    updateCoresAbout_(plugin);
}

void PluginManager::updateCoreAbout_(const CoreManager::CoreId coreId, const Plugin_& plugin) const
{
    CoreManager::getSingleton().sendPacketTo(coreId, Socket::ServerOpCode::PLUGIN, plugin.serialise());
}

void PluginManager::updateCoresAbout_(const Plugin_& plugin) const
{
    CoreManager::getSingleton().sendPacket(Socket::ServerOpCode::PLUGIN, plugin.serialise());
}

void PluginManager::InterfaceHeaders_::add(std::string name)
{
    auto name_ = names_.find(name);
    if (name_ == names_.end())
        names_.insert(std::make_pair(name, 1));
    else
        ++name_->second;
}

void PluginManager::InterfaceHeaders_::remove(std::string name)
{
    auto name_ = names_.find(name);
    if (name_ == names_.end())
        return;
    --name_->second;
    if (name_->second == 0)
        names_.erase(name_->first);
}

std::set<std::string> PluginManager::InterfaceHeaders_::getNames() const
{
    std::set<std::string> result;
    for (const auto& name : names_)
        result.insert(name.first);
    return result;
}
