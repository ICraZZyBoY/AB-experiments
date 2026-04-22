cmake_minimum_required(VERSION 3.16)

project(ab_experiments_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(userver COMPONENTS core postgresql REQUIRED)

add_library(${PROJECT_NAME}_objs OBJECT
    src/components/config_build_scheduler.cpp
    src/components/metric_calculation_scheduler.cpp
    src/handlers/health_handler.cpp
    src/handlers/register_service_handler.cpp
    src/handlers/login_handler.cpp
    src/handlers/client_services_handler.cpp
    src/handlers/config_versions_handler.cpp
    src/handlers/experiment_metrics_handler.cpp
    src/handlers/experiments_handler.cpp
    src/handlers/features_handler.cpp
    src/handlers/flags_handler.cpp
    src/handlers/metric_results_handler.cpp
    src/handlers/metrics_handler.cpp
    src/handlers/platform_users_handler.cpp
    src/handlers/api_keys_handler.cpp
    src/handlers/runtime_flags_handler.cpp
    src/handlers/runtime_events_handler.cpp
    src/repositories/client_service_repository.cpp
    src/services/auth_service.cpp
    src/services/assignment_service.cpp
    src/services/config_version_service.cpp
    src/services/event_service.cpp
    src/services/metric_calculation_state.cpp
    src/services/metric_service.cpp
    src/services/experiment_service.cpp
    src/services/session_store.cpp
    src/utils/http_utils.cpp
    src/utils/security.cpp
)

target_include_directories(${PROJECT_NAME}_objs PUBLIC src)

target_link_libraries(${PROJECT_NAME}_objs PUBLIC userver::postgresql)

target_compile_options(${PROJECT_NAME}_objs PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -ffunction-sections
    -fdata-sections
)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${PROJECT_NAME}_objs
)

target_link_options(${PROJECT_NAME} PRIVATE
    -Wl,--gc-sections
    -Wl,--as-needed
)
