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

#include <stack>
#include <utility>
#include <stdexcept>

#include <cstdio>
#include <cerrno>
#include <cassert>

#include "SettingsManager.h"
#include "Logger.h"
#include "json/json.h"

SettingsManager& SettingsManager::getSingleton()
{
    static SettingsManager singleton;
    return singleton;
}

SettingsManager::SettingsManager()
{
    auto branch = settingsTree_.insert(std::make_pair("settings", SettingsBranch())).first;
    branch->second.parent = branch;
    branch = settingsTree_.insert(std::make_pair("defaultSettings", SettingsBranch())).first;
    branch->second.parent = branch;

    // Here comes the default settings
    setDefault("Logger.minimumSeverity", itos((int)Logger::Severity::NOTICE));
    setDefault("PluginManager.includePath", "plugins/include");
    setDefault("PluginManager.managerPluginsPath", "plugins/manager"); // Unused
    setDefault("PluginManager.corePluginsPath", "plugins/core");
    setDefault("PatchCompiler.includePath", "include");
    setDefault("PatchCompiler.objectsPath", "objects");
    setDefault("PatchCompiler.CXX", "g++-4.7");
    setDefault("PatchCompiler.customCXXFLAGS", "-Wall -Wextra -pedantic -pipe -fvisibility=hidden -mtune=core2 -D_GLIBCXX_USE_NANOSLEEP -ggdb -DDEBUG");
    setDefault("PatchCompiler.customLDFLAGS", "");
    setDefault("CoreManager.applicationName", "./test"
    #ifdef _WIN32
        ".exe"
    #endif
        );
    setDefault("CoreManager.applicationParameters", "");
    setDefault("CoreManager.libraryPath", ".");
    setDefault("CoreManager.coreLibrary", "core");
    setDefault("CoreManager.patchesLibrary", "patches");
}

SettingsManager::~SettingsManager()
{
    if (saveOnExitFilename_.empty())
        return;
    try
    {
        save(saveOnExitFilename_);
    } catch (std::exception& e) {}
}

std::string SettingsManager::get(const std::string& name) const noexcept
{
    auto branch = get_(name, settingsTree_.find("settings"));
    if (branch == settingsTree_.end())
        branch = get_(name, settingsTree_.find("defaultSettings"));
    if (branch == settingsTree_.end())
        return std::string();
    return branch->second.value;
}

void SettingsManager::set(const std::string& name, const std::string& value)
{
    bool isClear = value.empty();
    if (!isClear)
    {
        auto defaultValue = get_(name, settingsTree_.find("defaultSettings"));
        if (defaultValue != settingsTree_.end() && value == defaultValue->second.value)
            isClear = true;
    }
    if (isClear)
        clear_(name, settingsTree_.find("settings"));
    else
        get_(name, settingsTree_.find("settings"), true)->second.value = value;
}

void SettingsManager::setDefault(const std::string& name, const std::string& value)
{
    if (value.empty())
        clear_(name, settingsTree_.find("defaultSettings"));
    else
        get_(name, settingsTree_.find("defaultSettings"), true)->second.value = value;
}

void SettingsManager::save(const std::string& filename) const
{
    // Convert settings to JSON
    auto rootBranch = settingsTree_.find("settings");
    Json::Value rootJson;
    std::stack<std::pair<Json::Value*, const SettingsBranch*>> stack;
    stack.push(std::make_pair(&rootJson, &rootBranch->second));
    while (stack.size() > 0)
    {
        auto json = stack.top().first;
        auto branch = stack.top().second;
        stack.pop();
        if (!branch->value.empty())
        {
            if (branch->children.empty())
                *json = branch->value;
            else
                (*json)["__value__"] = branch->value;
        }
        for (auto& childBranch : branch->children)
            stack.push(std::make_pair(&(*json)[childBranch.first], &childBranch.second));
    }
    if (!rootJson.isObject())
        return; // Nothing to save
    std::string buffer = Json::StyledWriter().write(rootJson);

    // Write the JSON to the file
    std::FILE* out = std::fopen(filename.c_str(), "w");
    if (out == nullptr)
        throw std::runtime_error("Could not open " + filename + " to write settings to: " + strError(errno));
    std::fwrite(buffer.data(), 1, buffer.size(), out);
    bool isNotSuccessful = std::ferror(out);
    std::fclose(out);
    if (isNotSuccessful)
        throw std::runtime_error("Could not write JSON settings successfully to " + filename + ".");
}

void SettingsManager::load(const std::string& filename, bool isSaveOnExit)
{
    // Read the JSON from the file
    std::FILE* in = std::fopen(filename.c_str(), "r");
    if (in == nullptr)
    {
        if (errno != ENOENT)
            throw std::runtime_error("Could not open " + filename + " to read settings from: " + strError(errno));
        if (isSaveOnExit)
            saveOnExitFilename_ = filename;
        return;
    }
    std::string buffer;
    buffer.resize(1024);
    std::string::pointer nextChunk = &buffer[0];
    std::string::size_type nextChunkSize = 1024;
    while (std::fread(nextChunk, 1, nextChunkSize, in) == nextChunkSize)
    {
        nextChunkSize = buffer.length();
        buffer.resize(buffer.length() * 2);
        nextChunk = &buffer[nextChunkSize];
    }
    bool isNotSuccessful = std::ferror(in);
    std::fclose(in);
    if (isNotSuccessful)
        throw std::runtime_error("Could not read JSON settings successfully from " + filename + ".");

    // Parse the JSON
    Json::Value rootJson;
    Json::Reader reader(Json::Features::strictMode());
    if (!reader.parse(buffer, rootJson))
        throw std::runtime_error("Failed to parse settings JSON:\n" + reader.getFormattedErrorMessages());
    auto rootBranch = settingsTree_.find("settings");
    rootBranch->second.children.clear();
    std::stack<std::pair<std::map<std::string, SettingsBranch>::iterator, Json::Value*>> stack;
    stack.push(std::make_pair(rootBranch, &rootJson));
    while (stack.size() > 0)
    {
        auto branch = stack.top().first;
        auto json = stack.top().second;
        stack.pop();
        if (!json->isObject())
        {
            if (json->isString())
                branch->second.value = json->asString();
            continue;
        }

        branch->second.value = json->get("__value__", std::string()).asString();
        for (auto child = json->begin(); child != json->end(); ++child)
        {
            if (std::string(child.memberName()) == "__value__")
                continue;
            auto childBranch = branch->second.children.insert(std::make_pair(child.memberName(), SettingsBranch())).first;
            childBranch->second.parent = branch;
            stack.push(std::make_pair(childBranch, &*child));
        }
    }

    if (isSaveOnExit)
        saveOnExitFilename_ = filename;
}

// Private members

std::map<std::string, SettingsManager::SettingsBranch>::const_iterator SettingsManager::get_(const std::string& name, std::map<std::string, SettingsManager::SettingsBranch>::const_iterator rootBranch) const noexcept
{
    std::vector<std::string> parts = split(name, ".");
    auto currentBranch = rootBranch;
    for (const auto& part : parts)
    {
        if (part.empty())
            continue;
        auto branch = currentBranch->second.children.find(part);
        if (branch == currentBranch->second.children.end())
        {
            currentBranch = settingsTree_.end();
            break;
        }
        currentBranch = branch;
    }
    return currentBranch;
}

std::map<std::string, SettingsManager::SettingsBranch>::iterator SettingsManager::get_(const std::string& name, std::map<std::string, SettingsManager::SettingsBranch>::iterator rootBranch, bool isCreateIfNotExists)
{
    std::vector<std::string> parts = split(name, ".");
    auto currentBranch = rootBranch;
    for (const auto& part : parts)
    {
        if (part.empty())
            continue;
        auto branch = currentBranch->second.children.find(part);
        if (branch == currentBranch->second.children.end())
        {
            if (!isCreateIfNotExists)
            {
                currentBranch = settingsTree_.end();
                break;
            }
            else
            {
                branch = currentBranch->second.children.insert(std::make_pair(part, SettingsBranch())).first;
                branch->second.parent = currentBranch;
            }
        }
        currentBranch = branch;
    }
    return currentBranch;
}

void SettingsManager::clear_(const std::string& name, std::map<std::string, SettingsManager::SettingsBranch>::iterator rootBranch)
{
    auto currentBranch = get_(name, rootBranch);
    if (currentBranch == settingsTree_.end())
        return;
    currentBranch->second.value = std::string();
    while (currentBranch->second.value.empty() && currentBranch->second.children.empty() && currentBranch->second.parent != currentBranch)
    {
        auto parentBranch = currentBranch->second.parent;
        parentBranch->second.children.erase(currentBranch);
        currentBranch = parentBranch;
    }
}
