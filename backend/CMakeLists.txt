cmake_minimum_required(VERSION 3.16)

project(ab_experiments_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(userver COMPONENTS core postgresql REQUIRED)

add_library(${PROJECT_NAME}_objs OBJECT
    src/handlers/health_handler.cpp
    src/handlers/register_service_handler.cpp
    src/handlers/login_handler.cpp
    src/handlers/client_services_handler.cpp
    src/handlers/platform_users_handler.cpp
    src/handlers/api_keys_handler.cpp
    src/repositories/client_service_repository.cpp
    src/services/auth_service.cpp
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
)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${PROJECT_NAME}_objs
    userver::postgresql
)
