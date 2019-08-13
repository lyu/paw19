#include <algorithm>
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>
#include <random>
#include <cmath>
#include <ctime>

#include <omp.h>
#include <shmem.h>
#include <getopt.h>
#include <unistd.h>


using key_type = uint32_t;

const key_type MAX_KEY = std::numeric_limits<key_type>::max() / 4;

// Three types of all-to-all communication schedules:
//   Round robin: at step i, every PE send data to the PE that is i PEs away
//   Incast: at step i, every PE send data to PE i
//   Random: at step i, every PE send data to a random PE
enum class Sched {
    RoundRobin,
    Incast,
    Random
};

struct params_t {
    size_t iters, n_threads;
    bool use_ctx, use_nbi, use_pipelining;
    Sched comm;

    // The last three depend on npes & n_threads
    // bucket_width is the length of the range of a bucket
    size_t n_keys, n_keys_th, n_buckets, bucket_width;

    // If the RNG is good, the number of keys that end up in each bucket
    // should be pretty close to n_keys_th, for a large number of keys
    // For fewer number of keys, increase the size of the buckets in case
    // the distribution isn't good
    double mem_scale;

    std::vector<key_type*> buckets;

    // In the all-to-all key exchange stage, these store the first slots of the
    // threads' buckets that are not filled yet, and they will be the total
    // numbers of keys in the bucket when this stage is finished
    size_t* recv_offsets;

    // Store the numbers of received keys for each thread, for verification
    size_t* n_recv_keys;
};

// For the verification stage
size_t total_exchanged_keys;

// For collecting timing data
double T_pe, T_sum;

long pSync[SHMEM_REDUCE_SYNC_SIZE];
double pWrk[SHMEM_REDUCE_MIN_WRKDATA_SIZE];


void print_help(const params_t& pr)
{
    std::cout << "Options:\n"
              << "    -h             Prints this help message\n"
              << "    -c             Use contexts (default: disabled)\n"
              << "    -n             Use non-blocking puts (default: disabled)\n"
              << "    -p             Use context pipelining (implies -c) (default: disabled)\n"
              << "    -i <iters>     Number of iterations (default: " << pr.iters << ")\n"
              << "    -t <n_threads> Number of threads per PE (default: " << pr.n_threads << ")\n"
              << "    -s <n_keys>    Test strong scalability by specifying the total number of keys\n"
              << "    -w <n_keys_th> Test weak scalability by specifying the number of keys per thread\n"
              << "    -m <scale>     Memory scaling factor for buckets (default: " << pr.mem_scale << ")\n"
              << "    -r <SCHEDULE>  Specify the scheduling of the all-to-all key exchange\n"
              << "                       SCHEDULE = 0: Round Robin (default)\n"
              << "                       SCHEDULE = 1: Incast\n"
              << "                       SCHEDULE = 2: Random\n";
}


// Parse commandline arguments and initialize global parameters
// Returns true if something goes wrong
bool parse_args(const int argc, char** argv, params_t& pr)
{
    int c;
    while ((c = getopt(argc, argv, "hcnpi:t:s:w:m:r:")) != -1) {
        switch (c) {
            case 'h':
                print_help(pr);
                return true;
            case 'c':
                pr.use_ctx = true;
                break;
            case 'n':
                pr.use_nbi = true;
                break;
            case 'p':
                pr.use_ctx = true;
                pr.use_pipelining = true;
                break;
            case 'i':
                pr.iters = std::atol(optarg);
                break;
            case 't':
                pr.n_threads = std::atoi(optarg);
                break;
            case 's':
                pr.n_keys = std::atol(optarg);
                break;
            case 'w':
                pr.n_keys_th = std::atol(optarg);
                break;
            case 'm':
                pr.mem_scale = std::atof(optarg);
                break;
            case 'r':
                {
                    const int type = std::atoi(optarg);
                    if (type == 0) {
                        pr.comm = Sched::RoundRobin;
                    } else if (type == 1) {
                        pr.comm = Sched::Incast;
                    } else {
                        pr.comm = Sched::Random;
                    }
                    break;
                }
            default:
                break;
        }
    }

    return false;
}


void init_params(params_t& pr, const size_t npes)
{
    pr.n_buckets = npes * pr.n_threads;

    // If we didn't set n_keys_th, compute it from n_keys
    if (pr.n_keys_th == 0)
        pr.n_keys_th = std::ceil(double(pr.n_keys) / pr.n_buckets);

    // And make sure npes divides n_keys
    pr.n_keys = npes * pr.n_threads * pr.n_keys_th;

    // Uniform bucket width for all the buckets
    pr.bucket_width = std::ceil(double(MAX_KEY) / pr.n_buckets);

    // Allocate buckets on the symmetric heap (w/ scaling)
    const size_t bucket_len = size_t(pr.n_keys_th * pr.mem_scale);
    const size_t page_size  = sysconf(_SC_PAGESIZE);

    for (size_t i = 0; i < pr.n_threads; i++) {
        auto bucket = (key_type*)shmem_align(page_size, bucket_len * sizeof(key_type));
        assert(bucket != nullptr);
        pr.buckets.push_back(bucket);
    }

    // Other thread-specific variables
    pr.recv_offsets = (size_t*)shmem_malloc(pr.n_threads * sizeof(size_t));
    pr.n_recv_keys  = (size_t*)shmem_malloc(pr.n_threads * sizeof(size_t));

    T_pe = 0.0;
}


void bucket_sort(params_t& pr, double& T_pe)
{
    const size_t mype = shmem_my_pe();
    const size_t npes = shmem_n_pes();
    const size_t tid  = omp_get_thread_num();

    shmem_ctx_t ctx_amo, ctx_put;

    if (pr.use_ctx) {
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx_amo);
        shmem_ctx_quiet(ctx_amo);

        if (pr.use_pipelining) {
            shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx_put);
            shmem_ctx_quiet(ctx_put);
        } else {
            ctx_put = ctx_amo;
        }

    } else {
        ctx_amo = SHMEM_CTX_DEFAULT;
        ctx_put = SHMEM_CTX_DEFAULT;
    }

    std::mt19937_64 rng(mype * pr.n_threads + tid);
    std::uniform_int_distribution<key_type> dis(0, MAX_KEY);

    // Generate random keys and increase the local counter for the corresponding bucket
    std::vector<key_type> keys(pr.n_keys_th);
    std::vector<size_t> local_bucket_sizes(pr.n_buckets);
    for (size_t i = 0; i < pr.n_keys_th; i++) {
        keys[i] = dis(rng);
        local_bucket_sizes[keys[i] / pr.bucket_width]++;
    }

    // Store the random all-to-all schedules if needed
    std::vector<size_t> shuffled_pes(npes);
    for (size_t p = 0; p < npes; p++) {
        shuffled_pes[p] = p;
    }


    // To partition all local keys based on which bucket they belong to, and
    // put them into a send buffer, we need to first compute the starting
    // offset of each bucket in the buffer
    // Another identical array is needed to record the next empty slot of each
    // of the local bucket in the buffer when we fill it, this array will be
    // destroyed in the process
    std::vector<size_t> send_buffer_offsets(pr.n_buckets, 0);
    for (size_t i = 1; i < pr.n_buckets; i++) {
        send_buffer_offsets[i] = send_buffer_offsets[i - 1] + local_bucket_sizes[i - 1];
    }
    std::vector<size_t> next_slots = send_buffer_offsets;

    // Create and fill the bucketed send buffer
    auto send_buffer = std::make_unique<key_type[]>(pr.n_keys_th);
    for (size_t i = 0; i < pr.n_keys_th; i++) {
        send_buffer[next_slots[keys[i] / pr.bucket_width]++] = keys[i];
    }

    const size_t warmup_iters = std::max(size_t(20), pr.iters / 10);

    timespec t0, t1;

    // Begin all-to-all key exchange
    for (size_t i = 0; i < pr.iters + warmup_iters; i++) {
        // Clear the counters
        pr.recv_offsets[tid] = 0;
        pr.n_recv_keys[tid]  = 0;

        // Generate a new random all-to-all schedule if random scheduling is enabled
        if (pr.comm == Sched::Random) {
            std::shuffle(shuffled_pes.begin(), shuffled_pes.end(), rng);
        }

        #pragma omp barrier
        #pragma omp master
        {
            total_exchanged_keys = 0;

            if (i == warmup_iters) {
                T_pe = 0.0;
            }

            shmem_barrier_all();
        }
        #pragma omp barrier

        // Start the timer
        clock_gettime(CLOCK_MONOTONIC, &t0);

        // Send keys
        for (size_t _p = 0; _p < npes; _p++) {
            size_t p;

            switch (pr.comm) {
                case Sched::Incast:
                    p = _p;
                    break;
                case Sched::RoundRobin:
                    p = (mype + _p) % npes;
                    break;
                default:
                    p = shuffled_pes[_p];
                    break;
            }

            for (size_t t = 0; t < pr.n_threads; t++) {
                const size_t bucket_id   = p * pr.n_threads + t;
                const size_t send_offset = send_buffer_offsets[bucket_id];
                const size_t send_size   = local_bucket_sizes[bucket_id];
                const size_t recv_offset = shmem_ctx_size_atomic_fetch_add(ctx_amo, &pr.recv_offsets[t], send_size, p);

                if (pr.use_nbi) {
                    shmem_ctx_putmem_nbi(ctx_put, &pr.buckets[t][recv_offset], &send_buffer[send_offset], send_size * sizeof(key_type), p);
                } else {
                    shmem_ctx_putmem(ctx_put, &pr.buckets[t][recv_offset], &send_buffer[send_offset], send_size * sizeof(key_type), p);
                }
            }
        }

        // Ensure remote completion
        shmem_ctx_quiet(ctx_put);

        clock_gettime(CLOCK_MONOTONIC, &t1);

        #pragma omp atomic
        T_pe += (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1000000.0;

        #pragma omp barrier
        #pragma omp master
        shmem_sync_all();
        #pragma omp barrier

        // Local sorting should happen here, but we skip this step

        // Make sure that there were no overflow
        assert(pr.recv_offsets[tid] <= size_t(pr.n_keys_th * pr.mem_scale));

        // Make sure that every thread received something (may fail for small numbers of keys)
        assert(pr.recv_offsets[tid] > 0);

        // Verify that all keys in our bucket are within range
        const key_type my_min_key = ((mype * pr.n_threads) + tid) * pr.bucket_width;
        const key_type my_max_key = my_min_key + pr.bucket_width;
        for (size_t k = 0; k < pr.recv_offsets[tid]; k++) {
            const key_type key = pr.buckets[tid][k];
            assert((key >= my_min_key) && (key < my_max_key));
        }

        // Verify sum of portions of each bucket equals to recv_offset
        for (size_t p = 0; p < npes; p++) {
            for (size_t t = 0; t < pr.n_threads; t++) {
                shmem_size_atomic_add(&pr.n_recv_keys[t], local_bucket_sizes[p * pr.n_threads + t], p);
            }
        }

        #pragma omp barrier
        #pragma omp master
        {
            // Verify that all keys are processed (sum of bucket sizes == n_keys)
            size_t n_received_keys = 0;
            for (size_t t = 0; t < pr.n_threads; t++) {
                n_received_keys += pr.recv_offsets[t];
            }
            shmem_size_atomic_add(&total_exchanged_keys, n_received_keys, 0);

            // Complete the atomics
            shmem_barrier_all();

            if (mype == 0) {
                assert(total_exchanged_keys == pr.n_keys);
            }
        }
        #pragma omp barrier

        assert(pr.n_recv_keys[tid] == pr.recv_offsets[tid]);
    }

    if (pr.use_ctx) {
        shmem_ctx_destroy(ctx_amo);

        if (pr.use_pipelining) {
            shmem_ctx_destroy(ctx_put);
        }
    }
}


void cleanup_params(params_t& pr)
{
    for (auto p : pr.buckets) {
        shmem_free(p);
    }

    shmem_free(pr.recv_offsets);
    shmem_free(pr.n_recv_keys);
}


int main(int argc, char** argv)
{
    params_t pr;

    pr.iters          = 50;
    pr.n_threads      = 1;
    pr.use_ctx        = false;
    pr.use_nbi        = false;
    pr.use_pipelining = false;
    pr.comm           = Sched::RoundRobin;
    pr.n_keys         = 1UL << 29;
    pr.n_keys_th      = 0;
    pr.mem_scale      = 1.2;

    // Exit in case anything goes wrong
    if (parse_args(argc, argv, pr)) {
        return 1;
    }

    for (int i = 0; i < SHMEM_REDUCE_SYNC_SIZE; i++) {
        pSync[i] = SHMEM_SYNC_VALUE;
    }

    int tl, tl_supported;

    if (pr.n_threads == 1) {
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

    const size_t mype = shmem_my_pe();
    const size_t npes = shmem_n_pes();

    init_params(pr, npes);

    if (mype == 0) {
        std::cout << "Starting benchmark on " << npes << " PEs, " << pr.n_threads
                  << " threads/PE, sorting " << pr.n_keys << " keys for "
                  << pr.iters << " iteration(s)\n";
    }

    #pragma omp parallel num_threads(pr.n_threads) default(none) shared(pr, T_pe)
    bucket_sort(pr, T_pe);

    shmem_double_sum_to_all(&T_sum, &T_pe, 1, 0, 0, npes, pWrk, pSync);

    if (mype == 0) {
        std::cout << "Cumulative all-to-all time (sec)           : " << T_sum / 1000.0
                  << "\nAverage all-to-all time per iteration (ms) : "
                  << T_sum / pr.n_buckets / pr.iters
                  << '\n';
    }

    cleanup_params(pr);

    shmem_finalize();
}
