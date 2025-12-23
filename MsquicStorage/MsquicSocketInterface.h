#pragma once

namespace hope {

	namespace quic {

		enum class SocketType {

			MsquicSocket = 0,

			WebSocket = 1,

		};
	
		class MsquicSocketInterface
		{
		public:

			MsquicSocketInterface() = default;

			virtual ~MsquicSocketInterface() = default;

			virtual void runEventLoop() = 0;

			virtual void writeAsync(unsigned char* data, size_t size) = 0;

			virtual void clear() = 0;

			virtual SocketType getType() = 0;

		};


	}

}
