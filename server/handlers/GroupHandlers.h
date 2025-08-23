#ifndef GROUPHANDLERS_H
#define GROUPHANDLERS_H

#include "nlohmann/json.hpp"

// Function declarations for group-related business logic
void handleCreateGroupRequest(int epfd, int fd, const nlohmann::json& msg);
void handleJoinGroupRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetManagedGroupsRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetGroupJoinRequests(int epfd, int fd, const nlohmann::json& msg);
void handleRespondToGroupJoinRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetGroupMembersRequest(int epfd, int fd, const nlohmann::json& msg);
void handleKickGroupMemberRequest(int epfd, int fd, const nlohmann::json& msg);
void handleAppointAdminRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetGroupAdminsRequest(int epfd, int fd, const nlohmann::json& msg);
void handleRevokeAdminRequest(int epfd, int fd, const nlohmann::json& msg);
void handleDeleteGroupRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGetJoinedGroupsRequest(int epfd, int fd, const nlohmann::json& msg);
void handleQuitGroupRequest(int epfd, int fd, const nlohmann::json& msg);
void handleInviteToGroupRequest(int epfd, int fd, const nlohmann::json& msg);
void handleStartGroupChatRequest(int epfd, int fd, const nlohmann::json& msg);
void handleGroupMessage(int epfd, int fd, const nlohmann::json& msg);

#endif // GROUPHANDLERS_H

