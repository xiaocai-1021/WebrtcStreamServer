cmake_minimum_required(VERSION 3.0)
project (WebrtcStreamServer)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message("Setting build type to 'Debug' as none was specified.")
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo")
endif()
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer" CACHE STRING "Set C++ Compiler Flags" FORCE)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty_dir/include)

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty_dir/lib)

file(GLOB SRC_LIST ./*.cpp)

set(LINK_LIBRARYS avformat avcodec avfilter swresample avutil sdptransform srtp2 ssl crypto spdlog boost_thread dl opus z pthread)

add_executable(${PROJECT_NAME} ${SRC_LIST})

target_link_libraries(${PROJECT_NAME}  ${LINK_LIBRARYS})
