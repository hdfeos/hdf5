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
 * Created:		H5HGcache.c
 *			Feb  5 2008
 *			Quincey Koziol <koziol@hdfgroup.org>
 *
 * Purpose:		Implement global heap metadata cache methods.
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#define H5HG_PACKAGE		/*suppress error about including H5HGpkg  */


/***********/
/* Headers */
/***********/
#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5Fprivate.h"		/* File access				*/
#include "H5HGpkg.h"		/* Global heaps				*/
#include "H5MFprivate.h"	/* File memory management		*/
#include "H5MMprivate.h"	/* Memory management			*/


/****************/
/* Local Macros */
/****************/


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

static herr_t H5HG_cache_heap_get_load_size(const void *udata_ptr,
                                            size_t *image_len_ptr);

static void *H5HG_cache_heap_deserialize(const void *image_ptr,
                                         size_t len,
                                         void *udata_ptr,
                                         hbool_t *dirty_ptr);

static herr_t H5HG_cache_heap_image_len(const void *thing,
                                        size_t *image_len_ptr);

static herr_t H5HG_cache_heap_serialize(const H5F_t *f,
                                        void *image_ptr,
                                        size_t len,
                                        void *thing);

static herr_t H5HG_cache_heap_free_icr(void *thing, const H5F_t *f,
                                       hid_t dxpl_id);



/*********************/
/* Package Variables */
/*********************/

/* H5HG inherits cache-like properties from H5AC */

const H5AC_class_t H5AC_GHEAP[1] = {{
  /* id            */ H5AC_GHEAP_ID,
  /* name          */ "global heap",
  /* mem_type      */ H5FD_MEM_GHEAP,
  /* flags         */ H5AC__CLASS_SPECULATIVE_LOAD_FLAG,
  /* get_load_size */ (H5AC_get_load_size_func_t)H5HG_cache_heap_get_load_size,
  /* deserialize   */ (H5AC_deserialize_func_t)H5HG_cache_heap_deserialize,
  /* image_len     */ (H5AC_image_len_func_t)H5HG_cache_heap_image_len,
  /* pre_serialize */ (H5AC_pre_serialize_func_t)NULL,
  /* serialize     */ (H5AC_serialize_func_t)H5HG_cache_heap_serialize,
  /* notify        */ (H5AC_notify_func_t)NULL,
  /* free_icr      */ (H5AC_free_icr_func_t)H5HG_cache_heap_free_icr,
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
 * Function:    H5HL_cache_heap_get_load_size()
 *
 * Purpose: Return the initial speculative read size to the metadata 
 *	cache.  This size will be used in the initial attempt to read 
 *	the global heap.  If this read is too small, the cache will 
 *	try again with the correct value obtained from 
 *	H5HG_cache_heap_image_len().
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              7/27/14
 *
 *-------------------------------------------------------------------------
 */

static herr_t
H5HG_cache_heap_get_load_size(const void *udata_ptr, size_t *image_len_ptr)
{
    herr_t              ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(image_len_ptr);

    *image_len_ptr = (size_t)H5HG_MINSIZE;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HG_cache_heap_get_load_size() */


/*-------------------------------------------------------------------------
 * Function:    H5HG_cache_heap_deserialize
 *
 * Purpose:  Given a buffer containing the on disk image of the global 
 *	heap, deserialize it, load its contents into a newly allocated
 *	instance of H5HG_heap_t, and return a pointer to the new instance.
 *
 *	Note that this heap client uses speculative reads.  If the supplied
 *	buffer is too small, we simply make note of the correct size, and 
 *	wait for the metadata cache to try again.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        Pointer to in core representation
 *              Failure:        NULL
 *
 * Programmer:  John Mainzer
 *              7/27/14
 *
 *-------------------------------------------------------------------------
 */
static void *
H5HG_cache_heap_deserialize(const void *image_ptr,
                            size_t len,
                            void *udata_ptr,
                            hbool_t *dirty_ptr)
{
    H5F_t       *f; /* file pointer -- obtained from user data */
    H5HG_heap_t *heap = NULL;
    uint8_t     *p;
    size_t       nalloc;
    size_t       need;
    size_t       max_idx = 0;
    void *       ret_value;      /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    HDassert(image_ptr);
    HDassert(len >= (size_t)H5HG_MINSIZE);
    HDassert(udata_ptr);
    HDassert(dirty_ptr);

    f = (H5F_t *)udata_ptr; /* sadly, no way to validate this */

    if(NULL == (heap = H5FL_CALLOC(H5HG_heap_t)))

        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed")

    heap->shared = H5F_SHARED(f);

    if(NULL == (heap->chunk = H5FL_BLK_MALLOC(gheap_chunk, len)))

        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed")


    /* copy the image buffer into the newly allocate chunk */
    HDmemcpy(heap->chunk, image_ptr, len);

    p = heap->chunk;

    /* Magic number */
    if(HDmemcmp(p, H5HG_MAGIC, (size_t)H5_SIZEOF_MAGIC))

        HGOTO_ERROR(H5E_HEAP, H5E_CANTLOAD, NULL, \
                    "bad global heap collection signature")

    p += H5_SIZEOF_MAGIC;

    /* Version */
    if(H5HG_VERSION != *p++)

        HGOTO_ERROR(H5E_HEAP, H5E_CANTLOAD, NULL, \
                    "wrong version number in global heap")

    /* Reserved */
    p += 3;

    /* Size */
    H5F_DECODE_LENGTH(f, p, heap->size);
    HDassert(heap->size >= H5HG_MINSIZE);
    HDassert((len == H5HG_MINSIZE) /* first try */ || \
             ((len == heap->size) && (len > H5HG_MINSIZE))); /* second try */
    
    if ( len == heap->size ) { /* proceed with the deserialize */

        /* Decode each object */
        p = heap->chunk + H5HG_SIZEOF_HDR(f);
        nalloc = H5HG_NOBJS(f, heap->size);

        /* Calloc the obj array because the file format spec makes no guarantee
         * about the order of the objects, and unused slots must be set to zero.
         */
        if(NULL == (heap->obj = H5FL_SEQ_CALLOC(H5HG_obj_t, nalloc)))

            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, \
                        "memory allocation failed")

        heap->nalloc = nalloc;

        while(p < (heap->chunk + heap->size)) {

            if((p + H5HG_SIZEOF_OBJHDR(f)) > (heap->chunk + heap->size)) {

                /*
                 * The last bit of space is too tiny for an object header, so
                 * we assume that it's free space.
                 */

                HDassert(NULL == heap->obj[0].begin);
                heap->obj[0].size = 
		    (size_t)(((const uint8_t *)heap->chunk + heap->size) - p);
                heap->obj[0].begin = p;
                p += heap->obj[0].size;

            } /* end if */
            else {

                unsigned idx;
                uint8_t *begin = p;

                UINT16DECODE(p, idx);

                /* Check if we need more room to store heap objects */
                if(idx >= heap->nalloc) {

                    size_t new_alloc;       /* New allocation number */
                    H5HG_obj_t *new_obj;    /* New array of object   */
                                            /* descriptions          */

                    /* Determine the new number of objects to index */
                    new_alloc = MAX(heap->nalloc * 2, (idx + 1));
                    HDassert(idx < new_alloc);

                    /* Reallocate array of objects */
                    if(NULL == (new_obj = H5FL_SEQ_REALLOC(H5HG_obj_t, 
                                                        heap->obj, new_alloc)))

                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, \
                                    "memory allocation failed")

                    /* Clear newly allocated space */
                    HDmemset(&new_obj[heap->nalloc], 0, 
                             (new_alloc - heap->nalloc) * sizeof(heap->obj[0]));

                    /* Update heap information */
                    heap->nalloc = new_alloc;
                    heap->obj = new_obj;
                    HDassert(heap->nalloc > heap->nused);

                } /* end if */

                UINT16DECODE(p, heap->obj[idx].nrefs);
                p += 4; /*reserved*/
                H5F_DECODE_LENGTH(f, p, heap->obj[idx].size);
                heap->obj[idx].begin = begin;

                /*
                 * The total storage size includes the size of the object 
                 * header and is zero padded so the next object header is 
                 * properly aligned. The entire obj array was calloc'ed, 
                 * so no need to zero the space here. The last bit of space 
                 * is the free space object whose size is never padded and 
                 * already includes the object header.
                 */
                if(idx > 0) {

                    need = H5HG_SIZEOF_OBJHDR(f) + 
                           H5HG_ALIGN(heap->obj[idx].size);

                    if(idx > max_idx)

                        max_idx = idx;

                } /* end if */
                else

                    need = heap->obj[idx].size;

                p = begin + need;
            } /* end else */
        } /* end while */

        HDassert(p == heap->chunk + heap->size);
        HDassert(H5HG_ISALIGNED(heap->obj[0].size));

        /* Set the next index value to use */
        if(max_idx > 0)

            heap->nused = max_idx + 1;

        else

            heap->nused = 1;

        HDassert(max_idx < heap->nused);

        /* Add the new heap to the CWFS list for the file */
        if(H5F_cwfs_add(f, heap) < 0)

            HGOTO_ERROR(H5E_HEAP, H5E_CANTINIT, NULL, \
                        "unable to add global heap collection to file's CWFS")

    } /* end if ( len == heap->size ) */
    else {

        /* if len is less than heap size, then the initial speculative 
         * read was too small.  In this case we return without reporting
         * failure.  H5C_load_entry() will call H5HG_cache_heap_image_len()
         * to get the actual read size, and then repeat the read with the 
         * correct size, and call this function a second time.
         */

        HDassert(len < heap->size);

    }

    ret_value = heap;

done:

    if(!ret_value && heap)

        if(H5HG_free(heap) < 0)

            HDONE_ERROR(H5E_HEAP, H5E_CANTFREE, NULL, \
                        "unable to destroy global heap collection")

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HG_cache_heap_deserialize() */


/*-------------------------------------------------------------------------
 * Function:    H5HG_cache_heap_image_len
 *
 * Purpose: Return the on disk image size of the global heap to the 
 *	metadata cache via the image_len_ptr.
 *
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 * Programmer:  John Mainzer
 *              7/27/14
 *
 *-------------------------------------------------------------------------
 */

static herr_t
H5HG_cache_heap_image_len(const void *thing, size_t *image_len_ptr)
{
    H5HG_heap_t *heap = NULL;
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);
    HDassert(image_len_ptr);

    heap = (H5HG_heap_t *)thing;

    HDassert(heap);
    HDassert(heap->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(heap->cache_info.type) == &(H5AC_GHEAP[0]));
    HDassert(heap->size >= H5HG_MINSIZE);

    *image_len_ptr = heap->size;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HG_cache_heap_image_len() */

/**************************************/
/* no H5HG_cache_heap_pre_serialize() */
/**************************************/


/*-------------------------------------------------------------------------
 * Function:    H5HG_cache_heap_serialize
 *
 * Purpose: Given an appropriately sized buffer and an instance of 
 *	H5HG_heap_t, serialize the global heap for writing to file,
 *	and copy the serialized verion into the buffer.
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
 *              7/27/14
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HG_cache_heap_serialize(const H5F_t *f,
                          void *image_ptr,
                          size_t len,
                          void *thing)
{
    H5HG_heap_t *heap = NULL;
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(f);
    HDassert(image_ptr);
    HDassert(thing);

    heap = (H5HG_heap_t *)thing;

    HDassert(heap);
    HDassert(heap->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(heap->cache_info.type) == &(H5AC_GHEAP[0]));
    HDassert(heap->size == len);
    HDassert(heap->chunk);

    /* copy the image into the buffer */
    HDmemcpy(image_ptr, heap->chunk, len);

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HG_cache_heap_serialize() */


/****************************************/
/* no H5HG_cache_heap_notify() function */
/****************************************/


/*-------------------------------------------------------------------------
 * Function:    H5HG_cache_heap_free_icr
 *
 * Purpose: Free the in memory representation of the supplied global heap.
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
 *              7/27/14
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5HG_cache_heap_free_icr(void *thing, const H5F_t UNUSED *f, hid_t UNUSED dxpl_id)
{
    H5HG_heap_t *heap = NULL;
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);

    heap = (H5HG_heap_t *)thing;

    HDassert(heap);

    /* the metadata cache sets cache_info.magic to
     * H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC before calling the
     * free_icr routine.  Hence the following assert:
     */

    HDassert(heap->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC);
    HDassert((const H5AC_class_t *)(heap->cache_info.type) == &(H5AC_GHEAP[0]));

    /* Destroy global heap collection */
    if(H5HG_free(heap) < 0)

        HGOTO_ERROR(H5E_HEAP, H5E_CANTFREE, FAIL, \
                    "unable to destroy global heap collection")


done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5HG_cache_heap_free_icr() */

