#pragma once

namespace httplib {
class Server;
}

namespace marketsim::gateway {
class SimulatorService;
}

namespace marketsim::gateway {

/// Binds REST routes on @p svr; thread ownership is the caller's (typically one `listen` thread).
void register_http_api(httplib::Server& svr, SimulatorService& svc);

}
