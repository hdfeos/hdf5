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

/*-------------------------------------------------------------------------
 *
 * Created:		H5HLcache.c
 *			Feb  5 2008
 *			Quincey Koziol <koziol@hdfgroup.org>
 *
 * Purpose:		Implement local heap metadata cache methods.
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#define H5HL_PACKAGE		/* Suppress error about including H5HLpkg */


/***********/
/* Headers */
/***********/
#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5HLpkg.h"		/* Local Heaps				*/
#include "H5MFprivate.h"	/* File memory management		*/
#include "H5WBprivate.h"        /* Wrapped Buffers                      */


/****************/
/* Local Macros */
/****************/

#define H5HL_VERSION	0               /* Local heap collection version    */

/* Set the local heap size to speculatively read in */
/* (needs to be more than the local heap prefix size to work at all and
 *      should be larger than the default local heap size to save the
 *      extra I/O operations) */
#define H5HL_SPEC_READ_SIZE 512


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/

/* Metadata cache callbacks */
#if 1 /* V3 metadata cache callbacks */

static herr_t H5HL_cache_prefix_get_load_size(const void *udata_ptr,
                                              size_t *image_len_ptr);

static void *H5HL_cache_prefix_deserialize(const void *image_ptr,
                                           size_t len,
                                           void *udata_ptr,
                                           hbool_t *dirty_ptr);

static herr_t H5HL_cache_prefix_image_len(const void *thing,
                                          size_t *image_len_ptr);

static herr_t H5HL_cache_prefix_serialize(const H5F_t *f,
                                          void *image_ptr,
                                          size_t len,
                                          void *thing);

static herr_t H5HL_cache_prefix_free_icr(void *thing);


static herr_t H5HL_cache_datablock_get_load_size(const void *udata_ptr,
                                                 size_t *image_len_ptr);

static void *H5HL_cache_datablock_deserialize(const void *image_ptr,
                                              size_t len,
                                              void *udata_ptr,
                                              hbool_t *dirty_ptr);

static herr_t H5HL_cache_datablock_image_len(const void *thing,
                                             size_t *image_len_ptr);

static herr_t H5HL_cache_datablock_serialize(const H5F_t *f,
                                             void *image_ptr,
                                             size_t len,
                                             void *thing);

static herr_t H5HL_cache_datablock_free_icr(void *thing);



#else /* V2 metadata cache callbacks */

static void *H5HL_prefix_load(H5F_t *f, hid_t dxpl_id, haddr_t addr, void *udata);
static herr_t H5HL_prefix_flush(H5F_t *f, hid_t dxpl_id, hbool_t dest, haddr_t addr,
    void *thing, unsigned *flags_ptr);
static herr_t H5HL_prefix_dest(H5F_t *f, void *thing);
static herr_t H5HL_prefix_clear(H5F_t *f, void *thing, hbool_t destroy);
static herr_t H5HL_prefix_size(const H5F_t *f, const void *thing, size_t *size_ptr);
static void *H5HL_datablock_load(H5F_t *f, hid_t dxpl_id, haddr_t addr, void *udata);
static herr_t H5HL_datablock_flush(H5F_t *f, hid_t dxpl_id, hbool_t dest, haddr_t addr,
    void *thing, unsigned *flags_ptr);
static herr_t H5HL_datablock_dest(H5F_t *f, void *thing);
static herr_t H5HL_datablock_clear(H5F_t *f, void *thing, hbool_t destroy);
static herr_t H5HL_datablock_size(const H5F_t *f, const void *thing, size_t *size_ptr);

#endif /* end V2 metadata cache callbacks */


/*********************/
/* Package Variables */
/*********************/

/* H5HL inherits cache-like properties from H5AC */
#if 1 /* V3 cache instaces of H5AC_class_t */

const H5AC_class_t H5AC_LHEAP_PRFX[1] = {{
  /* id            */ H5AC_LHEAP_PRFX_ID,
  /* name          */ "local heap prefix",
  /* mem_type      */ H5FD_MEM_LHEAP,
  /* flags         */ H5AC__CLASS_SPECULATIVE_LOAD_FLAG,
  /* get_load_size */ (H5AC_get_load_size_func_t)H5HL_cache_prefix_get_load_size,
  /* deserialize   */ (H5AC_deserialize_func_t)H5HL_cache_prefix_deserialize,
  /* image_len     */ (H5AC_image_len_func_t)H5HL_cache_prefix_image_len,
  /* pre_serialize */ (H5AC_pre_serialize_func_t)NULL,
  /* serialize     */ (H5AC_serialize_func_t)H5HL_cache_prefix_serialize,
  /* notify        */ (H5AC_notify_func_t)NULL,
  /* free_icr      */ (H5AC_free_icr_func_t)H5HL_cache_prefix_free_icr,
  /* clear         */ (H5AC_clear_func_t)NULL,
  /* fsf_size      */ (H5AC_get_fsf_size_t)NULL,
}};

const H5AC_class_t H5AC_LHEAP_DBLK[1] = {{
  /* id            */ H5AC_LHEAP_DBLK_ID,
  /* name          */ "local heap datablock",
  /* mem_type      */ H5FD_MEM_LHEAP,
  /* flags         */ H5AC__CLASS_NO_FLAGS_SET,
  /* get_load_size */ (H5AC_get_load_size_func_t)H5HL_cache_datablock_get_load_size,
  /* deserialize   */ (H5AC_deserialize_func_t)H5HL_cache_datablock_deserialize,
  /* image_len     */ (H5AC_image_len_func_t)H5HL_cache_datablock_image_len,
  /* pre_serialize */ (H5AC_pre_serialize_func_t)NULL,
  /* serialize     */ (H5AC_serialize_func_t)H5HL_cache_datablock_serialize,
  /* notify        */ (H5AC_notify_func_t)NULL,
  /* free_icr      */ (H5AC_free_icr_func_t)H5HL_cache_datablock_free_icr,
  /* clear         */ (H5AC_clear_func_t)NULL,
  /* fsf_size      */ (H5AC_get_fsf_size_t)NULL,
}};

#else /* V2 cache instances of H5AC_class_t */

const H5AC_class_t H5AC_LHEAP_PRFX[1] = {{
    H5AC_LHEAP_PRFX_ID,
    H5HL_prefix_load,
    H5HL_prefix_flush,
    H5HL_prefix_dest,
    H5HL_prefix_clear,
    NULL,
    H5HL_prefix_size,
}};

const H5AC_class_t H5AC_LHEAP_DBLK[1] = {{
    H5AC_LHEAP_DBLK_ID,
    H5HL_datablock_load,
    H5HL_datablock_flush,
    H5HL_datablock_dest,
    H5HL_datablock_clear,
    NULL,
    H5HL_datablock_size,
}};

#endif /* V2 cache instances of H5AC_class_t */


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/



/*-------------------------------------------------------------------------
 * Function:	H5HL_fl_deserialize
 *
 * Purpose:	Deserialize the free list for a heap data block
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Oct 12 2008
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_fl_deserialize(H5HL_t *heap)
{
    H5HL_free_t *fl = NULL, *tail = NULL;      /* Heap free block nodes */
    hsize_t free_block;                 /* Offset of free block */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* check arguments */
    HDassert(heap);
    HDassert(!heap->freelist);

    /* Build free list */
    free_block = heap->free_block;
    while(H5HL_FREE_NULL != free_block) {
        const uint8_t *p;               /* Pointer into image buffer */

        /* Sanity check */
        if(free_block >= heap->dblk_size)
            HGOTO_ERROR(H5E_HEAP, H5E_BADRANGE, FAIL, "bad heap free list")

        /* Allocate & initialize free list node */
        if(NULL == (fl = H5FL_MALLOC(H5HL_free_t)))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, FAIL, "memory allocation failed")
        fl->offset = (size_t)free_block;
        fl->prev = tail;
        fl->next = NULL;

        /* Decode offset of next free block */
        p = heap->dblk_image + free_block;
        H5F_DECODE_LENGTH_LEN(p, free_block, heap->sizeof_size);
        if(free_block == 0)
            HGOTO_ERROR(H5E_HEAP, H5E_BADVALUE, FAIL, "free block size is zero?")

        /* Decode length of this free block */
        H5F_DECODE_LENGTH_LEN(p, fl->size, heap->sizeof_size);
        if((fl->offset + fl->size) > heap->dblk_size)
            HGOTO_ERROR(H5E_HEAP, H5E_BADRANGE, FAIL, "bad heap free list")

        /* Append node onto list */
        if(tail)
            tail->next = fl;
        else
            heap->freelist = fl;
        tail = fl;
        fl = NULL;
    } /* end while */

done:
    if(ret_value < 0)
        if(fl)
            fl = H5FL_FREE(H5HL_free_t, fl);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HL_fl_deserialize() */


/*-------------------------------------------------------------------------
 * Function:	H5HL_fl_serialize
 *
 * Purpose:	Serialize the free list for a heap data block
 *
 * Return:	Success:	SUCCESS
 *		Failure:	FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Oct 12 2008
 *
 *-------------------------------------------------------------------------
 */
static void
H5HL_fl_serialize(const H5HL_t *heap)
{
    H5HL_free_t	*fl;                    /* Pointer to heap free list node */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* check arguments */
    HDassert(heap);

    /* Serialize the free list into the heap data's image */
    for(fl = heap->freelist; fl; fl = fl->next) {
        uint8_t     *p;                     /* Pointer into raw data buffer */

        HDassert(fl->offset == H5HL_ALIGN(fl->offset));
        p = heap->dblk_image + fl->offset;

        if(fl->next)
            H5F_ENCODE_LENGTH_LEN(p, fl->next->offset, heap->sizeof_size)
        else
            H5F_ENCODE_LENGTH_LEN(p, H5HL_FREE_NULL, heap->sizeof_size)

        H5F_ENCODE_LENGTH_LEN(p, fl->size, heap->sizeof_size)
    } /* end for */

    FUNC_LEAVE_NOAPI_VOID
} /* end H5HL_fl_serialize() */

#if 1 /* V3 cache callbacks */


/*-------------------------------------------------------------------------
 * Function:    H5HL_cache_prefix_get_load_size()
 *
 * Purpose: Return the size of the buffer the metadata cache should 
 *	load from file and pass to the deserialize routine.
 *
 *	The version 2 metadata cache callbacks included a test to 
 *	ensure that the read did not pass the end of file, but this 
 *	functionality has been moved to H5C_load_entry().  Thus 
 *	all this function does is set *image_len_ptr equal to 
 *	H5HL_SPEC_READ_SIZE, leaving it to the metadata cache to 
 *	reduce the size of the read if appropriate.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              6/21/14
 *
 *-------------------------------------------------------------------------
 */

static herr_t
H5HL_cache_prefix_get_load_size(const void UNUSED *udata_ptr, 
                                size_t *image_len_ptr)
{
    herr_t                ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(image_len_ptr);

   *image_len_ptr = H5HL_SPEC_READ_SIZE;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HL_cache_prefix_get_load_size() */


/*-------------------------------------------------------------------------
 * Function:    H5HL_cache_prefix_deserialize
 *
 * Purpose:
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        Pointer to in core representation
 *              Failure:        NULL
 *
 * Programmer:  John Mainzer
 *              6/21/14
 *
 *-------------------------------------------------------------------------
 */
static void *
H5HL_cache_prefix_deserialize(const void *image_ptr,
                              size_t len,
                              void *udata_ptr,
                              hbool_t *dirty_ptr)
{
    H5HL_t               *heap = NULL;            /* Local heap */
    H5HL_prfx_t          *prfx = NULL;       /* Heap prefix deserialized */
    const uint8_t        *p;         /* Pointer into decoding buffer */
    H5HL_cache_prfx_ud_t *udata = NULL;
    void *                ret_value;      /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    HDassert(image_ptr);
    HDassert(len > 0 );
    HDassert(udata_ptr);
    HDassert(dirty_ptr);

    udata = (H5HL_cache_prfx_ud_t *)udata_ptr;

    HDassert(udata);
    HDassert(udata->sizeof_size > 0);
    HDassert(udata->sizeof_addr > 0);
    HDassert(udata->sizeof_prfx > 0);
    HDassert(H5F_addr_defined(udata->prfx_addr));

    p = (const uint8_t *)image_ptr;

    /* Check magic number */
    if(HDmemcmp(p, H5HL_MAGIC, (size_t)H5_SIZEOF_MAGIC))

        HGOTO_ERROR(H5E_HEAP, H5E_BADVALUE, NULL, "bad local heap signature")

    p += H5_SIZEOF_MAGIC;

    /* Version */
    if(H5HL_VERSION != *p++)

        HGOTO_ERROR(H5E_HEAP, H5E_BADVALUE, NULL, \
                    "wrong version number in local heap")

    /* Reserved */
    p += 3;
    
    /* Allocate space in memory for the heap */
    if(NULL == (heap = H5HL_new(udata->sizeof_size, udata->sizeof_addr, 
                                udata->sizeof_prfx)))

        HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, NULL, \
                    "can't allocate local heap structure")

    /* Allocate the heap prefix */
    if(NULL == (prfx = H5HL_prfx_new(heap)))

        HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, NULL, \
                    "can't allocate local heap prefix")

#if 0 /* doesn't look like we need this -- JRM */
    HDassert(!prfx->spec_load_failed);
#endif /* JRM */

    /* Store the prefix's address & length */
    heap->prfx_addr = udata->prfx_addr;
    heap->prfx_size = udata->sizeof_prfx;

    /* Heap data size */
    H5F_DECODE_LENGTH_LEN(p, heap->dblk_size, udata->sizeof_size);

    /* Free list head */
    H5F_DECODE_LENGTH_LEN(p, heap->free_block, udata->sizeof_size);

    if((heap->free_block != H5HL_FREE_NULL) &&
       (heap->free_block >= heap->dblk_size))

        HGOTO_ERROR(H5E_HEAP, H5E_BADVALUE, NULL, "bad heap free list")

    /* Heap data address */
    H5F_addr_decode_len(udata->sizeof_addr, &p, &(heap->dblk_addr));

    /* Check if heap block exists */
    if(heap->dblk_size) {

        /* Check if heap data block is contiguous with header */
        if(H5F_addr_eq((heap->prfx_addr + heap->prfx_size), heap->dblk_addr)) {

            /* Note that the heap should be a single object in the cache */
            heap->single_cache_obj = TRUE;

            /* Check if the current buffer from the speculative read 
             * already has the heap data 
             */
            if(len >= (heap->prfx_size + heap->dblk_size)) {

                /* Allocate space for the heap data image */
                if(NULL == (heap->dblk_image = H5FL_BLK_MALLOC(lheap_chunk, 
                                                           heap->dblk_size)))

                    HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, NULL, \
                                "memory allocation failed")

                /* Set p to the start of the data block.  This is necessary
                 * because there may be a gap between the used portion of the
                 * prefix and the data block due to alignment constraints. */
                p = ((const uint8_t *)image_ptr) + heap->prfx_size;

                /* Copy the heap data from the speculative read buffer */
                HDmemcpy(heap->dblk_image, p, heap->dblk_size);

                /* Build free list */
                if(H5HL_fl_deserialize(heap) < 0)

                    HGOTO_ERROR(H5E_HEAP, H5E_CANTINIT, NULL, \
                                "can't initialize free list")

            } /* end if */
            else {

		/* the supplied buffer is too small -- We have already made note
                 * of the correct size, so simply return success.  H5C_load_entry()
                 * will notice the size discrepency, and re-try the load.
                 */

                /* Make certain that this is the first try ... */
                HDassert(!udata->made_attempt);

                /* ... and mark the udata so that we know that we have used up
                 * our first try.
                 */
                udata->made_attempt = TRUE;
#if 0 /* doesn't look like we need this -- JRM */
                /* Note that H5HL_cache_prefix_free_icr() must also free the
                 * the heap when it frees the prefix.  Do this by setting 
                 * prefix->spec_load_failed to TRUE.
                 */
	        prfx->spec_load_failed = TRUE;
#endif /* JRM */
	    } /* end else */
        } /* end if */
        else {

            /* Note that the heap should _NOT_ be a single 
             * object in the cache 
             */
            heap->single_cache_obj = FALSE;

	} /* end else */
    } /* end if */

    /* Set flag to indicate prefix from loaded from file */
    udata->loaded = TRUE;

    /* Set return value */
    ret_value = prfx;

done:

    /* Release the [possibly partially initialized] local heap on errors */
    if(!ret_value) {

        if(prfx) {

            if(H5HL_prfx_dest(prfx) < 0)

                HDONE_ERROR(H5E_HEAP, H5E_CANTRELEASE, NULL, \
                            "unable to destroy local heap prefix")

        } /* end if */
        else {

            if(heap && H5HL_dest(heap) < 0)

                HDONE_ERROR(H5E_HEAP, H5E_CANTRELEASE, NULL, \
                            "unable to destroy local heap")

        } /* end else */
    } /* end if */


    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HL_cache_prefix_deserialize() */


/*-------------------------------------------------------------------------
 * Function:    H5HL_cache_prefix_image_len
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
 *              6/21/14
 *
 *-------------------------------------------------------------------------
 */

static herr_t
H5HL_cache_prefix_image_len(const void *thing, size_t *image_len_ptr)
{
    size_t image_len;
    const H5HL_prfx_t *prfx = NULL; /* Pointer to local heap prefix to query */
    herr_t             ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);
    HDassert(image_len_ptr);

    prfx = (const H5HL_prfx_t *)thing;

    HDassert(prfx);
    HDassert(prfx->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(prfx->cache_info.type) == \
             &(H5AC_LHEAP_PRFX[0]));

    image_len = prfx->heap->prfx_size;

    /* If the heap is stored as a single object, add in the 
     * data block size also 
     */

    if(prfx->heap->single_cache_obj)

        image_len += prfx->heap->dblk_size;

    *image_len_ptr = image_len;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HL_cache_prefix_image_len() */

/****************************************/
/* no H5HL_cache_prefix_pre_serialize() */
/****************************************/


/*-------------------------------------------------------------------------
 * Function:    H5HL_cache_prefix_serialize
 *
 * Purpose: Given a pointer to an instance of H5HL_prfx_t and an 
 *	appropriately sized buffer, serialize the contents of the 
 *	instance for writing to disk, and copy the serialized data 
 *	into the buffer.
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
 *              7/21/14
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_cache_prefix_serialize(const H5F_t *f,
                            void *image_ptr,
                            size_t len,
                            void *thing)
{
    H5HL_prfx_t *prfx = NULL; /* Pointer to local heap prefix to query */
    H5HL_t      *heap = NULL; /* Pointer to the local heap */
    uint8_t     *p;           /* Pointer into image buffer */
    size_t       buf_size;    /* expected size of the image buffer */
    herr_t       ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(f);
    HDassert(image_ptr);
    HDassert(thing);

    prfx = (H5HL_prfx_t *)thing;

    HDassert(prfx);
    HDassert(prfx->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(prfx->cache_info.type) == \
             &(H5AC_LHEAP_PRFX[0]));
    HDassert(H5F_addr_eq(prfx->cache_info.addr, prfx->heap->prfx_addr));
    HDassert(prfx->heap);

    /* Get the pointer to the heap */
    heap = prfx->heap;

    HDassert(heap);

    buf_size = heap->prfx_size;

    if(heap->single_cache_obj)

        buf_size += heap->dblk_size;

    HDassert(len == buf_size);

    /* initialize pointer into the image */
    p = (uint8_t *)image_ptr;

    /* Update the free block value from the free list */
    heap->free_block = heap->freelist ? heap->freelist->offset : H5HL_FREE_NULL;

    /* Serialize the heap prefix */
    HDmemcpy(p, H5HL_MAGIC, (size_t)H5_SIZEOF_MAGIC);
    p += H5_SIZEOF_MAGIC;
    *p++ = H5HL_VERSION;
    *p++ = 0;       /*reserved*/
    *p++ = 0;       /*reserved*/
    *p++ = 0;       /*reserved*/
    H5F_ENCODE_LENGTH_LEN(p, heap->dblk_size, heap->sizeof_size);
    H5F_ENCODE_LENGTH_LEN(p, heap->free_block, heap->sizeof_size);
    H5F_addr_encode_len(heap->sizeof_addr, &p, heap->dblk_addr);

    /* Check if the local heap is a single object in cache */
    if(heap->single_cache_obj) {

        if((size_t)(p - (uint8_t *)image_ptr) < heap->prfx_size) {

            size_t gap;         /* Size of gap between prefix and data block */

            /* Set p to the start of the data block.  This is necessary 
             * because there may be a gap between the used portion of 
             * the prefix and the data block due to alignment constraints. 
             */
            gap = heap->prfx_size - (size_t)(p - (uint8_t *)image_ptr);
            HDmemset(p, 0, gap);
            p += gap;
        } /* end if */

        /* Serialize the free list into the heap data's image */
        H5HL_fl_serialize(heap);

        /* Copy the heap data block into the cache image */
        HDmemcpy(p, heap->dblk_image, heap->dblk_size);
    } /* end if */

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HL_cache_prefix_serialize() */


/******************************************/
/* no H5HL_cache_prefix_notify() function */
/******************************************/


/*-------------------------------------------------------------------------
 * Function:    H5HL_cache_prefix_free_icr
 *
 * Purpose: Free the supplied in core representation of a local heap 
 *	prefix.
 *
 *	Note that this function handles the partially initialize prefix 
 *	from a failed speculative load attempt.  See comments below for 
 *	details.
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
 *              6/21/14
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_cache_prefix_free_icr(void *thing)
{
#if 0 /* doesn't look like we need this -- JRM */
    hbool_t      spec_load_failed;
#endif /* JRM */
    H5HL_t      *heap = NULL; 
    H5HL_prfx_t *prfx = NULL; /* Pointer to local heap prefix to query */
    herr_t       ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);

    prfx = (H5HL_prfx_t *)thing;

    HDassert(prfx);

    /* the metadata cache sets cache_info.magic to
     * H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC before calling the
     * free_icr routine.  Hence the following assert:
     */

    HDassert(prfx->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC);
    HDassert((const H5AC_class_t *)(prfx->cache_info.type) == \
             &(H5AC_LHEAP_PRFX[0]));
    HDassert(H5F_addr_eq(prfx->cache_info.addr, prfx->heap->prfx_addr));
    HDassert(prfx->heap);

#if 0 /* doesn't look like we need this -- JRM */
    /* make note of the spec_load_failed and heap fields of the prefix */
    spec_load_failed = prfx->spec_load_failed;
    prfx->spec_load_failed = FALSE;
    heap = prfx->heap;
#endif /* JRM */

    /* Destroy local heap prefix */
    if(H5HL_prfx_dest(prfx) < 0)

        HGOTO_ERROR(H5E_HEAP, H5E_CANTRELEASE, FAIL, \
                    "can't destroy local heap prefix")

#if 0 /* doesn't look like we need this -- JRM */
    /* if spec_load_failed is TRUE, the instance of H5HL_prfx_t we were 
     * passed is a partially allocated instance that was allocated as 
     * part of a failed attempt to deserialize a buffer that was too small.
     *
     * In this case, we must also the instance of H5HL_t pointed to by 
     * its heap field.
     */
    if ( spec_load_failed )

	if ( heap && ( H5HL_dest(heap) < 0 ) )

	    HGOTO_ERROR(H5E_HEAP, H5E_CANTRELEASE, FAIL, \
                        "unable to destroy local heap")
#endif /* JRM */

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HL_cache_prefix_free_icr() */


/*-------------------------------------------------------------------------
 * Function:    H5HL_cache_datablock_get_load_size()
 *
 * Purpose: Tell the metadata cache how large a buffer to read from 
 *	file when loading a datablock.  In this case, we simply lookup
 *	the correct value in the user data, and return it in *image_len_ptr.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              6/21/14
 *
 *-------------------------------------------------------------------------
 */

static herr_t
H5HL_cache_datablock_get_load_size(const void *udata_ptr, size_t *image_len_ptr)
{
    H5HL_cache_dblk_ud_t *udata;                /* User data for callback */
    herr_t                ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(udata_ptr);
    HDassert(image_len_ptr);

    udata = (H5HL_cache_dblk_ud_t *)udata_ptr;

    HDassert(udata);
    HDassert(udata->heap);
    HDassert(udata->heap->dblk_size > 0);

    *image_len_ptr = udata->heap->dblk_size;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HL_cache_datablock_get_load_size() */



/*-------------------------------------------------------------------------
 * Function:    H5HL_cache_datablock_deserialize
 *
 * Purpose:
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        Pointer to in core representation
 *              Failure:        NULL
 *
 * Programmer:  John Mainzer
 *              6/21/14
 *
 *-------------------------------------------------------------------------
 */
static void *
H5HL_cache_datablock_deserialize(const void *image_ptr,
                              size_t len,
                              void *udata_ptr,
                              hbool_t *dirty_ptr)
{
    H5HL_dblk_t          *dblk = NULL;  /* Local heap data block deserialized */
    H5HL_cache_dblk_ud_t *udata = NULL; /* User data for callback */
    void *                ret_value;    /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    HDassert(image_ptr);
    HDassert(len > 0 );
    HDassert(udata_ptr);
    HDassert(dirty_ptr);

    udata = (H5HL_cache_dblk_ud_t *)udata_ptr;

    HDassert(udata);
    HDassert(udata->heap);
    HDassert(udata->heap->dblk_size == len);
    HDassert(!udata->heap->single_cache_obj);
    HDassert(NULL == udata->heap->dblk);

    /* Allocate space in memory for the heap data block */
    if(NULL == (dblk = H5HL_dblk_new(udata->heap)))

        HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, NULL, "memory allocation failed")

    /* Check for heap still retaining image */
    if(NULL == udata->heap->dblk_image) {

        /* Allocate space for the heap data image */
        if(NULL == (udata->heap->dblk_image = H5FL_BLK_MALLOC(lheap_chunk, 
                                                       udata->heap->dblk_size)))

            HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, NULL, \
                        "can't allocate data block image buffer")

        /* copy the datablock from the read buffer */
        HDmemcpy(udata->heap->dblk_image, image_ptr, len);

        /* Build free list */
        if(H5HL_fl_deserialize(udata->heap) < 0)

            HGOTO_ERROR(H5E_HEAP, H5E_CANTINIT, NULL, \
                        "can't initialize free list")
    } /* end if */

    /* Set flag to indicate data block from loaded from file */
    udata->loaded = TRUE;

    /* Set return value */
    ret_value = dblk;

done:

    /* Release the [possibly partially initialized] local heap on errors */
    if(!ret_value && dblk)

        if(H5HL_dblk_dest(dblk) < 0)

            HDONE_ERROR(H5E_HEAP, H5E_CANTRELEASE, NULL, \
                        "unable to destroy local heap data block")

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HL_cache_datablock_deserialize() */


/*-------------------------------------------------------------------------
 * Function:    H5HL_cache_datablock_image_len
 *
 * Purpose: Return the size of the on disk image of the datablock.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              6/21/14
 *
 *-------------------------------------------------------------------------
 */

static herr_t
H5HL_cache_datablock_image_len(const void *thing, size_t *image_len_ptr)
{
    const H5HL_dblk_t *dblk = NULL;  /* Pointer to the local heap data block */
    herr_t             ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);
    HDassert(image_len_ptr);

    dblk = (const H5HL_dblk_t *)thing;

    HDassert(dblk);
    HDassert(dblk->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(dblk->cache_info.type) == \
             &(H5AC_LHEAP_DBLK[0]));
    HDassert(dblk->heap);
    HDassert(dblk->heap->dblk_size > 0);

    *image_len_ptr = dblk->heap->dblk_size;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HL_cache_datablock_image_len() */

/*******************************************/
/* no H5HL_cache_datablock_pre_serialize() */
/*******************************************/


/*-------------------------------------------------------------------------
 * Function:    H5HL_cache_datablock_serialize
 *
 * Purpose: Serialize the supplied datablock, and copy the serialized
 *	image into the supplied image buffer.
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
 *              6/21/14
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_cache_datablock_serialize(const H5F_t *f,
                               void *image_ptr,
                               size_t len,
                               void *thing)
{
    H5HL_t      *heap = NULL;        /* Pointer to the local heap */
    H5HL_dblk_t *dblk = NULL;        /* Pointer to the local heap data block */
    herr_t       ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(f);
    HDassert(image_ptr);
    HDassert(thing);

    dblk = (H5HL_dblk_t *)thing;

    HDassert(dblk);
    HDassert(dblk->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(dblk->cache_info.type) == \
             &(H5AC_LHEAP_DBLK[0]));
    HDassert(dblk->heap);

    heap = dblk->heap;

    HDassert(heap->dblk_size == len);
    HDassert(!heap->single_cache_obj);


    /* Update the free block value from the free list */
    heap->free_block = heap->freelist ? heap->freelist->offset : H5HL_FREE_NULL;

    /* Serialize the free list into the heap data's image */
    H5HL_fl_serialize(heap);

    /* Copy the heap's data block into the cache's image */
    HDmemcpy(image_ptr, heap->dblk_image, heap->dblk_size);

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HL_cache_datablock_serialize() */


/*********************************************/
/* no H5HL_cache_datablock_notify() function */
/*********************************************/


/*-------------------------------------------------------------------------
 * Function:    H5HL_cache_datablock_free_icr
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
 *              6/21/14
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_cache_datablock_free_icr(void *thing)
{
    H5HL_dblk_t *dblk = NULL;        /* Pointer to the local heap data block */
    herr_t       ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);

    dblk = (H5HL_dblk_t *)thing;

    /* the metadata cache sets cache_info.magic to
     * H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC before calling the
     * free_icr routine.  Hence the following assert:
     */

    HDassert(dblk->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC);
    HDassert((const H5AC_class_t *)(dblk->cache_info.type) == \
             &(H5AC_LHEAP_DBLK[0]));

    if(H5HL_dblk_dest(dblk) < 0)

        HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, \
                    "unable to destroy local heap data block")

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HL_cache_datablock_free_icr() */


#else /* V2 cache callbacks */

/*-------------------------------------------------------------------------
 * Function:	H5HL_prefix_load
 *
 * Purpose:	Loads a local heap prefix from disk.
 *
 * Return:	Success:	Ptr to a local heap memory data structure.
 *		Failure:	NULL
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Jul 17 1997
 *
 *-------------------------------------------------------------------------
 */
static void *
H5HL_prefix_load(H5F_t *f, hid_t dxpl_id, haddr_t addr, void *_udata)
{
    H5HL_t *heap = NULL;            /* Local heap */
    H5HL_prfx_t *prfx = NULL;       /* Heap prefix deserialized */
    H5HL_cache_prfx_ud_t *udata = (H5HL_cache_prfx_ud_t *)_udata;       /* User data for callback */
    uint8_t		buf[H5HL_SPEC_READ_SIZE];   /* Buffer for decoding */
    size_t	        spec_read_size; /* Size of buffer to speculatively read in */
    const uint8_t	*p;         /* Pointer into decoding buffer */
    haddr_t             eoa;        /* Relative end of file address */
    H5HL_prfx_t *ret_value;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* check arguments */
    HDassert(f);
    HDassert(H5F_addr_defined(addr));
    HDassert(udata);
    HDassert(udata->sizeof_size > 0);
    HDassert(udata->sizeof_addr > 0);
    HDassert(udata->sizeof_prfx > 0);
    HDassert(udata->sizeof_prfx <= sizeof(buf));

    /* Make certain we don't speculatively read off the end of the file */
    if(HADDR_UNDEF == (eoa = H5F_get_eoa(f, H5FD_MEM_LHEAP)))
        HGOTO_ERROR(H5E_HEAP, H5E_CANTGET, NULL, "unable to determine file size")

    /* Compute the size of the speculative local heap prefix buffer */
    H5_ASSIGN_OVERFLOW(spec_read_size, MIN(eoa - addr, H5HL_SPEC_READ_SIZE), /* From: */ hsize_t, /* To: */ size_t);
    HDassert(spec_read_size >= udata->sizeof_prfx);

    /* Attempt to speculatively read both local heap prefix and heap data */
    if(H5F_block_read(f, H5FD_MEM_LHEAP, addr, spec_read_size, dxpl_id, buf) < 0)
	HGOTO_ERROR(H5E_HEAP, H5E_READERROR, NULL, "unable to read local heap prefix")
    p = buf;

    /* Check magic number */
    if(HDmemcmp(p, H5HL_MAGIC, (size_t)H5_SIZEOF_MAGIC))
	HGOTO_ERROR(H5E_HEAP, H5E_BADVALUE, NULL, "bad local heap signature")
    p += H5_SIZEOF_MAGIC;

    /* Version */
    if(H5HL_VERSION != *p++)
	HGOTO_ERROR(H5E_HEAP, H5E_BADVALUE, NULL, "wrong version number in local heap")

    /* Reserved */
    p += 3;

    /* Allocate space in memory for the heap */
    if(NULL == (heap = H5HL_new(udata->sizeof_size, udata->sizeof_addr, udata->sizeof_prfx)))
	HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, NULL, "can't allocate local heap structure")

    /* Allocate the heap prefix */
    if(NULL == (prfx = H5HL_prfx_new(heap)))
	HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, NULL, "can't allocate local heap prefix")

    /* Store the prefix's address & length */
    heap->prfx_addr = udata->prfx_addr;
    heap->prfx_size = udata->sizeof_prfx;

    /* Heap data size */
    H5F_DECODE_LENGTH_LEN(p, heap->dblk_size, udata->sizeof_size);

    /* Free list head */
    H5F_DECODE_LENGTH_LEN(p, heap->free_block, udata->sizeof_size);
    if(heap->free_block != H5HL_FREE_NULL && heap->free_block >= heap->dblk_size)
	HGOTO_ERROR(H5E_HEAP, H5E_BADVALUE, NULL, "bad heap free list")

    /* Heap data address */
    H5F_addr_decode_len(udata->sizeof_addr, &p, &(heap->dblk_addr));

    /* Check if heap block exists */
    if(heap->dblk_size) {
        /* Check if heap data block is contiguous with header */
        if(H5F_addr_eq((heap->prfx_addr + heap->prfx_size), heap->dblk_addr)) {
            /* Note that the heap should be a single object in the cache */
            heap->single_cache_obj = TRUE;

            /* Allocate space for the heap data image */
            if(NULL == (heap->dblk_image = H5FL_BLK_MALLOC(lheap_chunk, heap->dblk_size)))
                HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, NULL, "memory allocation failed")

            /* Check if the current buffer from the speculative read already has the heap data */
            if(spec_read_size >= (heap->prfx_size + heap->dblk_size)) {
                /* Set p to the start of the data block.  This is necessary
                 * because there may be a gap between the used portion of the
                 * prefix and the data block due to alignment constraints. */
                p = buf + heap->prfx_size;

                /* Copy the heap data from the speculative read buffer */
                HDmemcpy(heap->dblk_image, p, heap->dblk_size);
            } /* end if */
            else {
                /* Read the local heap data block directly into buffer */
                if(H5F_block_read(f, H5FD_MEM_LHEAP, heap->dblk_addr, heap->dblk_size, dxpl_id, heap->dblk_image) < 0)
                    HGOTO_ERROR(H5E_HEAP, H5E_READERROR, NULL, "unable to read heap data")
            } /* end else */

            /* Build free list */
            if(H5HL_fl_deserialize(heap) < 0)
                HGOTO_ERROR(H5E_HEAP, H5E_CANTINIT, NULL, "can't initialize free list")
        } /* end if */
        else
            /* Note that the heap should _NOT_ be a single object in the cache */
            heap->single_cache_obj = FALSE;
    } /* end if */

    /* Set return value */
    ret_value = prfx;

done:
    /* Release the [possibly partially initialized] local heap on errors */
    if(!ret_value) {
        if(prfx) {
            if(H5HL_prfx_dest(prfx) < 0)
                HDONE_ERROR(H5E_HEAP, H5E_CANTRELEASE, NULL, "unable to destroy local heap prefix")
        } /* end if */
        else {
            if(heap && H5HL_dest(heap) < 0)
                HDONE_ERROR(H5E_HEAP, H5E_CANTRELEASE, NULL, "unable to destroy local heap")
        } /* end else */
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HL_prefix_load() */


/*-------------------------------------------------------------------------
 * Function:	H5HL_prefix_flush
 *
 * Purpose:	Flushes a heap from memory to disk if it's dirty.  Optionally
 *		deletes the heap from memory.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Jul 17 1997
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_prefix_flush(H5F_t *f, hid_t dxpl_id, hbool_t destroy, haddr_t addr,
    void *thing, unsigned UNUSED *flags_ptr)
{
    H5HL_prfx_t *prfx = (H5HL_prfx_t *)thing;   /* Local heap prefix to flush */
    H5WB_t *wb = NULL;                  /* Wrapped buffer for heap data */
    uint8_t heap_buf[H5HL_SPEC_READ_SIZE]; /* Buffer for heap */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* check arguments */
    HDassert(f);
    HDassert(H5F_addr_defined(addr));
    HDassert(prfx);

    if(prfx->cache_info.is_dirty) {
        H5HL_t *heap = prfx->heap; /* Pointer to the local heap */
        uint8_t *buf;           /* Pointer to heap buffer */
        size_t buf_size;        /* Size of buffer for encoding & writing heap info */
        uint8_t *p;             /* Pointer into raw data buffer */

        /* Wrap the local buffer for serialized heap info */
        if(NULL == (wb = H5WB_wrap(heap_buf, sizeof(heap_buf))))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTINIT, FAIL, "can't wrap buffer")

        /* Compute the size of the buffer to encode & write */
        buf_size = heap->prfx_size;
        if(heap->single_cache_obj)
            buf_size += heap->dblk_size;

        /* Get a pointer to a buffer that's large enough for serialized heap */
        if(NULL == (buf = (uint8_t *)H5WB_actual(wb, buf_size)))
            HGOTO_ERROR(H5E_HEAP, H5E_NOSPACE, FAIL, "can't get actual buffer")

        /* Update the free block value from the free list */
        heap->free_block = heap->freelist ? heap->freelist->offset : H5HL_FREE_NULL;

        /* Serialize the heap prefix */
        p = buf;
        HDmemcpy(p, H5HL_MAGIC, (size_t)H5_SIZEOF_MAGIC);
        p += H5_SIZEOF_MAGIC;
        *p++ = H5HL_VERSION;
        *p++ = 0;	/*reserved*/
        *p++ = 0;	/*reserved*/
        *p++ = 0;	/*reserved*/
        H5F_ENCODE_LENGTH_LEN(p, heap->dblk_size, heap->sizeof_size);
        H5F_ENCODE_LENGTH_LEN(p, heap->free_block, heap->sizeof_size);
        H5F_addr_encode_len(heap->sizeof_addr, &p, heap->dblk_addr);

        /* Check if the local heap is a single object in cache */
        if(heap->single_cache_obj) {
            if((size_t)(p - buf) < heap->prfx_size) {
                size_t gap;         /* Size of gap between prefix and data block */

                /* Set p to the start of the data block.  This is necessary because
                 * there may be a gap between the used portion of the prefix and the
                 * data block due to alignment constraints. */
                gap = heap->prfx_size - (size_t)(p - buf);
                HDmemset(p, 0, gap);
                p += gap;
            } /* end if */

            /* Serialize the free list into the heap data's image */
            H5HL_fl_serialize(heap);

            /* Copy the heap data block into the cache image */
            HDmemcpy(p, heap->dblk_image, heap->dblk_size);
        } /* end if */

        /* Write the prefix [and possibly the data block] to the file */
        if(H5F_block_write(f, H5FD_MEM_LHEAP, addr, buf_size, dxpl_id, buf) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_WRITEERROR, FAIL, "unable to write heap header and data to file")

	prfx->cache_info.is_dirty = FALSE;
    } /* end if */

    /* Should we destroy the memory version? */
    if(destroy)
        if(H5HL_prefix_dest(f, prfx) < 0)
	    HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "unable to destroy local heap prefix")

done:
    /* Release resources */
    if(wb && H5WB_unwrap(wb) < 0)
        HDONE_ERROR(H5E_HEAP, H5E_CLOSEERROR, FAIL, "can't close wrapped buffer")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HL_prefix_flush() */


/*-------------------------------------------------------------------------
 * Function:	H5HL_prefix_dest
 *
 * Purpose:	Destroys a heap prefix in memory.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Jan 15 2003
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_prefix_dest(H5F_t *f, void *thing)
{
    H5HL_prfx_t *prfx = (H5HL_prfx_t *)thing;   /* Local heap prefix to destroy */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* check arguments */
    HDassert(prfx);
    HDassert(prfx->heap);
    HDassert(H5F_addr_eq(prfx->cache_info.addr, prfx->heap->prfx_addr));

    /* Verify that entry is clean */
    HDassert(prfx->cache_info.is_dirty == FALSE);

    /* If we're going to free the space on disk, the address must be valid */
    HDassert(!prfx->cache_info.free_file_space_on_destroy || H5F_addr_defined(prfx->cache_info.addr));

    /* Check for freeing file space for local heap prefix */
    if(prfx->cache_info.free_file_space_on_destroy) {
        hsize_t free_size;       /* Size of region to free in file */

        /* Compute size to free for later */
        free_size = prfx->heap->prfx_size;
        if(prfx->heap->single_cache_obj)
            free_size += prfx->heap->dblk_size;

        /* Free the local heap prefix [and possible the data block] on disk */
        /* (XXX: Nasty usage of internal DXPL value! -QAK) */
        if(H5MF_xfree(f, H5FD_MEM_LHEAP, H5AC_dxpl_id, prfx->cache_info.addr, free_size) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "unable to free local heap prefix")
    } /* end if */

    /* Destroy local heap prefix */
    if(H5HL_prfx_dest(prfx) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTRELEASE, FAIL, "can't destroy local heap prefix")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HL_prefix_dest() */


/*-------------------------------------------------------------------------
 * Function:	H5HL_prefix_clear
 *
 * Purpose:	Mark a local heap prefix in memory as non-dirty.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Mar 20 2003
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_prefix_clear(H5F_t UNUSED *f, void *thing, hbool_t destroy)
{
    H5HL_prfx_t *prfx = (H5HL_prfx_t *)thing;   /* The local heap prefix to operate on */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* check arguments */
    HDassert(prfx);

    /* Mark heap prefix as clean */
    prfx->cache_info.is_dirty = FALSE;

    if(destroy)
        if(H5HL_prefix_dest(f, prfx) < 0)
	    HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "unable to destroy local heap prefix")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HL_prefix_clear() */


/*-------------------------------------------------------------------------
 * Function:	H5HL_prefix_size
 *
 * Purpose:	Compute the size in bytes of the heap prefix on disk,
 *              and return it in *len_ptr.  On failure, the value of *len_ptr
 *              is undefined.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	John Mainzer
 *		5/13/04
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_prefix_size(const H5F_t UNUSED *f, const void *thing, size_t *size_ptr)
{
    const H5HL_prfx_t *prfx = (const H5HL_prfx_t *)thing;   /* Pointer to local heap prefix to query */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* check arguments */
    HDassert(prfx);
    HDassert(prfx->heap);
    HDassert(size_ptr);

    /* Calculate size of prefix in cache */
    *size_ptr = prfx->heap->prfx_size;

    /* If the heap is stored as a single object, add in the data block size also */
    if(prfx->heap->single_cache_obj)
        *size_ptr += prfx->heap->dblk_size;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* H5HL_prefix_size() */


/*-------------------------------------------------------------------------
 * Function:	H5HL_datablock_load
 *
 * Purpose:	Loads a local heap data block from disk.
 *
 * Return:	Success:	Ptr to a local heap data block memory data structure.
 *		Failure:	NULL
 *
 * Programmer:	Quincey Koziol
 *		koziol@hdfgroup.org
 *		Jan  5 2010
 *
 *-------------------------------------------------------------------------
 */
static void *
H5HL_datablock_load(H5F_t *f, hid_t dxpl_id, haddr_t addr, void *_udata)
{
    H5HL_dblk_t *dblk = NULL;       /* Local heap data block deserialized */
    H5HL_cache_dblk_ud_t *udata = (H5HL_cache_dblk_ud_t *)_udata;       /* User data for callback */
    H5HL_dblk_t *ret_value;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* check arguments */
    HDassert(f);
    HDassert(H5F_addr_defined(addr));
    HDassert(udata);
    HDassert(udata->heap);
    HDassert(!udata->heap->single_cache_obj);
    HDassert(NULL == udata->heap->dblk);

    /* Allocate space in memory for the heap data block */
    if(NULL == (dblk = H5HL_dblk_new(udata->heap)))
	HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, NULL, "memory allocation failed")

    /* Check for heap still retaining image */
    if(NULL == udata->heap->dblk_image) {
        /* Allocate space for the heap data image */
        if(NULL == (udata->heap->dblk_image = H5FL_BLK_MALLOC(lheap_chunk, udata->heap->dblk_size)))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, NULL, "can't allocate data block image buffer")

        /* Read local heap data block */
        if(H5F_block_read(f, H5FD_MEM_LHEAP, udata->heap->dblk_addr, udata->heap->dblk_size, dxpl_id, udata->heap->dblk_image) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_READERROR, NULL, "unable to read local heap data block")

        /* Build free list */
        if(H5HL_fl_deserialize(udata->heap) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTINIT, NULL, "can't initialize free list")
    } /* end if */

    /* Set flag to indicate data block from loaded from file */
    udata->loaded = TRUE;

    /* Set return value */
    ret_value = dblk;

done:
    /* Release the [possibly partially initialized] local heap on errors */
    if(!ret_value && dblk)
        if(H5HL_dblk_dest(dblk) < 0)
	    HDONE_ERROR(H5E_HEAP, H5E_CANTRELEASE, NULL, "unable to destroy local heap data block")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HL_datablock_load() */


/*-------------------------------------------------------------------------
 * Function:	H5HL_datablock_flush
 *
 * Purpose:	Flushes a heap's data block from memory to disk if it's dirty.
 *              Optionally deletes the heap data block from memory.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Jul 17 1997
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_datablock_flush(H5F_t *f, hid_t dxpl_id, hbool_t destroy, haddr_t addr,
    void *_thing, unsigned UNUSED * flags_ptr)
{
    H5HL_dblk_t *dblk = (H5HL_dblk_t *)_thing; /* Pointer to the local heap data block */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* check arguments */
    HDassert(f);
    HDassert(H5F_addr_defined(addr));
    HDassert(dblk);
    HDassert(dblk->heap);
    HDassert(!dblk->heap->single_cache_obj);

    if(dblk->cache_info.is_dirty) {
        H5HL_t *heap = dblk->heap;      /* Pointer to the local heap */

        /* Update the free block value from the free list */
        heap->free_block = heap->freelist ? heap->freelist->offset : H5HL_FREE_NULL;

        /* Serialize the free list into the heap data's image */
        H5HL_fl_serialize(heap);

        /* Write the data block to the file */
        if(H5F_block_write(f, H5FD_MEM_LHEAP, heap->dblk_addr, heap->dblk_size, dxpl_id, heap->dblk_image) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_WRITEERROR, FAIL, "unable to write heap data block to file")

	dblk->cache_info.is_dirty = FALSE;
    } /* end if */

    /* Should we destroy the memory version? */
    if(destroy)
        if(H5HL_datablock_dest(f, dblk) < 0)
	    HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "unable to destroy local heap data block")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HL_datablock_flush() */


/*-------------------------------------------------------------------------
 * Function:	H5HL_datablock_dest
 *
 * Purpose:	Destroys a local heap data block in memory.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Jan 15 2003
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_datablock_dest(H5F_t *f, void *_thing)
{
    H5HL_dblk_t *dblk = (H5HL_dblk_t *)_thing; /* Pointer to the local heap data block */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* check arguments */
    HDassert(dblk);
    HDassert(dblk->heap);
    HDassert(!dblk->heap->single_cache_obj);
    HDassert(H5F_addr_eq(dblk->cache_info.addr, dblk->heap->dblk_addr));

    /* Verify that entry is clean */
    HDassert(dblk->cache_info.is_dirty == FALSE);

    /* If we're going to free the space on disk, the address must be valid */
    HDassert(!dblk->cache_info.free_file_space_on_destroy || H5F_addr_defined(dblk->cache_info.addr));

    /* Check for freeing file space for local heap data block */
    if(dblk->cache_info.free_file_space_on_destroy) {
        /* Free the local heap data block on disk */
        /* (XXX: Nasty usage of internal DXPL value! -QAK) */
        if(H5MF_xfree(f, H5FD_MEM_LHEAP, H5AC_dxpl_id, dblk->cache_info.addr, (hsize_t)dblk->heap->dblk_size) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "unable to free local heap data block")
    } /* end if */

    /* Destroy local heap data block */
    if(H5HL_dblk_dest(dblk) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTRELEASE, FAIL, "can't destroy local heap data block")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HL_datablock_dest() */


/*-------------------------------------------------------------------------
 * Function:	H5HL_datablock_clear
 *
 * Purpose:	Mark a local heap data block in memory as non-dirty.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Mar 20 2003
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_datablock_clear(H5F_t *f, void *_thing, hbool_t destroy)
{
    H5HL_dblk_t *dblk = (H5HL_dblk_t *)_thing; /* Pointer to the local heap data block */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* check arguments */
    HDassert(dblk);

    /* Mark local heap data block as clean */
    dblk->cache_info.is_dirty = FALSE;

    if(destroy)
        if(H5HL_datablock_dest(f, dblk) < 0)
	    HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, "unable to destroy local heap data block")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HL_datablock_clear() */


/*-------------------------------------------------------------------------
 * Function:	H5HL_datablock_size
 *
 * Purpose:	Compute the size in bytes of the local heap data block on disk,
 *              and return it in *len_ptr.  On failure, the value of *len_ptr
 *              is undefined.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	John Mainzer
 *		5/13/04
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HL_datablock_size(const H5F_t UNUSED *f, const void *_thing, size_t *size_ptr)
{
    const H5HL_dblk_t *dblk = (const H5HL_dblk_t *)_thing; /* Pointer to the local heap data block */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* check arguments */
    HDassert(dblk);
    HDassert(dblk->heap);
    HDassert(size_ptr);

    /* Set size of data block in cache */
    *size_ptr = dblk->heap->dblk_size;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* H5HL_datablock_size() */

#endif /* V2 metadata cache callbacks */
