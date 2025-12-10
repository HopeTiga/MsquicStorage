#pragma once
#include <memory>
#include <boost/json.hpp>

namespace hope {

	namespace quic {

		class MsquicSocket;
		class MsquicManager;

		class MsquicData {

		public:

			MsquicData(boost::json::object json, std::shared_ptr<MsquicSocket> msquicSocket, MsquicManager* msquicManager);

			std::shared_ptr<MsquicSocket> msquicSocket;

			boost::json::object json;

			MsquicManager* msquicManager;

		};
	}
	
}

