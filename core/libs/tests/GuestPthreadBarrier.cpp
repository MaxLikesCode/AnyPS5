#include "SceTypes.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

struct GuestBarrier;
struct GuestBarrierattr;

extern "C" {
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name);
int APS5_VABI scePthreadJoin(Pthread thread, void** retval);
int APS5_VABI pthread_barrierattr_init_nid_postfix(GuestBarrierattr** attr);
int APS5_VABI pthread_barrierattr_destroy_nid_postfix(GuestBarrierattr** attr);
int APS5_VABI pthread_barrierattr_getpshared_nid_postfix(GuestBarrierattr* const* attr, int* pshared);
int APS5_VABI pthread_barrierattr_setpshared_nid_postfix(GuestBarrierattr** attr, int pshared);
int APS5_VABI pthread_barrier_init_nid_postfix(GuestBarrier** barrier, GuestBarrierattr* const* attr, unsigned count);
int APS5_VABI pthread_barrier_wait_nid_postfix(GuestBarrier** barrier);
int APS5_VABI pthread_barrier_destroy_nid_postfix(GuestBarrier** barrier);
int APS5_VABI scePthreadBarrierattrInit(GuestBarrierattr** attr);
int APS5_VABI scePthreadBarrierattrDestroy(GuestBarrierattr** attr);
int APS5_VABI scePthreadBarrierattrGetpshared(GuestBarrierattr* const* attr, int* pshared);
int APS5_VABI scePthreadBarrierattrSetpshared(GuestBarrierattr** attr, int pshared);
int APS5_VABI scePthreadBarrierInit(GuestBarrier** barrier, GuestBarrierattr* const* attr, unsigned count, const char* name);
int APS5_VABI scePthreadBarrierWait(GuestBarrier** barrier);
int APS5_VABI scePthreadBarrierDestroy(GuestBarrier** barrier);
}

static constexpr int GUEST_EINVAL = 22;
static constexpr int SCE_KERNEL_ERROR_EINVAL = static_cast<int>(0x80020016u);
static constexpr int PROCESS_PRIVATE = 0;
static constexpr int PROCESS_SHARED = 1;
static constexpr int BARRIER_SERIAL_THREAD = -1;
static constexpr unsigned WAITERS = 4;

static void Require(bool value) { if (!value) std::abort(); }

struct Context {
    GuestBarrier* barrier = nullptr;
    std::atomic<int> serial{0};
    std::atomic<int> released{0};
};

static void* APS5_VABI Waiter(void* arg) {
    auto& context = *static_cast<Context*>(arg);
    const int result = pthread_barrier_wait_nid_postfix(&context.barrier);
    if (result == BARRIER_SERIAL_THREAD) ++context.serial;
    else if (result == 0) ++context.released;
    else std::abort();
    return nullptr;
}

static void* APS5_VABI DestroyingWaiter(void* arg) {
    auto& context = *static_cast<Context*>(arg);
    if (pthread_barrier_wait_nid_postfix(&context.barrier) == BARRIER_SERIAL_THREAD)
        context.serial = pthread_barrier_destroy_nid_postfix(&context.barrier) == 0 && context.barrier == nullptr ? 1 : -1;
    return nullptr;
}

static void* APS5_VABI SceWaiter(void* arg) {
    auto& context = *static_cast<Context*>(arg);
    const int result = scePthreadBarrierWait(&context.barrier);
    if (result == BARRIER_SERIAL_THREAD) ++context.serial;
    else if (result == 0) ++context.released;
    else std::abort();
    return nullptr;
}

static void RunWaiters(Context& context) {
    std::array<Pthread, WAITERS> threads{};
    for (auto& thread : threads) Require(scePthreadCreate(&thread, nullptr, Waiter, &context, nullptr) == 0);
    for (auto& thread : threads) Require(scePthreadJoin(thread, nullptr) == 0);
    Require(context.serial == 1 && context.released == static_cast<int>(WAITERS) - 1);
}

static void SceBarrier() {
    GuestBarrierattr* missing = nullptr;
    int pshared = -1;
    Require(scePthreadBarrierattrInit(nullptr) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadBarrierattrDestroy(nullptr) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadBarrierattrDestroy(&missing) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadBarrierattrGetpshared(nullptr, &pshared) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadBarrierattrGetpshared(&missing, &pshared) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadBarrierattrSetpshared(&missing, PROCESS_PRIVATE) == SCE_KERNEL_ERROR_EINVAL);
    Require(pshared == -1);

    GuestBarrierattr* attr = nullptr;
    Require(scePthreadBarrierattrInit(&attr) == 0 && attr != nullptr);
    Require(scePthreadBarrierattrGetpshared(&attr, &pshared) == 0 && pshared == PROCESS_PRIVATE);
    Require(scePthreadBarrierattrSetpshared(&attr, PROCESS_SHARED) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadBarrierattrSetpshared(&attr, PROCESS_PRIVATE) == 0);

    auto* const untouched = reinterpret_cast<GuestBarrier*>(static_cast<std::uintptr_t>(0xababababababababull));
    GuestBarrier* invalid = untouched;
    Require(scePthreadBarrierInit(nullptr, &attr, 1, "barrier") == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadBarrierInit(&invalid, nullptr, 0, "barrier") == SCE_KERNEL_ERROR_EINVAL && invalid == untouched);
    Require(scePthreadBarrierWait(nullptr) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadBarrierDestroy(nullptr) == SCE_KERNEL_ERROR_EINVAL);

    GuestBarrier* single = nullptr;
    Require(scePthreadBarrierInit(&single, nullptr, 1, "barrier") == 0 && single != nullptr);
    Require(scePthreadBarrierWait(&single) == BARRIER_SERIAL_THREAD);
    Require(scePthreadBarrierWait(&single) == BARRIER_SERIAL_THREAD);
    Require(scePthreadBarrierDestroy(&single) == 0 && single == nullptr);
    Require(scePthreadBarrierDestroy(&single) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadBarrierWait(&single) == SCE_KERNEL_ERROR_EINVAL);

    for (int round = 0; round < 20; ++round) {
        Context context;
        Require(scePthreadBarrierInit(&context.barrier, round % 2 == 0 ? &attr : nullptr, WAITERS, nullptr) == 0);
        std::array<Pthread, WAITERS> threads{};
        for (auto& thread : threads) Require(scePthreadCreate(&thread, nullptr, SceWaiter, &context, nullptr) == 0);
        for (auto& thread : threads) Require(scePthreadJoin(thread, nullptr) == 0);
        Require(context.serial == 1 && context.released == static_cast<int>(WAITERS) - 1);
        Require(scePthreadBarrierDestroy(&context.barrier) == 0 && context.barrier == nullptr);
    }

    Require(scePthreadBarrierattrDestroy(&attr) == 0 && attr == nullptr);
    Require(scePthreadBarrierattrDestroy(&attr) == SCE_KERNEL_ERROR_EINVAL);
}

int main() {
    GuestBarrierattr* missing = nullptr;
    int pshared = -1;
    Require(pthread_barrierattr_init_nid_postfix(nullptr) == GUEST_EINVAL);
    Require(pthread_barrierattr_destroy_nid_postfix(nullptr) == GUEST_EINVAL);
    Require(pthread_barrierattr_destroy_nid_postfix(&missing) == GUEST_EINVAL);
    Require(pthread_barrierattr_getpshared_nid_postfix(nullptr, &pshared) == GUEST_EINVAL);
    Require(pthread_barrierattr_getpshared_nid_postfix(&missing, &pshared) == GUEST_EINVAL);
    Require(pthread_barrierattr_setpshared_nid_postfix(nullptr, PROCESS_PRIVATE) == GUEST_EINVAL);
    Require(pthread_barrierattr_setpshared_nid_postfix(&missing, PROCESS_PRIVATE) == GUEST_EINVAL);
    Require(pshared == -1);

    GuestBarrierattr* attr = nullptr;
    Require(pthread_barrierattr_init_nid_postfix(&attr) == 0 && attr != nullptr);
    Require(pthread_barrierattr_getpshared_nid_postfix(&attr, &pshared) == 0 && pshared == PROCESS_PRIVATE);
    Require(pthread_barrierattr_setpshared_nid_postfix(&attr, 2) == GUEST_EINVAL);
    Require(pthread_barrierattr_setpshared_nid_postfix(&attr, -1) == GUEST_EINVAL);
    Require(pthread_barrierattr_setpshared_nid_postfix(&attr, PROCESS_SHARED) == GUEST_EINVAL);
    pshared = -1;
    Require(pthread_barrierattr_getpshared_nid_postfix(&attr, &pshared) == 0 && pshared == PROCESS_PRIVATE);
    Context afterRejectedShared;
    Require(pthread_barrier_init_nid_postfix(&afterRejectedShared.barrier, &attr, WAITERS) == 0);
    RunWaiters(afterRejectedShared);
    Require(pthread_barrier_destroy_nid_postfix(&afterRejectedShared.barrier) == 0);
    Require(pthread_barrierattr_setpshared_nid_postfix(&attr, PROCESS_PRIVATE) == 0);
    pshared = -1;
    Require(pthread_barrierattr_getpshared_nid_postfix(&attr, &pshared) == 0 && pshared == PROCESS_PRIVATE);
    bool rejected = false;
    try { pthread_barrierattr_getpshared_nid_postfix(&attr, nullptr); }
    catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected);

    GuestBarrier* invalid = nullptr;
    Require(pthread_barrier_init_nid_postfix(nullptr, &attr, 1) == GUEST_EINVAL);
    Require(pthread_barrier_init_nid_postfix(&invalid, &attr, 0) == GUEST_EINVAL && invalid == nullptr);

    Context withAttr;
    Require(pthread_barrier_init_nid_postfix(&withAttr.barrier, &attr, WAITERS) == 0 && withAttr.barrier != nullptr);
    Require(pthread_barrierattr_destroy_nid_postfix(&attr) == 0 && attr == nullptr);
    Require(pthread_barrierattr_destroy_nid_postfix(&attr) == GUEST_EINVAL);
    RunWaiters(withAttr);
    Require(pthread_barrier_destroy_nid_postfix(&withAttr.barrier) == 0 && withAttr.barrier == nullptr);

    Context withEmptyAttr;
    Require(pthread_barrier_init_nid_postfix(&withEmptyAttr.barrier, &missing, WAITERS) == 0);
    RunWaiters(withEmptyAttr);
    Require(pthread_barrier_destroy_nid_postfix(&withEmptyAttr.barrier) == 0);

    Context withoutAttr;
    Require(pthread_barrier_init_nid_postfix(&withoutAttr.barrier, nullptr, WAITERS) == 0);
    RunWaiters(withoutAttr);
    Require(pthread_barrier_destroy_nid_postfix(&withoutAttr.barrier) == 0);

    for (int round = 0; round < 50; ++round) {
        Context destroyed;
        Require(pthread_barrier_init_nid_postfix(&destroyed.barrier, nullptr, WAITERS) == 0);
        std::array<Pthread, WAITERS> threads{};
        for (auto& thread : threads) Require(scePthreadCreate(&thread, nullptr, DestroyingWaiter, &destroyed, nullptr) == 0);
        for (auto& thread : threads) Require(scePthreadJoin(thread, nullptr) == 0);
        Require(destroyed.serial == 1);
    }

    GuestBarrier* single = nullptr;
    Require(pthread_barrierattr_init_nid_postfix(&attr) == 0);
    Require(pthread_barrier_init_nid_postfix(&single, &attr, 1) == 0);
    Require(pthread_barrier_wait_nid_postfix(&single) == BARRIER_SERIAL_THREAD);
    Require(pthread_barrier_wait_nid_postfix(&single) == BARRIER_SERIAL_THREAD);
    Require(pthread_barrier_destroy_nid_postfix(&single) == 0);
    Require(pthread_barrierattr_destroy_nid_postfix(&attr) == 0);

    SceBarrier();
}
