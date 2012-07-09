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

#include <string>
#include <vector>

#include <cstdio>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
namespace win32
{
    #include <windows.h>
    #include <io.h>
    #ifdef STD_INPUT_HANDLE
        #undef STD_INPUT_HANDLE
        #define STD_INPUT_HANDLE ((win32::DWORD)-10) // Fix up the define
    #endif
}
namespace posix
{
    #include <direct.h>
    #include <fcntl.h>
    using ::fdopen;
}
#else
namespace posix
{
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <dirent.h>
    using ::fdopen;
    using ::pid_t; // We shouldn't need to do this, but <string> causes everything in <pthreads.h> to be included in the global namespace
    int mkdir(const char* path) { return mkdir(path, 755); }
}
    #include "stringToArgcArgv.h"
#endif

#include "PatchCompiler.h"
#include "SettingsManager.h"
#include "Misc.h"

namespace PatchCompiler
{

using namespace PatchData;

// Declare some private functions
namespace
{
    std::string getHookSafename(const std::string& name);
    std::string getPatchPackSafename(const std::string& name);
    std::string getExternCAsmName(const std::string& name);

    std::string generateHookSource(const Hook& hook);
    std::string generatePatchPackSource(const PatchPack& patchPack);
    std::string getLicense();
    std::string generatePrettyLicense();

    std::string callGCC(const std::string& args);
    std::string getObjectDirectory();
    std::string getCXXFLAGS();
    std::string getLDFLAGS();
    std::string getCustomCXXFLAGS();
    std::string getCustomLDFLAGS();
}

std::string compileHook(const Hook& hook, bool& isSkipped, bool force)
{
    isSkipped = false;
    std::string source = generateHookSource(hook);
    uint32_t crc32 = calculateCrc32Checksum(std::vector<uint8_t>(source.begin(), source.end()));
    std::string objectFilename = getObjectDirectory() + getHookSafename(hook.name) + ".o";
    if (!force && crc32 == std::stoul("0" + SettingsManager::getSingleton().get("hooks." + hook.name + ".crc32")))
    {
        // Check if the compiled object exists before we skip
        std::FILE* objectFile = std::fopen(objectFilename.c_str(), "rb");
        if (objectFile != nullptr)
        {
            std::fclose(objectFile);
            isSkipped = true;
            return "";
        }
    }

    // Write the generated source to a file
    std::string sourceFilename = getObjectDirectory() + getHookSafename(hook.name) + ".cpp";
    std::FILE* sourceFile = std::fopen(sourceFilename.c_str(), "wb");
    std::fwrite(source.c_str(), 1, source.size(), sourceFile);
    std::fclose(sourceFile);

    // Compile the source
    std::string output = callGCC("\"" + sourceFilename + "\" -c -o \"" + objectFilename + "\" " + getCXXFLAGS() + " " + getCustomCXXFLAGS());

    // Touch a file so `linkObjects()' knows to relink
    std::fclose(std::fopen((getObjectDirectory() + "modified").c_str(), "wb"));

    SettingsManager::getSingleton().set("hooks." + hook.name + ".crc32", itos(crc32));
    return output;
}

std::string compilePatchPack(const PatchPack& patchPack, bool& isSkipped, bool force)
{
    isSkipped = false;
    std::string source = generatePatchPackSource(patchPack);
    uint32_t crc32 = calculateCrc32Checksum(std::vector<uint8_t>(source.begin(), source.end()));
    std::string objectFilename = getObjectDirectory() + getPatchPackSafename(patchPack.info.name) + ".o";
    if (!force && crc32 == std::stoul("0" + SettingsManager::getSingleton().get("patchPacks." + patchPack.info.name + ".crc32")))
    {
        // Check if the compiled object exists before we skip
        std::FILE* objectFile = std::fopen(objectFilename.c_str(), "rb");
        if (objectFile != nullptr)
        {
            std::fclose(objectFile);
            isSkipped = true;
            return "";
        }
    }

    // Write the generated source to a file
    std::string sourceFilename = getObjectDirectory() + getPatchPackSafename(patchPack.info.name) + ".cpp";
    std::FILE* sourceFile = std::fopen(sourceFilename.c_str(), "wb");
    std::fwrite(source.c_str(), 1, source.size(), sourceFile);
    std::fclose(sourceFile);

    // Compile the source
    std::string output = callGCC("\"" + sourceFilename + "\" -c -o \"" + objectFilename + "\" " + getCXXFLAGS() + " " + getCustomCXXFLAGS());

    // Touch a file so `linkObjects()' knows to relink
    std::fclose(std::fopen((getObjectDirectory() + "modified").c_str(), "wb"));

    SettingsManager::getSingleton().set("patchPacks." + patchPack.info.name + ".crc32", itos(crc32));
    return output;
}

std::string linkObjects(bool force)
{
    std::string patchesFilename = SettingsManager::getSingleton().get("core.patchesLibrary");
    if (!force)
    {
        // Check if we really need to relink
        std::FILE* patchesFile = std::fopen(patchesFilename.c_str(), "rb");
        if (patchesFile != nullptr) // If `patchesFilename' exists...
        {
            std::fclose(patchesFile);
            std::FILE* modifiedFile = std::fopen((getObjectDirectory() + "modified").c_str(), "rb");
            if (modifiedFile != nullptr) // If "modified" exists...
            {
                std::fclose(modifiedFile);
                std::remove((getObjectDirectory() + "modified").c_str());
            }
            else
                return "";
        }
    }

    // Enumerate the object files in the object directory
    std::vector<std::string> objectFilenames;
#ifdef _WIN32
    win32::WIN32_FIND_DATA objectDirectoryFile;
    win32::HANDLE objectDirectory = win32::FindFirstFile((getObjectDirectory() + "*.o").c_str(), &objectDirectoryFile);
    do
    {
        if (objectDirectoryFile.dwFileAttributes & FILE_ATTRIBUTE_DEVICE ||
            objectDirectoryFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        objectFilenames.push_back(getObjectDirectory() + objectDirectoryFile.cFileName);
    } while (win32::FindNextFile(objectDirectory, &objectDirectoryFile));
    win32::FindClose(objectDirectory);
#else
    posix::DIR* objectDirectory = posix::opendir(getObjectDirectory().c_str());
    posix::dirent* objectDirectoryFile;
    while ((objectDirectoryFile = posix::readdir(objectDirectory)) != nullptr)
    {
        if (!(objectDirectoryFile->d_type & posix::DT_REG))
            continue;
        std::string objectDirectoryFilename = objectDirectoryFile->d_name;
        if (objectDirectoryFilename.size() < 2)
            continue;
        if (objectDirectoryFilename.rfind(".o") != objectDirectoryFilename.size() - 2)
            continue;
        objectFilenames.push_back(getObjectDirectory() + objectDirectoryFilename);
    }
    posix::closedir(objectDirectory);
#endif

    // Ready the object file arguments
    std::string objectFilenamesArgument;
    for (const auto& objectFilename : objectFilenames)
        objectFilenamesArgument += "\"" + objectFilename + "\" ";

    // Call the linker!
    return callGCC(objectFilenamesArgument + " -o \"" + patchesFilename + "\" -shared " + getLDFLAGS() + " " + getCustomLDFLAGS());
}

// Private functions
namespace
{

std::string getHookSafename(const std::string& name)
{
    return "hook_" + btos(name);
}

std::string getPatchPackSafename(const std::string& name)
{
    return "patchpack_" + btos(name);
}

std::string getExternCAsmName(const std::string& name)
{
#ifdef _WIN32
    return "_" + name;
#else
    return name;
#endif
}

std::string generateHookSource(const Hook& hook)
{
    std::string output;
    output.reserve(4096);

    // Output the license
    output += generatePrettyLicense() + "\n";

    // Output the includes
    output += "#include <map>\n";
    for (const auto& headerInclude : hook.headerIncludes)
        output += "#include <" + headerInclude + ">\n";
    output += "#include \"HookFunctions.h\"\n";
    output += "\n";

    // Output the hook hook patch function vector and mutex
    output += "__attribute__ ((visibility (\"default\"))) std::map<hookPatchFunction_t, ExtraSettings> " + getHookSafename(hook.name) + "_hookPatchFunctions;\n"
              "__attribute__ ((visibility (\"default\"))) std::recursive_mutex "+ getHookSafename(hook.name) + "_hookPatchFunctionsMutex;\n\n"; // FIXME: Should be just a normal mutex

    // Output the hook function
    output += "extern \"C\" void " + getHookSafename(hook.name) + "(uint32_t& edi, uint32_t& esi, uint32_t& ebp, const uint32_t& espInsideFrame, uint32_t& ebx, uint32_t& edx, uint32_t& ecx, uint32_t& eax, uint32_t& returnAddress, uint8_t* extraStackSpace)\n"
              "{\n"
              "    const uint32_t esp = espInsideFrame + " + itos(hook.extraStackSpace + 4) + "; // Get esp before the hook call\n"
              "    returnAddress += " + itos(hook.returnRva) + "; // Add the return rva to the return address\n"
              "    std::vector<void*> extraParameters;\n"
              "    // Prologue function start\n"
              "    " + hook.prologueFunction + "\n"
              "    // Prologue function end\n"
              "    Registers registers;\n"
              "    registers.eax = eax;\n"
              "    registers.ebx = ebx;\n"
              "    registers.ecx = ecx;\n"
              "    registers.edx = edx;\n"
              "    registers.esp = esp;\n"
              "    registers.ebp = ebp;\n"
              "    registers.esi = esi;\n"
              "    registers.edi = edi;\n"
              "    {\n"
              "        std::lock_guard<std::recursive_mutex> hookPatchFunctionsLock(" + getHookSafename(hook.name) + "_hookPatchFunctionsMutex);\n"
              "        for (const auto& hookPatchFunction : " + getHookSafename(hook.name) + "_hookPatchFunctions)\n"
              "            hookPatchFunction.first(registers, returnAddress, hookPatchFunction.second, extraParameters);\n"
              "    }\n"
              "    // Epilogue function start\n"
              "    " + hook.epilogueFunction + "\n"
              "    // Epilogue function end\n"
              "}\n\n";

    // Output the hook wrapper
    output += "extern \"C\" __attribute__ ((visibility (\"default\"))) void " + getHookSafename(hook.name) + "_wrapper();\n"
              "// /src/manager/" __FILE__ ":" + itos(__LINE__ + 1) + " explains the following assembly.\n";
    // Raw assembly for the wrapper. Note: 32 is the amount of bytes that "pusha" pushes on to the stack.
    // Declaration and prototype
    output += "asm (\".globl " + getExternCAsmName(getHookSafename(hook.name) + "_wrapper") + "\\n\"\n" //     .globl [hookSafename]_wrapper
              "\"" + getExternCAsmName(getHookSafename(hook.name) + "_wrapper") + ":\\n\\t\"\n";        // [hookSafename]_wrapper:
    // Run the prologue instructions bytes
    output += "    \"addl $4, %esp\\n\\t\"\n";                                                          //     addl $4, %esp                                // Pretend we aren't in a call frame.
    for (const auto& prologueInstructionsByte : hook.prologueInstructionsBytes)
        output += "    \".byte " + itos(prologueInstructionsByte) + "\\n\\t\"\n";                       //     .byte [prologueInstructionsByte]             // Emits a prologue instructions byte.
    output += "    \"subl $4, %esp\\n\\t\"\n"                                                           //     subl $4, %esp                                // Un-pretend.
    // Allocate the extra stack space and save all registers to the stack
              "    \"subl $" + itos(hook.extraStackSpace) + ", %esp\\n\\t\"\n"                          //     subl $[extraStackSpace], %esp                // Allocate the extra stack space.
              "    \"pusha\\n\\t\"\n"                                                                   //     pusha                                        // Save all registers to the stack.
              "    \"movl " + itos(32 + hook.extraStackSpace) + "(%esp), %eax\\n\\t\"\n"                //     movl [32 + extraStackSpace](%esp), %eax      // Copy the return address out
              "    \"movl %eax, 32(%esp)\\n\\t\"\n"                                                     //     movl %eax, 32(%esp)                          // of the extra stack space.
    // Push the addresses of the extra stack space, the return address and the
    // registers on to the stack for use by the hook function
              "    \"leal " + itos(32 + hook.extraStackSpace) + "(%esp), %eax\\n\\t\"\n"                //     leal [32 + extraStackSpace](%esp), %eax      // Get start address of the extra stack space.
              "    \"push %eax\\n\\t\"\n"                                                               //     push %eax                                    // Push the start address on to the stack.
              "    \"subl $" + itos(hook.extraStackSpace) + ", %eax\\n\\t\"\n"                          //     subl $[extraStackSpace], %eax                // Move to the address of the return address.
              "    \"push %eax\\n\\t\"\n"                                                               //     push %eax                                    // Push the address on to the stack.
              "    \"subl $4, %eax\\n\\t\"\n"                                                           //     subl $4, %eax                                // Move to the address of eax.
              "    \"push %eax\\n\\t\"\n"                                                               //     push %eax                                    // etc.
              "    \"subl $4, %eax\\n\\t\"\n"                                                           //     subl $4, %eax                                // ecx
              "    \"push %eax\\n\\t\"\n"                                                               //     push %eax
              "    \"subl $4, %eax\\n\\t\"\n"                                                           //     subl $4, %eax                                // edx
              "    \"push %eax\\n\\t\"\n"                                                               //     push %eax
              "    \"subl $4, %eax\\n\\t\"\n"                                                           //     subl $4, %eax                                // ebx
              "    \"push %eax\\n\\t\"\n"                                                               //     push %eax
              "    \"subl $4, %eax\\n\\t\"\n"                                                           //     subl $4, %eax                                // esp
              "    \"push %eax\\n\\t\"\n"                                                               //     push %eax
              "    \"subl $4, %eax\\n\\t\"\n"                                                           //     subl $4, %eax                                // ebp
              "    \"push %eax\\n\\t\"\n"                                                               //     push %eax
              "    \"subl $4, %eax\\n\\t\"\n"                                                           //     subl $4, %eax                                // esi
              "    \"push %eax\\n\\t\"\n"                                                               //     push %eax
              "    \"subl $4, %eax\\n\\t\"\n"                                                           //     subl $4, %eax                                // edi
              "    \"push %eax\\n\\t\"\n"                                                               //     push %eax
    // Call the hook function
              "    \"call " + getExternCAsmName(getHookSafename(hook.name)) + "\\n\\t\"\n"              //     call [hookSafename]
    // Cleanup
              "    \"addl $40, %esp\\n\\t\"\n"                                                          //     addl $40, %esp                               // Clean up the stack used for the call.
              "    \"popa\\n\\t\"\n";                                                                   //     popa                                         // Restore the (possibly modified) registers.
    // Run the epilogue instructions bytes
    output += "    \"addl $4, %esp\\n\\t\"\n";                                                          //     addl $4, %esp                                // Pretend we aren't in a call frame.
    for (const auto& epilogueInstructionsByte : hook.epilogueInstructionsBytes)
        output += "    \".byte " + itos(epilogueInstructionsByte) + "\\n\\t\"\n";                       //     .byte [epilogueInstructionsByte]             // Emits a epilogue instructions byte.
    output += "    \"subl $4, %esp\\n\\t\"\n";                                                          //     subl $4, %esp                                // Un-pretend.
    // Return
    output += "    \"ret $" + itos(hook.stackSpaceToPopAfterReturn) + "\\n\\t\"\n"                      //     ret  $[stackSpaceToPopAfterReturn]           // Returns and pop bytes off stack
              ");\n\n";

    return output;
}

std::string generatePatchPackSource(const PatchPack& patchPack)
{
    std::string output;
    output.reserve(4096);

    // Output the license
    output += generatePrettyLicense() + "\n";

    // Output the includes
    for (const auto& headerInclude : patchPack.headerIncludes)
        output += "#include <" + headerInclude + ">\n";
    output += "#include \"HookFunctions.h\"\n";
    output += "\n";

    {
        // Output the shared variables
        output += "namespace\n"
                  "{\n";
        size_t s = 0;
        for (const auto& sharedVariable : patchPack.sharedVariables)
        {
            output += "    using " + getPatchPackSafename(patchPack.info.name) + "_sharedVariableType" + itos(s) + " = " + sharedVariable.second + ";\n"
                      "    " + getPatchPackSafename(patchPack.info.name) + "_sharedVariableType" + itos(s) + " " + sharedVariable.first + ";\n";
            ++s;
        }
        output += "}\n\n";
    }

    // Output the individual hook patches
    {
        size_t p = 0;
        for (const auto& patch : patchPack.patches)
            if (patch.getType() == Patch::Type::HOOK)
            {
                output += "extern \"C\" __attribute__ ((visibility (\"default\"))) void " + getPatchPackSafename(patchPack.info.name) + "_hookPatch" + itos(p) + "(const Registers& registers, const uint32_t returnAddress, const ExtraSettings extraSettings, std::vector<void*>& extraParameters)\n"
                          "{\n"
                          "    " + patch.getTypeData<HookPatch>().functionBody + "\n"
                          "}\n\n";
                ++p;
            }
    }

    return output;
}

std::string getLicense()
{
    return
    "This file is part of Memory Patcher.\n"
    "\n"
    "Memory Patcher is free software: you can redistribute it and/or modify\n"
    "it under the terms of the GNU Lesser General Public License as published by\n"
    "the Free Software Foundation, either version 3 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "Memory Patcher is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
    "GNU Lesser General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU Lesser General Public License\n"
    "along with Memory Patcher. If not, see <http://www.gnu.org/licenses/>.";
}

std::string generatePrettyLicense()
{
    std::string output;
    output.reserve(1024);

    output += "/*\n";
    std::vector<std::string> lines = split(getLicense(), "\n");
    for (const auto& line : lines)
        if (line.empty())
            output += "\n";
        else
            output += "    " + line + "\n";
    output += "*/\n";
    return output;
}

std::string callGCC(const std::string& args)
{
    std::string output;
    output.reserve(1024);
    output += "g++ " + args + "\n";
    int exitCode;
#ifdef _WIN32
    // Create pipes to redirect stdout and stderr of g++
    win32::SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(win32::SECURITY_ATTRIBUTES);
    sa.bInheritHandle = true;
    sa.lpSecurityDescriptor = nullptr;
    win32::HANDLE pipeReadHandle;
    win32::HANDLE pipeWriteHandle;
    win32::CreatePipe(&pipeReadHandle, &pipeWriteHandle, &sa, 1024);
    win32::SetHandleInformation(pipeReadHandle, HANDLE_FLAG_INHERIT, 0);

    // Create the process
    win32::STARTUPINFO si = {0};
    si.cb = sizeof(win32::STARTUPINFO);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = pipeWriteHandle;
    si.hStdError = pipeWriteHandle;
    si.hStdInput = win32::GetStdHandle(STD_INPUT_HANDLE);
    win32::PROCESS_INFORMATION pi = {0};
    if (!win32::CreateProcess(nullptr, &("g++ " + args)[0], nullptr, nullptr, true, 0, nullptr, nullptr, &si, &pi))
    {
        win32::CloseHandle(pipeWriteHandle);
        win32::CloseHandle(pipeReadHandle);
        throw std::runtime_error(output + "Could not create process: " + strErrorWin32(win32::GetLastError()));
    }
    win32::CloseHandle(pipeWriteHandle);

    // Convert `pipeReadHandle` to a FILE* and start reading from it
    int pipeReadFd = win32::_open_osfhandle((intptr_t)pipeReadHandle, _O_RDONLY | _O_BINARY);
    std::FILE* pipeRead = posix::fdopen(pipeReadFd, "rb");
    char outputBuffer[1024];
    while (fgets(outputBuffer, 1024, pipeRead) != nullptr)
        output += outputBuffer;
    fclose(pipeRead);

    // Wait for the process to exit
    win32::WaitForSingleObject(pi.hProcess, INFINITE);
    win32::GetExitCodeProcess(pi.hProcess, (win32::DWORD*)&exitCode);
#else
    // Create a new process with redirected stdout and stderr
    int errorPipes[2];
    int outputPipes[2];
    posix::pipe(errorPipes);
    posix::pipe(outputPipes);
    posix::fcntl(errorPipes[0], F_SETFD, posix::fcntl(errorPipes[0], F_GETFD) | FD_CLOEXEC);
    posix::fcntl(errorPipes[1], F_SETFD, posix::fcntl(errorPipes[1], F_GETFD) | FD_CLOEXEC);
    posix::pid_t pid = posix::fork();
    if (pid == 0)
    {
        posix::close(errorPipes[0]);
        posix::close(outputPipes[0]);
        posix::dup2(outputPipes[1], fileno(stdout));
        posix::dup2(outputPipes[1], fileno(stderr));
        int argc;
        char** _argv;
        try
        {
            stringToArgcArgv("g++ " + args, &argc, &_argv);
        }
        catch (std::exception e)
        {
            throw std::logic_error("Error reading parameters: " + std::string(e.what()));
        }
        // stringToArgcArgv doesn't output the ending NULL so
        // we have to manually add it on
        char* argv[argc + 1];
        for (int i = 0; i < argc; ++i)
            argv[i] = _argv[i];
        argv[argc] = NULL;

        // Screw memory deallocation. We are exec'ing!
        posix::execvp("g++", argv);
        // We shouldn't get here if execvpe was successful
        posix::write(errorPipes[1], &errno, sizeof(errno)); // Write error to parent
        posix::close(errorPipes[1]);
        std::abort();
    }
    posix::close(errorPipes[1]);
    posix::close(outputPipes[1]);
    if (pid < 0)
    {
        posix::close(errorPipes[0]);
        throw std::runtime_error("Could not fork: " + strError(errno));
    }
    int err;
    int amountRead = posix::read(errorPipes[0], &err, sizeof(err));
    posix::close(errorPipes[0]);
    if (amountRead > 0)
        throw std::runtime_error("Could not execvp: " + strError(err));

    // Convert `outputPipes[0]` to a FILE* and start reading from it
    std::FILE* pipeRead = posix::fdopen(outputPipes[0], "rb");
    char outputBuffer[1024];
    while (fgets(outputBuffer, 1024, pipeRead) != nullptr)
        output += outputBuffer;
    fclose(pipeRead);

    // Wait for the process to exit
    int status;
    posix::waitpid(pid, &status, 0);
    if (WIFEXITED(status))
        exitCode = WEXITSTATUS(status);
    else
        exitCode = 1;
#endif
    if (exitCode != 0)
        throw std::runtime_error("g++ failed. Output:\n" + output);
    return output;
}

std::string getObjectDirectory()
{
    std::string result = SettingsManager::getSingleton().get("manager.PatchCompiler.objectsPath");
    if (posix::mkdir(result.c_str()) != 0)
        if (errno != EEXIST)
            throw std::runtime_error(strError(errno));
    return result + "/";
}

std::string getCXXFLAGS()
{
    return "-m32 -std=gnu++11 -I\"" + SettingsManager::getSingleton().get("manager.PatchCompiler.includePath") + "\"";
}

std::string getLDFLAGS()
{
    return "-m32 -L\"" + SettingsManager::getSingleton().get("manager.PatchCompiler.libraryPath") + "\" -lcore";
}

std::string getCustomCXXFLAGS()
{
    return SettingsManager::getSingleton().get("manager.PatchCompiler.customCXXFLAGS");
}

std::string getCustomLDFLAGS()
{
    return SettingsManager::getSingleton().get("manager.PatchCompiler.customLDFLAGS");
}

}

}
