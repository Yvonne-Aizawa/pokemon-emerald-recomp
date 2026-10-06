/*
 * platform/win32/include/alloca.h
 *
 * MinGW has no <alloca.h> (it declares alloca in <malloc.h>, a name the game's
 * own include/malloc.h shadows). Windows builds only.
 */

#ifndef PLATFORM_WIN32_ALLOCA_H
#define PLATFORM_WIN32_ALLOCA_H

#define alloca(size) __builtin_alloca(size)

#endif /* PLATFORM_WIN32_ALLOCA_H */
