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
#ifndef PLUGINLOADER_H
#define PLUGINLOADER_H

#include <string>
#include <vector>

#include <stdint.h>

#include "Info.h"
#include "Misc.h"
#include "Module.h"

class CorePlugin;

class CORE_EXPORT PluginLoader final
{
    public:
        bool isPluginLoaded(const std::string& name) const noexcept;
        bool isEnabled(const std::string& name) const;

        const std::vector<Info> getPluginsInfo() const;
        const Info& getPluginInfo(const std::string& name) const;

        static PluginLoader& getSingleton();

    private:
        PluginLoader();
        PluginLoader(const PluginLoader&) = delete;
        PluginLoader& operator=(const PluginLoader&) = delete;
        ~PluginLoader();

        class Plugin_ final
        {
            public:
                Module module;
                std::unique_ptr<CorePlugin> plugin;
                Info info;
        };

        static void pluginReceiveHandler_(const std::vector<uint8_t>& data);
        static void pluginRemoveReceiveHandler_(const std::vector<uint8_t>& data);

        std::vector<Plugin_>::const_iterator getIteratorToPluginNoThrow_(const std::string& name) const noexcept;
        std::vector<Plugin_>::const_iterator getIteratorToPlugin_(const std::string& name) const;
        std::vector<Plugin_>::iterator getIteratorToPlugin_(const std::string& name);

        void add_(const std::string& pathfile, const Info& info);
        void update_(const Info& info);
        std::vector<Plugin_>::iterator remove_(std::vector<Plugin_>::iterator plugin);
        void removeAll_();

        void enable_(Plugin_& plugin);
        void disable_(Plugin_& plugin);

        std::vector<Plugin_> plugins_;
};

#endif
