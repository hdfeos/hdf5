#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "hdf5.h"

#define FILENAME        "/tmp/TEST.h5"
#define DSETNAME        "DATA"
#define NELEMENTS       2500000     /* (default) 10 MB of uint32 data */

/* Chunk sizes */
#define SMALL_CHUNK     1          
#define LARGE_CHUNK     1000        /* 1D only */
#define HUGE_CHUNK      10000       /* 1D only */

#define NPARAMS         7           /* # dataset conditions */

/* Dataset expansion direction */
/* Really only matters for 2D datasets */
typedef enum expand_dir_t {
    X,
    Y,
    XY
} expand_dir_t;

typedef struct test_params_t {
    int             ndims;
    hsize_t         chunk_size;
    expand_dir_t    direction;
} test_params_t;

typedef struct test_time_t {
    long tv_sec;
    long tv_usec;
} test_time_t;

/*---------------------------------------------------------------------------*/
static int
test_time_get_current(test_time_t *tv)
{
    struct timespec tp;

    if (!tv)
        return -1;
    if (clock_gettime(CLOCK_MONOTONIC, &tp))
        return -1;

    tv->tv_sec = tp.tv_sec;
    tv->tv_usec = tp.tv_nsec / 1000;

    return 0;
}

/*---------------------------------------------------------------------------*/
static double
test_time_to_double(test_time_t tv)
{
    return (double) tv.tv_sec + (double) (tv.tv_usec) * 0.000001;
}

/*---------------------------------------------------------------------------*/
static test_time_t
test_time_add(test_time_t in1, test_time_t in2)
{
    test_time_t out;

    out.tv_sec = in1.tv_sec + in2.tv_sec;
    out.tv_usec = in1.tv_usec + in2.tv_usec;
    if(out.tv_usec > 1000000) {
        out.tv_usec -= 1000000;
        out.tv_sec += 1;
    }

    return out;
}

/*---------------------------------------------------------------------------*/
static test_time_t
test_time_subtract(test_time_t in1, test_time_t in2)
{
    test_time_t out;

    out.tv_sec = in1.tv_sec - in2.tv_sec;
    out.tv_usec = in1.tv_usec - in2.tv_usec;
    if(out.tv_usec < 0) {
        out.tv_usec += 1000000;
        out.tv_sec -= 1;
    }

    return out;
}

/*---------------------------------------------------------------------------*/
int main
(int argc, char *argv[])
{
    hid_t fid, did, sid, dcpl, msid, fsid, fapl;
    hsize_t chunks[2];
    hsize_t max[2];
    hsize_t curr[2];
    int i;
    hsize_t n = 0;
    unsigned char *buf = NULL;
    test_params_t params[NPARAMS];
    test_time_t t = {0, 0}, t1 = {0, 0}, t2 = {0, 0};
    hsize_t nelements = NELEMENTS;
    expand_dir_t direction = X;
    int new = 0;
    int nunlimited = 1;

    /* Set up test parameters */
    if (argc > 1)
        nelements = (hsize_t) atoi(argv[1]);
    if (argc > 2)
        direction = (expand_dir_t) atoi(argv[2]);
    if (argc > 3)
        new = atoi(argv[3]);
    if (argc > 4)
        nunlimited = (atoi(argv[4]) > 1) ? 2 : 1;

    /* 0, 1, 2 are 1-d */
    params[0].ndims = 1;
    params[0].chunk_size = HUGE_CHUNK;
    params[0].direction = X;

    params[1].ndims = 1;
    params[1].chunk_size = LARGE_CHUNK;
    params[1].direction = X;

    params[2].ndims = 1;
    params[2].chunk_size = SMALL_CHUNK;
    params[2].direction = X;

    /* 3, 4, 5 are 2-d */
    params[3].ndims = 2;
    params[3].chunk_size = SMALL_CHUNK;
    params[3].direction = X;

    params[4].ndims = 2;
    params[4].chunk_size = SMALL_CHUNK;
    params[4].direction = Y;

    params[5].ndims = 2;
    params[5].chunk_size = SMALL_CHUNK;
    params[5].direction = XY;

    params[6].ndims = 3;
    params[6].chunk_size = SMALL_CHUNK;
    params[6].direction = X;

    /* Use old format for BT1 */
    /* Use latest format when using EA and BT2 */
    fapl = H5Pcreate(H5P_FILE_ACCESS);
    H5Pset_libver_bounds(fapl, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST);

    /* The data buffer, which we reuse */
    buf = (unsigned char *)malloc(HUGE_CHUNK * sizeof(unsigned char *));
    memset(buf, 42, HUGE_CHUNK * sizeof(unsigned char));


    /* 3-X, 4-Y */ /* can vary with small/large/huge chunk sizes */
    /* i = 4; */

    if (new) {
        /* Create the file and dataset */
        fid = H5Fcreate(FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
        if (nunlimited > 1)
            printf("Creating file with new format--BT2\n");
        else
            printf("Creating file with new format--EA\n");
    } else {
        fid = H5Fcreate(FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
        printf("Creating file with old format--BT1\n");
    }

    switch (direction) {
        case X:
            /* X direction */
            i = 3;
            curr[0] = 0;
            curr[1] = 1;
            max[0] = H5S_UNLIMITED;
            max[1] = (nunlimited > 1) ? H5S_UNLIMITED : 1;
            break;
        case Y:
            /* Y direction */
            i = 4;
            curr[0] = 1;
            curr[1] = 0;
            max[0] = (nunlimited > 1) ? H5S_UNLIMITED : 1;
            max[1] = H5S_UNLIMITED;
            break;
        case XY:
            /* XY direction */
            i = 5;
            curr[0] = 0;
            curr[1] = 0;
            max[0] = H5S_UNLIMITED;
            max[1] = H5S_UNLIMITED;
            break;
        default:
            break;
    }

    /* Create a 2-d dataset with 1 unlimited dimension */
    assert(params[i].ndims == 2);

    sid = H5Screate_simple(params[i].ndims, curr, max);

    dcpl = H5Pcreate(H5P_DATASET_CREATE);
    printf("Create 2-d dataset: %d unlim, chunk_size = %u\n", nunlimited,
            params[i].chunk_size);

    chunks[0] = params[i].chunk_size;
    chunks[1] = params[i].chunk_size;
    H5Pset_chunk(dcpl, params[i].ndims, chunks);

    did = H5Dcreate(fid, DSETNAME, H5T_NATIVE_UCHAR, sid, H5P_DEFAULT, dcpl, H5P_DEFAULT);

    H5Pclose(dcpl);
    H5Sclose(sid);
    H5Dclose(did);
    H5Fclose(fid);

    /* Reopen file */
    if (new) {
        fid = H5Fopen(FILENAME, H5F_ACC_RDWR, fapl);
        if (nunlimited > 1)
            printf("Re-open file with latest format--BT2\n");
        else
            printf("Re-open file with latest format--EA\n");
    } else {
        fid = H5Fopen(FILENAME, H5F_ACC_RDWR, H5P_DEFAULT);
        printf("Re-open file with old format--BT1\n");
    }

    /* Reopen dataset */
    did = H5Dopen(fid, DSETNAME, H5P_DEFAULT);
    printf("open/append to dataset...to maximum %u\n", nelements);
    if(params[i].direction == X)
        printf("appending in X direction...\n");
    else if(params[i].direction == Y)
        printf("appending in Y direction...\n");
    else if(params[i].direction == XY)
        printf("appending in XY direction...\n");

    /* Loop through appending 1 element at a time */
    while(n < nelements) {

        /* We're working in chunk-sized blocks to save time */
        hsize_t new_extent = n + params[i].chunk_size;
        hsize_t start[2] = {0, 0};
        hsize_t count[2] = {0, 0};
        hsize_t extent[2] = {0, 0};

        if(params[i].direction == X) {
            count[0] = params[i].chunk_size;
            count[1] = 1;
            start[0] = n;
            start[1] = 0;
            extent[0] = new_extent;
            extent[1] = 1;
        } else if(params[i].direction == Y) {
            count[0] = 1;
            count[1] = params[i].chunk_size;
            start[0] = 0;
            start[1] = n;
            extent[0] = 1;
            extent[1] = new_extent;
        } else if(params[i].direction == XY) {
            count[0] = params[i].chunk_size;
            count[1] = params[i].chunk_size;
            start[0] = n;
            start[1] = n;
            extent[0] = new_extent;
            extent[1] = new_extent;
        }

        H5Dset_extent(did, extent);

        /* Set up memory dataspace */
        if((msid = H5Screate_simple(params[i].ndims, count, NULL)) < 0) {
            fprintf(stderr, "\n*** ERROR *** Can't create memory dataspace\n");
            exit(-1);
        }

        /* Get file dataspace */
        if((fsid = H5Dget_space(did)) < 0) {
            fprintf(stderr, "\n*** ERROR *** Can't get file dataset space\n");
            exit(-1);
        }
        if((H5Sselect_hyperslab(fsid, H5S_SELECT_SET, start, NULL, count, NULL)) < 0) {
            fprintf(stderr, "\n*** ERROR *** Can't select hyperslab\n");
            exit(-1);
        }

        test_time_get_current(&t1);

        /* Write to the dataset */
        if(H5Dwrite(did, H5T_NATIVE_UCHAR, msid, fsid, H5P_DEFAULT, buf) < 0) {
            fprintf(stderr, "\n*** ERROR *** Can't write to dataset\n");
            exit(-1);
        }

        test_time_get_current(&t2);
        t = test_time_add(t, test_time_subtract(t2, t1));

        /* Get ready for the next iteration */
        H5Sclose(fsid);
        H5Sclose(msid);
        n = new_extent;
    } /* end while */
    printf("Extend time: %lf\n", test_time_to_double(t));

    /* Close dataset and file */
    H5Dclose(did);
    H5Fclose(fid);

    fprintf(stderr, "...DONE\n");

    /* close leftovers */
    H5Pclose(fapl);
    free(buf);

    return 0;
}
