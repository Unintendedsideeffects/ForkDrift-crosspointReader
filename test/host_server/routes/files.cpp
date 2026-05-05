#include "HostWebServer.h"
#include "network/FileRoutes.h"

void registerFileRoutes(HostWebServer& server) { network::mountFileRoutes(server, {}); }
