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
#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <set>

#include <stdint.h>

#include "Info.h"
#include "Misc.h"
#include "CoreManager.h"
#include "Module.h"

class ManagerPlugin;

class MANAGER_EXPORT PluginManager final
{
    public:
        void add(const std::string& pathfile);
        void remove(const std::string& name);
        void removeAll();
        bool isLoaded(const std::string& name) const noexcept;

        void enable(const std::string& name);
        void enableAll();
        void disable(const std::string& name);
        void disableAll();
        bool isEnabled(const std::string& name) const;

        const std::vector<Info> getPluginsInfo() const;
        const Info& getPluginInfo(const std::string& name) const;
        std::string getCorePluginName(const std::string& name) const;

        void setExtraSettingValue(const std::string& name, const std::string& extraSettingLabel, const std::string& value);
        void restoreExtraSettingDefaults(const std::string& name);
        void restoreAllExtraSettingDefaults();

        void updateCoreAbout(const CoreManager::CoreId coreId, const std::string& name) const;
        void updateCoresAbout(const std::string& name) const;
        void updateCoreAboutAll(const CoreManager::CoreId coreId) const;
        void updateCoresAboutAll() const;

        static PluginManager& getSingleton();

    private:
        PluginManager();
        PluginManager(const PluginManager&) = delete;
        PluginManager& operator=(const PluginManager&) = delete;
        ~PluginManager();

        class Plugin_ final
        {
            public:
                std::vector<uint8_t> serialise() const;

                Module module;
                std::unique_ptr<ManagerPlugin> plugin;
                Info info;
                std::string corePluginName;
        };

        std::vector<Plugin_>::const_iterator getIteratorToPluginNoThrow_(const std::string& name) const noexcept;
        std::vector<Plugin_>::const_iterator getIteratorToPlugin_(const std::string& name) const;
        std::vector<Plugin_>::iterator getIteratorToPlugin_(const std::string& name);

        std::vector<Plugin_>::iterator remove_(std::vector<Plugin_>::iterator plugin, bool isExiting = false);

        void enable_(Plugin_& plugin);
        void disable_(Plugin_& plugin, bool isExiting = false);

        void setExtraSettingValue_(Plugin_& plugin, const std::string& extraSettingLabel, const std::string& value);
        void restoreExtraSettingDefaults_(Plugin_& plugin);

        void updateCoreAbout_(const CoreManager::CoreId coreId, const Plugin_& plugin) const;
        void updateCoresAbout_(const Plugin_& plugin) const;

        void checkSingletonAndIsInitialised_() const;

        std::vector<Plugin_> plugins_;
};

#endif
