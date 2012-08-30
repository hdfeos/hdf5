/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Parallel tests for encoding/decoding plists sent between processes
 */

#include "testphdf5.h"
#include "H5Pprivate.h"

void
test_plist_ed(void)
{
    hid_t dcpl1, dcpl2;	       	/* dataset create prop. list */
    hid_t dapl1, dapl2;	       	/* dataset access prop. list */
    hid_t dxpl1, dxpl2;	       	/* dataset transfer prop. list */
    hid_t gcpl1, gcpl2;	       	/* group create prop. list */
    hid_t lcpl1, lcpl2;	       	/* link create prop. list */
    hid_t lapl1, lapl2;	       	/* link access prop. list */
    hid_t ocpypl1, ocpypl2;	/* object copy prop. list */
    hid_t ocpl1, ocpl2;	        /* object create prop. list */
    hid_t fapl1, fapl2;	       	/* file access prop. list */
    hid_t fcpl1, fcpl2;	       	/* file create prop. list */

    void *send_buf = NULL, *recv_buf = NULL;
    size_t send_size=0, recv_size=0;

    int mpi_size, mpi_rank, recv_proc;
    MPI_Status status;
    MPI_Request req[2];

    hsize_t chunk_size = 16384;	/* chunk size */ 
    double fill = 2.7f;         /* Fill value */
    size_t nslots = 521*2;
    size_t nbytes = 1048576 * 10;
    double w0 = 0.5f;
    unsigned max_compact;
    unsigned min_dense;
    hsize_t max_size[1]; /*data space maximum size */
    const char* c_to_f = "x+32";
    H5AC_cache_config_t my_cache_config = {
        H5AC__CURR_CACHE_CONFIG_VERSION,
        TRUE,
        FALSE,
        FALSE,
        "temp",
        TRUE,
        FALSE,
        ( 2 * 2048 * 1024),
        0.3f,
        (64 * 1024 * 1024),
        (4 * 1024 * 1024),
        60000,
        H5C_incr__threshold,
        0.8f,
        3.0f,
        TRUE,
        (8 * 1024 * 1024),
        H5C_flash_incr__add_space,
        2.0f,
        0.25f,
        H5C_decr__age_out_with_threshold,
        0.997f,
        0.8f,
        TRUE,
        (3 * 1024 * 1024),
        3,
        FALSE,
        0.2f,
        (256 * 2048),
        H5AC__DEFAULT_METADATA_WRITE_STRATEGY};

    herr_t ret;         	/* Generic return value */


    if(VERBOSE_MED)
	printf("Encode/Decode DCPLs\n");

    /* set up MPI parameters */
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);

    if(mpi_size == 1)
        recv_proc = 0;
    else
        recv_proc = 1;

    dcpl1 = H5Pcreate(H5P_DATASET_CREATE);
    VRFY((dcpl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_chunk(dcpl1, 1, &chunk_size);
    VRFY((ret >= 0), "H5Pset_chunk succeeded");

    ret = H5Pset_alloc_time(dcpl1, H5D_ALLOC_TIME_LATE);
    VRFY((ret >= 0), "H5Pset_alloc_time succeeded");

    ret = H5Pset_fill_value(dcpl1, H5T_NATIVE_DOUBLE, &fill);
    VRFY((ret>=0), "set fill-value succeeded");

    max_size[0] = 100;
    ret = H5Pset_external(dcpl1, "ext1.data", (off_t)0, 
                          (hsize_t)(max_size[0] * sizeof(int)/4));
    VRFY((ret>=0), "set external succeeded");
    ret = H5Pset_external(dcpl1, "ext2.data", (off_t)0, 
                          (hsize_t)(max_size[0] * sizeof(int)/4));
    VRFY((ret>=0), "set external succeeded");
    ret = H5Pset_external(dcpl1, "ext3.data", (off_t)0, 
                          (hsize_t)(max_size[0] * sizeof(int)/4));
    VRFY((ret>=0), "set external succeeded");
    ret = H5Pset_external(dcpl1, "ext4.data", (off_t)0, 
                          (hsize_t)(max_size[0] * sizeof(int)/4));
    VRFY((ret>=0), "set external succeeded");

    /******* ENCODE/DECODE DCPLS *****/
    if(mpi_rank == 0) {
        /* first call to encode returns only the size of the buffer needed */
        ret = H5Pencode(dcpl1, TRUE, NULL, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        send_buf = (uint8_t *)malloc(send_size);

        ret = H5Pencode(dcpl1, TRUE, send_buf, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    } /* end if */
    if(mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *)malloc(recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);

        dcpl2 = H5Pdecode(recv_buf);
        VRFY((dcpl2 >= 0), "H5Pdecode succeeded");

        VRFY(H5Pequal(dcpl1, dcpl2), "DCPLs Equal Succeeded");

        ret = H5Pclose(dcpl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    } /* end if */
    ret = H5Pclose(dcpl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(0 == mpi_rank)
        MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

    if(NULL != send_buf) {
        free(send_buf);
        send_buf = NULL;
    } /* end if */
    if(NULL != recv_buf) {
        free(recv_buf);
        recv_buf = NULL;
    } /* end if */

    MPI_Barrier(MPI_COMM_WORLD);



    /******* ENCODE/DECODE DAPLS *****/
    dapl1 = H5Pcreate(H5P_DATASET_ACCESS);
    VRFY((dapl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_chunk_cache(dapl1, nslots, nbytes, w0);
    VRFY((ret >= 0), "H5Pset_chunk_cache succeeded");

    if(mpi_rank == 0) {
        /* first call to encode returns only the size of the buffer needed */
        ret = H5Pencode(dapl1, TRUE, NULL, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        send_buf = (uint8_t *)malloc(send_size);

        ret = H5Pencode(dapl1, TRUE, send_buf, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    } /* end if */
    if(mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *)malloc(recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);

        dapl2 = H5Pdecode(recv_buf);
        VRFY((dapl2 >= 0), "H5Pdecode succeeded");

        VRFY(H5Pequal(dapl1, dapl2), "DAPLs Equal Succeeded");

        ret = H5Pclose(dapl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    } /* end if */
    ret = H5Pclose(dapl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(0 == mpi_rank)
        MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

    if(NULL != send_buf) {
        free(send_buf);
        send_buf = NULL;
    } /* end if */
    if(NULL != recv_buf) {
        free(recv_buf);
        recv_buf = NULL;
    } /* end if */

    MPI_Barrier(MPI_COMM_WORLD);


    /******* ENCODE/DECODE OCPLS *****/
    ocpl1 = H5Pcreate(H5P_OBJECT_CREATE);
    VRFY((ocpl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_attr_creation_order(ocpl1, (H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED));
    VRFY((ret >= 0), "H5Pset_attr_creation_order succeeded");

    ret = H5Pset_attr_phase_change(ocpl1, 110, 105);
    VRFY((ret >= 0), "H5Pset_attr_phase_change succeeded");

    ret = H5Pset_filter(ocpl1, H5Z_FILTER_FLETCHER32, 0, (size_t)0, NULL);
    VRFY((ret >= 0), "H5Pset_filter succeeded");

    if(mpi_rank == 0) {
        /* first call to encode returns only the size of the buffer needed */
        ret = H5Pencode(ocpl1, TRUE, NULL, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        send_buf = (uint8_t *)malloc(send_size);

        ret = H5Pencode(ocpl1, TRUE, send_buf, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    } /* end if */
    if(mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *)malloc(recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);

        ocpl2 = H5Pdecode(recv_buf);
        VRFY((ocpl2 >= 0), "H5Pdecode succeeded");

        VRFY(H5Pequal(ocpl1, ocpl2), "OCPLs Equal Succeeded");

        ret = H5Pclose(ocpl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    } /* end if */
    ret = H5Pclose(ocpl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(0 == mpi_rank)
        MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

    if(NULL != send_buf) {
        free(send_buf);
        send_buf = NULL;
    } /* end if */
    if(NULL != recv_buf) {
        free(recv_buf);
        recv_buf = NULL;
    } /* end if */

    MPI_Barrier(MPI_COMM_WORLD);


    /******* ENCODE/DECODE DXPLS *****/
    dxpl1 = H5Pcreate(H5P_DATASET_XFER);
    VRFY((dxpl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_btree_ratios(dxpl1, 0.2f, 0.6f, 0.2f);
    VRFY((ret >= 0), "H5Pset_btree_ratios succeeded");

    ret = H5Pset_hyper_vector_size(dxpl1, 5);
    VRFY((ret >= 0), "H5Pset_hyper_vector_size succeeded");

    ret = H5Pset_dxpl_mpio(dxpl1, H5FD_MPIO_COLLECTIVE);
    VRFY((ret >= 0), "H5Pset_dxpl_mpio succeeded");

    ret = H5Pset_dxpl_mpio_collective_opt(dxpl1, H5FD_MPIO_INDIVIDUAL_IO);
    VRFY((ret >= 0), "H5Pset_dxpl_mpio_collective_opt succeeded");

    ret = H5Pset_dxpl_mpio_chunk_opt(dxpl1, H5FD_MPIO_CHUNK_MULTI_IO);
    VRFY((ret >= 0), "H5Pset_dxpl_mpio_chunk_opt succeeded");

    ret = H5Pset_dxpl_mpio_chunk_opt_ratio(dxpl1, 30);
    VRFY((ret >= 0), "H5Pset_dxpl_mpio_chunk_opt_ratio succeeded");

    ret = H5Pset_dxpl_mpio_chunk_opt_num(dxpl1, 40);
    VRFY((ret >= 0), "H5Pset_dxpl_mpio_chunk_opt_num succeeded");

    ret = H5Pset_edc_check(dxpl1, H5Z_DISABLE_EDC);
    VRFY((ret >= 0), "H5Pset_edc_check succeeded");

    ret = H5Pset_data_transform(dxpl1, c_to_f);
    VRFY((ret >= 0), "H5Pset_data_transform succeeded");

    if(mpi_rank == 0) {
        /* first call to encode returns only the size of the buffer needed */
        ret = H5Pencode(dxpl1, TRUE, NULL, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        send_buf = (uint8_t *)malloc(send_size);

        ret = H5Pencode(dxpl1, TRUE, send_buf, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    } /* end if */
    if(mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *)malloc(recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);

        dxpl2 = H5Pdecode(recv_buf);
        VRFY((dxpl2 >= 0), "H5Pdecode succeeded");

        VRFY(H5Pequal(dxpl1, dxpl2), "DXPLs Equal Succeeded");

        ret = H5Pclose(dxpl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    } /* end if */
    ret = H5Pclose(dxpl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(0 == mpi_rank)
        MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

    if(NULL != send_buf) {
        free(send_buf);
        send_buf = NULL;
    } /* end if */
    if(NULL != recv_buf) {
        free(recv_buf);
        recv_buf = NULL;
    } /* end if */

    MPI_Barrier(MPI_COMM_WORLD);


    /******* ENCODE/DECODE GCPLS *****/
    gcpl1 = H5Pcreate(H5P_GROUP_CREATE);
    VRFY((gcpl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_local_heap_size_hint(gcpl1, 256);
    VRFY((ret >= 0), "H5Pset_local_heap_size_hint succeeded");

    ret = H5Pset_link_phase_change(gcpl1, 2, 2);
    VRFY((ret >= 0), "H5Pset_link_phase_change succeeded");

    /* Query the group creation properties */
    ret = H5Pget_link_phase_change(gcpl1, &max_compact, &min_dense);
    VRFY((ret >= 0), "H5Pget_est_link_info succeeded");

    ret = H5Pset_est_link_info(gcpl1, 3, 9);
    VRFY((ret >= 0), "H5Pset_est_link_info succeeded");

    ret = H5Pset_link_creation_order(gcpl1, (H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED));
    VRFY((ret >= 0), "H5Pset_link_creation_order succeeded");

    if(mpi_rank == 0) {
        /* first call to encode returns only the size of the buffer needed */
        ret = H5Pencode(gcpl1, TRUE, NULL, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        send_buf = (uint8_t *)malloc(send_size);

        ret = H5Pencode(gcpl1, TRUE, send_buf, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    } /* end if */
    if(mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *)malloc(recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);

        gcpl2 = H5Pdecode(recv_buf);
        VRFY((gcpl2 >= 0), "H5Pdecode succeeded");

        VRFY(H5Pequal(gcpl1, gcpl2), "GCPLs Equal Succeeded");

        ret = H5Pclose(gcpl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    } /* end if */
    ret = H5Pclose(gcpl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(0 == mpi_rank)
        MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

    if(NULL != send_buf) {
        free(send_buf);
        send_buf = NULL;
    } /* end if */
    if(NULL != recv_buf) {
        free(recv_buf);
        recv_buf = NULL;
    } /* end if */

    MPI_Barrier(MPI_COMM_WORLD);


    /******* ENCODE/DECODE LCPLS *****/
    lcpl1 = H5Pcreate(H5P_LINK_CREATE);
    VRFY((lcpl1 >= 0), "H5Pcreate succeeded");

    ret= H5Pset_create_intermediate_group(lcpl1, TRUE);
    VRFY((ret >= 0), "H5Pset_create_intermediate_group succeeded");

    if(mpi_rank == 0) {
        /* first call to encode returns only the size of the buffer needed */
        ret = H5Pencode(lcpl1, TRUE, NULL, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        send_buf = (uint8_t *)malloc(send_size);

        ret = H5Pencode(lcpl1, TRUE, send_buf, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    } /* end if */
    if(mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *)malloc(recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);

        lcpl2 = H5Pdecode(recv_buf);
        VRFY((lcpl2 >= 0), "H5Pdecode succeeded");

        VRFY(H5Pequal(lcpl1, lcpl2), "LCPLs Equal Succeeded");

        ret = H5Pclose(lcpl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    } /* end if */
    ret = H5Pclose(lcpl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(0 == mpi_rank)
        MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

    if(NULL != send_buf) {
        free(send_buf);
        send_buf = NULL;
    } /* end if */
    if(NULL != recv_buf) {
        free(recv_buf);
        recv_buf = NULL;
    } /* end if */

    MPI_Barrier(MPI_COMM_WORLD);


    /******* ENCODE/DECODE LAPLS *****/
    lapl1 = H5Pcreate(H5P_LINK_ACCESS);
    VRFY((lapl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_nlinks(lapl1, (size_t)134);
    VRFY((ret >= 0), "H5Pset_nlinks succeeded");

    ret = H5Pset_elink_acc_flags(lapl1, H5F_ACC_RDONLY);
    VRFY((ret >= 0), "H5Pset_elink_acc_flags succeeded");

    ret = H5Pset_elink_prefix(lapl1, "/tmpasodiasod");
    VRFY((ret >= 0), "H5Pset_nlinks succeeded");

    /* Create FAPL for the elink FAPL */
    fapl1 = H5Pcreate(H5P_FILE_ACCESS);
    VRFY((fapl1 >= 0), "H5Pcreate succeeded");
    ret = H5Pset_alignment(fapl1, 2, 1024);
    VRFY((ret >= 0), "H5Pset_alignment succeeded");

    ret = H5Pset_elink_fapl(lapl1, fapl1);
    VRFY((ret >= 0), "H5Pset_elink_fapl succeeded");

    /* Close the elink's FAPL */
    ret = H5Pclose(fapl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(mpi_rank == 0) {
        /* first call to encode returns only the size of the buffer needed */
        ret = H5Pencode(lapl1, TRUE, NULL, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        send_buf = (uint8_t *)malloc(send_size);

        ret = H5Pencode(lapl1, TRUE, send_buf, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    } /* end if */
    if(mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *)malloc(recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);

        lapl2 = H5Pdecode(recv_buf);
        VRFY((lapl2 >= 0), "H5Pdecode succeeded");

        VRFY(H5Pequal(lapl1, lapl2), "LAPLs Equal Succeeded");

        ret = H5Pclose(lapl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    } /* end if */
    ret = H5Pclose(lapl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(0 == mpi_rank)
        MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

    if(NULL != send_buf) {
        free(send_buf);
        send_buf = NULL;
    } /* end if */
    if(NULL != recv_buf) {
        free(recv_buf);
        recv_buf = NULL;
    } /* end if */

    MPI_Barrier(MPI_COMM_WORLD);


    /******* ENCODE/DECODE OCPYPLS *****/
    ocpypl1 = H5Pcreate(H5P_OBJECT_COPY);
    VRFY((ocpypl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_copy_object(ocpypl1, H5O_COPY_EXPAND_EXT_LINK_FLAG);
    VRFY((ret >= 0), "H5Pset_copy_object succeeded");

    if(mpi_rank == 0) {
        /* first call to encode returns only the size of the buffer needed */
        ret = H5Pencode(ocpypl1, TRUE, NULL, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        send_buf = (uint8_t *)malloc(send_size);

        ret = H5Pencode(ocpypl1, TRUE, send_buf, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    } /* end if */
    if(mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *)malloc(recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);

        ocpypl2 = H5Pdecode(recv_buf);
        VRFY((ocpypl2 >= 0), "H5Pdecode succeeded");

        VRFY(H5Pequal(ocpypl1, ocpypl2), "OCPYPLs Equal Succeeded");

        ret = H5Pclose(ocpypl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    } /* end if */
    ret = H5Pclose(ocpypl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(0 == mpi_rank)
        MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

    if(NULL != send_buf) {
        free(send_buf);
        send_buf = NULL;
    } /* end if */
    if(NULL != recv_buf) {
        free(recv_buf);
        recv_buf = NULL;
    } /* end if */

    MPI_Barrier(MPI_COMM_WORLD);


    /******* ENCODE/DECODE FAPLS *****/
    fapl1 = H5Pcreate(H5P_FILE_ACCESS);
    VRFY((fapl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_family_offset(fapl1, 1024);
    VRFY((ret >= 0), "H5Pset_family_offset succeeded");

    ret = H5Pset_meta_block_size(fapl1, 2098452);
    VRFY((ret >= 0), "H5Pset_meta_block_size succeeded");

    ret = H5Pset_sieve_buf_size(fapl1, 1048576);
    VRFY((ret >= 0), "H5Pset_sieve_buf_size succeeded");

    ret = H5Pset_alignment(fapl1, 2, 1024);
    VRFY((ret >= 0), "H5Pset_alignment succeeded");

    ret = H5Pset_cache(fapl1, 1024, 128, 10485760, 0.3f);
    VRFY((ret >= 0), "H5Pset_cache succeeded");

    ret = H5Pset_elink_file_cache_size(fapl1, 10485760);
    VRFY((ret >= 0), "H5Pset_elink_file_cache_size succeeded");

    ret = H5Pset_gc_references(fapl1, 1);
    VRFY((ret >= 0), "H5Pset_gc_references succeeded");

    ret = H5Pset_small_data_block_size(fapl1, 2048);
    VRFY((ret >= 0), "H5Pset_small_data_block_size succeeded");

    ret = H5Pset_libver_bounds(fapl1, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST);
    VRFY((ret >= 0), "H5Pset_libver_bounds succeeded");

    ret = H5Pset_fclose_degree(fapl1, H5F_CLOSE_WEAK);
    VRFY((ret >= 0), "H5Pset_fclose_degree succeeded");

    ret = H5Pset_multi_type(fapl1, H5FD_MEM_GHEAP);
    VRFY((ret >= 0), "H5Pset_multi_type succeeded");

    ret = H5Pset_mdc_config(fapl1, &my_cache_config);
    VRFY((ret >= 0), "H5Pset_mdc_config succeeded");

    if(mpi_rank == 0) {
        /* first call to encode returns only the size of the buffer needed */
        ret = H5Pencode(fapl1, TRUE, NULL, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        send_buf = (uint8_t *)malloc(send_size);

        ret = H5Pencode(fapl1, TRUE, send_buf, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    } /* end if */
    if(mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *)malloc(recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);

        fapl2 = H5Pdecode(recv_buf);
        VRFY((fapl2 >= 0), "H5Pdecode succeeded");

        VRFY(H5Pequal(fapl1, fapl2), "FAPLs Equal Succeeded");

        ret = H5Pclose(fapl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    } /* end if */
    ret = H5Pclose(fapl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(0 == mpi_rank)
        MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

    if(NULL != send_buf) {
        free(send_buf);
        send_buf = NULL;
    } /* end if */
    if(NULL != recv_buf) {
        free(recv_buf);
        recv_buf = NULL;
    } /* end if */

    MPI_Barrier(MPI_COMM_WORLD);


    /******* ENCODE/DECODE FCPLS *****/
    fcpl1 = H5Pcreate(H5P_FILE_CREATE);
    VRFY((fcpl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_userblock(fcpl1, 1024);
    VRFY((ret >= 0), "H5Pset_userblock succeeded");

    ret = H5Pset_istore_k(fcpl1, 3);
    VRFY((ret >= 0), "H5Pset_istore_k succeeded");

    ret = H5Pset_sym_k(fcpl1, 4, 5);
    VRFY((ret >= 0), "H5Pset_sym_k succeeded");

    ret = H5Pset_shared_mesg_nindexes(fcpl1, 8);
    VRFY((ret >= 0), "H5Pset_shared_mesg_nindexes succeeded");

    ret = H5Pset_shared_mesg_index(fcpl1, 1,  H5O_SHMESG_SDSPACE_FLAG, 32);
    VRFY((ret >= 0), "H5Pset_shared_mesg_index succeeded");

    ret = H5Pset_shared_mesg_phase_change(fcpl1, 60, 20);
    VRFY((ret >= 0), "H5Pset_shared_mesg_phase_change succeeded");

    ret = H5Pset_sizes(fcpl1, 8, 4);
    VRFY((ret >= 0), "H5Pset_sizes succeeded");

    if(mpi_rank == 0) {
        /* first call to encode returns only the size of the buffer needed */
        ret = H5Pencode(fcpl1, TRUE, NULL, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        send_buf = (uint8_t *)malloc(send_size);

        ret = H5Pencode(fcpl1, TRUE, send_buf, &send_size);
        VRFY((ret >= 0), "H5Pencode succeeded");

        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    } /* end if */
    if(mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *)malloc(recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);

        fcpl2 = H5Pdecode(recv_buf);
        VRFY((fcpl2 >= 0), "H5Pdecode succeeded");

        VRFY(H5Pequal(fcpl1, fcpl2), "FCPLs Equal Succeeded");

        ret = H5Pclose(fcpl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    } /* end if */
    ret = H5Pclose(fcpl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if(0 == mpi_rank)
        MPI_Waitall(2, req, MPI_STATUSES_IGNORE);

    if(NULL != send_buf) {
        free(send_buf);
        send_buf = NULL;
    } /* end if */
    if(NULL != recv_buf) {
        free(recv_buf);
        recv_buf = NULL;
    } /* end if */

    MPI_Barrier(MPI_COMM_WORLD);


}

