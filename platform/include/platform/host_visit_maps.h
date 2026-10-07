/*
 * platform/include/platform/host_visit_maps.h
 *
 * --visit-maps (PLAN.md, Phase 19c): warp to every map in turn, as the
 * debug menu's warp does, and run some frames on each. See
 * platform/src/host_visit_maps.c.
 */

#ifndef PLATFORM_HOST_VISIT_MAPS_H
#define PLATFORM_HOST_VISIT_MAPS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turns the visit on for map groups "all", "G" or "G-H". Returns false for
 * anything else. */
bool HostVisitMaps_Parse(const char *groups);

/* Once per frame, before the game's frame: warps when it's time, and
 * returns the buttons to press (getting from the title screen into the
 * game, and through battles and text a map starts). Asks the main loop to
 * quit when every map has been visited. */
uint16_t HostVisitMaps_Frame(uint32_t frame);

/* The process exit status: 0 if every map was visited, 1 otherwise. */
int HostVisitMaps_ExitStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_VISIT_MAPS_H */
