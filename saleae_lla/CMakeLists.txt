cmake_minimum_required(VERSION 3.13)
project(auxbus_lla)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Download Saleae Analyzer SDK
include(FetchContent)
FetchContent_Declare(
    AnalyzerSDK
    GIT_REPOSITORY https://github.com/saleae/AnalyzerSDK.git
    GIT_TAG master
)
FetchContent_MakeAvailable(AnalyzerSDK)

# SDK has no CMakeLists.txt — load its targets manually
include(${analyzersdk_SOURCE_DIR}/AnalyzerSDKConfig.cmake)

add_library(auxbus_lla SHARED
    src/AUXBUSAnalyzer.cpp
    src/AUXBUSAnalyzerSettings.cpp
    src/AUXBUSAnalyzerResults.cpp
)

target_compile_definitions(auxbus_lla PRIVATE LOGIC2)
target_link_libraries(auxbus_lla PRIVATE Saleae::AnalyzerSDK)
