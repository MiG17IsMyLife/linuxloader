#pragma once

#ifdef __cplusplus
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <cstdint>

class ElfLoader;

class SymbolResolver
{
  public:
    static SymbolResolver &GetInstance()
    {
        static SymbolResolver instance;
        return instance;
    }

    SymbolResolver(SymbolResolver const &) = delete;
    void operator=(SymbolResolver const &) = delete;

    // Initialize library search paths
    void InitSearchPaths(const std::string &libraryPathParam, const std::string &gameElfPath);

    // Add a subdirectory relative to the DLL search base (exeDir/ll-deps or libraryPath)
    void AddDllSearchSubPath(const std::string &subPath);

    // Get the DLL search base path (exeDir/ll-deps or libraryPathParam)
    std::string GetDllSearchBase() const { return m_DllSearchBase; }

    // Register a new mapped library (e.g. "openal32.dll" representing "libopenal.so")
    void RegisterLibrary(const std::string &linuxName, const std::string &windowsName);

    // Load a library mapped by RegisterLibrary
    void LoadNeededLibrary(const std::string &linuxName);

    // Resolve a function/symbol from the loaded chain of libraries or internal bridges
    void *ResolveSymbol(const std::string &symbolName, std::string *outModuleName);

    // Resolve a symbol by searching ONLY native loaded shared objects (excludes main EXE).
    // Used for R_386_COPY relocations which must copy from the SO, not the EXE's own BSS.
    void *ResolveSymbolInSharedLibs(const std::string &symbolName);

    // Provide the generated VTable for a class/structure
    void *GetVTable(const std::string &className);
    void RegisterVTable(const std::string &className, void *vtablePtr, void **originalSymbolPtr = nullptr);

    // Register a symbol exported by a dynamically loaded Linux ELF
    void RegisterNativeSymbol(const std::string &symbolName, void *symbolPtr);

    // Global relocation and initialization passes
    bool ProcessAllRelocations();
    void PatchAllSOs();
    bool RunAllInits();

  private:
    SymbolResolver();
    ~SymbolResolver()
    {
    }

    std::unordered_map<std::string, std::string> m_LibraryMap;
    std::unordered_map<std::string, void *> m_VTables;
    std::unordered_map<std::string, void **> m_OriginalSymbolPtrs;
    std::unordered_map<std::string, void *> m_NativeSymbols;
    std::vector<void *> m_LoadedLibraries;        // Stores handles to loaded DLLs
    std::vector<ElfLoader *> m_NativeLoaders;     // Stores loaders for purely native Linux shared objects
    std::vector<std::string> m_LoadedNativeNames; // Tracks already loaded Linux SO file paths
    std::vector<std::pair<uintptr_t, std::string>> m_PendingSOPatches; // Deferred SO patches (base, path)

    // Library search paths
    std::vector<std::string> m_LibrarySearchPaths; // Ordered list of paths to search for libraries

    // Stored paths for late addition of subdirectories
    std::string m_ExeDir;          // Directory of the executable
    std::string m_DllSearchBase;   // Base dll search path (exeDir/ll-deps or libraryPath)
};
#endif

#ifdef __cplusplus
extern "C"
{
#endif
    void *bridgeResolveSymbol(const char *symbolName);
    void bridgeLoadNeededLibrary(const char *filename);
    void EnsureLibGccLoaded();
    void *GetLibGccSymbol(const char *name);
#ifdef __cplusplus
}
#endif