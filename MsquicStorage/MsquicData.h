#pragma once
#include <memory>
#include <boost/json.hpp>

namespace hope {

	namespace quic {

		class MsquicSocketInterface;

		class MsquicManager;

		class MsquicData {

		public:

			MsquicData(boost::json::object json, std::shared_ptr<MsquicSocketInterface> msquicSocketInterface, MsquicManager* msquicManager);

			std::shared_ptr<MsquicSocketInterface> msquicSocketInterface;

			boost::json::object json;

			MsquicManager* msquicManager;

		};
	}
	
}

