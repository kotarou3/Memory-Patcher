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

#include "PluginLoader.h"
#include "CorePlugin.h"
#include "Socket.h"
#include "Misc.h"
#include "Core.h"
#include "Logger.h"

PluginLoader& PluginLoader::getSingleton()
{
    static PluginLoader singleton;
    return singleton;
}

PluginLoader::PluginLoader()
{
    Core::getSingleton().addReceiveHandler(Socket::ServerOpCode::PLUGIN, pluginReceiveHandler_);
    Core::getSingleton().addReceiveHandler(Socket::ServerOpCode::PLUGIN_REMOVE, pluginRemoveReceiveHandler_);
}

PluginLoader::~PluginLoader()
{
    removeAll_();
}

bool PluginLoader::isPluginLoaded(const std::string& name) const noexcept
{
    if (getIteratorToPluginNoThrow_(name) != plugins_.end())
        return true;
    return false;
}

bool PluginLoader::isEnabled(const std::string& name) const
{
    return getIteratorToPlugin_(name)->info.isCurrentlyEnabled;
}

const std::vector<Info> PluginLoader::getPluginsInfo() const
{
    std::vector<Info> retval;
    for (const auto& plugin : plugins_)
        retval.push_back(plugin.info);
    return retval;
}

const Info& PluginLoader::getPluginInfo(const std::string& name) const
{
    return getIteratorToPlugin_(name)->info;
}

// Private members

void PluginLoader::pluginReceiveHandler_(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();
    std::string corePluginName = deserialiseIntegralTypeContinuousContainer<std::string>(iterator);
    std::string pathfile = deserialiseIntegralTypeContinuousContainer<std::string>(iterator) + "/lib" + corePluginName;
    Info info;
    info.deserialise(deserialiseIntegralTypeContinuousContainer<std::vector<uint8_t>>(iterator));

    PluginLoader& pluginLoader = getSingleton();

    if (pluginLoader.isPluginLoaded(info.name))
        pluginLoader.update_(info);
    else
        pluginLoader.add_(pathfile, info);
}

void PluginLoader::pluginRemoveReceiveHandler_(const std::vector<uint8_t>& data)
{
    auto iterator = data.cbegin();
    std::string name = deserialiseIntegralTypeContinuousContainer<std::string>(iterator);

    PluginLoader& pluginLoader = getSingleton();

    if (!pluginLoader.isPluginLoaded(name))
        return;
    pluginLoader.remove_(pluginLoader.getIteratorToPlugin_(name));
}

std::vector<PluginLoader::Plugin_>::const_iterator PluginLoader::getIteratorToPluginNoThrow_(const std::string& name) const noexcept
{
    for (auto plugin_ = plugins_.begin(); plugin_ != plugins_.end(); ++plugin_)
        if (plugin_->info.name == name)
            return plugin_;
    return plugins_.end();
}

std::vector<PluginLoader::Plugin_>::const_iterator PluginLoader::getIteratorToPlugin_(const std::string& name) const
{
    auto result = getIteratorToPluginNoThrow_(name);
    if (result == plugins_.end())
        throw std::logic_error("No plugin with that name is loaded.");
    return result;
}

std::vector<PluginLoader::Plugin_>::iterator PluginLoader::getIteratorToPlugin_(const std::string& name)
{
    const PluginLoader* self = this;
    auto it = self->getIteratorToPlugin_(name);
    auto result = plugins_.begin();
    std::advance(result, std::distance<std::vector<PluginLoader::Plugin_>::const_iterator>(result, it));
    return result;
}

void PluginLoader::add_(const std::string& pathfile, const Info& info)
{
    Module module;
    try
    {
        module.load(pathfile);
    }
    catch (std::runtime_error& e)
    {
        Logger::getSingleton().write(Logger::Severity::WARNING, "Could not load plugin `" + pathfile + "': " + e.what());
        return;
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
    plugin.info = info;

    plugins_.push_back(std::move(plugin));
    auto& movedPlugin = plugins_.back();

    if (movedPlugin.info.isCurrentlyEnabled)
    {
        movedPlugin.info.isCurrentlyEnabled = false;
        enable_(movedPlugin);
    }
}

void PluginLoader::update_(const Info& info)
{
    auto plugin = getIteratorToPlugin_(info.name);
    if (!info.isCurrentlyEnabled && plugin->info.isCurrentlyEnabled)
    {
        plugin->info = info;
        plugin->info.isCurrentlyEnabled = true;
        disable_(*plugin);
    }
    else if (info.isCurrentlyEnabled && !plugin->info.isCurrentlyEnabled)
    {
        plugin->info = info;
        plugin->info.isCurrentlyEnabled = false;
        enable_(*plugin);
    }
    else
    {
        plugin->info = info;
        plugin->plugin->onSettingChange(info);
    }
}

std::vector<PluginLoader::Plugin_>::iterator PluginLoader::remove_(std::vector<Plugin_>::iterator plugin)
{
    disable_(*plugin);
    return plugins_.erase(plugin);
}

void PluginLoader::removeAll_()
{
    auto plugin = plugins_.begin();
    while (plugin != plugins_.end())
        plugin = remove_(plugin);
}

void PluginLoader::enable_(Plugin_& plugin)
{
    if (plugin.info.isCurrentlyEnabled)
        return;
    try
    {
        plugin.plugin->onEnable();
        plugin.info.isCurrentlyEnabled = true;
        plugin.plugin->onSettingChange(plugin.info);
    }
    catch (std::exception& e)
    {
        throw std::runtime_error("Could not enable plugin: " + std::string(e.what()));
    }
}

void PluginLoader::disable_(Plugin_& plugin)
{
    if (!plugin.info.isCurrentlyEnabled)
        return;
    try
    {
        plugin.plugin->onDisable();
    } catch (...) {} // Gobble any exceptions
    plugin.info.isCurrentlyEnabled = false;
    plugin.plugin->onSettingChange(plugin.info);
}
