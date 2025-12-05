#include "MsquicData.h"

#include "MsquicManager.h"
#include "MsquicWebTransportSocket.h"

namespace hope {
	namespace quic {

		MsquicData::MsquicData(boost::json::object json, MsquicWebTransportSocket* msquicWebTransportSocket, MsquicManager* msquicManager)
			:json(json)
			, msquicWebTransportSocket(msquicWebTransportSocket)
			, msquicManager(msquicManager) {

		}

	}
}