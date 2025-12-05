#pragma once
#include <boost/mysql/row_view.hpp>
#include <boost/mysql/datetime.hpp> 
#include <string>
#include <optional>
#include <cstdint>

namespace hope {
    namespace entity {

        class GameServers {
        public:
            explicit GameServers(const boost::mysql::row_view& row);

        private:
            void fromRow(const boost::mysql::row_view& row);

        public:
            std::string server_id;
            std::string ip_address;
            std::string name;
            std::optional<std::string> hostname;
            std::optional<std::string> location;
            std::optional<std::string> specifications;
            int max_processes;
            int current_processes;
            std::string status;
            std::optional<std::string> region;
            std::optional<std::string> tags;

          
            std::optional<boost::mysql::datetime> last_heartbeat;
            boost::mysql::datetime created_at;
            boost::mysql::datetime updated_at;

            int del_flag;
        };

    } // namespace entity
} // namespace hope