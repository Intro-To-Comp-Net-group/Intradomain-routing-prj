cmake_minimum_required(VERSION 3.15)
project(project3)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(project3
        Event.cc
        Event.h
        global.h
        Link.cc
        Link.h
        Node.cc
        Node.h
        RoutingProtocol.h
        RoutingProtocolImpl.cc
        RoutingProtocolImpl.h
        Simulator.cc
        Simulator.h utils.h AlarmHandler.h)
