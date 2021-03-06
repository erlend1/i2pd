#include <thread>
#include <memory>

#include "Daemon.h"

#include "Log.h"
#include "Base.h"
#include "version.h"
#include "Transports.h"
#include "NTCPSession.h"
#include "RouterInfo.h"
#include "RouterContext.h"
#include "Tunnel.h"
#include "NetDb.h"
#include "Garlic.h"
#include "util.h"
#include "Streaming.h"
#include "Destination.h"
#include "HTTPServer.h"
#include "I2PControl.h"
#include "ClientContext.h"
// ssl.h somehow pulls Windows.h stuff that has to go after asio
#include <openssl/ssl.h>

#ifdef USE_UPNP
#include "UPnP.h"
#endif

namespace i2p
{
	namespace util
	{
		class Daemon_Singleton::Daemon_Singleton_Private
		{
		public:
			Daemon_Singleton_Private() {};
			~Daemon_Singleton_Private() {};

			std::unique_ptr<i2p::util::HTTPServer> httpServer;
			std::unique_ptr<i2p::client::I2PControlService> m_I2PControlService;

#ifdef USE_UPNP
			i2p::transport::UPnP m_UPnP;
#endif	
		};

		Daemon_Singleton::Daemon_Singleton() : running(1), d(*new Daemon_Singleton_Private()) {};
		Daemon_Singleton::~Daemon_Singleton() {
			delete &d;
		};

		bool Daemon_Singleton::IsService () const
		{
#ifndef _WIN32
			return i2p::util::config::GetArg("-service", 0);
#else
			return false;
#endif
		}

		bool Daemon_Singleton::init(int argc, char* argv[])
		{
			SSL_library_init ();
			i2p::util::config::OptionParser(argc, argv);
			i2p::context.Init ();

			LogPrint(eLogInfo, "\n\n\n\ni2pd v", VERSION, " starting\n");
			LogPrint(eLogDebug, "data directory: ", i2p::util::filesystem::GetDataDir().string());
			i2p::util::filesystem::ReadConfigFile(i2p::util::config::mapArgs, i2p::util::config::mapMultiArgs);

			isDaemon = i2p::util::config::GetArg("-daemon", 0);
			isLogging = i2p::util::config::GetArg("-log", 1);

			int port = i2p::util::config::GetArg("-port", 0);
			if (port)
				i2p::context.UpdatePort (port);					
			std::string host = i2p::util::config::GetArg("-host", "");
			if (host != "")
				i2p::context.UpdateAddress (boost::asio::ip::address::from_string (host));	

			i2p::context.SetSupportsV6 (i2p::util::config::GetArg("-v6", 0));
			i2p::context.SetFloodfill (i2p::util::config::GetArg("-floodfill", 0));
			auto bandwidth = i2p::util::config::GetArg("-bandwidth", "");
			if (bandwidth.length () > 0)
			{
				if (bandwidth[0] > 'L')
					i2p::context.SetHighBandwidth ();
				else
					i2p::context.SetLowBandwidth ();
			}	

			LogPrint(eLogDebug, "Daemon: CMD parameters:");
			for (int i = 0; i < argc; ++i)
				LogPrint(eLogDebug, i, ":  ", argv[i]);

			return true;
		}
			
		bool Daemon_Singleton::start()
		{
			// initialize log			
			if (isLogging)
			{
				if (isDaemon)
				{
					std::string logfile_path = IsService () ? "/var/log" : i2p::util::filesystem::GetDataDir().string();
#ifndef _WIN32
					logfile_path.append("/i2pd.log");
#else
					logfile_path.append("\\i2pd.log");
#endif
					StartLog (logfile_path);
				} else {
					StartLog (""); // write to stdout
				}
				g_Log->SetLogLevel(i2p::util::config::GetArg("-loglevel", "info"));
			}

			LogPrint(eLogInfo, "Daemon: staring HTTP Server");
			d.httpServer = std::unique_ptr<i2p::util::HTTPServer>(new i2p::util::HTTPServer(i2p::util::config::GetArg("-httpaddress", "127.0.0.1"), i2p::util::config::GetArg("-httpport", 7070)));
			d.httpServer->Start();

			LogPrint(eLogInfo, "Daemon: starting NetDB");
			i2p::data::netdb.Start();

#ifdef USE_UPNP
			LogPrint(eLogInfo, "Daemon: starting UPnP");
			d.m_UPnP.Start ();
#endif			
			LogPrint(eLogInfo, "Daemon: starting Transports");
			i2p::transport::transports.Start();

			LogPrint(eLogInfo, "Daemon: starting Tunnels");
			i2p::tunnel::tunnels.Start();

			LogPrint(eLogInfo, "Daemon: starting Client");
			i2p::client::context.Start ();

			// I2P Control
			int i2pcontrolPort = i2p::util::config::GetArg("-i2pcontrolport", 0);
			if (i2pcontrolPort)
			{
				LogPrint(eLogInfo, "Daemon: starting I2PControl");
				d.m_I2PControlService = std::unique_ptr<i2p::client::I2PControlService>(new i2p::client::I2PControlService (i2p::util::config::GetArg("-i2pcontroladdress", "127.0.0.1"), i2pcontrolPort));
				d.m_I2PControlService->Start ();
			}
			return true;
		}

		bool Daemon_Singleton::stop()
		{
			LogPrint(eLogInfo, "Daemon: shutting down");
			LogPrint(eLogInfo, "Daemon: stopping Client");
			i2p::client::context.Stop();
			LogPrint(eLogInfo, "Daemon: stopping Tunnels");
			i2p::tunnel::tunnels.Stop();
#ifdef USE_UPNP
			LogPrint(eLogInfo, "Daemon: stopping UPnP");
			d.m_UPnP.Stop ();
#endif			
			LogPrint(eLogInfo, "Daemon: stopping Transports");
			i2p::transport::transports.Stop();
			LogPrint(eLogInfo, "Daemon: stopping NetDB");
			i2p::data::netdb.Stop();
			LogPrint(eLogInfo, "Daemon: stopping HTTP Server");
			d.httpServer->Stop();
			d.httpServer = nullptr;
			if (d.m_I2PControlService)
			{
				LogPrint(eLogInfo, "Daemon: stopping I2PControl");
				d.m_I2PControlService->Stop ();
				d.m_I2PControlService = nullptr;
			}	
			StopLog ();

			return true;
		}
	}
}
