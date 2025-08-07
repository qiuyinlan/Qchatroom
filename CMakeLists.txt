cmake_minimum_required(VERSION 3.10)

project(MessengerRebuild CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加 nlohmann/json 依赖
find_package(nlohmann_json 3.2.0 REQUIRED)

#加上这个可以直接#include "User.h"而不用#include "../utils/User.h"
include_directories(
    server
    client
    client/controller
    client/social
    client/service
    utils
)

add_executable(
        server.out
        server/server.cc
        server/LoginHandler.cc
        server/Transaction.cc
        server/Redis.cc
        server/group_chat.cc
        server/Threadpool.cpp
        server/tool.cc
        utils/User.cc
        utils/TCP.cc
        utils/IO.cc
        utils/proto.cc
        utils/Group.cc
        client/service/Notifications.cc

)

add_executable(
        client.out
        client/client.cc
        client/controller/StartMenu.cc
        client/controller/OperationMenu.cc
        client/service/FileTransfer.cc
        client/service/Notifications.cc
        client/social/FriendManager.cc
  
        client/social/chat.cc
        client/social/G_chat.cc
        utils/User.cc
        utils/TCP.cc
        utils/IO.cc
        utils/proto.cc
        utils/Group.cc
)

add_executable(
        test.out
        utils/User.cc
        utils/Group.cc
        server/test.cc
)

target_link_libraries(
        server.out PRIVATE curl
        nlohmann_json::nlohmann_json
        hiredis
)

target_link_libraries(
        client.out
        nlohmann_json::nlohmann_json
)

target_link_libraries(
        test.out
        nlohmann_json::nlohmann_json
)