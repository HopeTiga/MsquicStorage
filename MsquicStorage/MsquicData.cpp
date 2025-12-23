#include "MsquicData.h"

#include "MsquicSocketInterface.h"
#include "MsquicManager.h"

namespace hope {
	namespace quic {

		MsquicData::MsquicData(boost::json::object json, std::shared_ptr<MsquicSocketInterface> msquicSocketInterface, MsquicManager* msquicManager)
			: json(json)
			, msquicSocketInterface(msquicSocketInterface)
			, msquicManager(msquicManager) {

		}

	}
}