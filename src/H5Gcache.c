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
#if 1 /* V3 metadata cache callbacks */

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

#else /* V2 metadata cache callbacks */

static H5G_node_t *H5G_node_load(H5F_t *f, hid_t dxpl_id, haddr_t addr, void *udata);
static herr_t H5G_node_flush(H5F_t *f, hid_t dxpl_id, hbool_t destroy, haddr_t addr,
			     H5G_node_t *sym, unsigned *flags_ptr);
static herr_t H5G_node_dest(H5F_t *f, H5G_node_t *sym);
static herr_t H5G_node_clear(H5F_t *f, H5G_node_t *sym, hbool_t destroy);
static herr_t H5G_node_size(const H5F_t *f, const H5G_node_t *sym, size_t *size_ptr);

#endif /* end V2 metadata cache callbacks */


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

#if 1 /* V3 cache version of H5AC_SNODE */

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
}};

#else /* V2 cache version of H5AC_SNODE */

const H5AC_class_t H5AC_SNODE[1] = {{
    H5AC_SNODE_ID,
    (H5AC_load_func_t)H5G_node_load,
    (H5AC_flush_func_t)H5G_node_flush,
    (H5AC_dest_func_t)H5G_node_dest,
    (H5AC_clear_func_t)H5G_node_clear,
    (H5AC_notify_func_t)NULL,
    (H5AC_size_func_t)H5G_node_size,
}};

#endif /* end V2 cache version of H5AC_SNODE */

/* Declare extern the free list to manage the H5G_node_t struct */
H5FL_EXTERN(H5G_node_t);

/* Declare extern the free list to manage sequences of H5G_entry_t's */
H5FL_SEQ_EXTERN(H5G_entry_t);

#if 1 /* V3 metadata cache callback definitions */

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
    f = (H5F_t *)udata_ptr;

    /* compute image len */
    image_len = H5G_NODE_SIZE(f);

    /* report image length */
    *image_len_ptr - image_len;

done:

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

    sym->node_size = H5G_NODE_SIZE(f);

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

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(thing);
    HDassert(image_len_ptr);

    sym = (H5G_node_t *)thing;

    HDassert(sym);
    HDassert(sym->cache_info.magic == H5C__H5C_CACHE_ENTRY_T_MAGIC);
    HDassert((const H5AC_class_t *)(sym->cache_info.type) == &(H5AC_SNODE[0]));

    *image_len_ptr = sym->node_size;

done:

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

#else /* V2 metadata cache callback definitions */

/*-------------------------------------------------------------------------
 * Function:	H5G_node_load
 *
 * Purpose:	Loads a symbol table node from the file.
 *
 * Return:	Success:	Ptr to the new table.
 *
 *		Failure:	NULL
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Jun 23 1997
 *
 *-------------------------------------------------------------------------
 */
static H5G_node_t *
H5G_node_load(H5F_t *f, hid_t dxpl_id, haddr_t addr, void *udata)
{
    H5G_node_t		   *sym = NULL;
    H5WB_t                 *wb = NULL;     /* Wrapped buffer for node data */
    uint8_t                 node_buf[H5G_NODE_BUF_SIZE]; /* Buffer for node */
    uint8_t		   *node;           /* Pointer to node buffer */
    const uint8_t	   *p;
    H5G_node_t		   *ret_value;	/*for error handling */

    FUNC_ENTER_NOAPI_NOINIT

    /*
     * Check arguments.
     */
    HDassert(f);
    HDassert(H5F_addr_defined(addr));
    HDassert(udata);

    /*
     * Initialize variables.
     */

    /* Allocate symbol table data structures */
    if(NULL == (sym = H5FL_CALLOC(H5G_node_t)))
	HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed")
    sym->node_size = H5G_NODE_SIZE(f);
    if(NULL == (sym->entry = H5FL_SEQ_CALLOC(H5G_entry_t, (size_t)(2 * H5F_SYM_LEAF_K(f)))))
	HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed")

    /* Wrap the local buffer for serialized node info */
    if(NULL == (wb = H5WB_wrap(node_buf, sizeof(node_buf))))
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, NULL, "can't wrap buffer")

    /* Get a pointer to a buffer that's large enough for node */
    if(NULL == (node = (uint8_t *)H5WB_actual(wb, sym->node_size)))
        HGOTO_ERROR(H5E_SYM, H5E_NOSPACE, NULL, "can't get actual buffer")

    /* Read the serialized symbol table node. */
    if(H5F_block_read(f, H5FD_MEM_BTREE, addr, sym->node_size, dxpl_id, node) < 0)
	HGOTO_ERROR(H5E_SYM, H5E_READERROR, NULL, "unable to read symbol table node")

    /* Get temporary pointer to serialized node */
    p = node;

    /* magic */
    if(HDmemcmp(p, H5G_NODE_MAGIC, (size_t)H5_SIZEOF_MAGIC))
	HGOTO_ERROR(H5E_SYM, H5E_CANTLOAD, NULL, "bad symbol table node signature")
    p += 4;

    /* version */
    if(H5G_NODE_VERS != *p++)
	HGOTO_ERROR(H5E_SYM, H5E_CANTLOAD, NULL, "bad symbol table node version")

    /* reserved */
    p++;

    /* number of symbols */
    UINT16DECODE(p, sym->nsyms);

    /* entries */
    if(H5G__ent_decode_vec(f, &p, sym->entry, sym->nsyms) < 0)
	HGOTO_ERROR(H5E_SYM, H5E_CANTLOAD, NULL, "unable to decode symbol table entries")

    /* Set return value */
    ret_value = sym;

done:
    /* Release resources */
    if(wb && H5WB_unwrap(wb) < 0)
        HDONE_ERROR(H5E_SYM, H5E_CLOSEERROR, NULL, "can't close wrapped buffer")
    if(!ret_value)
        if(sym && H5G__node_free(sym) < 0)
            HDONE_ERROR(H5E_SYM, H5E_CANTFREE, NULL, "unable to destroy symbol table node")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5G_node_load() */


/*-------------------------------------------------------------------------
 * Function:	H5G_node_flush
 *
 * Purpose:	Flush a symbol table node to disk.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Robb Matzke
 *		matzke@llnl.gov
 *		Jun 23 1997
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5G_node_flush(H5F_t *f, hid_t dxpl_id, hbool_t destroy, haddr_t addr, H5G_node_t *sym, unsigned UNUSED * flags_ptr)
{
    H5WB_t     *wb = NULL;     /* Wrapped buffer for node data */
    uint8_t     node_buf[H5G_NODE_BUF_SIZE]; /* Buffer for node */
    herr_t      ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /*
     * Check arguments.
     */
    HDassert(f);
    HDassert(H5F_addr_defined(addr));
    HDassert(sym);

    /*
     * Write the symbol node to disk.
     */
    if(sym->cache_info.is_dirty) {
        uint8_t	*node;          /* Pointer to node buffer */
        uint8_t	*p;             /* Pointer into raw data buffer */

        /* Wrap the local buffer for serialized node info */
        if(NULL == (wb = H5WB_wrap(node_buf, sizeof(node_buf))))
            HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "can't wrap buffer")

        /* Get a pointer to a buffer that's large enough for node */
        if(NULL == (node = (uint8_t *)H5WB_actual(wb, sym->node_size)))
            HGOTO_ERROR(H5E_SYM, H5E_NOSPACE, FAIL, "can't get actual buffer")

        /* Get temporary pointer to serialized symbol table node */
        p = node;

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
        HDmemset(p, 0, sym->node_size - (size_t)(p - node));

	/* Write the serialized symbol table node. */
        if(H5F_block_write(f, H5FD_MEM_BTREE, addr, sym->node_size, dxpl_id, node) < 0)
            HGOTO_ERROR(H5E_SYM, H5E_WRITEERROR, FAIL, "unable to write symbol table node to the file")

        /* Reset the node's dirty flag */
        sym->cache_info.is_dirty = FALSE;
    } /* end if */

    /*
     * Destroy the symbol node?	 This might happen if the node is being
     * preempted from the cache.
     */
    if(destroy)
        if(H5G_node_dest(f, sym) < 0)
	    HGOTO_ERROR(H5E_SYM, H5E_CANTFREE, FAIL, "unable to destroy symbol table node")

done:
    /* Release resources */
    if(wb && H5WB_unwrap(wb) < 0)
        HDONE_ERROR(H5E_SYM, H5E_CLOSEERROR, FAIL, "can't close wrapped buffer")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5G_node_flush() */


/*-------------------------------------------------------------------------
 * Function:	H5G_node_dest
 *
 * Purpose:	Destroy a symbol table node in memory.
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
H5G_node_dest(H5F_t *f, H5G_node_t *sym)
{
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /*
     * Check arguments.
     */
    HDassert(f);
    HDassert(sym);

    /* Verify that node is clean */
    HDassert(sym->cache_info.is_dirty == FALSE);

    /* If we're going to free the space on disk, the address must be valid */
    HDassert(!sym->cache_info.free_file_space_on_destroy || H5F_addr_defined(sym->cache_info.addr));

    /* Check for freeing file space for symbol table node */
    if(sym->cache_info.free_file_space_on_destroy) {
        /* Release the space on disk */
        /* (XXX: Nasty usage of internal DXPL value! -QAK) */
        if(H5MF_xfree(f, H5FD_MEM_BTREE, H5AC_dxpl_id, sym->cache_info.addr, (hsize_t)sym->node_size) < 0)
            HGOTO_ERROR(H5E_SYM, H5E_CANTFREE, FAIL, "unable to free symbol table node")
    } /* end if */

    /* Destroy symbol table node */
    if(H5G__node_free(sym) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTFREE, FAIL, "unable to destroy symbol table node")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5G_node_dest() */


/*-------------------------------------------------------------------------
 * Function:	H5G_node_clear
 *
 * Purpose:	Mark a symbol table node in memory as non-dirty.
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
H5G_node_clear(H5F_t *f, H5G_node_t *sym, hbool_t destroy)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    /*
     * Check arguments.
     */
    HDassert(sym);

    /* Reset the node's dirty flag */
    sym->cache_info.is_dirty = FALSE;

    /*
     * Destroy the symbol node?	 This might happen if the node is being
     * preempted from the cache.
     */
    if(destroy)
        if(H5G_node_dest(f, sym) < 0)
	    HGOTO_ERROR(H5E_SYM, H5E_CANTFREE, FAIL, "unable to destroy symbol table node")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5G_node_clear() */


/*-------------------------------------------------------------------------
 * Function:	H5G_node_size
 *
 * Purpose:	Compute the size in bytes of the specified instance of
 *		H5G_node_t on disk, and return it in *size_ptr.  On failure
 *		the value of size_ptr is undefined.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	John Mainzer
 *		5/13/04
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5G_node_size(const H5F_t UNUSED *f, const H5G_node_t *sym, size_t *size_ptr)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /*
     * Check arguments.
     */
    HDassert(f);
    HDassert(size_ptr);

    *size_ptr = sym->node_size;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* H5G_node_size() */

#endif /* end V2 metadata cache callback definitions */

