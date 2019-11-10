cmake_minimum_required(VERSION 3.14)
project(DiscordPP)
find_package(cpprestsdk REQUIRED)

set(CMAKE_CXX_STANDARD 17)

add_executable(DiscordPP main.cpp plib.hpp)

target_link_libraries(DiscordPP pthread cpr boost_system curl ssl crypto cpprestsdk::cpprest fmt)