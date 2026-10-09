/*
 * platform/tests/test_map_connections.c
 *
 * GetMapConnection must handle a map without connections
 * (platform/patches/overworld.c.patch). Maps such as Underwater_SealedChamber
 * have connections == NULL, and GetMapConnection read ->count through it
 * before checking anything: harmless BIOS bytes on the GBA, a NULL
 * dereference on the host. Surfacing with Dive on such a map crashed in
 * SetDiveWarpEmerge.
 */

#include <stdio.h>

#include "global.h"
#include "overworld.h"

int main(void)
{
    static const struct MapConnection sConnection = {
        .direction = CONNECTION_EMERGE, .mapGroup = 1, .mapNum = 2,
    };
    static const struct MapConnections sConnections = { 1, &sConnection };
    static const struct MapConnections sEmpty = { 0, NULL };
    int failed = 0;

    gMapHeader.connections = NULL;
    if (GetMapConnection(CONNECTION_EMERGE) != NULL)
    {
        printf("FAIL: no connections: found one\n");
        failed++;
    }

    gMapHeader.connections = &sEmpty;
    if (GetMapConnection(CONNECTION_EMERGE) != NULL)
    {
        printf("FAIL: empty connection list: found one\n");
        failed++;
    }

    gMapHeader.connections = &sConnections;
    if (GetMapConnection(CONNECTION_EMERGE) != &sConnection)
    {
        printf("FAIL: emerge connection not found\n");
        failed++;
    }
    if (GetMapConnection(CONNECTION_DIVE) != NULL)
    {
        printf("FAIL: found a dive connection that isn't there\n");
        failed++;
    }

    if (!failed)
        printf("ok  : GetMapConnection handles missing connections\n");
    return failed != 0;
}
