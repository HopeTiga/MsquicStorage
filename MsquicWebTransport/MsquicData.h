#pragma once
#include <memory>
#include <boost/json.hpp>

namespace hope {

	namespace quic {

		class MsquicWebTransportSocket;
		class MsquicManager;

		class MsquicData {

		public:

			MsquicData(boost::json::object json, MsquicWebTransportSocket* msquicWebTransportSocket, MsquicManager* msquicManager);

			MsquicWebTransportSocket* msquicWebTransportSocket;

			boost::json::object json;

			MsquicManager* msquicManager;

		};
	}
	
}

