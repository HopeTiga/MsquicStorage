#include "MsquicData.h"

#include "MsquicSocket.h"
#include "MsquicManager.h"

namespace hope {
	namespace quic {

		MsquicData::MsquicData(boost::json::object json, MsquicSocket * msquicSocket, MsquicManager* msquicManager)
			:json(json)
			, msquicSocket(msquicSocket)
			, msquicManager(msquicManager) {

		}

	}
}