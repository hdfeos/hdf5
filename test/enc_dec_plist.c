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
 * Serial tests for encoding/decoding plists
 */

#include "h5test.h"
#include "H5Pprivate.h"

int
main(void)
{
    hid_t dcpl1, dcpl2;	       	/* dataset create prop. list */
    hid_t dapl1, dapl2;	       	/* dataset access prop. list */
    hid_t dxpl1, dxpl2;	       	/* dataset xfer prop. list */
    hid_t gcpl1, gcpl2;	       	/* group create prop. list */
    hid_t lcpl1, lcpl2;	       	/* link create prop. list */
    hid_t lapl1, lapl2;	       	/* link access prop. list */
    hid_t ocpypl1, ocpypl2;	/* object copy prop. list */
    hid_t ocpl1, ocpl2;	        /* object create prop. list */
    hid_t fapl1, fapl2;	       	/* file access prop. list */

    hsize_t chunk_size = 16384;	/* chunk size */ 
    double fill=2.7;            /* Fill value */
    hsize_t max_size[1];        /* data space maximum size */
    size_t nslots = 521*2;
    size_t nbytes = 1048576 * 10;
    double w0 = 0.5;
    unsigned max_compact;
    unsigned min_dense;
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
        0.3,
        (64 * 1024 * 1024),
        (4 * 1024 * 1024),
        60000,
        H5C_incr__threshold,
        0.8,
        3.0,
        TRUE,
        (8 * 1024 * 1024),
        H5C_flash_incr__add_space,
        2.0,
        0.25,
        H5C_decr__age_out_with_threshold,
        0.997,
        0.8,
        TRUE,
        (3 * 1024 * 1024),
        3,
        FALSE,
        0.2,
        (256 * 2048),
        H5AC__DEFAULT_METADATA_WRITE_STRATEGY};

    if(VERBOSE_MED)
	printf("Encode/Decode DCPLs\n");

    /******* ENCODE/DECODE DCPLS *****/
    TESTING("DCPL Encoding/Decoding");
    if ((dcpl1 = H5Pcreate(H5P_DATASET_CREATE)) < 0)
        goto error;

    if ((H5Pset_chunk(dcpl1, 1, &chunk_size)) < 0)
        goto error;

    if ((H5Pset_alloc_time(dcpl1, H5D_ALLOC_TIME_LATE)) < 0)
    goto error;

    if ((H5Pset_fill_value(dcpl1, H5T_NATIVE_DOUBLE, &fill)) < 0)
        goto error;

    max_size[0] = 100;
    if ((H5Pset_external(dcpl1, "ext1.data", (off_t)0, 
                         (hsize_t)(max_size[0] * sizeof(int)/4))) < 0)
        goto error;
    if ((H5Pset_external(dcpl1, "ext2.data", (off_t)0, 
                         (hsize_t)(max_size[0] * sizeof(int)/4))) < 0)
        goto error;
    if ((H5Pset_external(dcpl1, "ext3.data", (off_t)0, 
                         (hsize_t)(max_size[0] * sizeof(int)/4))) < 0)
        goto error;
    if ((H5Pset_external(dcpl1, "ext4.data", (off_t)0, 
                         (hsize_t)(max_size[0] * sizeof(int)/4))) < 0)
        goto error;

    {
        void *temp_buf = NULL;
        void *enc_buf, *dec_buf;
        size_t temp_size=0;

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (dcpl1, TRUE, NULL, &temp_size);

        temp_buf = (void *) malloc (temp_size);
        enc_buf = temp_buf;
        dec_buf = temp_buf;

        H5Pencode (dcpl1, TRUE, enc_buf, &temp_size);

        dcpl2 = H5Pdecode (dec_buf);

        if (0 == H5Pequal(dcpl1, dcpl2)) {
            printf ("DCPL encoding decoding failed\n");
            goto error;
        }
        free (temp_buf);
    }
        
    /* release resource */
    if ((H5Pclose(dcpl1)) < 0)
         goto error;
    if ((H5Pclose(dcpl2)) < 0)
         goto error;

    PASSED();

    /******* ENCODE/DECODE DAPLS *****/
    TESTING("DAPL Encoding/Decoding");
    if ((dapl1 = H5Pcreate(H5P_DATASET_ACCESS)) < 0)
        goto error;

    if ((H5Pset_chunk_cache(dapl1, nslots, nbytes, w0)) < 0)
        goto error;

    {
        void *temp_buf = NULL;
        void *enc_buf, *dec_buf;
        size_t temp_size=0;

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (dapl1, TRUE, NULL, &temp_size);

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = temp_buf;
        dec_buf = temp_buf;

        H5Pencode (dapl1, TRUE, enc_buf, &temp_size);

        dapl2 = H5Pdecode (dec_buf);

        if (0 == H5Pequal(dapl1, dapl2)) {
            printf ("DAPL encoding decoding failed\n");
            goto error;
        }

        free (temp_buf);
    }
        
    /* release resource */
    if ((H5Pclose(dapl1)) < 0)
         goto error;
    if ((H5Pclose(dapl2)) < 0)
         goto error;

    PASSED();

    /******* ENCODE/DECODE DXPLS *****/
    TESTING("DXPL Encoding/Decoding");
    if ((dxpl1 = H5Pcreate(H5P_DATASET_XFER)) < 0)
        goto error;

    if ((H5Pset_btree_ratios(dxpl1, 0.2, 0.6, 0.2)) < 0)
        goto error;
    if ((H5Pset_hyper_vector_size(dxpl1, 5)) < 0)
        goto error;

    /* MSC - This fails because we do not call H5P_set_driver after decoding
    if ((H5Pset_dxpl_mpio(dxpl1, H5FD_MPIO_COLLECTIVE)) < 0)
        goto error;
    */

    if ((H5Pset_dxpl_mpio_collective_opt(dxpl1, H5FD_MPIO_INDIVIDUAL_IO)) < 0)
        goto error;
    if ((H5Pset_dxpl_mpio_chunk_opt(dxpl1, H5FD_MPIO_CHUNK_MULTI_IO)) < 0)
        goto error;
    if ((H5Pset_dxpl_mpio_chunk_opt_ratio(dxpl1, 30)) < 0)
        goto error;
    if ((H5Pset_dxpl_mpio_chunk_opt_num(dxpl1, 40)) < 0)
        goto error;
    if ((H5Pset_edc_check(dxpl1, H5Z_DISABLE_EDC)) < 0)
        goto error;
    /* MSC - still not working
    if ((H5Pset_data_transform(dxpl1, c_to_f)) < 0)
        goto error;
    */
    {
        void *temp_buf = NULL;
        void *enc_buf, *dec_buf;
        size_t temp_size=0;

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (dxpl1, FALSE, NULL, &temp_size);

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = temp_buf;
        dec_buf = temp_buf;

        H5Pencode (dxpl1, FALSE, enc_buf, &temp_size);

        dxpl2 = H5Pdecode (dec_buf);

        if (0 == H5Pequal(dxpl1, dxpl2)) {
            printf ("DXPL encoding decoding failed\n");
            goto error;
        }

        free (temp_buf);
    }
        
    /* release resource */
    if ((H5Pclose(dxpl1)) < 0)
         goto error;
    if ((H5Pclose(dxpl2)) < 0)
         goto error;

    PASSED();


    /******* ENCODE/DECODE GCPLS *****/
    TESTING("GCPL Encoding/Decoding");
    if ((gcpl1 = H5Pcreate(H5P_GROUP_CREATE)) < 0)
        goto error;

    if ((H5Pset_local_heap_size_hint(gcpl1, 256)) < 0)
         goto error;

    if ((H5Pset_link_phase_change(gcpl1, 2, 2)) < 0)
         goto error;

    /* Query the group creation properties */
    if ((H5Pget_link_phase_change(gcpl1, &max_compact, &min_dense)) < 0)
         goto error;

    if ((H5Pset_est_link_info(gcpl1, 3, 9)) < 0)
         goto error;

    if ((H5Pset_link_creation_order(gcpl1, (H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED))) < 0)
         goto error;

    {
        void *temp_buf = NULL;
        void *enc_buf, *dec_buf;
        size_t temp_size=0;

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (gcpl1, TRUE, NULL, &temp_size);

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = temp_buf;
        dec_buf = temp_buf;

        H5Pencode (gcpl1, TRUE, enc_buf, &temp_size);

        gcpl2 = H5Pdecode (dec_buf);

        if (0 == H5Pequal(gcpl1, gcpl2)) {
            printf ("GCPL encoding decoding failed\n");
            goto error;
        }

        free (temp_buf);
    }
        
    /* release resource */
    if ((H5Pclose(gcpl1)) < 0)
         goto error;
    if ((H5Pclose(gcpl2)) < 0)
         goto error;

    PASSED();

    /******* ENCODE/DECODE LCPLS *****/
    TESTING("LCPL Encoding/Decoding");
    if ((lcpl1 = H5Pcreate(H5P_LINK_CREATE)) < 0)
        goto error;

    if ((H5Pset_create_intermediate_group(lcpl1, TRUE)) < 0)
        goto error;

    {
        void *temp_buf = NULL;
        void *enc_buf, *dec_buf;
        size_t temp_size=0;

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (lcpl1, TRUE, NULL, &temp_size);

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = temp_buf;
        dec_buf = temp_buf;

        H5Pencode (lcpl1, TRUE, enc_buf, &temp_size);

        lcpl2 = H5Pdecode (dec_buf);

        if (0 == H5Pequal(lcpl1, lcpl2)) {
            printf ("LCPL encoding decoding failed\n");
            goto error;
        }

        free (temp_buf);
    }
        
    /* release resource */
    if ((H5Pclose(lcpl1)) < 0)
        goto error;
    if ((H5Pclose(lcpl2)) < 0)
        goto error;

    PASSED();

    /******* ENCODE/DECODE OCPYLS *****/
    TESTING("OCPYPL Encoding/Decoding");
    if ((ocpypl1 = H5Pcreate(H5P_OBJECT_COPY)) < 0)
        goto error;

    if ((H5Pset_copy_object(ocpypl1, H5O_COPY_EXPAND_EXT_LINK_FLAG)) < 0)
        goto error;

    {
        void *temp_buf = NULL;
        void *enc_buf, *dec_buf;
        size_t temp_size=0;

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (ocpypl1, TRUE, NULL, &temp_size);

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = temp_buf;
        dec_buf = temp_buf;

        H5Pencode (ocpypl1, TRUE, enc_buf, &temp_size);

        ocpypl2 = H5Pdecode (dec_buf);

        if (0 == H5Pequal(ocpypl1, ocpypl2)) {
            printf ("OCPYPL encoding decoding failed\n");
            goto error;
        }

        free (temp_buf);
    }
        
    /* release resource */
    if ((H5Pclose(ocpypl1)) < 0)
         goto error;
    if ((H5Pclose(ocpypl2)) < 0)
         goto error;

    PASSED();

    /******* ENCODE/DECODE OCPLS *****/
    TESTING("OCPL Encoding/Decoding");
    if ((ocpl1 = H5Pcreate(H5P_OBJECT_CREATE)) < 0)
        goto error;

    if ((H5Pset_attr_creation_order(ocpl1, (H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED))) < 0)
         goto error;

    if ((H5Pset_attr_phase_change (ocpl1, 110, 105)) < 0)
         goto error;

    if ((H5Pset_filter (ocpl1, H5Z_FILTER_FLETCHER32, 0, (size_t)0, NULL)) < 0)
        goto error;

    {
        void *temp_buf = NULL;
        void *enc_buf, *dec_buf;
        size_t temp_size=0;

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (ocpl1, TRUE, NULL, &temp_size);

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = temp_buf;
        dec_buf = temp_buf;

        H5Pencode (ocpl1, TRUE, enc_buf, &temp_size);

        ocpl2 = H5Pdecode (dec_buf);
        if (0 == H5Pequal(ocpl1, ocpl2)) {
            printf ("OCPL encoding decoding failed\n");
            goto error;
        }

        free (temp_buf);
    }

    /* release resource */
    if ((H5Pclose(ocpl1)) < 0)
        goto error;
    if ((H5Pclose(ocpl2)) < 0)
        goto error;

    PASSED();

    /******* ENCODE/DECODE LAPLS *****/
    TESTING("LAPL Encoding/Decoding");
    if ((lapl1 = H5Pcreate(H5P_LINK_ACCESS)) < 0)
        goto error;

    if ((H5Pset_nlinks(lapl1, (size_t)134)) < 0)
        goto error;

    if ((H5Pset_elink_acc_flags(lapl1, H5F_ACC_RDONLY)) < 0)
        goto error;

    if ((H5Pset_elink_prefix (lapl1, "/tmpasodiasod")) < 0)
        goto error;

    {
        void *temp_buf = NULL;
        void *enc_buf, *dec_buf;
        size_t temp_size=0;

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (lapl1, TRUE, NULL, &temp_size);

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = temp_buf;
        dec_buf = temp_buf;

        H5Pencode (lapl1, TRUE, enc_buf, &temp_size);

        lapl2 = H5Pdecode (dec_buf);

        if (0 == H5Pequal(lapl1, lapl2)) {
            printf ("LAPL encoding decoding failed\n");
            goto error;
        }

        free (temp_buf);
    }

    /* release resource */
    if ((H5Pclose(lapl1)) < 0)
        goto error;
    if ((H5Pclose(lapl2)) < 0)
        goto error;

    PASSED();

    /******* ENCODE/DECODE FAPLS *****/
    TESTING("FAPL Encoding/Decoding");
    if ((fapl1 = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        goto error;

    if ((H5Pset_family_offset(fapl1, 1024)) < 0)
        goto error;
    if ((H5Pset_meta_block_size(fapl1, 2098452)) < 0)
        goto error;
    if ((H5Pset_sieve_buf_size(fapl1, 1048576)) < 0)
        goto error;
    if ((H5Pset_alignment(fapl1, 2, 1024)) < 0)
        goto error;
    if ((H5Pset_cache(fapl1, 1024, 128, 10485760, 0.3)) < 0)
        goto error;
    if ((H5Pset_elink_file_cache_size(fapl1, 10485760)) < 0)
        goto error;
    if ((H5Pset_gc_references(fapl1, 1)) < 0)
        goto error;
    if ((H5Pset_small_data_block_size(fapl1, 2048)) < 0)
        goto error;
    if ((H5Pset_libver_bounds(fapl1, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST)) < 0)
        goto error;
    if ((H5Pset_fclose_degree(fapl1, H5F_CLOSE_WEAK)) < 0)
        goto error;
    if ((H5Pset_multi_type(fapl1, H5FD_MEM_GHEAP)) < 0)
        goto error;
    /* values seem to be set right, but still not working */
    if ((H5Pset_mdc_config(fapl1, &my_cache_config)) < 0)
        goto error;

    {
        void *temp_buf = NULL;
        void *enc_buf, *dec_buf;
        size_t temp_size=0;

        /* first call to encode returns only the size of the buffer needed */
        H5Pencode (fapl1, TRUE, NULL, &temp_size);

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = temp_buf;
        dec_buf = temp_buf;

        H5Pencode (fapl1, TRUE, enc_buf, &temp_size);

        fapl2 = H5Pdecode (dec_buf);

        if (0 == H5Pequal(fapl1, fapl2)) {
            printf ("FAPL encoding decoding failed\n");
            goto error;
        }

        free (temp_buf);
    }

    /* release resource */
    if ((H5Pclose(fapl1)) < 0)
        goto error;
    if ((H5Pclose(fapl2)) < 0)
        goto error;

    PASSED();

    return 0;

error:
    printf("***** Plist Encode/Decode tests FAILED! *****\n");
    return 1;
}
