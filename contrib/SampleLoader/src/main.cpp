#include <string>
#include <vector>

#ifdef _WIN32
namespace win32
{
    #include <windows.h>
}
#else
namespace posix
{
    #include <dirent.h>
}
#endif

#include "PluginManager.h"
#include "SettingsManager.h"
#include "CoreManager.h"

std::vector<std::string> getFilesInDirectory(const std::string& path)
{
    std::vector<std::string> result;
#ifdef _WIN32
    win32::WIN32_FIND_DATA directoryFile;
    win32::HANDLE directoryHandle = win32::FindFirstFile(path.c_str(), &directoryFile);
    do
    {
        if (directoryFile.dwFileAttributes & FILE_ATTRIBUTE_DEVICE ||
            directoryFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        result.push_back(directoryFile.cFileName);
    } while (win32::FindNextFile(directoryHandle, &directoryFile));
    win32::FindClose(directoryHandle);
#else
    posix::DIR* directoryHandle = posix::opendir(path.c_str());
    posix::dirent* directoryFile;
    while ((directoryFile = posix::readdir(directoryHandle)) != nullptr)
    {
        if (!(directoryFile->d_type & posix::DT_REG))
            continue;
        result.push_back(directoryFile->d_name);
    }
    posix::closedir(directoryHandle);
#endif
    return result;
}

int main()
{
    // Load and save settings from the following file
    SettingsManager& settings = SettingsManager::getSingleton();
    settings.load("./settings.json");

    // Find and load all the manager plugins
    std::string pluginsPath = settings.get("PluginManager.managerPluginsPath");
    auto plugins = getFilesInDirectory(pluginsPath);
    PluginManager& pluginManager = PluginManager::getSingleton();
    for (const auto& plugin : plugins)
        pluginManager.add(pluginsPath + "/" + plugin);

    // Start one instance of the target program
    CoreManager::getSingleton().startCore();

    // Wait 5 seconds
#ifdef _WIN32
    win32::Sleep(5000);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
#endif
}
