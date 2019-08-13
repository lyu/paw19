#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>
#include <ctime>

#include <unistd.h>
#include <shmem.h>
#include <omp.h>


#define TH_SEG_LEN_LOG 21
#define TH_SEG_LEN (1UL << TH_SEG_LEN_LOG)

size_t N_THREADS, SR_BUF_LEN, HEAP_LEN;


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

    const size_t other_pe = (shmem_my_pe() + 1) % 2;

    const size_t page_size  = sysconf(_SC_PAGESIZE);
    auto heap = (uint32_t*)shmem_align(page_size, HEAP_LEN * sizeof(uint32_t));

    auto sbuf = heap;
    auto rbuf = heap + SR_BUF_LEN;

    double th_times[N_THREADS];

    if (shmem_my_pe() == 0) {
        std::cout << "Benchmarking ping-pong, time unit microseconds:\n"
                  << std::setw(12) << std::left << "Size (bytes)"
                  << std::setw(16) << std::right << "Min iter time"
                  << std::setw(16) << std::right << "Max iter time"
                  << std::setw(16) << std::right << "Avg iter time"
                  << '\n';
    }

    #pragma omp parallel num_threads(N_THREADS)             \
                         default(none)                      \
                         firstprivate(other_pe, sbuf, rbuf) \
                         shared(th_times, N_THREADS, std::cout)
    {
        const size_t tid = omp_get_thread_num();

        // Unique segments of the heap for all the threads
        sbuf += tid * TH_SEG_LEN;
        rbuf += tid * TH_SEG_LEN;

        timespec t0, t1;

        #ifdef USE_CTX
        shmem_ctx_t ctx;
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx);
        shmem_ctx_quiet(ctx);
        #endif

        // TODO: Variable message length?
        const size_t msg_len = 1UL << 0;
        const size_t iter    = 1UL << 20;
        const size_t warm_up = 8192;

        // Find an unique and easily identifiable base number to begin the game
        // The most significant digits is the thread ID, and the rest stores the
        // number of times the ball has been hit
        const size_t magnitude = std::log10(2 * (iter + warm_up)) + 1;
        const size_t th_base   = tid * std::pow(size_t(10), magnitude);

        // Fill the buffers with garbage
        for (size_t i = 0; i < TH_SEG_LEN; i++) {
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
        if (shmem_my_pe() == 0) {
            *sbuf_end = th_base;            // PE 0 hasn't hit the ball yet
            *rbuf_end = th_base + 1;        // The judge throws the ball to PE 0
        } else {
            *sbuf_end = th_base + 1;        // Expecting the ball from PE 0
            *rbuf_end = 0xFFFFFFFF;         // To be updated by the put
        }

        #pragma omp barrier
        #pragma omp master
        shmem_barrier_all();
        #pragma omp barrier

        // Let the games begin!
        for (size_t i = 0; i < iter + warm_up; i++) {
            if (i == warm_up) {
                #pragma omp barrier
                #pragma omp master
                shmem_barrier_all();
                #pragma omp barrier

                clock_gettime(CLOCK_MONOTONIC, &t0);
            }

            // Wait for the ball, it has been hit one more time since the last
            // time it left me
            while (*rbuf_end != (*sbuf_end + 1)) {
                sched_yield();
            }

            // Send the ball back
            *sbuf_end += 2;

            #ifdef USE_CTX
            shmem_ctx_putmem(ctx, rbuf, sbuf, msg_len * sizeof(uint32_t), other_pe);
            #else
            shmem_putmem(rbuf, sbuf, msg_len * sizeof(uint32_t), other_pe);
            #endif
        }

        clock_gettime(CLOCK_MONOTONIC, &t1);

        th_times[tid] = (t1.tv_sec - t0.tv_sec) * 1000000.0
                      + (t1.tv_nsec - t0.tv_nsec) / 1000.0;

        #pragma omp barrier
        #pragma omp master
        shmem_barrier_all();
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

            if (shmem_my_pe() == 0) {
                std::cout << std::fixed << std::setprecision(3)
                          << std::setw(12) << std::left << msg_len * sizeof(uint32_t)
                          << std::setw(16) << std::right << (min_time / iter)
                          << std::setw(16) << std::right << (max_time / iter)
                          << std::setw(16) << std::right << (avg_time / iter)
                          << '\n';
            }
        }

        #ifdef USE_CTX
        shmem_ctx_destroy(ctx);
        #endif
    }

    shmem_barrier_all();

    shmem_free(heap);

    shmem_finalize();
}
