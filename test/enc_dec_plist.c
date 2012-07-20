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
    hid_t gcpl1, gcpl2;	       	/* group create prop. list */
    hid_t lcpl1, lcpl2;	       	/* link create prop. list */
    hid_t lapl1, lapl2;	       	/* link create prop. list */
    hid_t ocpypl1, ocpypl2;	/* object copy prop. list */
    hid_t ocpl1, ocpl2;	        /* object create prop. list */

    hsize_t chunk_size = 16384;	/* chunk size */ 
    double fill=2.7;            /* Fill value */
    hsize_t max_size[1];        /* data space maximum size */
    size_t nslots = 521*2;
    size_t nbytes = 1048576 * 10;
    double w0 = 0.5;
    unsigned max_compact;
    unsigned min_dense;

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
        uint8_t *enc_buf, *dec_buf;
        size_t temp_size=0;
        H5P_plist_type_t class_type;

        class_type = H5P_TYPE_DATASET_CREATE;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (dcpl1, NULL, &temp_size);
        /* extra space for plist type */
        temp_size += sizeof(uint8_t); 

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = (uint8_t *)temp_buf;
        dec_buf = (uint8_t *)temp_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (dcpl1, enc_buf, &temp_size);

        class_type = *dec_buf++;
        if (class_type != H5P_TYPE_DATASET_CREATE) {
            goto error;
        }

        if ((dcpl2 = H5Pcreate(H5P_DATASET_CREATE)) < 0)
            goto error;

        H5P_decode (dcpl2, temp_size, dec_buf);

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
        uint8_t *enc_buf, *dec_buf;
        size_t temp_size=0;
        H5P_plist_type_t class_type;

        class_type = H5P_TYPE_DATASET_ACCESS;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (dapl1, NULL, &temp_size);
        /* extra space for plist type */
        temp_size += sizeof(uint8_t); 

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = (uint8_t *)temp_buf;
        dec_buf = (uint8_t *)temp_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (dapl1, enc_buf, &temp_size);

        class_type = *dec_buf++;
        if (class_type != H5P_TYPE_DATASET_ACCESS) {
            printf ("DAPL encoding decoding failed\n");
            goto error;
        }

        if ((dapl2 = H5Pcreate(H5P_DATASET_ACCESS)) < 0)
            goto error;

        H5P_decode (dapl2, temp_size, dec_buf);

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

    /******* ENCODE/DECODE GCPLS *****/
    TESTING("GCPL Encoding/Decoding");
    if ((gcpl1 = H5Pcreate(H5P_GROUP_CREATE)) < 0)
        goto error;

    if ((H5Pset_link_creation_order(gcpl1, (H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED))) < 0)
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

    {
        void *temp_buf = NULL;
        uint8_t *enc_buf, *dec_buf;
        size_t temp_size=0;
        H5P_plist_type_t class_type;

        class_type = H5P_TYPE_GROUP_CREATE;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (gcpl1, NULL, &temp_size);
        /* extra space for plist type */
        temp_size += sizeof(uint8_t); 

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = (uint8_t *)temp_buf;
        dec_buf = (uint8_t *)temp_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (gcpl1, enc_buf, &temp_size);

        class_type = *dec_buf++;
        if (class_type != H5P_TYPE_GROUP_CREATE) {
            printf ("GCPL encoding decoding failed\n");
            goto error;
        }

        if ((gcpl2 = H5Pcreate(H5P_GROUP_CREATE)) < 0)
            goto error;

        H5P_decode (gcpl2, temp_size, dec_buf);

        if (0 == H5Pequal(gcpl1, gcpl2)){
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
        uint8_t *enc_buf, *dec_buf;
        size_t temp_size=0;
        H5P_plist_type_t class_type;

        class_type = H5P_TYPE_LINK_CREATE;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (lcpl1, NULL, &temp_size);
        /* extra space for plist type */
        temp_size += sizeof(uint8_t); 

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = (uint8_t *)temp_buf;
        dec_buf = (uint8_t *)temp_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (lcpl1, enc_buf, &temp_size);

        class_type = *dec_buf++;
        if (class_type != H5P_TYPE_LINK_CREATE) {
            printf ("LCPL encoding decoding failed\n");
            goto error;
        }

        if ((lcpl2 = H5Pcreate(H5P_LINK_CREATE)) < 0)
            goto error;

        H5P_decode (lcpl2, temp_size, dec_buf);

        if (0 == H5Pequal(lcpl1, lcpl2)){
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
        uint8_t *enc_buf, *dec_buf;
        size_t temp_size=0;
        H5P_plist_type_t class_type;

        class_type = H5P_TYPE_OBJECT_COPY;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (ocpypl1, NULL, &temp_size);
        /* extra space for plist type */
        temp_size += sizeof(uint8_t); 

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = (uint8_t *)temp_buf;
        dec_buf = (uint8_t *)temp_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (ocpypl1, enc_buf, &temp_size);

        class_type = *dec_buf++;
        if (class_type != H5P_TYPE_OBJECT_COPY) {
            printf ("OCPYPL encoding decoding failed\n");
            goto error;
        }

        if ((ocpypl2 = H5Pcreate(H5P_OBJECT_COPY)) < 0)
            goto error;

        H5P_decode (ocpypl2, temp_size, dec_buf);

        if (0 == H5Pequal(ocpypl1, ocpypl2)){
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
        uint8_t *enc_buf, *dec_buf;
        size_t temp_size=0;
        H5P_plist_type_t class_type;

        class_type = H5P_TYPE_OBJECT_CREATE;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (ocpl1, NULL, &temp_size);
        /* extra space for plist type */
        temp_size += sizeof(uint8_t); 

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = (uint8_t *)temp_buf;
        dec_buf = (uint8_t *)temp_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (ocpl1, enc_buf, &temp_size);

        class_type = *dec_buf++;
        if (class_type != H5P_TYPE_OBJECT_CREATE) {
            printf ("OCPL encoding decoding failed\n");
            goto error;
        }

        if ((ocpl2 = H5Pcreate(H5P_OBJECT_CREATE)) < 0)
            goto error;

        H5P_decode (ocpl2, temp_size, dec_buf);

        if (0 == H5Pequal(ocpl1, ocpl2)){
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
        uint8_t *enc_buf, *dec_buf;
        size_t temp_size=0;
        H5P_plist_type_t class_type;

        class_type = H5P_TYPE_LINK_ACCESS;

        /* first call to encode returns only the size of the buffer needed */
        H5P_encode (lapl1, NULL, &temp_size);
        /* extra space for plist type */
        temp_size += sizeof(uint8_t); 

        temp_buf = (uint8_t *) malloc (temp_size);
        enc_buf = (uint8_t *)temp_buf;
        dec_buf = (uint8_t *)temp_buf;

        *enc_buf++ = (uint8_t)class_type;
        H5P_encode (lapl1, enc_buf, &temp_size);

        class_type = *dec_buf++;
        if (class_type != H5P_TYPE_LINK_ACCESS) {
            printf ("LAPL encoding decoding failed\n");
            goto error;
        }

        if ((lapl2 = H5Pcreate(H5P_LINK_ACCESS)) < 0)
            goto error;

        H5P_decode (lapl2, temp_size, dec_buf);

        if (0 == H5Pequal(lapl1, lapl2)){
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

    return 0;

error:
    printf("***** Plist Encode/Decode tests FAILED! *****\n");
    return 1;
}
