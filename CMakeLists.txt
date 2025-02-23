CMAKE_MINIMUM_REQUIRED(VERSION 3.21)
if(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

project(CNPY)

set(CMAKE_CXX_STANDARD 23)
include_directories("include")

include(FetchContent)
# Set the third-party directory where external libraries will be stored
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

# Fetch ZLIB
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    SOURCE_DIR ${THIRD_PARTY_DIR}/zlib
)

# Fetch the content
FetchContent_MakeAvailable(zlib)

include_directories(${ZLIB_INCLUDE_DIRS})
add_library(cnpy STATIC "./src/cnpy.cpp")
target_link_libraries(cnpy zlibstatic)

# add_executable(cnpy_example example/main.cpp)
# target_link_libraries(cnpy_example PRIVATE cnpy)