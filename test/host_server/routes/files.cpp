#include "HostWebServer.h"
#include "network/server/FileRoutes.h"

void registerFileRoutes(HostWebServer& server) { network::mountFileRoutes(server, {}); }
