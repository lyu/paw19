// Latency: do one communication call and one quiet in every iteration
// Message Rate: do communication calls in a loop, then call quiet once

#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstdint>
#include <ctime>

#include <unistd.h>
#include <shmem.h>
#include <omp.h>


#define TH_SEG_LEN_LOG 20
#define TH_SEG_LEN (1UL << TH_SEG_LEN_LOG)

size_t N_THREADS, SR_BUF_LEN, HEAP_LEN;


void shmem_barrier_all_omp()
{
    #pragma omp barrier
    #pragma omp master
    shmem_barrier_all();
    #pragma omp barrier
}


void stress_test(uint8_t* heap)
{
    const size_t other_pe = (shmem_my_pe() + 1) % 2;

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    uint64_t omp_redu = 0;

    #pragma omp parallel num_threads(N_THREADS)                     \
                         default(none)                              \
                         firstprivate(other_pe, sbuf, rbuf, heap)   \
                         shared(omp_redu, N_THREADS, SR_BUF_LEN, HEAP_LEN, std::cout)
    {
        const size_t tid = omp_get_thread_num();

        sbuf += tid * TH_SEG_LEN;
        rbuf += tid * TH_SEG_LEN;

        timespec t0, t1;

        #ifdef USE_CTX
        shmem_ctx_t ctx;
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx);
        shmem_ctx_quiet(ctx);
        #endif

        for (size_t e = 0; e <= TH_SEG_LEN_LOG; e++) {
            const size_t msg_len = 1UL << e;
            const size_t n_msg   = TH_SEG_LEN >> e;

            uint8_t this_put_pattern, that_put_pattern;
            uint8_t this_get_pattern, that_get_pattern;

            // Determine iteration-specific heap data patterns
            if (shmem_my_pe() == 0) {
                this_put_pattern = 11 + e;
                that_put_pattern = 13 + e;
                this_get_pattern = 17 + e;
                that_get_pattern = 19 + e;
            } else {
                this_put_pattern = 13 + e;
                that_put_pattern = 11 + e;
                this_get_pattern = 19 + e;
                that_get_pattern = 17 + e;
            }

            if (shmem_my_pe() == 0) {
            #pragma omp master
                std::cout << "Starting stress test with message size 2^" << e
                          << " byte(s)\n";
            }

            #pragma omp barrier

            // Stage 1.1:
            // Initialize the entire heap w/ different patterns on different PEs
            auto heap_ptr = heap;

            #pragma omp master
            for (size_t i = 0; i < HEAP_LEN; i++) {
                heap_ptr[i] = i % this_put_pattern;
            }

            shmem_barrier_all_omp();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 1.2:
            // Send this thread's send buffer to the receive buffer of its peer
            // thread on the other PE using blocking puts.
            for (size_t i = 0; i < n_msg; i++) {
                #ifdef USE_CTX
                shmem_ctx_putmem(ctx, rbuf + i * msg_len, sbuf + i * msg_len, msg_len, other_pe);
                #else
                shmem_putmem(rbuf + i * msg_len, sbuf + i * msg_len, msg_len, other_pe);
                #endif
            }

            #ifdef USE_CTX
            shmem_ctx_quiet(ctx);
            #else
            shmem_quiet();
            #endif

            #pragma omp barrier

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all_omp();

            // Stage 1.3:
            // Verify the result and reinitialize the heap
            #pragma omp master
            {
                bool sbuf_ok = true;
                bool rbuf_ok = true;

                for (size_t i = 0; i < SR_BUF_LEN; i++) {
                    // Send buffer must be untouched
                    if (sbuf[i] != i % this_put_pattern) {
                        sbuf_ok = false;
                    }

                    // Receive buffer must has the pattern of the other PE
                    if (rbuf[i] != i % that_put_pattern) {
                        rbuf_ok = false;
                    }
                }

                if (!sbuf_ok) {
                    std::cout << "** ERROR: incorrect sbuf in put test\n";
                }

                if (!rbuf_ok) {
                    std::cout << "** ERROR: incorrect rbuf in put test\n";
                }

                const double T = (t1.tv_sec - t0.tv_sec) * 1000000.0 +
                                 (t1.tv_nsec - t0.tv_nsec) / 1000.0;

                std::cout << "    put test: " << std::setprecision(6)
                          << std::scientific << T << " us\n";

                // Refill the buffer
                for (size_t i = 0; i < HEAP_LEN; i++) {
                    heap_ptr[i] = i % this_get_pattern;
                }
            }

            shmem_barrier_all_omp();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 2.1
            // Fetch the send buffer of this thread's peer on the other PE
            // to its receive buffer using blocking gets.
            for (size_t i = 0; i < n_msg; i++) {
                #ifdef USE_CTX
                shmem_ctx_getmem(ctx, rbuf + i * msg_len, sbuf + i * msg_len, msg_len, other_pe);
                #else
                shmem_getmem(rbuf + i * msg_len, sbuf + i * msg_len, msg_len, other_pe);
                #endif
            }

            #ifdef USE_CTX
            shmem_ctx_quiet(ctx);
            #else
            shmem_quiet();
            #endif

            #pragma omp barrier

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all_omp();

            // Stage 2.2
            // Verify the result
            #pragma omp master
            {
                bool sbuf_ok = true;
                bool rbuf_ok = true;

                for (size_t i = 0; i < SR_BUF_LEN; i++) {
                    // Send buffer must be untouched
                    if (sbuf[i] != i % this_get_pattern) {
                        sbuf_ok = false;
                    }

                    // Receive buffer must has the pattern of the other PE
                    if (rbuf[i] != i % that_get_pattern) {
                        rbuf_ok = false;
                    }
                }

                if (!sbuf_ok) {
                    std::cout << "** ERROR: incorrect sbuf in get test\n";
                }

                if (!rbuf_ok) {
                    std::cout << "** ERROR: incorrect rbuf in get test\n";
                }

                const double T = (t1.tv_sec - t0.tv_sec) * 1000000.0 +
                                 (t1.tv_nsec - t0.tv_nsec) / 1000.0;

                std::cout << "    get test: " << std::setprecision(6)
                          << std::scientific << T << " us\n";
            }

            // Stage 3.1
            // Prepare the target for atomic operations
            auto amo_target = ((uint64_t*)heap) + 1;

            #pragma omp master
            *amo_target = 0;

            shmem_barrier_all_omp();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 3.2
            // All threads increment the same counter on the other PE
            // Result should be: msg_len * n_msg * N_THREADS = SR_BUF_LEN
            for (size_t i = 0; i < n_msg; i++) {
                #ifdef USE_CTX
                shmem_ctx_uint64_atomic_add(ctx, amo_target, msg_len, other_pe);
                #else
                shmem_uint64_atomic_add(amo_target, msg_len, other_pe);
                #endif
            }

            #ifdef USE_CTX
            shmem_ctx_quiet(ctx);
            #else
            shmem_quiet();
            #endif

            #pragma omp barrier

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all_omp();

            // Stage 3.3
            // Verify the result
            #pragma omp master
            {
                if (*amo_target != SR_BUF_LEN) {
                    std::cout << "** ERROR: incorrect result in AMO ADD test"
                              << "\n** Expected: " << SR_BUF_LEN
                              << "\n** Received: " << *amo_target << '\n';
                }

                const double T = (t1.tv_sec - t0.tv_sec) * 1000000.0 +
                                 (t1.tv_nsec - t0.tv_nsec) / 1000.0;

                std::cout << "    amo post test: " << std::setprecision(6)
                          << std::scientific << T << " us\n";
            }

            // Stage 4.1
            // Prepare local buffers for AMO FADD, SWAP and CSWAP
            auto amo_result = ((uint64_t*)rbuf) + 2;
            *amo_result     = 0;

            #pragma omp master
            *amo_target = 0;

            shmem_barrier_all_omp();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 4.2
            // All threads increment the same counter on the other PE using AMO
            // fetch-add
            for (size_t i = 0; i < n_msg; i++) {
                #ifdef USE_CTX
                *amo_result = shmem_ctx_uint64_atomic_fetch_add(ctx, amo_target, msg_len, other_pe);
                #else
                *amo_result = shmem_uint64_atomic_fetch_add(amo_target, msg_len, other_pe);
                #endif
            }

            #ifdef USE_CTX
            shmem_ctx_quiet(ctx);
            #else
            shmem_quiet();
            #endif

            #pragma omp barrier

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all_omp();

            // Stage 4.3
            // Verify the result and prepare for the CSWAP test
            #pragma omp master
            {
                if (*amo_target != SR_BUF_LEN) {
                    std::cout << "** ERROR: incorrect result in AMO FADD test"
                              << "\n** Expected: " << SR_BUF_LEN
                              << "\n** Received: " << *amo_target << '\n';
                }

                const double T = (t1.tv_sec - t0.tv_sec) * 1000000.0 +
                                 (t1.tv_nsec - t0.tv_nsec) / 1000.0;

                std::cout << "    amo fadd test: " << std::setprecision(6)
                          << std::scientific << T << " us\n";

                *amo_target = 0;

                // For the next test we use this for storing the total number
                // of successful CSWAPs
                omp_redu    = 0;
            }

            // Local counter for number of successful CSWAPs
            uint64_t succ_loc  = 0;

            shmem_barrier_all_omp();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 4.4
            // All threads compete against each other to increment the same
            // counter on the other PE using AMO compare-and-swap
            for (size_t i = 0; i < n_msg; i++) {
                *amo_result = i + 1;

                #pragma omp barrier

                #ifdef USE_CTX
                *amo_result = shmem_ctx_uint64_atomic_compare_swap(ctx, amo_target, i, *amo_result, other_pe);
                #else
                *amo_result = shmem_uint64_atomic_compare_swap(amo_target, i, *amo_result, other_pe);
                #endif

                // Did I succeed?
                if (*amo_result == i) {
                    succ_loc++;
                }
            }

            #ifdef USE_CTX
            shmem_ctx_quiet(ctx);
            #else
            shmem_quiet();
            #endif

            #pragma omp barrier

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all_omp();

            // Stage 4.5
            // Verify the result and prepare for the SWAP test
            #pragma omp atomic
            omp_redu += succ_loc;

            #pragma omp barrier

            #pragma omp master
            {
                if ((*amo_target != n_msg) || (omp_redu != n_msg)) {
                    std::cout << "** ERROR: incorrect result in AMO CSWAP test"
                              << "\n** Expected: " << n_msg
                              << "\n** Received: " << *amo_target
                              << "\n** Succeed: " << omp_redu << '\n';
                }

                const double T = (t1.tv_sec - t0.tv_sec) * 1000000.0 +
                                 (t1.tv_nsec - t0.tv_nsec) / 1000.0;

                std::cout << "    amo cswap test: " << std::setprecision(6)
                          << std::scientific << T << " us\n";

                // Assign the sum of the thread IDs to the common AMO target
                *amo_target = (N_THREADS * (N_THREADS - 1)) / 2;

                // For the next test we use this for summing the results
                omp_redu    = 0;
            }

            // Use the thread ID as the local value
            *amo_result = tid;

            shmem_barrier_all_omp();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 4.6
            // All threads swap their local values with the same remote target
            // There is no guarantee where will the sum reside at the end of
            // the loop
            // The sum of the number of times each thread owned the sum is also
            // unpredictable, could be much bigger than n_msg
            for (size_t i = 0; i < n_msg; i++) {
                #ifdef USE_CTX
                *amo_result = shmem_ctx_uint64_atomic_swap(ctx, amo_target, *amo_result, other_pe);
                #else
                *amo_result = shmem_uint64_atomic_swap(amo_target, *amo_result, other_pe);
                #endif
            }

            #ifdef USE_CTX
            shmem_ctx_quiet(ctx);
            #else
            shmem_quiet();
            #endif

            #pragma omp barrier

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all_omp();

            // Stage 4.7
            // Verify the result

            // First, sum all the local values
            #pragma omp atomic
            omp_redu += *amo_result;

            #pragma omp barrier

            #pragma omp master
            {
                // Remember we have a thread ID resting on the other PE
                // I could get it back by a single AMO SWAP, but the code
                // nesting will be too deep
                const uint64_t v = *amo_target;

                shmem_barrier_all();

                shmem_putmem(amo_target, &v, sizeof(uint64_t), other_pe);

                shmem_barrier_all();

                omp_redu += *amo_target;

                // The end result should be twice the sum of all the thread IDs
                if (omp_redu != (N_THREADS * (N_THREADS - 1))) {
                    std::cout << "** ERROR: incorrect result in AMO SWAP test"
                              << "\n** Expected: " << (N_THREADS * (N_THREADS - 1))
                              << "\n** Received: " << omp_redu << '\n';
                }

                const double T = (t1.tv_sec - t0.tv_sec) * 1000000.0 +
                                 (t1.tv_nsec - t0.tv_nsec) / 1000.0;

                std::cout << "    amo swap test: " << std::setprecision(6)
                          << std::scientific << T << " us\n";
            }

            shmem_barrier_all_omp();
        }

        #ifdef USE_CTX
        shmem_ctx_destroy(ctx);
        #endif
    }
}


void bench_put_nbi(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() == 0) && one_way) {
        return;
    }

    const size_t other_pe = (shmem_my_pe() + 1) % 2;

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    double th_post_times[N_THREADS];
    double th_wait_times[N_THREADS];

    if (shmem_my_pe() == 1) {
        if (one_way) {
        std::cout << "Benchmarking unidirectional non-blocking put, "
                  << "time unit microseconds:\n";
        } else {
        std::cout << "Benchmarking bidirectional non-blocking put, "
                  << "time unit microseconds:\n";
        }

        std::cout << std::setw(12) << std::left << "Size (bytes)"
                  << std::setw(16) << std::right << "Min put time"
                  << std::setw(16) << std::right << "Max put time"
                  << std::setw(16) << std::right << "Avg put time"
                  << std::setw(16) << std::right << "Min flush time"
                  << std::setw(16) << std::right << "Max flush time"
                  << std::setw(16) << std::right << "Avg flush time"
                  << '\n';
    }

    #pragma omp parallel num_threads(N_THREADS)                     \
                         default(none)                              \
                         firstprivate(other_pe, sbuf, rbuf, one_way)\
                         shared(th_post_times, th_wait_times, N_THREADS, std::cout)
    {
        const size_t tid = omp_get_thread_num();

        sbuf += tid * TH_SEG_LEN;
        rbuf += tid * TH_SEG_LEN;

        #ifdef USE_CTX
        shmem_ctx_t ctx;
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx);
        shmem_ctx_quiet(ctx);
        #endif

        timespec t0, t1, t2;

        for (size_t e = 0; e <= TH_SEG_LEN_LOG; e++) {
            const size_t msg_len = 1UL << e;

            size_t iter, warm_up;

            if (msg_len < (1UL << 17)) {
                iter = 10000;
                warm_up = iter / 10;
            } else {
                iter = 500;
                warm_up = iter / 10;
            }

            double post_time = 0.0;
            double wait_time = 0.0;

            if (!one_way) {
                shmem_barrier_all_omp();
            }

            #pragma omp barrier

            for (size_t i = 0; i < iter + warm_up; i++) {
                clock_gettime(CLOCK_MONOTONIC, &t0);

                #ifdef USE_CTX
                shmem_ctx_putmem_nbi(ctx, rbuf, sbuf, msg_len, other_pe);
                #else
                shmem_putmem_nbi(rbuf, sbuf, msg_len, other_pe);
                #endif

                clock_gettime(CLOCK_MONOTONIC, &t1);

                #ifdef USE_CTX
                shmem_ctx_quiet(ctx);
                #else
                shmem_quiet();
                #endif

                clock_gettime(CLOCK_MONOTONIC, &t2);

                if (i >= warm_up) {
                    post_time += (t1.tv_sec - t0.tv_sec) * 1000000.0
                               + (t1.tv_nsec - t0.tv_nsec) / 1000.0;
                    wait_time += (t2.tv_sec - t1.tv_sec) * 1000000.0
                               + (t2.tv_nsec - t1.tv_nsec) / 1000.0;
                }
            }

            post_time /= double(iter);
            wait_time /= double(iter);

            th_post_times[tid] = post_time;
            th_wait_times[tid] = wait_time;

            #pragma omp barrier

            // Do statistics
            #pragma omp master
            {
                double min_post_time = th_post_times[0];
                double max_post_time = th_post_times[0];
                double tot_post_time = th_post_times[0];

                double min_wait_time = th_wait_times[0];
                double max_wait_time = th_wait_times[0];
                double tot_wait_time = th_wait_times[0];

                for (size_t i = 1; i < N_THREADS; i++) {
                    if (th_post_times[i] < min_post_time) {
                        min_post_time = th_post_times[i];
                    }

                    if (th_post_times[i] > max_post_time) {
                        max_post_time = th_post_times[i];
                    }

                    tot_post_time += th_post_times[i];

                    if (th_wait_times[i] < min_wait_time) {
                        min_wait_time = th_wait_times[i];
                    }

                    if (th_wait_times[i] > max_wait_time) {
                        max_wait_time = th_wait_times[i];
                    }

                    tot_wait_time += th_wait_times[i];
                }

                const double avg_post_time = tot_post_time / double(N_THREADS);
                const double avg_wait_time = tot_wait_time / double(N_THREADS);

                std::cout << std::fixed << std::setprecision(3)
                          << std::setw(12) << std::left << msg_len
                          << std::setw(16) << std::right << min_post_time
                          << std::setw(16) << std::right << max_post_time
                          << std::setw(16) << std::right << avg_post_time
                          << std::setw(16) << std::right << min_wait_time
                          << std::setw(16) << std::right << max_wait_time
                          << std::setw(16) << std::right << avg_wait_time
                          << '\n';
            }
        }

        #ifdef USE_CTX
        shmem_ctx_destroy(ctx);
        #endif
    }
}


void bench_get_nbi(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() == 0) && one_way) {
        return;
    }

    const size_t other_pe = (shmem_my_pe() + 1) % 2;

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    double th_post_times[N_THREADS];
    double th_wait_times[N_THREADS];

    if (shmem_my_pe() == 1) {
        if (one_way) {
        std::cout << "Benchmarking unidirectional non-blocking get, "
                  << "time unit microseconds:\n";
        } else {
        std::cout << "Benchmarking bidirectional non-blocking get, "
                  << "time unit microseconds:\n";
        }

        std::cout << std::setw(12) << std::left << "Size (bytes)"
                  << std::setw(16) << std::right << "Min put time"
                  << std::setw(16) << std::right << "Max put time"
                  << std::setw(16) << std::right << "Avg put time"
                  << std::setw(16) << std::right << "Min flush time"
                  << std::setw(16) << std::right << "Max flush time"
                  << std::setw(16) << std::right << "Avg flush time"
                  << '\n';
    }

    #pragma omp parallel num_threads(N_THREADS)                     \
                         default(none)                              \
                         firstprivate(other_pe, sbuf, rbuf, one_way)\
                         shared(th_post_times, th_wait_times, N_THREADS, std::cout)
    {
        const size_t tid = omp_get_thread_num();

        sbuf += tid * TH_SEG_LEN;
        rbuf += tid * TH_SEG_LEN;

        #ifdef USE_CTX
        shmem_ctx_t ctx;
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx);
        shmem_ctx_quiet(ctx);
        #endif

        timespec t0, t1, t2;

        for (size_t e = 0; e <= TH_SEG_LEN_LOG; e++) {
            const size_t msg_len = 1UL << e;

            size_t iter, warm_up;

            if (msg_len < (1UL << 17)) {
                iter = 10000;
                warm_up = iter / 10;
            } else {
                iter = 500;
                warm_up = iter / 10;
            }

            double post_time = 0.0;
            double wait_time = 0.0;

            if (!one_way) {
                shmem_barrier_all_omp();
            }

            #pragma omp barrier

            for (size_t i = 0; i < iter + warm_up; i++) {
                clock_gettime(CLOCK_MONOTONIC, &t0);

                #ifdef USE_CTX
                shmem_ctx_getmem_nbi(ctx, rbuf, sbuf, msg_len, other_pe);
                #else
                shmem_getmem_nbi(rbuf, sbuf, msg_len, other_pe);
                #endif

                clock_gettime(CLOCK_MONOTONIC, &t1);

                #ifdef USE_CTX
                shmem_ctx_quiet(ctx);
                #else
                shmem_quiet();
                #endif

                clock_gettime(CLOCK_MONOTONIC, &t2);

                if (i >= warm_up) {
                    post_time += (t1.tv_sec - t0.tv_sec) * 1000000.0
                               + (t1.tv_nsec - t0.tv_nsec) / 1000.0;
                    wait_time += (t2.tv_sec - t1.tv_sec) * 1000000.0
                               + (t2.tv_nsec - t1.tv_nsec) / 1000.0;
                }
            }

            post_time /= double(iter);
            wait_time /= double(iter);

            th_post_times[tid] = post_time;
            th_wait_times[tid] = wait_time;

            #pragma omp barrier

            // Do statistics
            #pragma omp master
            {
                double min_post_time = th_post_times[0];
                double max_post_time = th_post_times[0];
                double tot_post_time = th_post_times[0];

                double min_wait_time = th_wait_times[0];
                double max_wait_time = th_wait_times[0];
                double tot_wait_time = th_wait_times[0];

                for (size_t i = 1; i < N_THREADS; i++) {
                    if (th_post_times[i] < min_post_time) {
                        min_post_time = th_post_times[i];
                    }

                    if (th_post_times[i] > max_post_time) {
                        max_post_time = th_post_times[i];
                    }

                    tot_post_time += th_post_times[i];

                    if (th_wait_times[i] < min_wait_time) {
                        min_wait_time = th_wait_times[i];
                    }

                    if (th_wait_times[i] > max_wait_time) {
                        max_wait_time = th_wait_times[i];
                    }

                    tot_wait_time += th_wait_times[i];
                }

                const double avg_post_time = tot_post_time / double(N_THREADS);
                const double avg_wait_time = tot_wait_time / double(N_THREADS);

                std::cout << std::fixed << std::setprecision(3)
                          << std::setw(12) << std::left << msg_len
                          << std::setw(16) << std::right << min_post_time
                          << std::setw(16) << std::right << max_post_time
                          << std::setw(16) << std::right << avg_post_time
                          << std::setw(16) << std::right << min_wait_time
                          << std::setw(16) << std::right << max_wait_time
                          << std::setw(16) << std::right << avg_wait_time
                          << '\n';
            }
        }

        #ifdef USE_CTX
        shmem_ctx_destroy(ctx);
        #endif
    }
}


void bench_put(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() == 0) && one_way) {
        return;
    }

    const size_t other_pe = (shmem_my_pe() + 1) % 2;

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    double th_times[N_THREADS];

    if (shmem_my_pe() == 1) {
        if (one_way) {
        std::cout << "Benchmarking unidirectional blocking put, "
                  << "time unit microseconds:\n";
        } else {
        std::cout << "Benchmarking bidirectional blocking put, "
                  << "time unit microseconds:\n";
        }

        std::cout << std::setw(12) << std::left << "Size (bytes)"
                  << std::setw(16) << std::right << "Min time"
                  << std::setw(16) << std::right << "Max time"
                  << std::setw(16) << std::right << "Avg time"
                  << '\n';
    }

    #pragma omp parallel num_threads(N_THREADS)                     \
                         default(none)                              \
                         shared(th_times, N_THREADS, std::cout)     \
                         firstprivate(other_pe, sbuf, rbuf, one_way)
    {
        const size_t tid = omp_get_thread_num();

        sbuf += tid * TH_SEG_LEN;
        rbuf += tid * TH_SEG_LEN;

        #ifdef USE_CTX
        shmem_ctx_t ctx;
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx);
        shmem_ctx_quiet(ctx);
        #endif

        timespec t0, t1;

        for (size_t e = 0; e <= TH_SEG_LEN_LOG; e++) {
            const size_t msg_len = 1UL << e;

            size_t iter, warm_up;

            if (msg_len < (1UL << 17)) {
                iter = 10000;
                warm_up = iter / 10;
            } else {
                iter = 500;
                warm_up = iter / 10;
            }

            double put_time = 0.0;

            if (!one_way) {
                shmem_barrier_all_omp();
            }

            size_t offset = 0;

            #pragma omp barrier

            for (size_t i = 0; i < iter + warm_up; i++) {
                if (i == warm_up) {
                    clock_gettime(CLOCK_MONOTONIC, &t0);
                }

                #ifdef USE_CTX
                shmem_ctx_putmem(ctx, rbuf + offset, sbuf + offset, msg_len, other_pe);
                #else
                shmem_putmem(rbuf + offset, sbuf + offset, msg_len, other_pe);
                #endif

                offset += msg_len;
                if ((offset + msg_len) >= TH_SEG_LEN) {
                    offset = 0;
                }
            }

            #ifdef USE_CTX
            shmem_ctx_quiet(ctx);
            #else
            shmem_quiet();
            #endif

            clock_gettime(CLOCK_MONOTONIC, &t1);

            put_time += (t1.tv_sec - t0.tv_sec) * 1000000.0
                      + (t1.tv_nsec - t0.tv_nsec) / 1000.0;

            put_time /= double(iter);

            th_times[tid] = put_time;

            #pragma omp barrier

            // Do statistics
            #pragma omp master
            {
                double min_time = th_times[0];
                double max_time = th_times[0];
                double tot_time = th_times[0];

                for (size_t i = 1; i < N_THREADS; i++) {
                    if (th_times[i] < min_time) {
                        min_time = th_times[i];
                    }

                    if (th_times[i] > max_time) {
                        max_time = th_times[i];
                    }

                    tot_time += th_times[i];
                }

                const double avg_time = tot_time / double(N_THREADS);

                std::cout << std::fixed << std::setprecision(3)
                          << std::setw(12) << std::left << msg_len
                          << std::setw(16) << std::right << min_time
                          << std::setw(16) << std::right << max_time
                          << std::setw(16) << std::right << avg_time
                          << '\n';
            }
        }

        #ifdef USE_CTX
        shmem_ctx_destroy(ctx);
        #endif
    }
}


void bench_get(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() == 0) && one_way) {
        return;
    }

    const size_t other_pe = (shmem_my_pe() + 1) % 2;

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    double th_times[N_THREADS];

    if (shmem_my_pe() == 1) {
        if (one_way) {
        std::cout << "Benchmarking unidirectional blocking get, "
                  << "time unit microseconds:\n";
        } else {
        std::cout << "Benchmarking bidirectional blocking get, "
                  << "time unit microseconds:\n";
        }

        std::cout << std::setw(12) << std::left << "Size (bytes)"
                  << std::setw(16) << std::right << "Min time"
                  << std::setw(16) << std::right << "Max time"
                  << std::setw(16) << std::right << "Avg time"
                  << '\n';
    }

    #pragma omp parallel num_threads(N_THREADS)                     \
                         default(none)                              \
                         shared(th_times, N_THREADS, std::cout)     \
                         firstprivate(other_pe, sbuf, rbuf, one_way)
    {
        const size_t tid = omp_get_thread_num();

        sbuf += tid * TH_SEG_LEN;
        rbuf += tid * TH_SEG_LEN;

        #ifdef USE_CTX
        shmem_ctx_t ctx;
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx);
        shmem_ctx_quiet(ctx);
        #endif

        timespec t0, t1;

        for (size_t e = 0; e <= TH_SEG_LEN_LOG; e++) {
            const size_t msg_len = 1UL << e;

            size_t iter, warm_up;

            if (msg_len < (1UL << 17)) {
                iter = 10000;
                warm_up = iter / 10;
            } else {
                iter = 500;
                warm_up = iter / 10;
            }

            double put_time = 0.0;

            if (!one_way) {
                shmem_barrier_all_omp();
            }

            size_t offset = 0;

            #pragma omp barrier

            for (size_t i = 0; i < iter + warm_up; i++) {
                if (i == warm_up) {
                    clock_gettime(CLOCK_MONOTONIC, &t0);
                }

                #ifdef USE_CTX
                shmem_ctx_getmem(ctx, rbuf + offset, sbuf + offset, msg_len, other_pe);
                #else
                shmem_getmem(rbuf + offset, sbuf + offset, msg_len, other_pe);
                #endif

                offset += msg_len;
                if ((offset + msg_len) >= TH_SEG_LEN) {
                    offset = 0;
                }
            }

            #ifdef USE_CTX
            shmem_ctx_quiet(ctx);
            #else
            shmem_quiet();
            #endif

            clock_gettime(CLOCK_MONOTONIC, &t1);

            put_time += (t1.tv_sec - t0.tv_sec) * 1000000.0
                      + (t1.tv_nsec - t0.tv_nsec) / 1000.0;

            put_time /= double(iter);

            th_times[tid] = put_time;

            #pragma omp barrier

            // Do statistics
            #pragma omp master
            {
                double min_time = th_times[0];
                double max_time = th_times[0];
                double tot_time = th_times[0];

                for (size_t i = 1; i < N_THREADS; i++) {
                    if (th_times[i] < min_time) {
                        min_time = th_times[i];
                    }

                    if (th_times[i] > max_time) {
                        max_time = th_times[i];
                    }

                    tot_time += th_times[i];
                }

                const double avg_time = tot_time / double(N_THREADS);

                std::cout << std::fixed << std::setprecision(3)
                          << std::setw(12) << std::left << msg_len
                          << std::setw(16) << std::right << min_time
                          << std::setw(16) << std::right << max_time
                          << std::setw(16) << std::right << avg_time
                          << '\n';
            }
        }

        #ifdef USE_CTX
        shmem_ctx_destroy(ctx);
        #endif
    }
}


void bench_amo64_post(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() == 0) && one_way) {
        // Prevent active polling from the waituntil in the barrier
        sleep(5);
        return;
    }

    const size_t other_pe = (shmem_my_pe() + 1) % 2;

    auto amo_target = ((uint64_t*)heap) + 1;

    double th_times[N_THREADS];

    if (shmem_my_pe() == 1) {
        if (one_way) {
        std::cout << "Benchmarking unidirectional atomic post, "
                  << "time unit microseconds:\n";
        } else {
        std::cout << "Benchmarking bidirectional atomic post, "
                  << "time unit microseconds:\n";
        }

        std::cout << std::setw(12) << std::left << "N Iterations"
                  << std::setw(16) << std::right << "Min time"
                  << std::setw(16) << std::right << "Max time"
                  << std::setw(16) << std::right << "Avg time"
                  << '\n';
    }

    #pragma omp parallel num_threads(N_THREADS)                 \
                         default(none)                          \
                         shared(th_times, N_THREADS, std::cout) \
                         firstprivate(other_pe, amo_target, one_way)
    {
        const size_t tid = omp_get_thread_num();

        #ifdef USE_CTX
        shmem_ctx_t ctx;
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx);
        shmem_ctx_quiet(ctx);
        #endif

        timespec t0, t1;

        const size_t iter = 100000;
        const size_t warm_up = iter / 10;

        double time = 0.0;

        if (!one_way) {
            shmem_barrier_all_omp();
        }

        #pragma omp barrier

        for (size_t i = 0; i < iter + warm_up; i++) {
            if (i == warm_up) {
                #ifdef USE_CTX
                shmem_ctx_quiet(ctx);
                #else
                shmem_quiet();
                #endif

                #pragma omp barrier

                clock_gettime(CLOCK_MONOTONIC, &t0);
            }

            #ifdef USE_CTX
            shmem_ctx_uint64_atomic_add(ctx, amo_target, 1, other_pe);
            #else
            shmem_uint64_atomic_add(amo_target, 1, other_pe);
            #endif
        }

        #ifdef USE_CTX
        shmem_ctx_quiet(ctx);
        #else
        shmem_quiet();
        #endif

        clock_gettime(CLOCK_MONOTONIC, &t1);

        time += (t1.tv_sec - t0.tv_sec) * 1000000.0
              + (t1.tv_nsec - t0.tv_nsec) / 1000.0;

        time /= double(iter);

        th_times[tid] = time;

        #pragma omp barrier

        #pragma omp master
        {
            double min_time = th_times[0];
            double max_time = th_times[0];
            double tot_time = th_times[0];

            for (size_t i = 1; i < N_THREADS; i++) {
                if (th_times[i] < min_time) {
                    min_time = th_times[i];
                }

                if (th_times[i] > max_time) {
                    max_time = th_times[i];
                }

                tot_time += th_times[i];
            }

            const double avg_time = tot_time / N_THREADS;

            std::cout << std::fixed << std::setprecision(3)
                      << std::setw(12) << std::left << iter
                      << std::setw(16) << std::right << min_time
                      << std::setw(16) << std::right << max_time
                      << std::setw(16) << std::right << avg_time
                      << '\n';
        }

        #ifdef USE_CTX
        shmem_ctx_destroy(ctx);
        #endif
    }
}


void bench_amo64_fetch(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() == 0) && one_way) {
        return;
    }

    const size_t other_pe = (shmem_my_pe() + 1) % 2;

    auto amo_target = ((uint64_t*)heap) + 1;

    double th_times[N_THREADS];

    if (shmem_my_pe() == 1) {
        if (one_way) {
        std::cout << "Benchmarking unidirectional atomic fetch, "
                  << "time unit microseconds:\n";
        } else {
        std::cout << "Benchmarking bidirectional atomic fetch, "
                  << "time unit microseconds:\n";
        }

        std::cout << std::setw(12) << std::left << "N Iterations"
                  << std::setw(16) << std::right << "Min time"
                  << std::setw(16) << std::right << "Max time"
                  << std::setw(16) << std::right << "Avg time"
                  << '\n';
    }

    #pragma omp parallel num_threads(N_THREADS)                 \
                         default(none)                          \
                         shared(th_times, N_THREADS, std::cout) \
                         firstprivate(other_pe, amo_target, one_way)
    {
        const size_t tid = omp_get_thread_num();

        uint64_t amo_result;

        timespec t0, t1;

        #ifdef USE_CTX
        shmem_ctx_t ctx;
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx);
        shmem_ctx_quiet(ctx);
        #endif

        const size_t iter = 100000;
        const size_t warm_up = iter / 10;

        double time = 0.0;

        if (!one_way) {
            shmem_barrier_all_omp();
        }

        #pragma omp barrier

        for (size_t i = 0; i < iter + warm_up; i++) {
            if (i == warm_up) {
                clock_gettime(CLOCK_MONOTONIC, &t0);
            }

            #ifdef USE_CTX
            amo_result = shmem_ctx_uint64_atomic_swap(ctx, amo_target, tid, other_pe);
            #else
            amo_result = shmem_uint64_atomic_swap(amo_target, tid, other_pe);
            #endif
        }

        clock_gettime(CLOCK_MONOTONIC, &t1);

        time += (t1.tv_sec - t0.tv_sec) * 1000000.0
              + (t1.tv_nsec - t0.tv_nsec) / 1000.0;

        time /= double(iter);

        th_times[tid] = time;

        #pragma omp barrier

        // Do statistics
        #pragma omp master
        {
            double min_time = th_times[0];
            double max_time = th_times[0];
            double tot_time = th_times[0];

            for (size_t i = 1; i < N_THREADS; i++) {
                if (th_times[i] < min_time) {
                    min_time = th_times[i];
                }

                if (th_times[i] > max_time) {
                    max_time = th_times[i];
                }

                tot_time += th_times[i];
            }

            const double avg_time = tot_time / N_THREADS;

            std::cout << std::fixed << std::setprecision(3)
                      << std::setw(12) << std::left << iter
                      << std::setw(16) << std::right << min_time
                      << std::setw(16) << std::right << max_time
                      << std::setw(16) << std::right << avg_time
                      << '\n';
        }

        #ifdef USE_CTX
        shmem_ctx_destroy(ctx);
        #endif
    }
}


int main(int argc, char** argv)
{
    if (argc > 1) {
        N_THREADS = std::atoi(argv[1]);
    } else {
        N_THREADS = 1;
    }

    SR_BUF_LEN = N_THREADS * TH_SEG_LEN;
    HEAP_LEN   = 2 * SR_BUF_LEN;

    int tl, tl_supported;

    if (N_THREADS == 1) {
        tl = SHMEM_THREAD_FUNNELED;
    } else {
        tl = SHMEM_THREAD_MULTIPLE;
    }

    shmem_init_thread(tl, &tl_supported);

    if (tl != tl_supported) {
        if (shmem_my_pe() == 0) {
            std::cout << "Error: Could not enable the desired thread level!\n";
        }
        shmem_global_exit(1);
    }

    assert(shmem_n_pes() == 2);

    const size_t page_size  = sysconf(_SC_PAGESIZE);
    auto heap = (uint8_t*)shmem_align(page_size, HEAP_LEN * sizeof(uint8_t));

    stress_test(heap);

    const bool one_way = true;

    // bench_put_nbi(heap, one_way);

    // bench_get_nbi(heap, one_way);

    // bench_put(heap, one_way);

    // bench_get(heap, one_way);

    // bench_amo64_post(heap, one_way);

    // bench_amo64_fetch(heap, one_way);

    shmem_barrier_all();

    shmem_free(heap);

    shmem_finalize();
}
