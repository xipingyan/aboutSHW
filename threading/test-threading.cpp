/*
 what's the fatest way to synchronize using multi-threading on multi-core CPU, especially on Xeon with lots of cores?
 
https://en.wikipedia.org/wiki/Spinlock
https://stackoverflow.com/questions/38447226/atomicity-of-loads-and-stores-on-x86
https://www.cl.cam.ac.uk/%7Epes20/cpp/cpp0xmappings.html

Retired load instructions whose data sources comes from different places:

XSNP_NONE                : ... were hits in L3 without snoops required                (data is not owned by any other core's local cache)
XSNP_FWD   /XSNP_HITM    : ... were HitM responses from shared L3                     (data was exclusivly/dirty owned by another core's local cache)
XSNP_NO_FWD/XSNP_HIT     : ... were L3 and cross-core snoop hits in on-pkg core cache (data was shared/clean in another core's local cache)

#0:HW_CPU_CYCLES, HW_INSTRUCTIONS, STALLS_TOTAL, XSNP_MISS, XSNP_NO_FWD, XSNP_FWD, XSNP_NONE, 
       470326,          590020,       285717,         0,          93,      156,      3057,  [test_lockfree1_#0] 174.331 us CPU:2.70(GHz) CPI:0.80
       472114,          511319,       300717,         0,          43,       39,      3463,  [test_lockfree1_#5] 173.879 us CPU:2.72(GHz) CPI:0.92
       467990,          557457,       290387,         0,          40,       37,      3307,  [test_lockfree1_#43] 173.855 us CPU:2.69(GHz) CPI:0.84
       461357,          454194,       305761,         0,          41,       31,      3451,  [test_lockfree1_#48] 174.352 us CPU:2.65(GHz) CPI:1.02
       462776,          537337,       292743,         0,          26,       31,      3297,  [test_lockfree1_#14] 174.467 us CPU:2.65(GHz) CPI:0.86
       463005,          503132,       291098,         0,          44,       39,      3094,  [test_lockfree1_#33] 173.812 us CPU:2.66(GHz) CPI:0.92


#0:HW_CPU_CYCLES, HW_INSTRUCTIONS, STALLS_TOTAL, XSNP_MISS, XSNP_NO_FWD, XSNP_FWD, XSNP_NONE, 
       413281,          502461,       220537,         0,           1,     5192,        75,  [test_lockfree2_#0] 147.484 us CPU:2.80(GHz) CPI:0.82
       414851,          926697,        94780,         0,          98,        0,         2,  [test_lockfree2_#44] 148.044 us CPU:2.80(GHz) CPI:0.45
       413825,          924818,        94481,         0,          95,        0,         3,  [test_lockfree2_#42] 147.716 us CPU:2.80(GHz) CPI:0.45
       413522,          964548,        80648,         0,          99,        0,         3,  [test_lockfree2_#38] 147.544 us CPU:2.80(GHz) CPI:0.43
       414174,          916794,        97186,         0,          47,        0,         2,  [test_lockfree2_#14] 147.814 us CPU:2.80(GHz) CPI:0.45
       413361,          956514,        83046,         0,          48,        0,         2,  [test_lockfree2_#37] 147.497 us CPU:2.80(GHz) CPI:0.43
*/
#define JIT_DEBUG
#include "../include/jit.h"
#include "../include/linux_perf.hpp"

#include "omp.h"

thread_local LinuxPerf::PerfEventGroup pevg({
    {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, "HW_CPU_CYCLES"},
    {PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, "HW_INSTRUCTIONS"},
    //{PERF_TYPE_HARDWARE, PERF_COUNT_HW_REF_CPU_CYCLES, "HW_REF_CPU_CYCLES"},

    {PERF_TYPE_RAW, X86_RAW_EVENT(0xa3, 0x04, 0x04),"STALLS_TOTAL"},
    //{PERF_TYPE_RAW, X86_RAW_EVENT(0xa3, 0x06, 0x06),"STALLS_L3_MISS"},
    //{PERF_TYPE_RAW, X86_RAW_EVENT(0xa3, 0x05, 0x05),"STALLS_L2_MISS"},
    //{PERF_TYPE_RAW, X86_RAW_EVENT(0xa6, 0x40, 0x02),"BOUND_ON_STORES"},
    //{PERF_TYPE_RAW, X86_RAW_EVENT(0xa6, 0x21, 0x05), "BOUND_ON_LOADS"},
    //{PERF_TYPE_RAW, X86_RAW_EVENT(0xd0, 0x81, 0x00), "ALL_LOADS"},
    
    {PERF_TYPE_RAW, X86_RAW_EVENT(0xd2, 0x01, 0x00), "XSNP_MISS"},
    {PERF_TYPE_RAW, X86_RAW_EVENT(0xd2, 0x02, 0x00), "XSNP_NO_FWD"},
    {PERF_TYPE_RAW, X86_RAW_EVENT(0xd2, 0x04, 0x00), "XSNP_FWD"},
    {PERF_TYPE_RAW, X86_RAW_EVENT(0xd2, 0x08, 0x00), "XSNP_NONE"},
    
    //{PERF_TYPE_RAW, X86_RAW_EVENT(0xd0, 0x41, 0x00), "SPLIT_LOADS"},
    //{PERF_TYPE_RAW, X86_RAW_EVENT(0xd0, 0x42, 0x00), "SPLIT_STORES"},
});

static int REPEAT = getenv("REPEAT", 100);

// padding between atomics to avoid false-sharing
#define PADDING_MULT 64

struct SyncThreads1 {
    std::vector<std::atomic<uint64_t>> sync_ids; 
    //std::shared_ptr<uint64_t> sync_buff;
    const int nthr;
    SyncThreads1(int nthr) : nthr(nthr), sync_ids(nthr * PADDING_MULT) {
        // leave extra padding space to avoid false-sharing
        // sync_buff = alloc_cache_aligned<uint64_t>(nthr * PADDING_MULT, 0);
    }
    void operator()(int ithr) {
        auto id = ithr * PADDING_MULT;
        sync_ids[id] ++;
        auto expected = sync_ids[id].load();


        // N-producer vs N-consumer, optimized cache would finally put the data in L3, and most access would be XSNP_NONE
        // and the load event number is (nthr) for each sync of each worker thread.
        for(int i = 0; i < nthr; i++) {
            // some threads finished sync_threads() faster and may have
            // started & finished next task & added its sync index.
            // but it cannot goes further until current thread also reach that point.
            for(;;) {
                auto s = sync_ids[i*PADDING_MULT].load();
                if (s == expected || s == (expected + 1))
                    break;
            }
        }
    }
};

struct SyncThreads2 {
    std::vector<std::atomic<uint64_t>> sync_ids;
    const int nthr;
    SyncThreads2(int nthr) : nthr(nthr), sync_ids(nthr * PADDING_MULT) {
    }
    void operator()(int ithr) {
        auto id = ithr * PADDING_MULT;
        if (ithr == 0) {
            auto expected = sync_ids[id].load() + 1;
            // main core, load & check worker thread/core's sync_id, this 1-producer vs 1-consumer pattern
            // would generate XSNP_FWD (cross-core L2 cache read) events on main-core (x nthr)
            for(int i = 1; i < nthr; i++) {
                for(;;) {
                    auto s = sync_ids[i*PADDING_MULT].load();
                    if (s == expected)
                        break;
                }
            }
            // main core step-forward only after all worker threads have finished.
            sync_ids[id] ++;
        } else {
            sync_ids[id] ++;
            auto expected = sync_ids[id].load();
            // worker threads, load & check main-core's sync id, this 1-producer vs N-consumer pattern
            // would generate XSNP_NO_FWD (data was shared/clean in another core's local cache)
            // this design decreases the number of cross-core loads for worker threads, since they
            // don't need to check all other worker's status.
            while(sync_ids[0].load() != expected);
        }
    }
};

SyncThreads1 sync_threads1(omp_get_max_threads());
SyncThreads2 sync_threads2(omp_get_max_threads());

void test_openmp() {
    thread_local volatile int a = 0;
    for(int i = 0; i < REPEAT; i++) {
        #pragma omp parallel
        {
            // do some tiny workload
            if (a > 0) printf("not possible\n");
        }
    }
}


void test_lockfree1(bool show_detailed_pmc) {
    thread_local volatile int a = 0;
    auto nthr = omp_get_max_threads();
    auto test_body = [&](int ithr){
        for(int i = 0; i < REPEAT; i++) {
            // do some tiny workload
            if (a > 0) printf("not possible\n");
            sync_threads1(ithr);
        }
    };

    #pragma omp parallel
    {
        int ithr = omp_get_thread_num();
        if (show_detailed_pmc && ithr < 8) {
            std::string name = std::to_string(ithr);
            pevg.rdpmc([&](){ test_body(ithr); }, name);
        } else {
            test_body(ithr);
        }
    }
}

void test_lockfree2(bool show_detailed_pmc) {
    thread_local volatile int a = 0;
    auto nthr = omp_get_max_threads();

    auto test_body = [&](int ithr){
        for(int i = 0; i < REPEAT; i++) {
            // do some tiny workload
            if (a > 0) printf("not possible\n");
            sync_threads2(ithr);
        }
    };

    #pragma omp parallel
    {
        int ithr = omp_get_thread_num();
        if (show_detailed_pmc && ithr < 8) {
            std::string name = std::to_string(ithr);
            pevg.rdpmc([&](){ test_body(ithr); }, name);
        } else {
            test_body(ithr);
        }
    }
}

#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>
#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)
#endif

struct ThreadPool {
    std::vector<std::atomic<uint64_t>> sync_ids;
    const int nthr;

    std::vector< std::thread > workers;
    std::atomic<std::function<void(int ithr, int nthr)>*> p_task;

    struct CPUAffinity {
        cpu_set_t cpu_mask;
        std::vector<int> cpu_ids;
        CPUAffinity() {
            CPU_ZERO(&cpu_mask);
            if (sched_getaffinity(getpid(), sizeof(cpu_set_t), &cpu_mask)) {
                perror(LINUX_PERF_"sched_getaffinity failed:");
                abort();
            }
            for (int cpu = 0; cpu < (int)sizeof(cpu_set_t)*8; cpu++)
                if(CPU_ISSET(cpu, &cpu_mask))
                    cpu_ids.push_back(cpu);
        }
        ~CPUAffinity() {
            // recover affinity of main thread
            if (sched_setaffinity(getpid(), sizeof(cpu_mask), &cpu_mask) == -1) {
                perror("sched_setaffinity");
                abort();
            }
        }
        cpu_set_t set(int ithr) {
            auto ttid = gettid();
            cpu_set_t org_set;
            if (sched_getaffinity(ttid, sizeof(org_set), &org_set)) {
                perror(LINUX_PERF_"sched_getaffinity failed:");
                abort();
            }
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(cpu_ids[ithr], &set);
            if (sched_setaffinity(ttid, sizeof(set), &set) == -1) {
                perror("sched_setaffinity");
                abort();
            }
            return org_set;
        }
    } cpu_affinity;
    

    ThreadPool(int nthr) : nthr(nthr), sync_ids(nthr * PADDING_MULT) {
        cpu_affinity.set(0);

        // main threads is also a worker thread
        // start other worker threads
        for(int ithr = 1; ithr < nthr; ++ithr) {
            workers.emplace_back(
                [this, ithr, nthr]
                {
                    cpu_affinity.set(ithr);
                    uint64_t expected_main_id = 0;
                    auto id = ithr * PADDING_MULT;
                    for(;;)
                    {
                        // worker threads, check main-core's sync id, this 1-producer vs N-consumer pattern
                        // would be optimized and much faster.
                        expected_main_id ++;
                        while(sync_ids[0].load() != expected_main_id);

                        if (p_task == nullptr)
                            break;
                        // wait for 
                        (*p_task)(ithr, nthr);

                        sync_ids[id] ++;
                    }
                }
            );
        }
    }
    ~ThreadPool() {
        p_task.store(nullptr); // std::atomic_thread_fence(std::memory_order_seq_cst); // mfence
        sync_ids[0] ++;
        for(int ithr = 1; ithr < nthr; ++ithr) {
            workers[ithr-1].join();
        }
    }

    void run(std::function<void(int ithr, int nthr)>& task) {
        p_task.store(&task);
        // main core step-forward only after all worker threads have finished
        sync_ids[0] ++;

        // start next work
        (*p_task)(0, nthr);

        // wait all other worker thread to finish.
        auto expected = sync_ids[0].load();
        // main core, check other core's sync_id, this 1-producer vs 1-consumer pattern
        // would generate XSNP_FWD (cross-core L2 cache read) events on main-core
        for(int i = 1; i < nthr; i++) {
            for(;;) {
                auto s = sync_ids[i*PADDING_MULT].load();
                if (s == expected)
                    break;
            }
        }
    }
};

void test_threadpool(ThreadPool& tpool, bool show_detailed_pmc) {
    thread_local volatile int a = 0;
    auto nthr = omp_get_max_threads();

    std::function<void(int ithr, int nthr)> task = [](int ithr, int nthr) {
        if (ithr < 0) printf("not possible\n");
    };

    auto test_body = [&](){
        for(int i = 0; i < REPEAT; i++) {
            tpool.run(task);
        }
    };

    if (show_detailed_pmc) {
        pevg.rdpmc([&](){ test_body(); }, "x");
    } else {
        test_body();
    }
}

static int ROUNDS = getenv("ROUNDS", 10);

int main() {
    pevg.show_header();
    printf("========\n");
    for(int i = 0; i < ROUNDS; i++) pevg.rdpmc([&]() { test_openmp(); }, "test_openmp", REPEAT);
    printf("========\n");
    for(int i = 0; i < ROUNDS; i++) pevg.rdpmc([&]() { test_lockfree1(i == ROUNDS-1); }, "test_lockfree1", REPEAT);
    printf("========\n");
    for(int i = 0; i < ROUNDS; i++) pevg.rdpmc([&]() { test_lockfree2(i == ROUNDS-1); }, "test_lockfree2", REPEAT);
    printf("========\n");
    ThreadPool tpool(omp_get_max_threads());
    for(int i = 0; i < ROUNDS; i++) pevg.rdpmc([&]() { test_threadpool(tpool, i == ROUNDS-1); }, "test_threadpool", REPEAT);
    return 0;
}