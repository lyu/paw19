#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>
#include <ctime>

#include <unistd.h>
#include <shmem.h>


#define SR_BUF_LEN_LOG 21
#define SR_BUF_LEN (1UL << SR_BUF_LEN_LOG)
#define HEAP_LEN (2 * SR_BUF_LEN)

int64_t N_PES_PER_NODE;

#define N_PES_PER_NODE_MAX 64


int main(int argc, char** argv)
{
    if (argc > 1) {
        N_PES_PER_NODE = std::atoi(argv[1]);
    } else {
        N_PES_PER_NODE = 1;
    }

    shmem_init();

    assert(shmem_n_pes() == 2 * N_PES_PER_NODE);

    const size_t mype = shmem_my_pe();

    const size_t other_pe = (shmem_my_pe() + N_PES_PER_NODE) % shmem_n_pes();

    int report_pe = 0;
    if (shmem_my_pe() >= N_PES_PER_NODE) {
        report_pe = N_PES_PER_NODE;
    }

    const size_t page_size  = sysconf(_SC_PAGESIZE);
    auto heap = (uint32_t*)shmem_align(page_size, HEAP_LEN * sizeof(uint32_t));

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    static double pe_times[N_PES_PER_NODE_MAX];

    if (shmem_my_pe() == 0) {
        std::cout << "Benchmarking ping-pong, time unit microseconds:\n"
                  << std::setw(12) << std::left << "Size (bytes)"
                  << std::setw(16) << std::right << "Min iter time"
                  << std::setw(16) << std::right << "Max iter time"
                  << std::setw(16) << std::right << "Avg iter time"
                  << '\n';
    }

        timespec t0, t1;

        // TODO: Variable message length?
        const size_t msg_len = 1UL << 0;
        const size_t iter    = 1UL << 20;
        const size_t warm_up = 8192;

        // Find an unique and easily identifiable base number to begin the game
        // The most significant digits is the thread ID, and the rest stores the
        // number of times the ball has been hit
        const size_t magnitude = std::log10(2 * (iter + warm_up)) + 1;
        const size_t th_base   = (mype % N_PES_PER_NODE) * std::pow(size_t(10), magnitude);

        // Fill the buffers with garbage
        for (size_t i = 0; i < SR_BUF_LEN; i++) {
            sbuf[i] = 0xFFFFFFFF;
            rbuf[i] = 0xFFFFFFFF;
        }

        // In every iteration we verify the end of the data, which stores the
        // status of the ball
        //   *sbuf_end: # of times the ball has been hit the last time it left me
        //   *rbuf_end: # of times the ball has been hit when it is on its way back to me
        auto sbuf_end = sbuf + msg_len - 1;
        auto rbuf_end = rbuf + msg_len - 1;

        // Proper initialization of the players' statuses
        if (shmem_my_pe() < N_PES_PER_NODE) {
            *sbuf_end = th_base;            // PE 0 hasn't hit the ball yet
            *rbuf_end = th_base + 1;        // The judge throws the ball to PE 0
        } else {
            *sbuf_end = th_base + 1;        // Expecting the ball from PE 0
            *rbuf_end = 0xFFFFFFFF;         // To be updated by the put
        }

        shmem_barrier_all();

        // Let the games begin!
        for (size_t i = 0; i < iter + warm_up; i++) {
            if (i == warm_up) {
                shmem_barrier_all();

                clock_gettime(CLOCK_MONOTONIC, &t0);
            }

            // Wait for the ball, it has been hit one more time since the last
            // time it left me
            while (*rbuf_end != (*sbuf_end + 1)) {
                sched_yield();
            }

            // Send the ball back
            *sbuf_end += 2;

            shmem_putmem(rbuf, sbuf, msg_len * sizeof(uint32_t), other_pe);
        }

        clock_gettime(CLOCK_MONOTONIC, &t1);

        double time = (t1.tv_sec - t0.tv_sec) * 1000000.0
                    + (t1.tv_nsec - t0.tv_nsec) / 1000.0;

        shmem_double_p(&pe_times[shmem_my_pe()], time, report_pe);

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

            if (shmem_my_pe() == 0) {
                std::cout << std::fixed << std::setprecision(3)
                          << std::setw(12) << std::left << msg_len * sizeof(uint32_t)
                          << std::setw(16) << std::right << (min_time / iter)
                          << std::setw(16) << std::right << (max_time / iter)
                          << std::setw(16) << std::right << (avg_time / iter)
                          << '\n';
            }
        }

    shmem_barrier_all();

    shmem_free(heap);

    shmem_finalize();
}
