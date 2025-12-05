#pragma once
#include <boost/mysql/row_view.hpp>
#include <boost/mysql/datetime.hpp>  
#include <string>
#include <optional>

namespace hope {
    namespace entity {

        class GameProcesses {
        public:
            GameProcesses() : is_idle(true), del_flag(0), is_login(0) {}
            explicit GameProcesses(const boost::mysql::row_view& row) { fromRow(row); }

        private:
            void fromRow(const boost::mysql::row_view& row);

        public:
            std::string process_id;
            std::string server_id;
            std::string process_name;
            std::string game_type;
            std::string game_version;
            bool is_idle;
            std::string startup_parameters;
            std::string health_status;


            std::optional<boost::mysql::datetime> last_health_check;
            std::optional<boost::mysql::datetime> last_heartbeat;
            std::optional<boost::mysql::datetime> started_at;
            boost::mysql::datetime created_at;
            boost::mysql::datetime updated_at;

            int del_flag;
            int is_login;

            void print() const;
            bool isHealthy() const { return health_status == "healthy"; }
            bool isAvailable() const { return is_idle && isHealthy() && del_flag == 0 && is_login == 0; }
        };

    } // namespace entity
} // namespace hope