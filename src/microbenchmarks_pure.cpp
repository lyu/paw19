#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstdint>
#include <ctime>

#include <unistd.h>
#include <shmem.h>


#define SR_BUF_LEN_LOG 20
#define SR_BUF_LEN (1UL << SR_BUF_LEN_LOG)
#define HEAP_LEN (2 * SR_BUF_LEN)

int64_t N_PES_PER_NODE;
uint64_t sum_t;

#define N_PES_PER_NODE_MAX 64


void stress_test(uint8_t* heap)
{
    const size_t other_pe = (shmem_my_pe() + N_PES_PER_NODE) % shmem_n_pes();

    int other_pe_amo;
    if (shmem_my_pe() < N_PES_PER_NODE) {
        other_pe_amo = 0;
    } else {
        other_pe_amo = N_PES_PER_NODE;
    }

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    uint64_t omp_redu = 0;

        timespec t0, t1;

        for (size_t e = 0; e <= SR_BUF_LEN_LOG; e++) {
            const size_t msg_len = 1UL << e;
            const size_t n_msg   = SR_BUF_LEN >> e;

            uint8_t this_put_pattern, that_put_pattern;
            uint8_t this_get_pattern, that_get_pattern;

            // Determine iteration-specific heap data patterns
            if (shmem_my_pe() < N_PES_PER_NODE) {
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
                std::cout << "Starting stress test with message size 2^" << e
                          << " byte(s)\n";
            }

            // Stage 1.1:
            // Initialize the entire heap w/ different patterns on different PEs
            auto heap_ptr = heap;

            for (size_t i = 0; i < HEAP_LEN; i++) {
                heap_ptr[i] = i % this_put_pattern;
            }

            sum_t = 0;

            shmem_barrier_all();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 1.2:
            // Send this thread's send buffer to the receive buffer of its peer
            // thread on the other PE using blocking puts.
            for (size_t i = 0; i < n_msg; i++) {
                shmem_putmem(rbuf + i * msg_len, sbuf + i * msg_len, msg_len, other_pe);
            }

            shmem_quiet();

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all();

            // Stage 1.3:
            // Verify the result and reinitialize the heap
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

            uint64_t t = (t1.tv_sec - t0.tv_sec) * 1000000 +
                         (t1.tv_nsec - t0.tv_nsec) / 1000;

            shmem_barrier_all();
            if (shmem_my_pe() < N_PES_PER_NODE) {
                shmem_uint64_atomic_add(&sum_t, t, 0);
            } else {
                shmem_uint64_atomic_add(&sum_t, t, N_PES_PER_NODE);
            }
            shmem_barrier_all();

            if ((shmem_my_pe() == 0) || (shmem_my_pe() == N_PES_PER_NODE)) {
                std::cout << "    put test: " << std::setprecision(6)
                          << std::scientific << double(sum_t) / N_PES_PER_NODE << " us\n";
            }

            // Refill the buffer
            for (size_t i = 0; i < HEAP_LEN; i++) {
                heap_ptr[i] = i % this_get_pattern;
            }

            sum_t = 0;

            shmem_barrier_all();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 2.1
            // Fetch the send buffer of this thread's peer on the other PE
            // to its receive buffer using blocking gets.
            for (size_t i = 0; i < n_msg; i++) {
                shmem_getmem(rbuf + i * msg_len, sbuf + i * msg_len, msg_len, other_pe);
            }

            shmem_quiet();

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all();

            // Stage 2.2
            // Verify the result
            sbuf_ok = true;
            rbuf_ok = true;

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

            t = (t1.tv_sec - t0.tv_sec) * 1000000 +
                (t1.tv_nsec - t0.tv_nsec) / 1000;

            shmem_barrier_all();
            if (shmem_my_pe() < N_PES_PER_NODE) {
                shmem_uint64_atomic_add(&sum_t, t, 0);
            } else {
                shmem_uint64_atomic_add(&sum_t, t, N_PES_PER_NODE);
            }
            shmem_barrier_all();

            if ((shmem_my_pe() == 0) || (shmem_my_pe() == N_PES_PER_NODE)) {
                std::cout << "    get test: " << std::setprecision(6)
                          << std::scientific << double(sum_t) / N_PES_PER_NODE << " us\n";
            }

            // Stage 3.1
            // Prepare the target for atomic operations
            auto amo_target = ((uint64_t*)heap) + 1;

            *amo_target = 0;

            sum_t = 0;

            shmem_barrier_all();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 3.2
            // All threads increment the same counter on the other PE
            // Result should be: msg_len * n_msg * N_THREADS = SR_BUF_LEN
            for (size_t i = 0; i < n_msg; i++) {
                shmem_uint64_atomic_add(amo_target, msg_len, other_pe_amo);
            }

            shmem_quiet();

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all();

            // Stage 3.3
            // Verify the result
            if (shmem_my_pe() == other_pe_amo) {
                if (*amo_target != SR_BUF_LEN * N_PES_PER_NODE) {
                    std::cout << "** ERROR: incorrect result in AMO ADD test"
                              << "\n** Expected: " << SR_BUF_LEN * N_PES_PER_NODE
                              << "\n** Received: " << *amo_target << '\n';
                }
            }

            t = (t1.tv_sec - t0.tv_sec) * 1000000 +
                (t1.tv_nsec - t0.tv_nsec) / 1000;

            shmem_barrier_all();
            if (shmem_my_pe() < N_PES_PER_NODE) {
                shmem_uint64_atomic_add(&sum_t, t, 0);
            } else {
                shmem_uint64_atomic_add(&sum_t, t, N_PES_PER_NODE);
            }
            shmem_barrier_all();

            if ((shmem_my_pe() == 0) || (shmem_my_pe() == N_PES_PER_NODE)) {
                std::cout << "    amo post test: " << std::setprecision(6)
                          << std::scientific << double(sum_t) / N_PES_PER_NODE << " us\n";
            }

            // Stage 4.1
            // Prepare local buffers for AMO FADD, SWAP and CSWAP
            auto amo_result = ((uint64_t*)rbuf) + 2;
            *amo_result     = 0;

            *amo_target = 0;

            sum_t = 0;

            shmem_barrier_all();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 4.2
            // All threads increment the same counter on the other PE using AMO
            // fetch-add
            for (size_t i = 0; i < n_msg; i++) {
                *amo_result = shmem_uint64_atomic_fetch_add(amo_target, msg_len, other_pe_amo);
            }

            shmem_quiet();

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all();

            // Stage 4.3
            // Verify the result and prepare for the CSWAP test
            if (shmem_my_pe() == other_pe_amo) {
                if (*amo_target != SR_BUF_LEN * N_PES_PER_NODE) {
                    std::cout << "** ERROR: incorrect result in AMO FADD test"
                              << "\n** Expected: " << SR_BUF_LEN * N_PES_PER_NODE
                              << "\n** Received: " << *amo_target << '\n';
                }
            }

            t = (t1.tv_sec - t0.tv_sec) * 1000000 +
                (t1.tv_nsec - t0.tv_nsec) / 1000;

            shmem_barrier_all();
            if (shmem_my_pe() < N_PES_PER_NODE) {
                shmem_uint64_atomic_add(&sum_t, t, 0);
            } else {
                shmem_uint64_atomic_add(&sum_t, t, N_PES_PER_NODE);
            }
            shmem_barrier_all();

            if ((shmem_my_pe() == 0) || (shmem_my_pe() == N_PES_PER_NODE)) {
                std::cout << "    amo fadd test: " << std::setprecision(6)
                          << std::scientific << double(sum_t) / N_PES_PER_NODE << " us\n";
            }

            *amo_target = 0;

            // For the next test we use this for storing the total number
            // of successful CSWAPs
            omp_redu    = 0;

            // Local counter for number of successful CSWAPs
            uint64_t succ_loc  = 0;

            sum_t = 0;

            shmem_barrier_all();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 4.4
            // All threads compete against each other to increment the same
            // counter on the other PE using AMO compare-and-swap
            for (size_t i = 0; i < n_msg; i++) {
                *amo_result = i + 1;

                *amo_result = shmem_uint64_atomic_compare_swap(amo_target, i, *amo_result, other_pe_amo);

                // Did I succeed?
                if (*amo_result == i) {
                    succ_loc++;
                }
            }

            shmem_quiet();

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all();

            // Stage 4.5
            // Verify the result and prepare for the SWAP test
            if (shmem_my_pe() == other_pe_amo) {
                if (*amo_target != n_msg) {
                    std::cout << "** ERROR: incorrect result in AMO CSWAP test"
                              << "\n** Expected: " << n_msg
                              << "\n** Received: " << *amo_target
                              << "\n** Succeed: " << omp_redu << '\n';
                }
            }

            t = (t1.tv_sec - t0.tv_sec) * 1000000 +
                (t1.tv_nsec - t0.tv_nsec) / 1000;

            shmem_barrier_all();
            if (shmem_my_pe() < N_PES_PER_NODE) {
                shmem_uint64_atomic_add(&sum_t, t, 0);
            } else {
                shmem_uint64_atomic_add(&sum_t, t, N_PES_PER_NODE);
            }
            shmem_barrier_all();

            if ((shmem_my_pe() == 0) || (shmem_my_pe() == N_PES_PER_NODE)) {
                std::cout << "    amo cswap test: " << std::setprecision(6)
                          << std::scientific << double(sum_t) / N_PES_PER_NODE << " us\n";
            }

            // Assign the sum of the thread IDs to the common AMO target
            *amo_target = (shmem_n_pes() * (shmem_n_pes() - 1)) / 2;

            // For the next test we use this for summing the results
            omp_redu    = 0;

            // Use the thread ID as the local value
            *amo_result = shmem_my_pe();

            sum_t = 0;

            shmem_barrier_all();

            clock_gettime(CLOCK_MONOTONIC, &t0);

            // Stage 4.6
            // All threads swap their local values with the same remote target
            // There is no guarantee where will the sum reside at the end of
            // the loop
            // The sum of the number of times each thread owned the sum is also
            // unpredictable, could be much bigger than n_msg
            for (size_t i = 0; i < n_msg; i++) {
                *amo_result = shmem_uint64_atomic_swap(amo_target, *amo_result, other_pe_amo);
            }

            shmem_quiet();

            clock_gettime(CLOCK_MONOTONIC, &t1);

            shmem_barrier_all();

            // Stage 4.7
            // Verify the result
            // lol no

            t = (t1.tv_sec - t0.tv_sec) * 1000000 +
                (t1.tv_nsec - t0.tv_nsec) / 1000;

            shmem_barrier_all();
            if (shmem_my_pe() < N_PES_PER_NODE) {
                shmem_uint64_atomic_add(&sum_t, t, 0);
            } else {
                shmem_uint64_atomic_add(&sum_t, t, N_PES_PER_NODE);
            }
            shmem_barrier_all();

            if ((shmem_my_pe() == 0) || (shmem_my_pe() == N_PES_PER_NODE)) {
                std::cout << "    amo swap test: " << std::setprecision(6)
                          << std::scientific << double(sum_t) / N_PES_PER_NODE << " us\n";
            }

            shmem_barrier_all();
        }
}


void bench_put_nbi(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() < N_PES_PER_NODE) && one_way) {
        for (size_t e = 0; e <= SR_BUF_LEN_LOG; e++) {
            // Match #1
            shmem_barrier_all();
            // Match #2
            shmem_barrier_all();
        }
        return;
    }

    const size_t other_pe = (shmem_my_pe() + N_PES_PER_NODE) % shmem_n_pes();

    int report_pe = 0;
    if (shmem_my_pe() >= N_PES_PER_NODE) {
        report_pe = N_PES_PER_NODE;
    }

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    static double pe_post_times[2 * N_PES_PER_NODE_MAX];
    static double pe_wait_times[2 * N_PES_PER_NODE_MAX];

    if (shmem_my_pe() == N_PES_PER_NODE) {
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

        timespec t0, t1, t2;

        for (size_t e = 0; e <= SR_BUF_LEN_LOG; e++) {
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

            // Match #1
            shmem_barrier_all();

            for (size_t i = 0; i < iter + warm_up; i++) {
                clock_gettime(CLOCK_MONOTONIC, &t0);

                shmem_putmem_nbi(rbuf, sbuf, msg_len, other_pe);

                clock_gettime(CLOCK_MONOTONIC, &t1);

                shmem_quiet();

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

            shmem_double_p(&pe_post_times[shmem_my_pe()], post_time, report_pe);
            shmem_double_p(&pe_wait_times[shmem_my_pe()], wait_time, report_pe);

            // Match #2
            shmem_barrier_all();

            // Do statistics
            if (shmem_my_pe() == report_pe)
            {
                double min_post_time = pe_post_times[report_pe];
                double max_post_time = pe_post_times[report_pe];
                double tot_post_time = pe_post_times[report_pe];

                double min_wait_time = pe_wait_times[report_pe];
                double max_wait_time = pe_wait_times[report_pe];
                double tot_wait_time = pe_wait_times[report_pe];

                for (int i = 1; i < N_PES_PER_NODE; i++) {
                    if (pe_post_times[i + report_pe] < min_post_time) {
                        min_post_time = pe_post_times[i + report_pe];
                    }

                    if (pe_post_times[i + report_pe] > max_post_time) {
                        max_post_time = pe_post_times[i + report_pe];
                    }

                    tot_post_time += pe_post_times[i + report_pe];

                    if (pe_wait_times[i + report_pe] < min_wait_time) {
                        min_wait_time = pe_wait_times[i + report_pe];
                    }

                    if (pe_wait_times[i + report_pe] > max_wait_time) {
                        max_wait_time = pe_wait_times[i + report_pe];
                    }

                    tot_wait_time += pe_wait_times[i + report_pe];
                }

                const double avg_post_time = tot_post_time / double(N_PES_PER_NODE);
                const double avg_wait_time = tot_wait_time / double(N_PES_PER_NODE);

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
}


void bench_get_nbi(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() < N_PES_PER_NODE) && one_way) {
        for (size_t e = 0; e <= SR_BUF_LEN_LOG; e++) {
            // Match #1
            shmem_barrier_all();
            // Match #2
            shmem_barrier_all();
        }
        return;
    }

    const size_t other_pe = (shmem_my_pe() + N_PES_PER_NODE) % shmem_n_pes();

    int report_pe = 0;
    if (shmem_my_pe() >= N_PES_PER_NODE) {
        report_pe = N_PES_PER_NODE;
    }

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    static double pe_post_times[2 * N_PES_PER_NODE_MAX];
    static double pe_wait_times[2 * N_PES_PER_NODE_MAX];

    if (shmem_my_pe() == N_PES_PER_NODE) {
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

        timespec t0, t1, t2;

        for (size_t e = 0; e <= SR_BUF_LEN_LOG; e++) {
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

            // Match #1
            shmem_barrier_all();

            for (size_t i = 0; i < iter + warm_up; i++) {
                clock_gettime(CLOCK_MONOTONIC, &t0);

                shmem_getmem_nbi(rbuf, sbuf, msg_len, other_pe);

                clock_gettime(CLOCK_MONOTONIC, &t1);

                shmem_quiet();

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

            shmem_double_p(&pe_post_times[shmem_my_pe()], post_time, report_pe);
            shmem_double_p(&pe_wait_times[shmem_my_pe()], wait_time, report_pe);

            // Match #2
            shmem_barrier_all();

            // Do statistics
            if (shmem_my_pe() == report_pe)
            {
                double min_post_time = pe_post_times[report_pe];
                double max_post_time = pe_post_times[report_pe];
                double tot_post_time = pe_post_times[report_pe];

                double min_wait_time = pe_wait_times[report_pe];
                double max_wait_time = pe_wait_times[report_pe];
                double tot_wait_time = pe_wait_times[report_pe];

                for (int i = 1; i < N_PES_PER_NODE; i++) {
                    if (pe_post_times[i + report_pe] < min_post_time) {
                        min_post_time = pe_post_times[i + report_pe];
                    }

                    if (pe_post_times[i + report_pe] > max_post_time) {
                        max_post_time = pe_post_times[i + report_pe];
                    }

                    tot_post_time += pe_post_times[i + report_pe];

                    if (pe_wait_times[i + report_pe] < min_wait_time) {
                        min_wait_time = pe_wait_times[i + report_pe];
                    }

                    if (pe_wait_times[i + report_pe] > max_wait_time) {
                        max_wait_time = pe_wait_times[i + report_pe];
                    }

                    tot_wait_time += pe_wait_times[i + report_pe];
                }

                const double avg_post_time = tot_post_time / double(N_PES_PER_NODE);
                const double avg_wait_time = tot_wait_time / double(N_PES_PER_NODE);

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
}


void bench_put(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() < N_PES_PER_NODE) && one_way) {
        for (size_t e = 0; e <= SR_BUF_LEN_LOG; e++) {
            // Match #1
            shmem_barrier_all();
            // Match #2
            shmem_barrier_all();
        }
        return;
    }

    const size_t other_pe = (shmem_my_pe() + N_PES_PER_NODE) % shmem_n_pes();

    int report_pe = 0;
    if (shmem_my_pe() >= N_PES_PER_NODE) {
        report_pe = N_PES_PER_NODE;
    }

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    static double pe_times[2 * N_PES_PER_NODE_MAX];

    if (shmem_my_pe() == N_PES_PER_NODE) {
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

        timespec t0, t1;

        for (size_t e = 0; e <= SR_BUF_LEN_LOG; e++) {
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

            size_t offset = 0;

            // Match #1
            shmem_barrier_all();

            for (size_t i = 0; i < iter + warm_up; i++) {
                if (i == warm_up) {
                    clock_gettime(CLOCK_MONOTONIC, &t0);
                }

                shmem_putmem(rbuf + offset, sbuf + offset, msg_len, other_pe);

                offset += msg_len;
                if ((offset + msg_len) >= SR_BUF_LEN) {
                    offset = 0;
                }
            }

            shmem_quiet();

            clock_gettime(CLOCK_MONOTONIC, &t1);

            put_time += (t1.tv_sec - t0.tv_sec) * 1000000.0
                      + (t1.tv_nsec - t0.tv_nsec) / 1000.0;

            put_time /= double(iter);

            shmem_double_p(&pe_times[shmem_my_pe()], put_time, report_pe);

            // Match #2
            shmem_barrier_all();

            // Do statistics
            if (shmem_my_pe() == report_pe)
            {
                double min_time = pe_times[report_pe];
                double max_time = pe_times[report_pe];
                double tot_time = pe_times[report_pe];

                for (int i = 1; i < N_PES_PER_NODE; i++) {
                    if (pe_times[i + report_pe] < min_time) {
                        min_time = pe_times[i + report_pe];
                    }

                    if (pe_times[i + report_pe] > max_time) {
                        max_time = pe_times[i + report_pe];
                    }

                    tot_time += pe_times[i + report_pe];
                }

                const double avg_time = tot_time / double(N_PES_PER_NODE);

                std::cout << std::fixed << std::setprecision(3)
                          << std::setw(12) << std::left << msg_len
                          << std::setw(16) << std::right << min_time
                          << std::setw(16) << std::right << max_time
                          << std::setw(16) << std::right << avg_time
                          << '\n';
            }
        }
}


void bench_get(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() < N_PES_PER_NODE) && one_way) {
        for (size_t e = 0; e <= SR_BUF_LEN_LOG; e++) {
            // Match #1
            shmem_barrier_all();
            // Match #2
            shmem_barrier_all();
        }
        return;
    }

    const size_t other_pe = (shmem_my_pe() + N_PES_PER_NODE) % shmem_n_pes();

    int report_pe = 0;
    if (shmem_my_pe() >= N_PES_PER_NODE) {
        report_pe = N_PES_PER_NODE;
    }

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    static double pe_times[2 * N_PES_PER_NODE_MAX];

    if (shmem_my_pe() == N_PES_PER_NODE) {
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

        timespec t0, t1;

        for (size_t e = 0; e <= SR_BUF_LEN_LOG; e++) {
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

            size_t offset = 0;

            // Match #1
            shmem_barrier_all();

            for (size_t i = 0; i < iter + warm_up; i++) {
                if (i == warm_up) {
                    clock_gettime(CLOCK_MONOTONIC, &t0);
                }

                shmem_getmem(rbuf + offset, sbuf + offset, msg_len, other_pe);

                offset += msg_len;
                if ((offset + msg_len) >= SR_BUF_LEN) {
                    offset = 0;
                }
            }

            shmem_quiet();

            clock_gettime(CLOCK_MONOTONIC, &t1);

            put_time += (t1.tv_sec - t0.tv_sec) * 1000000.0
                      + (t1.tv_nsec - t0.tv_nsec) / 1000.0;

            put_time /= double(iter);

            shmem_double_p(&pe_times[shmem_my_pe()], put_time, report_pe);

            // Match #2
            shmem_barrier_all();

            // Do statistics
            if (shmem_my_pe() == report_pe)
            {
                double min_time = pe_times[report_pe];
                double max_time = pe_times[report_pe];
                double tot_time = pe_times[report_pe];

                for (int i = 1; i < N_PES_PER_NODE; i++) {
                    if (pe_times[i + report_pe] < min_time) {
                        min_time = pe_times[i + report_pe];
                    }

                    if (pe_times[i + report_pe] > max_time) {
                        max_time = pe_times[i + report_pe];
                    }

                    tot_time += pe_times[i + report_pe];
                }

                const double avg_time = tot_time / double(N_PES_PER_NODE);

                std::cout << std::fixed << std::setprecision(3)
                          << std::setw(12) << std::left << msg_len
                          << std::setw(16) << std::right << min_time
                          << std::setw(16) << std::right << max_time
                          << std::setw(16) << std::right << avg_time
                          << '\n';
            }
        }
}


void bench_amo64_post(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() < N_PES_PER_NODE) && one_way) {
        // Match #1
        shmem_barrier_all();
        // Match #2
        shmem_barrier_all();

        // Prevent active polling from the waituntil in the barrier
        sleep(5);

        // Match #3
        shmem_barrier_all();

        return;
    }

    size_t other_pe = N_PES_PER_NODE;
    int report_pe = 0;
    if (shmem_my_pe() >= N_PES_PER_NODE) {
        other_pe = 0;
        report_pe = N_PES_PER_NODE;
    }

    auto amo_target = ((uint64_t*)heap) + 1;

    static double pe_times[2 * N_PES_PER_NODE_MAX];

    if (shmem_my_pe() == N_PES_PER_NODE) {
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

        timespec t0, t1;

        const size_t iter = 100000;
        const size_t warm_up = iter / 10;

        double time = 0.0;

        // Match #1
        shmem_barrier_all();

        for (size_t i = 0; i < iter + warm_up; i++) {
            if (i == warm_up) {
                // Match #2
                shmem_barrier_all();

                clock_gettime(CLOCK_MONOTONIC, &t0);
            }

            shmem_uint64_atomic_add(amo_target, 1, other_pe);

        }

        shmem_quiet();

        clock_gettime(CLOCK_MONOTONIC, &t1);

        time += (t1.tv_sec - t0.tv_sec) * 1000000.0
              + (t1.tv_nsec - t0.tv_nsec) / 1000.0;

        time /= double(iter);

        shmem_double_p(&pe_times[shmem_my_pe()], time, report_pe);

        // Match #3
        shmem_barrier_all();

        if (shmem_my_pe() == report_pe)
        {
            double min_time = pe_times[report_pe];
            double max_time = pe_times[report_pe];
            double tot_time = pe_times[report_pe];

            for (int i = 1; i < N_PES_PER_NODE; i++) {
                if (pe_times[i + report_pe] < min_time) {
                    min_time = pe_times[i + report_pe];
                }

                if (pe_times[i + report_pe] > max_time) {
                    max_time = pe_times[i + report_pe];
                }

                tot_time += pe_times[i + report_pe];
            }

            const double avg_time = tot_time / N_PES_PER_NODE;

            std::cout << std::fixed << std::setprecision(3)
                      << std::setw(12) << std::left << iter
                      << std::setw(16) << std::right << min_time
                      << std::setw(16) << std::right << max_time
                      << std::setw(16) << std::right << avg_time
                      << '\n';
        }
}


void bench_amo64_fetch(uint8_t* heap, const bool one_way)
{
    if ((shmem_my_pe() < N_PES_PER_NODE) && one_way) {
        // Match #1
        shmem_barrier_all();
        // Match #2
        shmem_barrier_all();

        return;
    }

    size_t other_pe = N_PES_PER_NODE;
    int report_pe = 0;
    if (shmem_my_pe() >= N_PES_PER_NODE) {
        other_pe = 0;
        report_pe = N_PES_PER_NODE;
    }

    auto amo_target = ((uint64_t*)heap) + 1;

    static double pe_times[2 * N_PES_PER_NODE_MAX];

    if (shmem_my_pe() == N_PES_PER_NODE) {
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

        uint64_t amo_result = 42;

        timespec t0, t1;

        const size_t iter = 100000;
        const size_t warm_up = iter / 10;

        double time = 0.0;

        // Match #1
        shmem_barrier_all();

        for (size_t i = 0; i < iter + warm_up; i++) {
            if (i == warm_up) {
                clock_gettime(CLOCK_MONOTONIC, &t0);
            }

            amo_result = shmem_uint64_atomic_swap(amo_target, amo_result, other_pe);
        }

        clock_gettime(CLOCK_MONOTONIC, &t1);

        time += (t1.tv_sec - t0.tv_sec) * 1000000.0
              + (t1.tv_nsec - t0.tv_nsec) / 1000.0;

        time /= double(iter);

        shmem_double_p(&pe_times[shmem_my_pe()], time, report_pe);

        // Match #2
        shmem_barrier_all();

        // Do statistics
        if (shmem_my_pe() == report_pe)
        {
            double min_time = pe_times[report_pe];
            double max_time = pe_times[report_pe];
            double tot_time = pe_times[report_pe];

            for (int i = 1; i < N_PES_PER_NODE; i++) {
                if (pe_times[i + report_pe] < min_time) {
                    min_time = pe_times[i + report_pe];
                }

                if (pe_times[i + report_pe] > max_time) {
                    max_time = pe_times[i + report_pe];
                }

                tot_time += pe_times[i + report_pe];
            }

            const double avg_time = tot_time / N_PES_PER_NODE;

            std::cout << std::fixed << std::setprecision(3)
                      << std::setw(12) << std::left << iter
                      << std::setw(16) << std::right << min_time
                      << std::setw(16) << std::right << max_time
                      << std::setw(16) << std::right << avg_time
                      << '\n';
        }
}


int main(int argc, char** argv)
{
    if (argc > 1) {
        N_PES_PER_NODE = std::atoi(argv[1]);
    } else {
        N_PES_PER_NODE = 1;
    }

    shmem_init();

    assert(shmem_n_pes() == 2 * N_PES_PER_NODE);

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
