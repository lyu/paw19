// ############################################################################
// NOTE: This version removed computation and only performs the halo exchange
// ############################################################################
//
//
// This program simulates the heat transfer in a 3D block of solid material,
// placed in a right-handed coordinate system.
//
// Ref: https://dournac.org/info/parallel_heat3d
//
// The simplest explicit scheme is used, so each mesh point uses a 7-point stencil.
//
// Whenever we need to pick an ordering, the z-y-x order is used.
// This means:
//  1. Use all the available slots in the z-direction first, fix x & y
//  2. When the z-column is filled up, we move to the next z-column in the y-direction
//  3. When the yz-plane is filled up, we move to the next yz-plane in the x-direction
//
// Therefore:
//  1. Neighboring PEs will likely to have consecutive sub-domain z-coordinates
//  2. When storing the mesh points in a 1D array, the z-columns of a yz-plane will be
//     stored one-after-one, then we move to the next yz-plane
//  3. As a consequence of (2), although we transfer data between a 3D array and a 2D
//     array when we pack/unpack the buffers, we can use the same triple-loop to deal
//     with different xyz-ranges and buffer dimensions, as long as the loop ordering
//     is the same everywhere. This is because one of the i/j/k index is fixed during
//     the process, so we traverse the sub-domain mesh and the buffer in the same order.
//
//
// The 3D matrices stored in each PE look like onions, there are three layers:
//  1. The outer most "shell" stores the ghost points for the six facets, which will be
//     updated at the end of every iteration, using the latest data sent by the six
//     neighbors. For each ghost facet, one of the coordinates is fixed to either [0]
//     or [npt_* + 1], the other two coordinates are in the range of [1, npt_*].
//     The edges and the corners are unused.
//
//  2. The next "shell" is the surface of the sub-domain that is managed by this PE.
//     For each sub-domain facet, one of the coordinates is fixed to either [1] or
//     [npt_*], the other two coordinates are in the range of [1, npt_*]. Notice that
//     there are overlaps in this definition, so when updating the facets, we need to
//     be careful to not update any points on the edges for more than once. In this
//     implementation, we choose to update the full yz-facets, partial xz-facets and
//     the rest of the xy-facets. However, the full facets should be copied to and
//     from the send and the receive buffers, respectively.
//
//  3. The most inner block is the interior of the sub-domain that does not need data
//     from any other sub-domain. All three coordinates of this block are in the range
//     [2, npt_* - 1].
//
//  Because we cannot overwrite the temperature of unupdated mesh points, we allocate
//  two buffers to store two copies of the sub-domain, and alternate them at the end
//  of each iteration.
#include <type_traits>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <chrono>
#include <cmath>

#include <getopt.h>
#include <shmem.h>
#include <omp.h>


#ifndef USE_DOUBLE
    #define USE_DOUBLE 0
#endif

// Using double leads to more stable results across runs with different # of PEs
using real_t = std::conditional<USE_DOUBLE == 0, float, double>::type;

// To store and sum the residual
real_t res_pe, res_tot;
real_t pWrk[SHMEM_REDUCE_MIN_WRKDATA_SIZE];
long pSync[SHMEM_REDUCE_SYNC_SIZE];


// Identify the six facets of a sub-domain
enum class facet_t : int {
    XU   = 0,
    XD   = 1,
    YU   = 2,
    YD   = 3,
    ZU   = 4,
    ZD   = 5,
    LAST = 6
};


// Stores info about a particular facet in a unified form
struct facet_info {
    // The send buffer and receive buffer of this facet
    real_t *sbf, *rbf;
    // Length of the buffers
    size_t bf_len;
    // The neighbor PE in this facet's direction
    size_t nbr_pe;
    // The facet on the neighbor PE that is connected to this facet (U<->D)
    facet_t nbr_FT;
    // Indices below take ghost points into account and are exact, so use
    // <= when iterating over the 3D matrices
    // The starting & ending indices for the outer shell ghost facets
    // Use these to copy to the outer shell facets
    size_t osf_xs, osf_xe, osf_ys, osf_ye, osf_zs, osf_ze;
    // The starting & ending indices for the inner shell sub-domain facets
    // Use these to copy from the inner shell facets
    size_t isf_xs, isf_xe, isf_ys, isf_ye, isf_zs, isf_ze;
    // The starting & ending indices for the inner shell sub-domain facets, deduplicated
    // Use these to update the inner shell facets
    size_t isd_xs, isd_xe, isd_ys, isd_ye, isd_zs, isd_ze;
};


// Stores various simulation parameters
struct params_t {
    size_t mype, npes;
    // Number of sub-domains in each direction
    size_t nsd_x, nsd_y, nsd_z;
    // Sub-domain coordinates of this PE
    size_t sdc_x, sdc_y, sdc_z;
    // Neighbor PEs in each direction, up & down
    // Assuming periodic BC, so the neighbors wraparound
    size_t nbrs[int(facet_t::LAST)];
    // Number of mesh points on each side of the sub-domain, sans the ghost arrays
    size_t npt_x, npt_y, npt_z;
    // The total number of non-ghost points in the entire mesh, stored as a floating
    // point number so we don't need to do conversion every time
    real_t tot_pts;
    // Store the sub-domain (including the ghost arrays), for the current time step
    // and the next time step
    real_t *sd_flat_1, *sd_flat_2;
    // The 3D arrays that will be used to address the sub-domain
    real_t ***sd_old, ***sd_new;
    // Send buffers for the six facets (on the private heap)
    real_t* sbfs[int(facet_t::LAST)];
    // Receive buffers for the six facets (on the symmetric heap)
    real_t* rbfs[int(facet_t::LAST)];
    // Simulation parameters
    real_t K, ds, dt, dsl_x, dsl_y, dsl_z, cnv_tol;
    // We use a cubic mesh
    size_t mesh_len, max_iter, n_threads;
    // Store information of the six facets in the sub-domain
    // Determined by the topology of the PEs, won't change during the simulation
    facet_info fis[int(facet_t::LAST)];
};


// Records which facet(s) a certain thread is responsible for, and the SHMEM
// contexts that will be used by this thread
struct th_comm_t {
    size_t tid, n_fcs;
    facet_t fcs[int(facet_t::LAST)];
    shmem_ctx_t ctxs[int(facet_t::LAST)];
};


// Reverse up/down facet type
facet_t reverse_facet_ud(const facet_t FT)
{
    return facet_t(((int(FT) / 2) * 2) + ((int(FT) % 2) ^ 1));
}


// Initialize this PE's sub-domain coordinates in params
void init_sdc(params_t& pr)
{
    // When assigning PEs to the grid of sub-domains:
    //  1. Fill a yz-plane before advancing in the x direction
    //  2. Fill a z-column before advancing in the y direction
    // Therefore:
    // x-coordinate is the yz-plane which this PE belongs to
    pr.sdc_x = pr.mype / (pr.nsd_y * pr.nsd_z);
    // y-coordinate is the z-column which this PE belongs to
    pr.sdc_y = (pr.mype - pr.sdc_x * pr.nsd_y * pr.nsd_z) / pr.nsd_z;
    // z-coordinate is the elevation of this PE's grid point from the xy-plane
    pr.sdc_z = pr.mype - pr.sdc_x * pr.nsd_y * pr.nsd_z - pr.sdc_y * pr.nsd_z;
}


// Compute PE ID from provided sub-domain coordinates
size_t sdc_to_pe(const size_t x, const size_t y, const size_t z, const params_t& pr)
{
    return x * pr.nsd_y * pr.nsd_z + y * pr.nsd_z + z;
}


// Compute the IDs of this PE's six neighbors (wraparound)
void find_neighbors(params_t& pr)
{
    // Compute the coordinates for each of the directions
    const size_t crd_xu = (pr.sdc_x + 1 + pr.nsd_x) % pr.nsd_x;
    const size_t crd_xd = (pr.sdc_x - 1 + pr.nsd_x) % pr.nsd_x;
    const size_t crd_yu = (pr.sdc_y + 1 + pr.nsd_y) % pr.nsd_y;
    const size_t crd_yd = (pr.sdc_y - 1 + pr.nsd_y) % pr.nsd_y;
    const size_t crd_zu = (pr.sdc_z + 1 + pr.nsd_z) % pr.nsd_z;
    const size_t crd_zd = (pr.sdc_z - 1 + pr.nsd_z) % pr.nsd_z;

    // Compute the IDs of the neighboring PEs
    pr.nbrs[int(facet_t::XU)] = sdc_to_pe(crd_xu, pr.sdc_y, pr.sdc_z, pr);
    pr.nbrs[int(facet_t::XD)] = sdc_to_pe(crd_xd, pr.sdc_y, pr.sdc_z, pr);
    pr.nbrs[int(facet_t::YU)] = sdc_to_pe(pr.sdc_x, crd_yu, pr.sdc_z, pr);
    pr.nbrs[int(facet_t::YD)] = sdc_to_pe(pr.sdc_x, crd_yd, pr.sdc_z, pr);
    pr.nbrs[int(facet_t::ZU)] = sdc_to_pe(pr.sdc_x, pr.sdc_y, crd_zu, pr);
    pr.nbrs[int(facet_t::ZD)] = sdc_to_pe(pr.sdc_x, pr.sdc_y, crd_zd, pr);
}


// Allocate all the buffers required by the simulation
void alloc_storage(params_t& pr)
{
    // Storage for the sub-domain, including the ghost arrays (hence the +2)
    // pr.sd_flat_1 = new real_t[(pr.npt_x + 2) * (pr.npt_y + 2) * (pr.npt_z + 2)];
    // pr.sd_flat_2 = new real_t[(pr.npt_x + 2) * (pr.npt_y + 2) * (pr.npt_z + 2)];

    // Allocate and initialize the 3D array pointer for the sub-domain
    // i-indices identify yz-slices => npt_x + 2 slices
    // pr.sd_old = new real_t**[pr.npt_x + 2];
    // pr.sd_new = new real_t**[pr.npt_x + 2];

    // for (size_t i = 0; i < pr.npt_x + 2; i++) {
    //     // j-indices identify z-columns in a yz-slice => npt_y + 2 columns
    //     pr.sd_old[i] = new real_t*[pr.npt_y + 2];
    //     pr.sd_new[i] = new real_t*[pr.npt_y + 2];

    //     for (size_t j = 0; j < pr.npt_y + 2; j++) {
    //         // Find the start of the z-column in the current yz-slice
    //         pr.sd_old[i][j] = &pr.sd_flat_1[i * ((pr.npt_y + 2) * (pr.npt_z + 2)) + j * (pr.npt_z + 2)];
    //         pr.sd_new[i][j] = &pr.sd_flat_2[i * ((pr.npt_y + 2) * (pr.npt_z + 2)) + j * (pr.npt_z + 2)];
    //     }
    // }

    // Allocate the send buffers for the ghost arrays on the heap
    pr.sbfs[int(facet_t::XU)] = new real_t[pr.npt_y * pr.npt_z];
    pr.sbfs[int(facet_t::XD)] = new real_t[pr.npt_y * pr.npt_z];
    pr.sbfs[int(facet_t::YU)] = new real_t[pr.npt_x * pr.npt_z];
    pr.sbfs[int(facet_t::YD)] = new real_t[pr.npt_x * pr.npt_z];
    pr.sbfs[int(facet_t::ZU)] = new real_t[pr.npt_x * pr.npt_y];
    pr.sbfs[int(facet_t::ZD)] = new real_t[pr.npt_x * pr.npt_y];

    // Allocate the receive buffers for the ghost arrays on the symmetric heap
    pr.rbfs[int(facet_t::XU)] = (real_t*)shmem_malloc(pr.npt_y * pr.npt_z * sizeof(real_t));
    pr.rbfs[int(facet_t::XD)] = (real_t*)shmem_malloc(pr.npt_y * pr.npt_z * sizeof(real_t));
    pr.rbfs[int(facet_t::YU)] = (real_t*)shmem_malloc(pr.npt_x * pr.npt_z * sizeof(real_t));
    pr.rbfs[int(facet_t::YD)] = (real_t*)shmem_malloc(pr.npt_x * pr.npt_z * sizeof(real_t));
    pr.rbfs[int(facet_t::ZU)] = (real_t*)shmem_malloc(pr.npt_x * pr.npt_y * sizeof(real_t));
    pr.rbfs[int(facet_t::ZD)] = (real_t*)shmem_malloc(pr.npt_x * pr.npt_y * sizeof(real_t));
}


// Given a specific facet type, initialize its facet info stored in params
// Notice that this must happen after the buffers have been allocated
facet_info make_facet_info(const facet_t FT, params_t& pr)
{
    facet_info fi;

    fi.sbf    = pr.sbfs[int(FT)];
    fi.rbf    = pr.rbfs[int(FT)];
    fi.nbr_pe = pr.nbrs[int(FT)];
    fi.nbr_FT = reverse_facet_ud(FT);

    // wow, such brute-force, so error-prone
    switch (FT) {
        case facet_t::XU:
            fi.bf_len = pr.npt_y * pr.npt_z;

            fi.osf_xs = pr.npt_x + 1;       // X-Front of the outer shell
            fi.osf_xe = pr.npt_x + 1;       // X-Front of the outer shell
            fi.osf_ys = 1;                  // Y range covers the whole
            fi.osf_ye = pr.npt_y;           // sub-domain facet
            fi.osf_zs = 1;                  // Z range covers the whole
            fi.osf_ze = pr.npt_z;           // sub-domain facet

            fi.isf_xs = pr.npt_x;           // X-Front of the inner shell
            fi.isf_xe = pr.npt_x;           // X-Front of the inner shell
            fi.isf_ys = 1;                  // Y range covers the whole
            fi.isf_ye = pr.npt_y;           // sub-domain facet
            fi.isf_zs = 1;                  // Z range covers the whole
            fi.isf_ze = pr.npt_z;           // sub-domain facet

            fi.isd_xs = pr.npt_x;           // X-Front of the inner shell
            fi.isd_xe = pr.npt_x;           // X-Front of the inner shell
            fi.isd_ys = 1;                  // Y range covers the whole
            fi.isd_ye = pr.npt_y;           // sub-domain facet
            fi.isd_zs = 1;                  // Z range covers the whole
            fi.isd_ze = pr.npt_z;           // sub-domain facet
            break;

        case facet_t::XD:
            fi.bf_len = pr.npt_y * pr.npt_z;

            fi.osf_xs = 0;                  // X-Back of the outer shell
            fi.osf_xe = 0;                  // X-Back of the outer shell
            fi.osf_ys = 1;                  // Y range covers the whole
            fi.osf_ye = pr.npt_y;           // sub-domain facet
            fi.osf_zs = 1;                  // Z range covers the whole
            fi.osf_ze = pr.npt_z;           // sub-domain facet

            fi.isf_xs = 1;                  // X-Back of the inner shell
            fi.isf_xe = 1;                  // X-Back of the inner shell
            fi.isf_ys = 1;                  // Y range covers the whole
            fi.isf_ye = pr.npt_y;           // sub-domain facet
            fi.isf_zs = 1;                  // Z range covers the whole
            fi.isf_ze = pr.npt_z;           // sub-domain facet

            fi.isd_xs = 1;                  // X-Back of the inner shell
            fi.isd_xe = 1;                  // X-Back of the inner shell
            fi.isd_ys = 1;                  // Y range covers the whole
            fi.isd_ye = pr.npt_y;           // sub-domain facet
            fi.isd_zs = 1;                  // Z range covers the whole
            fi.isd_ze = pr.npt_z;           // sub-domain facet
            break;

        case facet_t::YU:
            fi.bf_len = pr.npt_x * pr.npt_z;

            fi.osf_xs = 1;                  // X range covers the whole
            fi.osf_xe = pr.npt_x;           // sub-domain facet
            fi.osf_ys = pr.npt_y + 1;       // Y-Front of the outer shell
            fi.osf_ye = pr.npt_y + 1;       // Y-Front of the outer shell
            fi.osf_zs = 1;                  // Z range covers the whole
            fi.osf_ze = pr.npt_z;           // sub-domain facet

            fi.isf_xs = 1;                  // X range covers the whole
            fi.isf_xe = pr.npt_x;           // sub-domain facet
            fi.isf_ys = pr.npt_y;           // Y-Front of the inner shell
            fi.isf_ye = pr.npt_y;           // Y-Front of the inner shell
            fi.isf_zs = 1;                  // Z range covers the whole
            fi.isf_ze = pr.npt_z;           // sub-domain facet

            fi.isd_xs = 2;                  // Avoid duplicating the
            fi.isd_xe = pr.npt_x - 1;       // X-Front and X-Back
            fi.isd_ys = pr.npt_y;           // Y-Front of the inner shell
            fi.isd_ye = pr.npt_y;           // Y-Front of the inner shell
            fi.isd_zs = 1;                  // Z range covers the whole
            fi.isd_ze = pr.npt_z;           // sub-domain facet
            break;

        case facet_t::YD:
            fi.bf_len = pr.npt_x * pr.npt_z;

            fi.osf_xs = 1;                  // X range covers the whole
            fi.osf_xe = pr.npt_x;           // sub-domain facet
            fi.osf_ys = 0;                  // Y-Back of the outer shell
            fi.osf_ye = 0;                  // Y-Back of the outer shell
            fi.osf_zs = 1;                  // Z range covers the whole
            fi.osf_ze = pr.npt_z;           // sub-domain facet

            fi.isf_xs = 1;                  // X range covers the whole
            fi.isf_xe = pr.npt_x;           // sub-domain facet
            fi.isf_ys = 1;                  // Y-Back of the inner shell
            fi.isf_ye = 1;                  // Y-Back of the inner shell
            fi.isf_zs = 1;                  // Z range covers the whole
            fi.isf_ze = pr.npt_z;           // sub-domain facet

            fi.isd_xs = 2;                  // Avoid duplicating the
            fi.isd_xe = pr.npt_x - 1;       // X-Front and X-Back
            fi.isd_ys = 1;                  // Y-Back of the inner shell
            fi.isd_ye = 1;                  // Y-Back of the inner shell
            fi.isd_zs = 1;                  // Z range covers the whole
            fi.isd_ze = pr.npt_z;           // sub-domain facet
            break;

        case facet_t::ZU:
            fi.bf_len = pr.npt_x * pr.npt_y;

            fi.osf_xs = 1;                  // X range covers the whole
            fi.osf_xe = pr.npt_x;           // sub-domain facet
            fi.osf_ys = 1;                  // Y range covers the whole
            fi.osf_ye = pr.npt_y;           // sub-domain facet
            fi.osf_zs = pr.npt_z + 1;       // Z-Front of the outer shell
            fi.osf_ze = pr.npt_z + 1;       // Z-Front of the outer shell

            fi.isf_xs = 1;                  // X range covers the whole
            fi.isf_xe = pr.npt_x;           // sub-domain facet
            fi.isf_ys = 1;                  // Y range covers the whole
            fi.isf_ye = pr.npt_y;           // sub-domain facet
            fi.isf_zs = pr.npt_z;           // Z-Front of the inner shell
            fi.isf_ze = pr.npt_z;           // Z-Front of the inner shell

            fi.isd_xs = 2;                  // Avoid duplicating the
            fi.isd_xe = pr.npt_x - 1;       // X-Front and X-Back
            fi.isd_ys = 2;                  // Avoid duplicating the
            fi.isd_ye = pr.npt_y - 1;       // Y-Front and Y-Back
            fi.isd_zs = pr.npt_z;           // Z-Front of the inner shell
            fi.isd_ze = pr.npt_z;           // Z-Front of the inner shell
            break;

        case facet_t::ZD:
            fi.bf_len = pr.npt_x * pr.npt_y;

            fi.osf_xs = 1;                  // X range covers the whole
            fi.osf_xe = pr.npt_x;           // sub-domain facet
            fi.osf_ys = 1;                  // Y range covers the whole
            fi.osf_ye = pr.npt_y;           // sub-domain facet
            fi.osf_zs = 0;                  // Z-Back of the outer shell
            fi.osf_ze = 0;                  // Z-Back of the outer shell

            fi.isf_xs = 1;                  // X range covers the whole
            fi.isf_xe = pr.npt_x;           // sub-domain facet
            fi.isf_ys = 1;                  // Y range covers the whole
            fi.isf_ye = pr.npt_y;           // sub-domain facet
            fi.isf_zs = 1;                  // Z-Back of the inner shell
            fi.isf_ze = 1;                  // Z-Back of the inner shell

            fi.isd_xs = 2;                  // Avoid duplicating the
            fi.isd_xe = pr.npt_x - 1;       // X-Front and X-Back
            fi.isd_ys = 2;                  // Avoid duplicating the
            fi.isd_ye = pr.npt_y - 1;       // Y-Front and Y-Back
            fi.isd_zs = 1;                  // Z-Back of the inner shell
            fi.isd_ze = 1;                  // Z-Back of the inner shell
            break;

        default:
            shmem_global_exit(1);
            break;
    }

    return fi;
}


void print_help(const params_t& pr)
{
    std::cout << "Options:\n"
              << "    -x <dim>  Number of sub-domains in the x-direction (default: " << pr.nsd_x << ")\n"
              << "    -y <dim>  Number of sub-domains in the y-direction (default: " << pr.nsd_y << ")\n"
              << "    -z <dim>  Number of sub-domains in the z-direction (default: " << pr.nsd_z << ")\n"
              << "    -T <tol>  Convergence tolerance (default: " << pr.cnv_tol << ")\n"
              << "    -I <iter> Maximum number of iterations (default: " << pr.max_iter << ")\n"
              << "    -M <len>  Side length of the mesh (default: " << pr.mesh_len << ")\n"
              << "    -t <num>  Number of threads per PE (default: " << pr.n_threads << ")\n";
}


bool parse_args(int argc, char** argv, params_t& pr)
{
    int c;
    while ((c = getopt(argc, argv, "x:y:z:T:I:M:t:")) != -1) {
        switch (c) {
            case 'x':
                pr.nsd_x = std::atoi(optarg);
                break;
            case 'y':
                pr.nsd_y = std::atoi(optarg);
                break;
            case 'z':
                pr.nsd_z = std::atoi(optarg);
                break;
            case 'T':
                pr.cnv_tol = std::atof(optarg);
                break;
            case 'I':
                pr.max_iter = std::atoi(optarg);
                break;
            case 'M':
                pr.mesh_len = std::atoi(optarg);
                break;
            case 't':
                pr.n_threads = std::atoi(optarg);
                break;
            default:
                print_help(pr);
                return true;
        }
    }

    return false;
}


// Initialize the params struct
void init_params(params_t& pr)
{
    pr.mype = shmem_my_pe();
    pr.npes = shmem_n_pes();

    // One-to-one and onto
    if (pr.npes != pr.nsd_x * pr.nsd_y * pr.nsd_z) {
        if (pr.mype == 0) {
            std::cout << "Error: Number of PEs doesn't equal to the number of sub-domains!\n";
        }
        shmem_global_exit(1);
    }

    // Compute sub-domain coordinates, locate where this PE is inside the
    // grid of sub-domains
    init_sdc(pr);

    if (sdc_to_pe(pr.sdc_x, pr.sdc_y, pr.sdc_z, pr) != pr.mype) {
        if (pr.mype == 0) {
            std::cout << "Error: Incorrect PE coordinates!\n";
        }
        shmem_global_exit(1);
    }

    // Locate and store the IDs of our nearest neighbors
    find_neighbors(pr);

    // Side lengths of the entire cubic simulation domain
    const real_t dsl = 1.0;
    pr.dsl_x         = dsl;
    pr.dsl_y         = dsl;
    pr.dsl_z         = dsl;

    // Check if the mesh size is too small, or if it isn't divisible by the
    // number of sub-domains in any direction
    if (((pr.mesh_len % pr.nsd_x) != 0) ||
        ((pr.mesh_len % pr.nsd_y) != 0) ||
        ((pr.mesh_len % pr.nsd_z) != 0)) {
        if (pr.mype == 0) {
            std::cout << "Error: Bad mesh size!\n";
        }
        shmem_global_exit(1);
    }

    // Numbers of mesh points on each side of the sub-domains
    pr.npt_x = pr.mesh_len / pr.nsd_x;
    pr.npt_y = pr.mesh_len / pr.nsd_y;
    pr.npt_z = pr.mesh_len / pr.nsd_z;

    // The total number of non-ghost points in the entire mesh
    pr.tot_pts = real_t(pr.npt_x * pr.npt_y * pr.npt_z * pr.npes);

    // Spacial discretization length, uniform in all three dimensions
    pr.ds = dsl / pr.mesh_len;

    // Thermal conductivity parameter
    pr.K = 1.0;

    // Calculate dt, respecting the convergence criterion
    pr.dt = pr.ds * pr.ds / (8.1 * pr.K);

    // Prepare the buffers
    alloc_storage(pr);

    // When the buffers are ready and we know who our neighbors are, we can
    // associate them with each of the six facets
    for (int i = 0; i < int(facet_t::LAST); i++)
        pr.fis[i] = make_facet_info(facet_t(i), pr);
}


// Specify a facet, copy the mesh data to its send buffer
void pack_send_buffer(const facet_t FT, params_t& pr)
{
    const facet_info& fi      = pr.fis[int(FT)];
    const real_t*** const sdp = const_cast<const real_t***>(pr.sd_new);
    real_t* const sbf         = fi.sbf;

    // Copying from the inner shell facets, use isf_*
    const size_t x_r = fi.isf_xe - fi.isf_xs + 1;
    const size_t y_r = fi.isf_ye - fi.isf_ys + 1;
    const size_t z_r = fi.isf_ze - fi.isf_zs + 1;

    #pragma omp for collapse(2) schedule(static)
    for (size_t i = 0; i < x_r; i++) {
        for (size_t j = 0; j < y_r; j++) {
            for (size_t k = 0; k < z_r; k++) {
                sbf[i * y_r * z_r + j * z_r + k] = sdp[i + fi.isf_xs][j + fi.isf_ys][k + fi.isf_zs];
            }
        }
    }
}


// Prepare the send buffers after we have initialized the non-ghost points in
// our sub-domain
void init_pack_send_buffers(params_t& pr)
{
    for (int i = 0; i < int(facet_t::LAST); i++) {
        pack_send_buffer(facet_t(i), pr);
    }
}


// Specify a facet, send the send buffer to the correct receive buffer on the neighbor PE
// Uses non-blocking putmem, does not ensure completion
void send_facet(const facet_t FT, params_t& pr, shmem_ctx_t& ctx)
{
    facet_info& fi     = pr.fis[int(FT)];
    facet_info& nbr_fi = pr.fis[int(fi.nbr_FT)];

    shmem_ctx_putmem_nbi(ctx, nbr_fi.rbf, fi.sbf, fi.bf_len * sizeof(real_t), fi.nbr_pe);
}


// Send all the six facets to this PE's neighbors during initialization
// Does not ensure completion
void init_halo_exchange(params_t& pr)
{
    for (int i = 0; i < int(facet_t::LAST); i++) {
        send_facet(facet_t(i), pr, SHMEM_CTX_DEFAULT);
    }
}


// Initialize the temperature distribution
void init_temperature(params_t& pr)
{
    // Let's put a high temperature ball in the center of the simulation domain
    const real_t ambient = 20.0;
    const real_t hi_temp = 1000.0;

    // Radius of the ball will be 1/10 of the shortest side length
    const real_t ball_r = std::min({pr.dsl_x, pr.dsl_y, pr.dsl_z}) / 10.0;
    const real_t ball_r2 = ball_r * ball_r;

    // Go through all the non-ghost points in our sub-domain
    // If the distance between the current point and the center of the domain
    // is greater than r^2, then it is not part of the ball
    for (size_t i = 1; i <= pr.npt_x; i++) {
        const real_t x = pr.ds * (real_t(pr.sdc_x * pr.npt_x + i - 1) + 0.5);
        const real_t diff_x2 = std::pow(pr.dsl_x / 2 - x, 2);

        for (size_t j = 1; j <= pr.npt_y; j++) {
            const real_t y = pr.ds * (real_t(pr.sdc_y * pr.npt_y + j - 1) + 0.5);
            const real_t diff_y2 = std::pow(pr.dsl_y / 2 - y, 2);

            for (size_t k = 1; k <= pr.npt_z; k++) {
                const real_t z = pr.ds * (real_t(pr.sdc_z * pr.npt_z + k - 1) + 0.5);
                const real_t diff_z2 = std::pow(pr.dsl_z / 2 - z, 2);

                if (diff_x2 + diff_y2 + diff_z2 > ball_r2) {
                    pr.sd_new[i][j][k] = ambient;
                } else {
                    pr.sd_new[i][j][k] = hi_temp;
                }
            }
        }
    }
}


// Specify a facet, copy the received data from its receive buffer to the mesh
void unpack_recv_buffer_helper(const facet_t FT, params_t& pr)
{
    const facet_info& fi    = pr.fis[int(FT)];
    real_t*** const sdp     = pr.sd_new;
    const real_t* const rbf = fi.rbf;

    // Copying to the outer shell facets, use osf_*
    const size_t x_r = fi.osf_xe - fi.osf_xs + 1;
    const size_t y_r = fi.osf_ye - fi.osf_ys + 1;
    const size_t z_r = fi.osf_ze - fi.osf_zs + 1;

    #pragma omp for collapse(2) schedule(static)
    for (size_t i = 0; i < x_r; i++) {
        for (size_t j = 0; j < y_r; j++) {
            for (size_t k = 0; k < z_r; k++) {
                sdp[i + fi.osf_xs][j + fi.osf_ys][k + fi.osf_zs] = rbf[i * y_r * z_r + j * z_r + k];
            }
        }
    }
}


// Copy the received ghost arrays from the receive buffers
void unpack_recv_buffers(params_t& pr)
{
    for (int i = 0; i < int(facet_t::LAST); i++) {
        unpack_recv_buffer_helper(facet_t(i), pr);
    }
}


// Specify a facet, calculate the new temperature distribution for the next time step
// Returns the residual
real_t update_facet(const facet_t FT, params_t& pr)
{
    facet_info& fi = pr.fis[int(FT)];

    real_t residual = 0.0;
    const real_t weight = pr.K * pr.dt / (pr.ds * pr.ds);

    // Updating the inner shell facets, use deduplicated isd_*
    // Couldn't pack the data into the send buffers b/c we are using
    // deduplicated indices
    #pragma omp for collapse(2) schedule(static)
    for (size_t i = fi.isd_xs; i <= fi.isd_xe; i++) {
        for (size_t j = fi.isd_ys; j <= fi.isd_ye; j++) {
            for (size_t k = fi.isd_zs; k <= fi.isd_ze; k++) {
                const real_t u = weight * ( pr.sd_old[i-1][j][k] + pr.sd_old[i+1][j][k]
                                          + pr.sd_old[i][j-1][k] + pr.sd_old[i][j+1][k]
                                          + pr.sd_old[i][j][k-1] + pr.sd_old[i][j][k+1]
                                          - 6.0 * pr.sd_old[i][j][k]
                                          );

                pr.sd_new[i][j][k] = pr.sd_old[i][j][k] + u;

                residual += u * u;
            }
        }
    }

    return residual;
}


// Calculate the new temperature distribution in the interior of our sub-domain
// for the next time step
// Returns the residual
// TODO: red-black ordering? loop-tiling?
real_t update_interior(params_t& pr)
{
    real_t residual = 0.0;
    const real_t weight = pr.K * pr.dt / (pr.ds * pr.ds);

    #pragma omp for collapse(2) schedule(static)
    for (size_t i = 2; i <= pr.npt_x - 1; i++) {
        for (size_t j = 2; j <= pr.npt_y - 1; j++) {
            for (size_t k = 2; k <= pr.npt_z - 1; k++) {
                const real_t u = weight * ( pr.sd_old[i-1][j][k] + pr.sd_old[i+1][j][k]
                                          + pr.sd_old[i][j-1][k] + pr.sd_old[i][j+1][k]
                                          + pr.sd_old[i][j][k-1] + pr.sd_old[i][j][k+1]
                                          - 6.0 * pr.sd_old[i][j][k]
                                          );

                pr.sd_new[i][j][k] = pr.sd_old[i][j][k] + u;

                residual += u * u;
            }
        }
    }

    return residual;
}


// Perform cleanup operations when the simulation is finished
// Mostly freeing the memory
void cleanup_params(const params_t& pr)
{
    // Free the sub-domain storage
    // delete[] pr.sd_flat_1;
    // delete[] pr.sd_flat_2;

    // Free the 3D arrays
    // for (size_t i = 0; i < pr.npt_x + 2; i++) {
    //     delete[] pr.sd_old[i];
    //     delete[] pr.sd_new[i];
    // }
    // delete[] pr.sd_old;
    // delete[] pr.sd_new;

    // Free the send/receive buffers
    for (int i = 0; i < int(facet_t::LAST); i++) {
        delete[] pr.sbfs[i];
        shmem_free(pr.rbfs[i]);
    }
}


// All threads will call this function to figure out how to deal with the facets
void init_th_comm(const params_t& pr, th_comm_t& tc)
{
    tc.tid                = omp_get_thread_num();
    const size_t n_facets = size_t(facet_t::LAST);

    // Calculate the number of facets that this thread will be responsible for
    if (pr.n_threads >= n_facets) {
        // We have enough threads
        if (tc.tid < n_facets) {
            tc.n_fcs = 1;
        } else {
            tc.n_fcs = 0;
        }
    } else {
        const size_t quot = n_facets / pr.n_threads;
        const size_t rmdr = n_facets - (quot * pr.n_threads);

        if (rmdr == 0) {
            // Number of threads divides number of facets
            tc.n_fcs = quot;
        } else {
            // Otherwise, go around and assign the remaining facets
            if (tc.tid < rmdr) {
                tc.n_fcs = quot + 1;
            } else {
                tc.n_fcs = quot;
            }
        }
    }

    // Calculate which facet(s) will this thread be responsible for, and
    // create the SHMEM contexts
    for (size_t f = 0; f < tc.n_fcs; f++) {
        tc.fcs[f] = facet_t(tc.tid + f * pr.n_threads);
        #ifdef USE_CTX
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &tc.ctxs[f]);
        #else
        tc.ctxs[f] = SHMEM_CTX_DEFAULT;
        #endif
        shmem_ctx_quiet(tc.ctxs[f]);
    }
}


int main(int argc, char** argv)
{
    for (int i = 0; i < SHMEM_REDUCE_SYNC_SIZE; i++)
        pSync[i] = SHMEM_SYNC_VALUE;

    params_t pr;

    pr.nsd_x     = 4;
    pr.nsd_y     = 4;
    pr.nsd_z     = 4;
    pr.cnv_tol   = 1e-4;
    pr.max_iter  = 500;
    pr.mesh_len  = 3 * 256;
    pr.n_threads = 1;

    if (parse_args(argc, argv, pr)) {
        return 1;
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

    // Initialize all the parameters
    init_params(pr);

    // Initialize the simulation domain with a temperature distribution
    // init_temperature(pr);

    // Prepare the send buffers for the first halo exchange
    // init_pack_send_buffers(pr);

    // Make sure all the PEs have their receive buffers ready
    shmem_sync_all();

    // Perform the halo exchange
    init_halo_exchange(pr);

    shmem_barrier_all();

    // Copy the arrived ghost arrays into the shell of the sub-domain
    // unpack_recv_buffers(pr);

    // Prepare for the next time step
    std::swap(pr.sd_new, pr.sd_old);

    res_pe      = 0.0;
    double T    = 0.0;

    if (pr.mype == 0) {
        std::cout << "3D halo exchange benchmark: sub-domain mesh "
                  << pr.npt_x << " x " << pr.npt_y << " x " << pr.npt_z
                  << ", ds = " << pr.ds << ", dt = " << pr.dt << '\n';
    }

    #pragma omp parallel num_threads(pr.n_threads) \
                         default(none) \
                         shared(pr, res_pe, res_tot, T, pWrk, pSync)
    {
        th_comm_t tc;
        init_th_comm(pr, tc);

        #pragma omp barrier
        #pragma omp master
        shmem_barrier_all();
        #pragma omp barrier

        const auto t_start = std::chrono::steady_clock::now();

        for (size_t i = 0; i < pr.max_iter; i++) {
            // Each thread send the facet(s) that it is responsible for
            for (size_t f = 0; f < tc.n_fcs; f++) {
                send_facet(tc.fcs[f], pr, tc.ctxs[f]);
            }

            // Ensure the delivery of the ghost arrays
            for (size_t f = 0; f < tc.n_fcs; f++) {
                shmem_ctx_quiet(tc.ctxs[f]);
            }

            #pragma omp barrier

            // Sync and prepare for the next time step
            #pragma omp master
            {
                shmem_sync_all();
            }

            #pragma omp barrier
        }

        const auto t_end = std::chrono::steady_clock::now();

        T = std::chrono::duration<double>(t_end - t_start).count();

        #ifdef USE_CTX
        for (size_t f = 0; f < tc.n_fcs; f++) {
            shmem_ctx_destroy(tc.ctxs[f]);
        }
        #endif
    }

    if (pr.mype == 0) {
        std::cout << "Time elapsed: " << T << " seconds" << '\n';
    }

    cleanup_params(pr);

    shmem_finalize();
}
