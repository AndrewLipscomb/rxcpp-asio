cmake_minimum_required(VERSION 2.8.3)
project(sandbox)

set(CMAKE_CXX_FLAGS "-std=c++17 ${CMAKE_CXX_FLAGS}")

find_package(
  Boost
  1.74.0 EXACT
  COMPONENTS
    coroutine
)

## Specify additional locations of header files
## Your package locations should be listed before other locations
include_directories(
  include
  ${Boost_INCLUDE_DIRS}
  RxCpp/Rx/v2/src
)

add_executable(
    ${PROJECT_NAME}
    src/client.cpp
)

target_link_libraries(
    ${PROJECT_NAME}
    Boost::coroutine
)

add_executable(
    client_orig
    src/client_orig.cpp
)

target_link_libraries(
    client_orig
    Boost::coroutine
)


add_executable(
    shitshow
    src/shitshow.cpp
)

target_link_libraries(
    shitshow
    Boost::coroutine
)