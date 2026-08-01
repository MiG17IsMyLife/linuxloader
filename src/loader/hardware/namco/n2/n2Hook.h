#pragma once

/*
 * Every N2 module patches the loaded ELF the same way: look a mangled symbol up
 * through the SymbolResolver, then point MinHook at it.  The helpers live here
 * so n2.cpp, n2Audio.cpp and n2SteeringIo.cpp share one implementation and one
 * set of log messages.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Address of a symbol exported by the game, or NULL when it does not exist.
void *n2ResolveSymbol(const char *name);

// Both return non-zero on success and log why they failed otherwise.  A symbol
// the game does not export is only reported at debug level, since callers hook
// entry points that vary between revisions.
int n2HookSymbol(const char *name, void *replacement);
int n2HookSymbolWithOriginal(const char *name, void *replacement, void **original);

// Activates every hook created so far.  Only needed by callers that install
// after the loader's start-up arming pass, such as hooks on a dlopened module.
int n2ArmHooks(void);

#ifdef __cplusplus
}
#endif
