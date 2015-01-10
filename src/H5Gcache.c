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
 * Created:		H5Gcache.c
 *			Feb  5 2008
 *			Quincey Koziol <koziol@hdfgroup.org>
 *
 * Purpose:		Implement group metadata cache methods.
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#define H5G_PACKAGE		/*suppress error about including H5Gpkg   */


/***********/
/* Headers */
/***********/
#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5Gpkg.h"		/* Groups		  		*/
#include "H5MFprivate.h"	/* File memory management		*/
#include "H5WBprivate.h"        /* Wrapped Buffers                      */


/****************/
/* Local Macros */
/****************/

#define H5G_NODE_VERS           1       /* Symbol table node version number   */
#define H5G_NODE_BUF_SIZE       512     /* Size of stack buffer for serialized nodes */


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/

/* Metadata cache (H5AC) callbacks */

static herr_t H5G_cache_node_get_load_size(const void *udata_ptr,
                                           size_t *image_len_ptr);

static void *H5G_cache_node_deserialize(const void *image_ptr,
                                        size_t len,
                                        void *udata_ptr,
                                        hbool_t *dirty_ptr);

static herr_t H5G_cache_node_image_len(const void *thing,
                                       size_t *image_len_ptr);

static herr_t H5G_cache_node_serialize(const H5F_t *f,
                                       void *image_ptr,
                                       size_t len,
                                       void *thing);

static herr_t H5G_cache_node_free_icr(void *thing);


/*********************/
/* Package Variables */
/*********************/


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/

/* Symbol table nodes inherit cache-like properties from H5AC */

const H5AC_class_t H5AC_SNODE[1] = {{
    /* id            */ H5AC_SNODE_ID,
    /* name          */ "group node",
    /* mem_type      */ H5FD_MEM_BTREE,
    /* flags         */ H5AC__CLASS_NO_FLAGS_SET,
    /* get_load_size */ (H5AC_get_load_size_func_t)H5G_cache_node_get_load_size,
    /* deserialize   */ (H5AC_deserialize_func_t)H5G_cache_node_deserialize,
    /* image_len     */ (H5AC_image_len_func_t)H5G_cache_node_image_len,
    /* pre_serialize */ (H5AC_pre_serialize_func_t)NULL,
    /* serialize     */ (H5AC_serialize_func_t)H5G_cache_node_serialize,
    /* notify        */ (H5AC_notify_func_t)NULL,
    /* free_icr      */ (H5AC_free_icr_func_t)H5G_cache_node_free_icr,
    /* clear         */ (H5AC_clear_func_t)NULL,
    /* fsf_size      */ (H5AC_get_fsf_size_t)NULL,
}};


/* Declare extern the free list to manage the H5G_node_t struct */
H5FL_EXTERN(H5G_node_t);

/* Declare extern the free list to manage sequences of H5G_entry_t's */
H5FL_SEQ_EXTERN(H5G_entry_t);


/*-------------------------------------------------------------------------
 * Function:    H5G_cache_node_get_load_size()
 *
 * Purpose:  Determine the size of the on disk image of the node, and 
 *	return this value in *image_len_ptr.
 *
 *	Note that this computation requires access to the file pointer,
 *	which is not provided in the parameter list for this callback.
 *	Finesse this issue by passing in the file pointer twice to the
 *	H5AC_protect() call -- once as the file pointer proper, and 
 *	again as the user data.
 *      
 *
 *      A generic discussion of metadata cache callbacks of this type
 *      may be found in H5Cprivate.h:
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
H5G_cache_node_get_load_size(const void *udata_ptr, size_t *image_len_ptr)
{
    size_t		image_len;
    H5F_t	       *f;
    herr_t              ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(udata_ptr);
    HDassert(image_len_ptr);

    /* we pass the file pointer as the user data -- no way to 
     * verify that we do in fact have a file pointer.
     */
    f = (const H5F_t *)udata_ptr;

    /* compute image len */
    image_len = (size_t)(H5G_NODE_SIZE(f));

    /* report image length */
    *image_len_ptr = image_len;

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5G_cache_node_get_load_size() */


/*-------------------------------------------------------------------------
 * Function:    H5G_cache_node_deserialize
 *
 * Purpose: Given a buffer containing the on disk image of a symbol table
 *	node, allocate an instance of H5G_node_t, load the contence of the
 *	image into it, and return a pointer to the instance.
 *
 *	Note that deserializing the image requires access to the file 
 *	pointer, which is not included in the parameter list for this 
 *	callback.  Finesse this issue by passing in the file pointer 
 *	twice to the H5AC_protect() call -- once as the file pointer 
 *	proper, and again as the user data
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
H5G_cache_node_deserialize(const void *image_ptr,
                           size_t len,
                           void *udata_ptr,
                           hbool_t *dirty_ptr)
{
    H5F_t                  *f;
    H5G_node_t             *sym = NULL;
    const uint8_t          *p;
    void *                  ret_value;      /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    HDassert(image_ptr);
    HDassert(len > 0 );
    HDassert(udata_ptr);
    HDassert(dirty_ptr);

    /* we pass the file pointer as the user data -- no way to
     * verify that we do in fact have a file pointer.
     */
    f = (H5F_t *)udata_ptr;

    /*
     * Initialize variables.
     */

    /* Allocate symbol table data structures */
    if(NULL == (sym = H5FL_CALLOC(H5G_node_t)))

        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed")

    sym->node_size = (size_t)(H5G_NODE_SIZE(f));

    if(NULL == (sym->entry = H5FL_SEQ_CALLOC(H5G_entry_t, 
                                             (size_t)(2 * H5F_SYM_LEAF_K(f)))))

        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed")

    /* Get temporary pointer to serialized node */
    p = (const uint8_t *)image_ptr;

    /* magic */
    if(HDmemcmp(p, H5G_NODE_MAGIC, (size_t)H5_SIZEOF_MAGIC))

        HGOTO_ERROR(H5E_SYM, H5E_CANTLOAD, NULL, \
                    "bad symbol table node signature")
    p += 4;

    /* version */
    if(H5G_NODE_VERS != *p++)

        HGOTO_ERROR(H5E_SYM, H5E_CANTLOAD, NULL, \
                    "bad symbol table node version")

    /* reserved */
    p++;

    /* number of symbols */
    UINT16DECODE(p, sym->nsyms);

    /* entries */
    if(H5G__ent_decode_vec(f, &p, sym->entry, sym->nsyms) < 0)

        HGOTO_ERROR(H5E_SYM, H5E_CANTLOAD, NULL, \
                    "unable to decode symbol table entries")

    /* Set return value */
    ret_value = sym;

done:

    if(!ret_value)

        if(sym && H5G__node_free(sym) < 0)

            HDONE_ERROR(H5E_SYM, H5E_CANTFREE, NULL, \
                        "unable to destroy symbol table node")

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5G_cache_node_deserialize() */


/*-------------------------------------------------------------------------
 * Function:    H5G_cache_node_image_len
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
H5G_cache_node_image_len(const void *thing, size_t *image_len_ptr)
{
    H5G_node_t *sym;
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(thing);
    HDassert(image_len_ptr);

    sym = (const H5G_node_t *)thing;

    HDassert(sym);
    HDassert(sym->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(sym->cache_info.type) == &(H5AC_SNODE[0]));

    *image_len_ptr = sym->node_size;

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5G_cache_node_image_len() */


/*************************************/
/* no H5G_cache_node_pre_serialize() */
/*************************************/


/*-------------------------------------------------------------------------
 * Function:    H5G_cache_node_serialize
 *
 * Purpose: Given a correctly sized buffer and an instace of H5G_node_t,
 *	serialize the contents of the instance of H5G_node_t, and write
 *	this data into the supplied buffer.  This buffer will be written
 *	to disk.
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
H5G_cache_node_serialize(const H5F_t *f,
                         void *image_ptr,
                         size_t len,
                         void *thing)
{
    H5G_node_t *sym = NULL;
    uint8_t    *p;                      /* Pointer into raw data buffer */
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(f);
    HDassert(image_ptr);
    HDassert(thing);

    sym = (H5G_node_t *)thing;

    HDassert(sym);
    HDassert(sym->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(sym->cache_info.type) == &(H5AC_SNODE[0]));
    HDassert(len == sym->node_size);


    /* Get temporary pointer to serialized symbol table node */
    p = (uint8_t *)image_ptr;

    /* magic number */
    HDmemcpy(p, H5G_NODE_MAGIC, (size_t)H5_SIZEOF_MAGIC);
    p += 4;

    /* version number */
    *p++ = H5G_NODE_VERS;

    /* reserved */
    *p++ = 0;

    /* number of symbols */
    UINT16ENCODE(p, sym->nsyms);

    /* entries */
    if(H5G__ent_encode_vec(f, &p, sym->entry, sym->nsyms) < 0)

            HGOTO_ERROR(H5E_SYM, H5E_CANTENCODE, FAIL, "can't serialize")

    HDmemset(p, 0, sym->node_size - (size_t)(p - (uint8_t *)image_ptr));

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5G_cache_node_serialize() */


/***************************************/
/* no H5G_cache_node_notify() function */
/***************************************/


/*-------------------------------------------------------------------------
 * Function:    H5G_cache_node_free_icr
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
H5G_cache_node_free_icr(void *thing)
{
    H5G_node_t *sym = NULL;
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);

    sym = (H5G_node_t *)thing;

    HDassert(sym);

    /* the metadata cache sets cache_info.magic to
     * H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC before calling the
     * free_icr routine.  Hence the following assert:
     */

    HDassert(sym->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_BAD_MAGIC);
    HDassert((const H5AC_class_t *)(sym->cache_info.type) == &(H5AC_SNODE[0]));

    /* Destroy symbol table node */
    if(H5G__node_free(sym) < 0)

        HGOTO_ERROR(H5E_SYM, H5E_CANTFREE, FAIL, \
                    "unable to destroy symbol table node")

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5G_cache_node_free_icr() */

