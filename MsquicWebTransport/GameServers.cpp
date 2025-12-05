#include "GameServers.h"
#include <boost/mysql/field_view.hpp>
#include <boost/mysql/datetime.hpp>
#include <stdexcept>
#include <string>

namespace hope {
    namespace entity {

        GameServers::GameServers(const boost::mysql::row_view& row) {
            fromRow(row);
        }

        void GameServers::fromRow(const boost::mysql::row_view& row) {
            if (row.size() < 15) {
                throw std::runtime_error("Row does not contain enough columns for GameServers (expected 15)");
            }

            const char* column_names[] = {
                "server_id", "ip_address", "name", "hostname", "location",
                "specifications", "max_processes", "current_processes", "status",
                "region", "tags", "last_heartbeat", "created_at", "updated_at", "del_flag"
            };

            size_t idx = 0;
            try {
                auto to_opt_str = [&](size_t i) -> std::optional<std::string> {
                    return row.at(i).is_null() ? std::nullopt : std::make_optional<std::string>(std::string(row.at(i).as_string()));
                    };

                auto to_opt_json = [&](size_t i) -> std::optional<std::string> {
                    return row.at(i).is_null() ? std::make_optional<std::string>("{}")
                        : std::make_optional<std::string>(std::string(row.at(i).as_string()));
                    };

                // 0-2
                server_id = row.at(idx++).as_string();
                ip_address = row.at(idx++).as_string();
                name = row.at(idx++).as_string();

                // 3-5
                hostname = to_opt_str(idx++);
                location = to_opt_str(idx++);
                specifications = to_opt_json(idx++);

                // 6-8
                max_processes = row.at(idx).is_null() ? 10 : static_cast<int>(row.at(idx++).as_int64());
                current_processes = row.at(idx).is_null() ? 0 : static_cast<int>(row.at(idx++).as_int64());
                status = row.at(idx).is_null() ? "online" : row.at(idx++).as_string();

                // 9-10
                region = to_opt_str(idx++);
                tags = to_opt_json(idx++);

                // 11. last_heartbeat (DATETIME, NULLABLE)
                last_heartbeat = row.at(idx).is_null()
                    ? std::nullopt
                    : std::make_optional(row.at(idx).as_datetime());
                ++idx;

                // 12. created_at (DATETIME, NOT NULL)
                created_at = row.at(idx).is_null()
                    ? boost::mysql::datetime::now()
                    : row.at(idx).as_datetime();
                ++idx;

                // 13. updated_at
                updated_at = row.at(idx).is_null()
                    ? boost::mysql::datetime::now()
                    : row.at(idx).as_datetime();
                ++idx;

                // 14. del_flag
                del_flag = row.at(idx).is_null() ? 0 : static_cast<int>(row.at(idx++).as_int64());
            }
            catch (const std::exception& e) {
                throw std::runtime_error(
                    std::string("Error mapping GameServers at column ") +
                    std::to_string(idx) + " (" + column_names[idx] + "): " + e.what()
                );
            }
        }

    } // namespace entity
} // namespace hope