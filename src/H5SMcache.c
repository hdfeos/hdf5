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

/****************/
/* Module Setup */
/****************/

#define H5SM_PACKAGE		/*suppress error about including H5SMpkg	  */


/***********/
/* Headers */
/***********/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5Fprivate.h"		/* File access                          */
#include "H5FLprivate.h"	/* Free Lists                           */
#include "H5MFprivate.h"        /* File memory management		*/
#include "H5MMprivate.h"	/* Memory management			*/
#include "H5SMpkg.h"            /* Shared object header messages        */
#include "H5WBprivate.h"        /* Wrapped Buffers                      */


/****************/
/* Local Macros */
/****************/

/* Size of stack buffer for serialized tables */
#define H5SM_TBL_BUF_SIZE       1024

/* Size of stack buffer for serialized list indices */
#define H5SM_LST_BUF_SIZE       1024


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Local Prototypes */
/********************/

/* Metadata cache (H5AC) callbacks */

static herr_t H5SM_cache_table_get_load_size(const void *udata_ptr,
                                             size_t *image_len_ptr);

static void *H5SM_cache_table_deserialize(const void *image_ptr,
                                          size_t len,
                                          void *udata_ptr,
                                          hbool_t *dirty_ptr);

static herr_t H5SM_cache_table_image_len(const void *thing,
                                         size_t *image_len_ptr);

static herr_t H5SM_cache_table_serialize(const H5F_t *f,
                                         void *image_ptr,
                                         size_t len,
                                         void *thing);

static herr_t H5SM_cache_table_free_icr(void *thing);


static herr_t H5SM_cache_list_get_load_size(const void *udata_ptr,
                                            size_t *image_len_ptr);

static void *H5SM_cache_list_deserialize(const void *image_ptr,
                                         size_t len,
                                         void *udata_ptr,
                                         hbool_t *dirty_ptr);

static herr_t H5SM_cache_list_image_len(const void *thing,
                                        size_t *image_len_ptr);

static herr_t H5SM_cache_list_serialize(const H5F_t *f,
                                        void *image_ptr,
                                        size_t len,
                                        void *thing);

static herr_t H5SM_cache_list_free_icr(void *thing);


/*********************/
/* Package Variables */
/*********************/
/* H5SM inherits cache-like properties from H5AC */

const H5AC_class_t H5AC_SOHM_TABLE[1] = {{
   /* id            */ H5AC_SOHM_TABLE_ID,
   /* name          */ "shared message table",
   /* mem_type      */ H5FD_MEM_SOHM_TABLE,
   /* flags         */ H5AC__CLASS_NO_FLAGS_SET,
   /* get_load_size */ (H5AC_get_load_size_func_t)H5SM_cache_table_get_load_size,
   /* deserialize   */ (H5AC_deserialize_func_t)H5SM_cache_table_deserialize,
   /* image_len     */ (H5AC_image_len_func_t)H5SM_cache_table_image_len,
   /* pre_serialize */ (H5AC_pre_serialize_func_t)NULL,
   /* serialize     */ (H5AC_serialize_func_t)H5SM_cache_table_serialize,
   /* notify        */ (H5AC_notify_func_t)NULL,
   /* free_icr      */ (H5AC_free_icr_func_t)H5SM_cache_table_free_icr,
   /* clear         */ (H5AC_clear_func_t)NULL,
   /* fsf_size      */ (H5AC_get_fsf_size_t)NULL,
}};

const H5AC_class_t H5AC_SOHM_LIST[1] = {{
   /* id            */ H5AC_SOHM_LIST_ID,
   /* name          */ "shared message list",
   /* mem_type      */ H5FD_MEM_SOHM_TABLE,
   /* flags         */ H5AC__CLASS_NO_FLAGS_SET,
   /* get_load_size */ (H5AC_get_load_size_func_t)H5SM_cache_list_get_load_size,
   /* deserialize   */ (H5AC_deserialize_func_t)H5SM_cache_list_deserialize,
   /* image_len     */ (H5AC_image_len_func_t)H5SM_cache_list_image_len,
   /* pre_serialize */ (H5AC_pre_serialize_func_t)NULL,
   /* serialize     */ (H5AC_serialize_func_t)H5SM_cache_list_serialize,
   /* notify        */ (H5AC_notify_func_t)NULL,
   /* free_icr      */ (H5AC_free_icr_func_t)H5SM_cache_list_free_icr,
   /* clear         */ (H5AC_clear_func_t)NULL,
   /* fsf_size      */ (H5AC_get_fsf_size_t)NULL,
}};


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/


/*-------------------------------------------------------------------------
 * Function:    H5SM_cache_table_get_load_size()
 *
 * Purpose: Return the size of the master table of Shared Object Header 
 *	Message indexes on disk.  As this cache client doesn't use 
 *	speculative reads, this value should be accurate.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              7/28/14
 *
 *-------------------------------------------------------------------------
 */

static herr_t
H5SM_cache_table_get_load_size(const void *udata_ptr, size_t *image_len_ptr)
{
    H5SM_table_cache_ud_t *udata = NULL;
    herr_t                 ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(udata_ptr);
    HDassert(image_len_ptr);

    udata = (H5SM_table_cache_ud_t *)udata_ptr;

    HDassert(udata);
    HDassert(udata->f);

    *image_len_ptr = H5SM_TABLE_SIZE(udata->f);

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5SM_cache_table_get_load_size() */


/*-------------------------------------------------------------------------
 * Function:    H5SM_cache_table_deserialize
 *
 * Purpose: Given a buffer containing the on disk representation of the 
 *	master table of Shared Object Header Message indexes, deserialize
 *	the table, copy the contents into a newly allocated instance of 
 *	H5SM_master_table_t, and return a pointer to the new instance.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        Pointer to in core representation
 *              Failure:        NULL
 *
 * Programmer:  John Mainzer
 *              7/28/14
 *
 *-------------------------------------------------------------------------
 */
static void *
H5SM_cache_table_deserialize(const void *image_ptr,
                             size_t len,
                             void *udata_ptr,
                             hbool_t *dirty_ptr)
{
    H5SM_table_cache_ud_t *udata = NULL;    /* pointer to user data */
    H5F_t		  *f = NULL;        /* file pointer -- from user data */
    H5SM_master_table_t   *table = NULL;    /* shared message table that we */
                                            /* deserializeing. */
    const uint8_t         *p;               /* Pointer into input buffer */
    uint32_t               stored_chksum;   /* Stored metadata checksum value */
    uint32_t               computed_chksum; /* Computed metadata checksum */
                                            /* value */
    size_t                 x;               /* Counter variable for index */
                                            /* headers */
    void *                 ret_value;       /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    HDassert(image_ptr);
    HDassert(len > 0 );
    HDassert(udata_ptr);
    HDassert(dirty_ptr);

    udata = (H5SM_table_cache_ud_t *)udata_ptr;

    HDassert(udata);
    HDassert(udata->f);

    f = udata->f;

    /* Verify that we're reading version 0 of the table; this is the only
     * version defined so far.
     */
    HDassert(H5F_SOHM_VERS(f) == HDF5_SHAREDHEADER_VERSION);

    /* Allocate space for the master table in memory */
    if(NULL == (table = H5FL_CALLOC(H5SM_master_table_t)))
        HGOTO_ERROR(H5E_SOHM, H5E_NOSPACE, NULL, "memory allocation failed")

    /* Read number of indexes and version from file superblock */
    table->num_indexes = H5F_SOHM_NINDEXES(f);

    HDassert(table->num_indexes > 0);

    /* Compute the size of the SOHM table header on disk.  This is the "table"
     * itself plus each index within the table
     */
    table->table_size = H5SM_TABLE_SIZE(f);

    HDassert(table->table_size == len);

    /* Get temporary pointer to serialized table */
    p = (const uint8_t *)image_ptr;

    /* Check magic number */
    if(HDmemcmp(p, H5SM_TABLE_MAGIC, (size_t)H5_SIZEOF_MAGIC))

        HGOTO_ERROR(H5E_SOHM, H5E_CANTLOAD, NULL, "bad SOHM table signature")

    p += H5_SIZEOF_MAGIC;

    /* Allocate space for the index headers in memory*/
    if(NULL == (table->indexes = 
                (H5SM_index_header_t *)H5FL_ARR_MALLOC(H5SM_index_header_t, 
                                                   (size_t)table->num_indexes)))

        HGOTO_ERROR(H5E_SOHM, H5E_NOSPACE, NULL, \
                    "memory allocation failed for SOHM indexes")

    /* Read in the index headers */
    for(x = 0; x < table->num_indexes; ++x) {

        /* Verify correct version of index list */
        if(H5SM_LIST_VERSION != *p++)

            HGOTO_ERROR(H5E_SOHM, H5E_VERSION, NULL, \
                        "bad shared message list version number")

        /* Type of the index (list or B-tree) */
        table->indexes[x].index_type= (H5SM_index_type_t)*p++;

        /* Type of messages in the index */
        UINT16DECODE(p, table->indexes[x].mesg_types);

        /* Minimum size of message to share */
        UINT32DECODE(p, table->indexes[x].min_mesg_size);

        /* List cutoff; fewer than this number and index becomes a list */
        UINT16DECODE(p, table->indexes[x].list_max);

        /* B-tree cutoff; more than this number and index becomes a B-tree */
        UINT16DECODE(p, table->indexes[x].btree_min);

        /* Number of messages shared */
        UINT16DECODE(p, table->indexes[x].num_messages);

        /* Address of the actual index */
        H5F_addr_decode(f, &p, &(table->indexes[x].index_addr));

        /* Address of the index's heap */
        H5F_addr_decode(f, &p, &(table->indexes[x].heap_addr));

        /* Compute the size of a list index for this SOHM index */
        table->indexes[x].list_size = 
            H5SM_LIST_SIZE(f, table->indexes[x].list_max);

    } /* end for */

    /* Read in checksum */
    UINT32DECODE(p, stored_chksum);

    /* Sanity check */
    HDassert((size_t)(p - (const uint8_t *)image_ptr) == table->table_size);

    /* Compute checksum on entire header */
    computed_chksum = H5_checksum_metadata(image_ptr, 
                                 (table->table_size - H5SM_SIZEOF_CHECKSUM), 0);

    /* Verify checksum */
    if(stored_chksum != computed_chksum)

        HGOTO_ERROR(H5E_SOHM, H5E_BADVALUE, NULL, \
                    "incorrect metadata checksum for shared message table")

    /* Set return value */
    ret_value = table;

done:

    if(!ret_value && table)

        if(H5SM_table_free(table) < 0)

            HDONE_ERROR(H5E_SOHM, H5E_CANTFREE, NULL, \
                        "unable to destroy sohm table")

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5SM_cache_table_deserialize() */


/*-------------------------------------------------------------------------
 * Function:    H5SM_cache_table_image_len
 *
 * Purpose:
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              7/28/14
 *
 *-------------------------------------------------------------------------
 */

static herr_t
H5SM_cache_table_image_len(const void *thing, size_t *image_len_ptr)
{
    const H5SM_master_table_t *table = NULL;
    herr_t                     ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);
    HDassert(image_len_ptr);

    table = (const H5SM_master_table_t *)thing;

    HDassert(table);
    HDassert(table->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(table->cache_info.type) == \
             &(H5AC_SOHM_TABLE[0]));

    *image_len_ptr = table->table_size;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5SM_cache_table_image_len() */


/***************************************/
/* no H5SM_cache_table_pre_serialize() */
/***************************************/


/*-------------------------------------------------------------------------
 * Function:    H5SM_cache_table_serialize
 *
 * Purpose:
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              7/28/14
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5SM_cache_table_serialize(const H5F_t *f,
                           void *image_ptr,
                           size_t len,
                           void *thing)
{
    H5SM_master_table_t *table = NULL;
    uint8_t             *p = NULL;         /* Pointer into raw data buffer */
    uint32_t             computed_chksum;  /* Computed metadata checksum value */
    size_t               x;                /* Counter variable */
    herr_t               ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(f);
    HDassert(image_ptr);
    HDassert(thing);

    table = (H5SM_master_table_t *)thing;

    HDassert(table);
    HDassert(table->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(table->cache_info.type) == \
             &(H5AC_SOHM_TABLE[0]));
    HDassert(table->table_size == len);

    /* Verify that we're writing version 0 of the table; this is the only
     * version defined so far.
     */
    HDassert(H5F_SOHM_VERS(f) == HDF5_SHAREDHEADER_VERSION);

    /* Get temporary pointer to buffer for serialized table */
    p = (uint8_t *)image_ptr;

    /* Encode magic number */
    HDmemcpy(p, H5SM_TABLE_MAGIC, (size_t)H5_SIZEOF_MAGIC);
    p += H5_SIZEOF_MAGIC;

    /* Encode each index header */
   for(x = 0; x < table->num_indexes; ++x) {

        /* Version for this list. */
        *p++ = H5SM_LIST_VERSION;

        /* Is message index a list or a B-tree? */
        *p++ = table->indexes[x].index_type;

        /* Type of messages in the index */
        UINT16ENCODE(p, table->indexes[x].mesg_types);

        /* Minimum size of message to share */
        UINT32ENCODE(p, table->indexes[x].min_mesg_size);

        /* List cutoff; fewer than this number and index becomes a list */
        UINT16ENCODE(p, table->indexes[x].list_max);

        /* B-tree cutoff; more than this number and index becomes a B-tree */
        UINT16ENCODE(p, table->indexes[x].btree_min);

        /* Number of messages shared */
        UINT16ENCODE(p, table->indexes[x].num_messages);

        /* Address of the actual index */
        H5F_addr_encode(f, &p, table->indexes[x].index_addr);

        /* Address of the index's heap */
         H5F_addr_encode(f, &p, table->indexes[x].heap_addr);

    } /* end for */

    /* Compute checksum on buffer */
    computed_chksum = 
	H5_checksum_metadata(image_ptr, 
                             (table->table_size - H5SM_SIZEOF_CHECKSUM), 0);
    UINT32ENCODE(p, computed_chksum);

    /* sanity check */
    HDassert((size_t)(p - ((uint8_t *)image_ptr)) == table->table_size);

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5SM_cache_table_serialize() */


/*****************************************/
/* no H5SM_cache_table_notify() function */
/*****************************************/


/*-------------------------------------------------------------------------
 * Function:    H5SM_cache_table_free_icr
 *
 * Purpose: Free memory used by the SOHM table.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              7/28/14
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5SM_cache_table_free_icr(void *thing)
{
    H5SM_master_table_t *table = NULL;
    herr_t               ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);

    table = (H5SM_master_table_t *)thing;

    HDassert(table);

    /* the metadata cache sets cache_info.magic to
     * H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC before calling the
     * free_icr routine.  Hence the following assert:
     */

    HDassert(table->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC);
    HDassert((const H5AC_class_t *)(table->cache_info.type) == \
             &(H5AC_SOHM_TABLE[0]));

    /* Destroy Shared Object Header Message table */
    if(H5SM_table_free(table) < 0)

        HGOTO_ERROR(H5E_SOHM, H5E_CANTRELEASE, FAIL, \
                    "unable to free shared message table")

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5O_cache_table_free_icr() */


/*-------------------------------------------------------------------------
 * Function:    H5SM_cache_list_get_load_size()
 *
 * Purpose: Return the on disk size of list of SOHM messages.  In this case,
 *	we simply look up the size in the user data, and return that value
 *	in *image_len_ptr.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              7/28/14
 *
 *-------------------------------------------------------------------------
 */

static herr_t
H5SM_cache_list_get_load_size(const void *udata_ptr, size_t *image_len_ptr)
{
    H5SM_list_cache_ud_t *udata = NULL;        /* User data for callback */
    herr_t                ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(udata_ptr);
    HDassert(image_len_ptr);

    udata = (H5SM_list_cache_ud_t *)udata_ptr;

    HDassert(udata);
    HDassert(udata->header);
    HDassert(udata->header->list_size > 0);

    *image_len_ptr = udata->header->list_size;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5SM_cache_list_get_load_size() */


/*-------------------------------------------------------------------------
 * Function:    H5SM_cache_list_deserialize
 *
 * Purpose: Given a buffer containing the on disk image of a list of 
 *	SOHM message, deserialize the list, load it into a newly allocated 
 *	instance of H5SM_list_t, and return a pointer to same.
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        Pointer to in core representation
 *              Failure:        NULL
 *
 * Programmer:  John Mainzer
 *              7/28/14
 *
 *-------------------------------------------------------------------------
 */
static void *
H5SM_cache_list_deserialize(const void *image_ptr,
                            size_t len,
                            void *udata_ptr,
                            hbool_t *dirty_ptr)
{
    H5SM_list_t          *list = NULL;     /* The SOHM list being read in */
    H5SM_list_cache_ud_t *udata = NULL;    /* User data for callback */
    H5SM_bt2_ctx_t        ctx;             /* Message encoding context */
    uint8_t              *p;               /* Pointer into input buffer */
    uint32_t              stored_chksum;   /* Stored metadata checksum value */
    uint32_t              computed_chksum; /* Computed metadata checksum value */
    size_t                x;               /* Counter variable for messages */
                                           /* in list */
    void *                ret_value;       /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    HDassert(image_ptr);
    HDassert(len > 0 );
    HDassert(udata_ptr);
    HDassert(dirty_ptr);

    udata = (H5SM_list_cache_ud_t *)udata_ptr;

    HDassert(udata);
    HDassert(udata->header);
    HDassert(udata->header->list_size == len);

    /* Allocate space for the SOHM list data structure */
    if(NULL == (list = H5FL_MALLOC(H5SM_list_t)))

        HGOTO_ERROR(H5E_SOHM, H5E_NOSPACE, NULL, "memory allocation failed")

    HDmemset(&list->cache_info, 0, sizeof(H5AC_info_t));

    /* Allocate list in memory as an array*/
    if((list->messages = (H5SM_sohm_t *)H5FL_ARR_MALLOC(H5SM_sohm_t, 
                                              udata->header->list_max)) == NULL)

        HGOTO_ERROR(H5E_SOHM, H5E_NOSPACE, NULL, \
                   "file allocation failed for SOHM list")

    list->header = udata->header;

    /* Get temporary pointer to serialized list index */
    p = (uint8_t *)image_ptr;

    /* Check magic number */
    if(HDmemcmp(p, H5SM_LIST_MAGIC, (size_t)H5_SIZEOF_MAGIC))

        HGOTO_ERROR(H5E_SOHM, H5E_CANTLOAD, NULL, "bad SOHM list signature")

    p += H5_SIZEOF_MAGIC;

    /* Read messages into the list array */
    ctx.sizeof_addr = H5F_SIZEOF_ADDR(udata->f);
    for(x = 0; x < udata->header->num_messages; x++) {

        if(H5SM_message_decode(p, &(list->messages[x]), &ctx) < 0)

            HGOTO_ERROR(H5E_SOHM, H5E_CANTLOAD, NULL, \
                        "can't decode shared message")

        p += H5SM_SOHM_ENTRY_SIZE(udata->f);

    } /* end for */

    /* Read in checksum */
    UINT32DECODE(p, stored_chksum);

    /* Sanity check */
    HDassert((size_t)(p - (uint8_t *)image_ptr) <= udata->header->list_size);

    /* Compute checksum on entire header */
    computed_chksum = 
	H5_checksum_metadata(image_ptr, 
                 ((size_t)(p - (uint8_t *)image_ptr) - H5SM_SIZEOF_CHECKSUM), 0);

    /* Verify checksum */
    if(stored_chksum != computed_chksum)

        HGOTO_ERROR(H5E_SOHM, H5E_BADVALUE, NULL, \
                    "incorrect metadata checksum for shared message list")

    /* Initialize the rest of the array */
    for(x = udata->header->num_messages; x < udata->header->list_max; x++)

        list->messages[x].location = H5SM_NO_LOC;

    /* Set return value */
    ret_value = list;

done:

    if(!ret_value && list) {

        if(list->messages)

            list->messages = H5FL_ARR_FREE(H5SM_sohm_t, list->messages);

        list = H5FL_FREE(H5SM_list_t, list);

    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5SM_cache_list_deserialize() */


/*-------------------------------------------------------------------------
 * Function:    H5SM_cache_list_image_len
 *
 * Purpose: Get the size of the shared message list on disk.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              7/28/14
 *
 *-------------------------------------------------------------------------
 */

static herr_t
H5SM_cache_list_image_len(const void *thing, size_t *image_len_ptr)
{
    const H5SM_list_t *list = NULL;
    herr_t             ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);
    HDassert(image_len_ptr);

    list = (const H5SM_list_t *)thing;

    HDassert(list);
    HDassert(list->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(list->cache_info.type) == \
             &(H5AC_SOHM_LIST[0]));
    HDassert(list->header);

    *image_len_ptr = list->header->list_size;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5SM_cache_list_image_len() */


/**************************************/
/* no H5SM_cache_list_pre_serialize() */
/**************************************/



/*-------------------------------------------------------------------------
 * Function:    H5SM_cache_list_serialize
 *
 * Purpose:
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              7/28/14
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5SM_cache_list_serialize(const H5F_t *f,
                          void *image_ptr,
                          size_t len,
                          void *thing)
{
    H5SM_list_t *list = NULL;   /* instance being serialized */
    H5SM_bt2_ctx_t ctx;         /* Message encoding context */
    uint8_t *p;                 /* Pointer into raw data buffer */
    uint32_t computed_chksum;   /* Computed metadata checksum value */
    size_t mesgs_serialized;    /* Number of messages serialized */
    size_t x;                   /* Local index variable */
    herr_t       ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(f);
    HDassert(image_ptr);
    HDassert(thing);

    list = (H5SM_list_t *)thing;

    HDassert(list);
    HDassert(list->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(list->cache_info.type) == \
             &(H5AC_SOHM_LIST[0]));
    HDassert(list->header);
    HDassert(list->header->list_size == len);

    /* Get temporary pointer to buffer for serialized list index */
    p = (uint8_t *)image_ptr;

    /* Encode magic number */
    HDmemcpy(p, H5SM_LIST_MAGIC, (size_t)H5_SIZEOF_MAGIC);
    p += H5_SIZEOF_MAGIC;

    /* serialize messages from the messages array */
    mesgs_serialized = 0;
    ctx.sizeof_addr = H5F_SIZEOF_ADDR(f);
    for(x = 0; 
        ((x < list->header->list_max) && 
         (mesgs_serialized < list->header->num_messages)); 
        x++) {

        if(list->messages[x].location != H5SM_NO_LOC) {

            if(H5SM_message_encode(p, &(list->messages[x]), &ctx) < 0)

                HGOTO_ERROR(H5E_SOHM, H5E_CANTFLUSH, FAIL, \
                            "unable to serialize shared message")

            p += H5SM_SOHM_ENTRY_SIZE(f);
            ++mesgs_serialized;

        } /* end if */
    } /* end for */

    HDassert(mesgs_serialized == list->header->num_messages);

    /* Compute checksum on buffer */
    computed_chksum = 
	H5_checksum_metadata(image_ptr, (size_t)(p - (uint8_t *)image_ptr), 0);
    UINT32ENCODE(p, computed_chksum);

#ifdef H5_CLEAR_MEMORY
    HDmemset(p, 0, 
             (list->header->list_size - (size_t)(p - (uint8_t *)image_ptr)));
#endif /* H5_CLEAR_MEMORY */

    /* sanity check */
    HDassert((size_t)(p - (uint8_t *)image_ptr) <= list->header->list_size);

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5SM_cache_list_serialize() */


/****************************************/
/* no H5SM_cache_list_notify() function */
/****************************************/


/*-------------------------------------------------------------------------
 * Function:    H5SM_cache_list_free_icr
 *
 * Purpose: Free all memory used by the list.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              7/28/14
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5SM_cache_list_free_icr(void *thing)
{
    H5SM_list_t *list = NULL;
    herr_t       ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);

    list = (H5SM_list_t *)thing;

    HDassert(list);

    /* the metadata cache sets cache_info.magic to
     * H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC before calling the
     * free_icr routine.  Hence the following assert:
     */

    HDassert(list->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC);
    HDassert((const H5AC_class_t *)(list->cache_info.type) == \
             &(H5AC_SOHM_LIST[0]));

    /* Destroy Shared Object Header Message list */
    if(H5SM_list_free(list) < 0)

        HGOTO_ERROR(H5E_SOHM, H5E_CANTRELEASE, FAIL, \
                    "unable to free shared message list")

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5O_cache_list_free_icr() */


