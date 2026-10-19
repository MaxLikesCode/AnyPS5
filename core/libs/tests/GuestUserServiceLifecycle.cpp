#include "SceTypes.hpp"
#include "prx/libSceUserService/UserService.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

extern "C" {
int APS5_VABI sceUserServiceInitialize(const UserServiceInitializeParams* params);
int APS5_VABI sceUserServiceInitialize2(int32_t thread_priority);
int APS5_VABI sceUserServiceTerminate(void);
int APS5_VABI sceUserServiceGetLoginUserIdList(UserServiceLoginUserIdList* user_id_list);
int APS5_VABI sceUserServiceGetEvent(SceUserServiceEvent* event);
}

namespace {

void Require(bool value) { if (!value) std::abort(); }

void RequireNotInitialized() {
    UserServiceLoginUserIdList list{};
    Require(sceUserServiceGetLoginUserIdList(&list) == USER_SERVICE_ERROR_NOT_INITIALIZED);
    SceUserServiceEvent event{};
    std::memset(&event, 0xAA, sizeof(event));
    Require(sceUserServiceGetEvent(&event) == USER_SERVICE_ERROR_NOT_INITIALIZED);
    Require(event.user_id == static_cast<int>(0xAAAAAAAAu));
    Require(sceUserServiceGetEvent(nullptr) == USER_SERVICE_ERROR_NOT_INITIALIZED);
}

void RequireLoginOnce() {
    SceUserServiceEvent event{};
    Require(sceUserServiceGetEvent(&event) == USER_SERVICE_OK);
    Require(event.event_type == 0 && event.user_id == USER_SERVICE_INITIAL_USER_ID);
    Require(sceUserServiceGetEvent(&event) == USER_SERVICE_ERROR_NO_EVENT);
}

void RequireRejectedPriorities() {
    for (const std::int32_t priority : {0, 255, 768, -1, INT32_MIN, INT32_MAX}) {
        const UserServiceInitializeParams params{priority};
        Require(sceUserServiceInitialize(&params) == USER_SERVICE_ERROR_INVALID_ARGUMENT);
        Require(sceUserServiceInitialize2(priority) == USER_SERVICE_ERROR_INVALID_ARGUMENT);
    }
}

void RequireAcceptedPrioritiesAlreadyInitialized() {
    for (const std::int32_t priority : {256, 700, 767}) {
        const UserServiceInitializeParams params{priority};
        Require(sceUserServiceInitialize(&params) == USER_SERVICE_ERROR_ALREADY_INITIALIZED);
        Require(sceUserServiceInitialize2(priority) == USER_SERVICE_ERROR_ALREADY_INITIALIZED);
    }
}

}

int main() {
    RequireNotInitialized();
    Require(sceUserServiceTerminate() == USER_SERVICE_ERROR_NOT_INITIALIZED);
    RequireRejectedPriorities();
    RequireNotInitialized();

    Require(sceUserServiceInitialize(nullptr) == USER_SERVICE_OK);
    Require(sceUserServiceInitialize(nullptr) == USER_SERVICE_ERROR_ALREADY_INITIALIZED);
    RequireRejectedPriorities();
    RequireAcceptedPrioritiesAlreadyInitialized();
    UserServiceLoginUserIdList list{};
    Require(sceUserServiceGetLoginUserIdList(&list) == USER_SERVICE_OK);
    Require(list.user_id[0] == USER_SERVICE_INITIAL_USER_ID);
    RequireLoginOnce();

    Require(sceUserServiceTerminate() == USER_SERVICE_OK);
    Require(sceUserServiceTerminate() == USER_SERVICE_ERROR_NOT_INITIALIZED);
    RequireNotInitialized();

    Require(sceUserServiceInitialize(nullptr) == USER_SERVICE_OK);
    RequireLoginOnce();
    Require(sceUserServiceTerminate() == USER_SERVICE_OK);

    Require(sceUserServiceInitialize2(256) == USER_SERVICE_OK);
    RequireLoginOnce();
    RequireAcceptedPrioritiesAlreadyInitialized();
    Require(sceUserServiceTerminate() == USER_SERVICE_OK);

    const UserServiceInitializeParams highest{767};
    Require(sceUserServiceInitialize(&highest) == USER_SERVICE_OK);
    RequireLoginOnce();
    Require(sceUserServiceTerminate() == USER_SERVICE_OK);
}
