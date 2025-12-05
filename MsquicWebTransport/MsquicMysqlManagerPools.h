#pragma once

#include <memory>
#include <vector>
#include "concurrentqueue.h"

#include "MsquicMysqlManager.h"

namespace hope {

	namespace mysql {
	
		class MsquicMysqlManagerPools : public std::enable_shared_from_this<MsquicMysqlManagerPools>
		{

		public:

			static std::shared_ptr<MsquicMysqlManagerPools> getInstance() {
			
				static std::shared_ptr<MsquicMysqlManagerPools> instance = std::make_shared<MsquicMysqlManagerPools>();

				return instance;
			}

			std::shared_ptr<MsquicMysqlManager> getMysqlManager();

			std::shared_ptr<MsquicMysqlManager> getTransactionMysqlManager();

			void returnTransactionMysqlManager(std::shared_ptr<MsquicMysqlManager> mysqlManager);

			MsquicMysqlManagerPools(size_t size = std::thread::hardware_concurrency());

			~MsquicMysqlManagerPools();

		private:

			std::atomic<size_t> size;

			moodycamel::ConcurrentQueue<std::shared_ptr<MsquicMysqlManager>> transactionMysqlManagers { 1 };

			std::vector<std::shared_ptr<MsquicMysqlManager>> mysqlManagers;

			std::atomic<size_t> loadBalancing{ 0 };

		};

	}

}

