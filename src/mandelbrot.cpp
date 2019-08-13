#include <iostream>
#include <cstdint>
#include <fstream>
#include <chrono>
#include <memory>
#include <vector>

#include <shmem.h>
#include <getopt.h>


struct config {
    size_t w, h, job_len;
    uint16_t max_iters, n_threads;
    bool use_nbi, use_ctx, use_pipelining, save_img;
};


class comm_env {
private:
    const config cf;
    shmem_ctx_t ctxs[2];
    uint8_t ctx_idx;
    uint8_t ctx_buf_idx[2];
    std::unique_ptr<uint16_t[]> ctx_buf[2][2];

public:
    comm_env(const config _cf) : cf(_cf)
    {
        if (cf.use_pipelining) {
            shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctxs[0]);
            shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctxs[1]);

            shmem_ctx_quiet(ctxs[0]);
            shmem_ctx_quiet(ctxs[1]);
        } else if (cf.use_ctx) {
            shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctxs[0]);

            shmem_ctx_quiet(ctxs[0]);

            ctxs[1] = ctxs[0];
        } else {
            ctxs[0] = SHMEM_CTX_DEFAULT;
            ctxs[1] = SHMEM_CTX_DEFAULT;
        }

        ctx_idx        = 0;
        ctx_buf_idx[0] = 0;
        ctx_buf_idx[1] = 0;

        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                ctx_buf[i][j] = std::make_unique<uint16_t[]>(cf.job_len);
            }
        }
    }

    ~comm_env()
    {
        if (cf.use_pipelining) {
            shmem_ctx_destroy(ctxs[0]);
            shmem_ctx_destroy(ctxs[1]);
        } else if (cf.use_ctx) {
            shmem_ctx_destroy(ctxs[0]);
        }
    }

    shmem_ctx_t ctx() const
    {
        return ctxs[ctx_idx];
    }

    uint16_t* buf() const
    {
        return ctx_buf[ctx_idx][ctx_buf_idx[ctx_idx]].get();
    }

    void advance()
    {
        ctx_buf_idx[ctx_idx] ^= 1;
        ctx_idx              ^= 1;
    }
};


double pWrk[SHMEM_REDUCE_MIN_WRKDATA_SIZE];
long pSync[SHMEM_REDUCE_SYNC_SIZE];

double total_t  = 0.0;
double total_wr = 0.0;
double local_t  = 0.0;
double local_wr = 0.0;


uint16_t compute_pixel(const config cf, const size_t w)
{
    const size_t cx = w % cf.w;
    const size_t cy = w / cf.w;
    const double x0 = -2.5 + cx * (4.0 / cf.w);
    const double y0 = -2.0 + cy * (4.0 / cf.h);
    double x = 0.0;
    double y = 0.0;
    double x2 = x * x;
    double y2 = y * y;

    uint16_t i;
    for (i = 0; (i < cf.max_iters) && (x2 + y2 < 4.0); i++) {
        y = 2 * x * y + y0;
        x = x2 - y2 + x0;
        x2 = x * x;
        y2 = y * y;
    }

    return cf.max_iters - i;
}


void save_image(const config cf,
                const uint16_t* image,
                const size_t npes,
                const size_t w_quot,
                const size_t w_rmdr)
{
    auto pic = std::make_unique<uint16_t[]>(cf.w * cf.h);

    for (size_t i = 0; i < npes; i++) {
        auto dest = pic.get() + i * w_quot;

        if (i < (npes - 1)) {
            shmem_uint16_get_nbi(dest, image, w_quot, i);
        } else {
            shmem_uint16_get_nbi(dest, image, w_quot + w_rmdr, i);
        }
    }


    shmem_quiet();

    std::cout << "Saving the image...\n";

    std::ofstream f("mandelbrot.pgm", std::ios::out);

    f << "P2\n" << cf.w << ' ' << cf.h << '\n' << cf.max_iters << '\n';

    for (size_t j = 0; j < cf.h; j++) {
        for (size_t i = 0; i < cf.w; i++) {
            f << pic[i + j * cf.w] << ' ';
        }
        f << '\n';
    }

    f.close();
}


void draw_mandelbrot(const config cf)
{
    const size_t mype = shmem_my_pe();
    const size_t npes = shmem_n_pes();

    const size_t w_quot  = (cf.w * cf.h) / npes;
    const size_t w_rmdr  = (cf.w * cf.h) - (w_quot * npes);
    static size_t w_next = w_quot * mype;

    std::vector<size_t> w_pes_min(npes);
    std::vector<size_t> w_pes_max(npes);
    for (size_t i = 0; i < npes; i++) {
        w_pes_min[i] = w_quot * i;

        if (i < (npes - 1)) {
            w_pes_max[i] = w_quot * (i + 1);
        } else {
            w_pes_max[i] = cf.w * cf.h;
        }
    }

    auto image = (uint16_t*)shmem_malloc(sizeof(uint16_t) * (w_quot + w_rmdr));

    shmem_barrier_all();

    if (mype == 0)
        std::cout << "Starting benchmark on " << npes << " PEs, " << cf.n_threads
                  << " threads/PE, image size: " << cf.w << " x " << cf.h
                  << '\n' << cf.job_len << " points per job, with a maximum of "
                  << cf.max_iters << " iterations per point\n";

    #pragma omp parallel num_threads(cf.n_threads)          \
                         default(none)                      \
                         firstprivate(image, cf, npes, mype)\
                         shared(w_next, w_pes_min, w_pes_max, local_t, local_wr)
    {
        comm_env cv(cf);

        auto pe_mask = std::make_unique<bool[]>(npes);
        for (size_t i = 0; i < npes; i++)
            pe_mask[i] = true;

        #pragma omp barrier
        #pragma omp master
        shmem_barrier_all();
        #pragma omp barrier

        size_t pe_pending = npes;
        size_t victim_pe  = mype;
        size_t total_work = 0;

        const auto t_start = std::chrono::steady_clock::now();

        while (pe_pending != 0) {
            do {
                victim_pe = (victim_pe + 1) % npes;
            } while (!pe_mask[victim_pe]);

            // Don't use ctx for this one
            const size_t w_start = shmem_ctx_size_atomic_fetch_add(cv.ctx(), &w_next, cf.job_len, victim_pe);
            size_t w_end = w_start + cf.job_len;

            if (w_start >= w_pes_max[victim_pe]) {
                pe_pending--;
                pe_mask[victim_pe] = false;
                continue;
            } else if (w_end >= w_pes_max[victim_pe]) {
                w_end = w_pes_max[victim_pe];
                pe_pending--;
                pe_mask[victim_pe] = false;
            }

            auto buf = cv.buf();

            for (size_t w = w_start; w < w_end; w++)
                buf[w - w_start] = compute_pixel(cf, w);

            if (cf.use_nbi) {
                shmem_ctx_quiet(cv.ctx());
                shmem_ctx_uint16_put_nbi(cv.ctx(), &image[w_start - w_pes_min[victim_pe]], buf, w_end - w_start, victim_pe);
            } else {
                shmem_ctx_uint16_put(cv.ctx(), &image[w_start - w_pes_min[victim_pe]], buf, w_end - w_start, victim_pe);
            }

            total_work += w_end - w_start;

            cv.advance();
        }

        shmem_ctx_quiet(cv.ctx());

        if (cf.use_pipelining) {
            cv.advance();
            shmem_ctx_quiet(cv.ctx());
        }

        const auto t_end = std::chrono::steady_clock::now();
        const double t = std::chrono::duration<double>(t_end - t_start).count();

        #pragma omp atomic
        local_t += t;

        #pragma omp atomic
        local_wr += total_work / t;
    }

    shmem_double_sum_to_all(&total_t, &local_t, 1, 0, 0, npes, pWrk, pSync);

    shmem_barrier_all();

    shmem_double_sum_to_all(&total_wr, &local_wr, 1, 0, 0, npes, pWrk, pSync);

    if (mype == 0) {
        std::cout << "Total cumulative runtime (sec)        : " << total_t
                  << "\nAverage thread runtime (sec)          : " << total_t / (npes * cf.n_threads)
                  << "\nTotal work rate (points/sec)          : " << total_wr
                  << "\nAverage thread work rate (points/sec) : " << total_wr / (npes * cf.n_threads)
                  << '\n';
    }

    if (cf.save_img && (mype == 0))
        save_image(cf, image, npes, w_quot, w_rmdr);

    shmem_barrier_all();

    shmem_free(image);
}


void print_help(const config cf)
{
    std::cout << "Options:\n"
              << "    -t <n_threads>  number of OpenMP threads per PE (default:" << cf.n_threads << ")\n"
              << "    -i <iterations> maximum iterations per point (default:" << cf.max_iters << ")\n"
              << "    -j <job_len>    load balancing granularity (default:" << cf.job_len << ")\n"
              << "    -w <width>      width of the Mandelbrot image (default:" << cf.w << ")\n"
              << "    -h <height>     height of the Mandelbrot image (default:" << cf.h << ")\n"
              << "    -c              use contexts (default: disabled)\n"
              << "    -b              use blocking puts (default: disabled)\n"
              << "    -p              enable pipelining (implies -c) (default: disabled)\n"
              << "    -o              save the Mandelbrot image (default: disabled)\n";
}


int main(int argc, char** argv)
{
    config cf;

    cf.w              = 32000;
    cf.h              = 32000;
    cf.job_len        = 400;
    cf.max_iters      = 1000;
    cf.n_threads      = 1;
    cf.use_nbi        = true;
    cf.use_ctx        = false;
    cf.use_pipelining = false;
    cf.save_img       = false;

    int c;
    while ((c = getopt(argc, argv, "cbpow:h:t:j:i:")) != -1) {
        switch (c) {
            case 'o':
                cf.save_img = true;
                break;
            case 'w':
                cf.w = std::atoi(optarg);
                break;
            case 'h':
                cf.h = std::atoi(optarg);
                break;
            case 't':
                cf.n_threads = std::atoi(optarg);
                break;
            case 'j':
                cf.job_len = std::atoi(optarg);
                break;
            case 'i':
                cf.max_iters = std::atoi(optarg);
                break;
            case 'c':
                cf.use_ctx = true;
                break;
            case 'b':
                cf.use_nbi = false;
                break;
            case 'p':
                cf.use_ctx = true;
                cf.use_pipelining = true;
                break;
            default:
                print_help(cf);
                return 1;
        }
    }

    for (int i = 0; i < SHMEM_REDUCE_SYNC_SIZE; i++)
        pSync[i]= SHMEM_SYNC_VALUE;

    int tl, tl_supported;

    if (cf.n_threads == 1) {
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

    draw_mandelbrot(cf);

    shmem_finalize();
}
