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
    hid_t gcpl1, gcpl2;	       	/* group create prop. list */
    hid_t lcpl1, lcpl2;	       	/* link create prop. list */
    hid_t lapl1, lapl2;	       	/* link create prop. list */
    hid_t ocpypl1, ocpypl2;	/* object copy prop. list */
    hid_t ocpl1, ocpl2;	        /* object create prop. list */

    void *send_buf = NULL, *recv_buf = NULL;
    uint8_t *enc_buf, *dec_buf;
    size_t send_size=0, recv_size=0;
    H5P_plist_type_t class_type;

    int mpi_size, mpi_rank, recv_proc;
    MPI_Status status;
    MPI_Request req[2];

    hsize_t chunk_size = 16384;	/* chunk size */ 
    double fill=2.7;            /* Fill value */
    size_t nslots = 521*2;
    size_t nbytes = 1048576 * 10;
    double w0 = 0.5;
    unsigned max_compact;
    unsigned min_dense;
    hsize_t max_size[1]; /*data space maximum size */

    herr_t ret;         	/* Generic return value */


    if(VERBOSE_MED)
	printf("Encode/Decode DCPLs\n");

    /* set up MPI parameters */
    MPI_Comm_size(MPI_COMM_WORLD,&mpi_size);
    MPI_Comm_rank(MPI_COMM_WORLD,&mpi_rank);

    if (mpi_size == 1) {
        recv_proc = 0;
    }
    else {
        recv_proc = 1;
    }

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
    if (mpi_rank == 0) {

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (dcpl1, TRUE, NULL, &send_size);

        send_buf = (uint8_t *) malloc (send_size);
        enc_buf = (uint8_t *)send_buf;

        H5Pencode (dcpl1, TRUE, enc_buf, &send_size);
        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    }
    if (mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *) malloc (recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);
        dec_buf = (uint8_t *)recv_buf;

        dcpl2 = H5Pcreate(H5P_DATASET_CREATE);
        VRFY((dcpl2 >= 0), "H5Pcreate succeeded");

        H5Pdecode (dec_buf);

        VRFY(H5Pequal(dcpl1, dcpl2), "DCPLs Equal Succeeded");

        ret = H5Pclose(dcpl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    }
    ret = H5Pclose(dcpl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if (0 == mpi_rank)
        MPI_Waitall (2, req, MPI_STATUSES_IGNORE);

    if (NULL != send_buf) {
        free (send_buf);
        send_buf = NULL;
    }
    if (NULL != recv_buf) {
        free (recv_buf);
        recv_buf = NULL;
    }

    MPI_Barrier (MPI_COMM_WORLD);



    /******* ENCODE/DECODE DAPLS *****/
    dapl1 = H5Pcreate(H5P_DATASET_ACCESS);
    VRFY((dapl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_chunk_cache(dapl1, nslots, nbytes, w0);
    VRFY((ret >= 0), "H5Pset_chunk_cache succeeded");

    if (mpi_rank == 0) {

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (dapl1, TRUE, NULL, &send_size);

        send_buf = (uint8_t *) malloc (send_size);
        enc_buf = (uint8_t *)send_buf;

        H5P_encode (dapl1, TRUE, enc_buf, &send_size);
        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    }
    if (mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *) malloc (recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);
        dec_buf = (uint8_t *)recv_buf;

        dapl2 = H5Pcreate(H5P_DATASET_ACCESS);
        VRFY((dapl2 >= 0), "H5Pcreate succeeded");

        H5Pdecode (dec_buf);

        VRFY(H5Pequal(dapl1, dapl2), "DAPLs Equal Succeeded");

        ret = H5Pclose(dapl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    }
    ret = H5Pclose(dapl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if (0 == mpi_rank)
        MPI_Waitall (2, req, MPI_STATUSES_IGNORE);

    if (NULL != send_buf) {
        free (send_buf);
        send_buf = NULL;
    }
    if (NULL != recv_buf) {
        free (recv_buf);
        recv_buf = NULL;
    }

    MPI_Barrier (MPI_COMM_WORLD);

#if 0 /* MSC - not implemented yet */
    /******* ENCODE/DECODE GCPLS *****/
    gcpl1 = H5Pcreate(H5P_GROUP_CREATE);
    VRFY((gcpl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_link_creation_order(gcpl1, (H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED));
    VRFY((ret >= 0), "H5Pset_link_creation_order succeeded");

    ret = H5Pset_local_heap_size_hint(gcpl1, 256);
    VRFY((ret >= 0), "H5Pset_local_heap_size_hint succeeded");

    ret = H5Pset_link_phase_change(gcpl1, 2, 2);
    VRFY((ret >= 0), "H5Pset_link_phase_change succeeded");

    /* Query the group creation properties */
    ret = H5Pget_link_phase_change(gcpl1, &max_compact, &min_dense);
    VRFY((ret >= 0), "H5Pget_est_link_info succeeded");

    ret = H5Pset_est_link_info(gcpl1, 3, 9);
    VRFY((ret >= 0), "H5Pset_est_link_info succeeded");

    if (mpi_rank == 0) {
        class_type = H5P_TYPE_GROUP_CREATE;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (gcpl1, NULL, &send_size);
        /* extra space for plist type */
        send_size += sizeof(uint8_t); 

        send_buf = (uint8_t *) malloc (send_size);
        enc_buf = (uint8_t *)send_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (gcpl1, enc_buf, &send_size);
        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    }
    if (mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *) malloc (recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);
        dec_buf = (uint8_t *)recv_buf;

        class_type = *dec_buf++;
        VRFY((class_type == H5P_TYPE_GROUP_CREATE), "PLIST type decode Succeeded");

        gcpl2 = H5Pcreate(H5P_GROUP_CREATE);
        VRFY((gcpl2 >= 0), "H5Pcreate succeeded");

        H5P_decode (gcpl2, recv_size, dec_buf);

        VRFY(H5Pequal(gcpl1, gcpl2), "GCPLs Equal Succeeded");

        ret = H5Pclose(gcpl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    }
    ret = H5Pclose(gcpl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if (0 == mpi_rank)
        MPI_Waitall (2, req, MPI_STATUSES_IGNORE);

    if (NULL != send_buf) {
        free (send_buf);
        send_buf = NULL;
    }
    if (NULL != recv_buf) {
        free (recv_buf);
        recv_buf = NULL;
    }

    MPI_Barrier (MPI_COMM_WORLD);



    /******* ENCODE/DECODE LCPLS *****/
    lcpl1 = H5Pcreate(H5P_LINK_CREATE);
    VRFY((lcpl1 >= 0), "H5Pcreate succeeded");

    ret= H5Pset_create_intermediate_group(lcpl1, TRUE);
    VRFY((ret >= 0), "H5Pset_create_intermediate_group succeeded");

    if (mpi_rank == 0) {
        class_type = H5P_TYPE_LINK_CREATE;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (lcpl1, NULL, &send_size);
        /* extra space for plist type */
        send_size += sizeof(uint8_t); 

        send_buf = (uint8_t *) malloc (send_size);
        enc_buf = (uint8_t *)send_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (lcpl1, enc_buf, &send_size);
        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    }
    if (mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *) malloc (recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);
        dec_buf = (uint8_t *)recv_buf;

        class_type = *dec_buf++;
        VRFY((class_type == H5P_TYPE_LINK_CREATE), "PLIST type decode Succeeded");

        lcpl2 = H5Pcreate(H5P_LINK_CREATE);
        VRFY((lcpl2 >= 0), "H5Pcreate succeeded");

        H5P_decode (lcpl2, recv_size, dec_buf);

        VRFY(H5Pequal(lcpl1, lcpl2), "LCPLs Equal Succeeded");

        ret = H5Pclose(lcpl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    }
    ret = H5Pclose(lcpl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if (0 == mpi_rank)
        MPI_Waitall (2, req, MPI_STATUSES_IGNORE);

    if (NULL != send_buf) {
        free (send_buf);
        send_buf = NULL;
    }
    if (NULL != recv_buf) {
        free (recv_buf);
        recv_buf = NULL;
    }

    MPI_Barrier (MPI_COMM_WORLD);



    /******* ENCODE/DECODE OCPYLS *****/
    ocpypl1 = H5Pcreate(H5P_OBJECT_COPY);
    VRFY((ocpypl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_copy_object(ocpypl1, H5O_COPY_EXPAND_EXT_LINK_FLAG);
    VRFY((ret >= 0), "H5Pset_copy_object succeeded");

    if (mpi_rank == 0) {
        class_type = H5P_TYPE_OBJECT_COPY;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (ocpypl1, NULL, &send_size);
        /* extra space for plist type */
        send_size += sizeof(uint8_t); 

        send_buf = (uint8_t *) malloc (send_size);
        enc_buf = (uint8_t *)send_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (ocpypl1, enc_buf, &send_size);
        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    }
    if (mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *) malloc (recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);
        dec_buf = (uint8_t *)recv_buf;

        class_type = *dec_buf++;
        VRFY((class_type == H5P_TYPE_OBJECT_COPY), "PLIST type decode Succeeded");

        ocpypl2 = H5Pcreate(H5P_OBJECT_COPY);
        VRFY((ocpypl2 >= 0), "H5Pcreate succeeded");

        H5P_decode (ocpypl2, recv_size, dec_buf);

        VRFY(H5Pequal(ocpypl1, ocpypl2), "OCPYPLs Equal Succeeded");

        ret = H5Pclose(ocpypl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    }
    ret = H5Pclose(ocpypl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if (0 == mpi_rank)
        MPI_Waitall (2, req, MPI_STATUSES_IGNORE);

    if (NULL != send_buf) {
        free (send_buf);
        send_buf = NULL;
    }
    if (NULL != recv_buf) {
        free (recv_buf);
        recv_buf = NULL;
    }

    MPI_Barrier (MPI_COMM_WORLD);


    /******* ENCODE/DECODE OCPLS *****/
    ocpl1 = H5Pcreate(H5P_OBJECT_CREATE);
    VRFY((ocpl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_attr_creation_order(ocpl1, (H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED));
    VRFY((ret >= 0), "H5Pset_attr_creation_order succeeded");

    ret = H5Pset_attr_phase_change (ocpl1, 110, 105);
    VRFY((ret >= 0), "H5Pset_attr_phase_change succeeded");

    ret = H5Pset_filter (ocpl1, H5Z_FILTER_FLETCHER32, 0, (size_t)0, NULL);
    VRFY((ret >= 0), "H5Pset_filter succeeded");

    if (mpi_rank == 0) {
        class_type = H5P_TYPE_OBJECT_CREATE;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (ocpl1, NULL, &send_size);
        /* extra space for plist type */
        send_size += sizeof(uint8_t); 

        send_buf = (uint8_t *) malloc (send_size);
        enc_buf = (uint8_t *)send_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (ocpl1, enc_buf, &send_size);
        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    }
    if (mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *) malloc (recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);
        dec_buf = (uint8_t *)recv_buf;

        class_type = *dec_buf++;
        VRFY((class_type == H5P_TYPE_OBJECT_CREATE), "PLIST type decode Succeeded");

        ocpl2 = H5Pcreate(H5P_OBJECT_CREATE);
        VRFY((ocpl2 >= 0), "H5Pcreate succeeded");

        H5P_decode (ocpl2, recv_size, dec_buf);

        VRFY(H5Pequal(ocpl1, ocpl2), "OCPLs Equal Succeeded");

        ret = H5Pclose(ocpl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    }
    ret = H5Pclose(ocpl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if (0 == mpi_rank)
        MPI_Waitall (2, req, MPI_STATUSES_IGNORE);

    if (NULL != send_buf) {
        free (send_buf);
        send_buf = NULL;
    }
    if (NULL != recv_buf) {
        free (recv_buf);
        recv_buf = NULL;
    }

    MPI_Barrier (MPI_COMM_WORLD);



    /******* ENCODE/DECODE LAPLS *****/
    lapl1 = H5Pcreate(H5P_LINK_ACCESS);
    VRFY((lapl1 >= 0), "H5Pcreate succeeded");

    ret = H5Pset_nlinks(lapl1, (size_t)134);
    VRFY((ret >= 0), "H5Pset_nlinks succeeded");

    ret = H5Pset_elink_acc_flags(lapl1, H5F_ACC_RDONLY);
    VRFY((ret >= 0), "H5Pset_elink_acc_flags succeeded");

    ret = H5Pset_elink_prefix (lapl1, "/tmpasodiasod");
    VRFY((ret >= 0), "H5Pset_nlinks succeeded");

    if (mpi_rank == 0) {
        class_type = H5P_TYPE_LINK_ACCESS;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (lapl1, NULL, &send_size);
        /* extra space for plist type */
        send_size += sizeof(uint8_t); 

        send_buf = (uint8_t *) malloc (send_size);
        enc_buf = (uint8_t *)send_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (lapl1, enc_buf, &send_size);
        MPI_Isend(&send_size, 1, MPI_INT, recv_proc, 123, MPI_COMM_WORLD, &req[0]);
        MPI_Isend(send_buf, (int)send_size, MPI_BYTE, recv_proc, 124, MPI_COMM_WORLD, &req[1]);
    }
    if (mpi_rank == recv_proc) {
        MPI_Recv(&recv_size, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        recv_buf = (uint8_t *) malloc (recv_size);
        MPI_Recv(recv_buf, (int)recv_size, MPI_BYTE, 0, 124, MPI_COMM_WORLD, &status);
        dec_buf = (uint8_t *)recv_buf;

        class_type = *dec_buf++;
        VRFY((class_type == H5P_TYPE_LINK_ACCESS), "PLIST type decode Succeeded");

        lapl2 = H5Pcreate(H5P_LINK_ACCESS);
        VRFY((lapl2 >= 0), "H5Pcreate succeeded");

        H5P_decode (lapl2, recv_size, dec_buf);

        VRFY(H5Pequal(lapl1, lapl2), "LAPLs Equal Succeeded");

        ret = H5Pclose(lapl2);
        VRFY((ret >= 0), "H5Pclose succeeded");
    }
    ret = H5Pclose(lapl1);
    VRFY((ret >= 0), "H5Pclose succeeded");

    if (0 == mpi_rank)
        MPI_Waitall (2, req, MPI_STATUSES_IGNORE);

    if (NULL != send_buf) {
        free (send_buf);
        send_buf = NULL;
    }
    if (NULL != recv_buf) {
        free (recv_buf);
        recv_buf = NULL;
    }

    MPI_Barrier (MPI_COMM_WORLD);
#endif /* MSC - not implemented yet */
}
