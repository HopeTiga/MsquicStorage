#include "GameProcesses.h"
#include <boost/mysql/field_view.hpp>
#include <boost/mysql/datetime.hpp>
#include <stdexcept>
#include <cstdio>

namespace hope {
    namespace entity {

        void GameProcesses::fromRow(const boost::mysql::row_view& row) {
            if (row.size() < 15) {
                throw std::runtime_error("Row does not contain enough columns for GameProcesses (expected 15)");
            }

            const char* column_names[] = {
                "process_id", "server_id", "process_name", "game_type", "game_version",
                "is_idle", "startup_parameters", "health_status",
                "last_health_check", "last_heartbeat", "started_at",
                "created_at", "updated_at", "del_flag", "is_login"
            };

            size_t idx = 0;
            try {
         
                auto str = [&](size_t i) -> std::string {
                    return row.at(i).is_null() ? "" : std::string(row.at(i).as_string());
                    };

             
                auto dt_opt = [&](size_t i) -> std::optional<boost::mysql::datetime> {
                    return row.at(i).is_null() ? std::nullopt : std::make_optional(row.at(i).as_datetime());
                    };

              
                process_id = str(idx++);
                server_id = str(idx++);
                process_name = str(idx++);
                game_type = str(idx++);
                game_version = str(idx++);

    
                is_idle = row.at(idx).is_null() ? true : (row.at(idx++).as_int64() != 0);

         
                startup_parameters = str(idx++);
                health_status = row.at(idx).is_null() ? "healthy" : str(idx++);

                last_health_check = dt_opt(idx++);
                last_heartbeat = dt_opt(idx++);
                started_at = dt_opt(idx++);

    
                created_at = row.at(idx).is_null()
                    ? boost::mysql::datetime::now()
                    : row.at(idx).as_datetime();
                ++idx;

                updated_at = row.at(idx).is_null()
                    ? boost::mysql::datetime::now()
                    : row.at(idx).as_datetime();
                ++idx;

                // 13-14: del_flag, is_login
                del_flag = row.at(idx).is_null() ? 0 : static_cast<int>(row.at(idx++).as_int64());
                is_login = row.at(idx).is_null() ? 0 : static_cast<int>(row.at(idx++).as_int64());
            }
            catch (const std::exception& e) {
                throw std::runtime_error(
                    std::string("Error mapping GameProcesses at column ") +
                    std::to_string(idx) + " (" + column_names[idx] + "): " + e.what()
                );
            }
        }

        void GameProcesses::print() const {
            printf("Process ID: %s\n", process_id.c_str());
            printf("Server ID: %s\n", server_id.c_str());
            printf("Process Name: %s\n", process_name.c_str());
            printf("Game Type: %s, Version: %s\n", game_type.c_str(), game_version.c_str());
            printf("Status: Idle=%d, Health=%s, Login=%d\n", is_idle, health_status.c_str(), is_login);
            printf("Delete Flag: %d\n", del_flag);
        }

    } // namespace entity
} // namespace hope