cmake_minimum_required(VERSION 3.21)

project(sugarcane-ai)

FILE(GLOB_RECURSE BASE_CODES CONFIGURE_DEPENDS src/base/*.h src/base/*.cpp)
FILE(GLOB_RECURSE LIB_CODES CONFIGURE_DEPENDS src/lib/*.h)
FILE(GLOB_RECURSE TEEWORLDS_MAP_CODES CONFIGURE_DEPENDS src/teeworlds/map/*.h src/teeworlds/map/*.cpp)
FILE(GLOB_RECURSE TEEWORLDS_CODES CONFIGURE_DEPENDS src/teeworlds/six/*.h src/teeworlds/six/*.cpp)
FILE(GLOB_RECURSE AI_CODES CONFIGURE_DEPENDS
    src/sugarcane/sugarcane-talk.cpp
    src/sugarcane/sugarcane.cpp
    src/sugarcane/sugarcane.h
    src/teeworlds/sugarcane.cpp
)

set(CMAKE_CXX_STANDARD 20)

find_package(rapidfuzz REQUIRED)
find_package(ZLIB REQUIRED)
add_executable(${PROJECT_NAME} ${BASE_CODES} ${LIB_CODES} ${TEEWORLDS_MAP_CODES} ${TEEWORLDS_CODES} ${AI_CODES})

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_BINARY_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE rapidfuzz::rapidfuzz)
target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)
