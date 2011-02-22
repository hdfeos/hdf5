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
 * Programmer:  Robb Matzke <matzke@llnl.gov>
 *              Thursday, July 29, 1999
 *
 * Purpose:	The POSIX unbuffered file driver using only the HDF5 public
 *		API and with a few optimizations: the lseek() call is made
 *		only when the current file position is unknown or needs to be
 *		changed based on previous I/O through this driver (don't mix
 *		I/O from this driver with I/O from other parts of the
 *		application to the same file).
 */

/* Interface initialization */
#define H5_INTERFACE_INIT_FUNC	H5FD_sec2_init_interface


#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5Fprivate.h"		/* File access				*/
#include "H5FDprivate.h"	/* File drivers				*/
#include "H5FDsec2.h"		/* Sec2 file driver			*/
#include "H5FLprivate.h"	/* Free Lists                           */
#include "H5Iprivate.h"		/* IDs			  		*/
#include "H5MMprivate.h"	/* Memory management			*/
#include "H5Pprivate.h"		/* Property lists			*/

/* The driver identification number, initialized at runtime */
static hid_t H5FD_SEC2_g = 0;

#define H5FD_SEC2_MAX_FILENAME_LEN      1024

/*
 * The description of a file belonging to this driver. The `eoa' and `eof'
 * determine the amount of hdf5 address space in use and the high-water mark
 * of the file (the current size of the underlying Unix file). The `pos'
 * value is used to eliminate file position updates when they would be a
 * no-op. Unfortunately we've found systems that use separate file position
 * indicators for reading and writing so the lseek can only be eliminated if
 * the current operation is the same as the previous operation.  When opening
 * a file the `eof' will be set to the current file size, `eoa' will be set
 * to zero, `pos' will be set to H5F_ADDR_UNDEF (as it is when an error
 * occurs), and `op' will be set to H5F_OP_UNKNOWN.
 */
#define PAGE_SIZE       16384
#define NPAGES          16
/* #define H5FD_SEC2_PAGE_DEBUG */		/* Uncommenting this macro will enable
                                         * debugging printfs for various page
                                         * caching info.
                                         */
/* #define H5FD_SEC2_PAGE_STATS */		/* Uncommenting this macro will enable
                                         * printfs for various page cache
                                         * statistics.
                                         */
typedef struct H5FD_sec2_t {
    H5FD_t	pub;			/*public stuff, must be first	*/
    int		fd;			/*the unix file			*/
    haddr_t	eoa;			/*end of allocated region	*/
    haddr_t	eof;			/*end of file; current file size*/
    char	filename[H5FD_MAX_FILENAME_LEN];     /* Copy of file name from open operation */
#ifndef _WIN32
    /*
     * On most systems the combination of device and i-node number uniquely
     * identify a file.
     */
    dev_t	device;			/*file device number		*/
#ifdef H5_VMS
    ino_t	inode[3];		/*file i-node number		*/
#else
    ino_t	inode;			/*file i-node number		*/
#endif /*H5_VMS*/
#else
    /*
     * On _WIN32 the low-order word of a unique identifier associated with the
     * file and the volume serial number uniquely identify a file. This number
     * (which, both? -rpm) may change when the system is restarted or when the
     * file is opened. After a process opens a file, the identifier is
     * constant until the file is closed. An application can use this
     * identifier and the volume serial number to determine whether two
     * handles refer to the same file.
     */
    DWORD fileindexlo;
    DWORD fileindexhi;
#endif

    /* Information from properties set by 'h5repart' tool */
    hbool_t     fam_to_sec2;    /* Whether to eliminate the family driver info
                                 * and convert this file to a single file */

    /* Page buffering information */
    void *pages;
    void *page[NPAGES];
    haddr_t page_addr[NPAGES];
    hbool_t page_dirty[NPAGES];
#ifdef H5FD_SEC2_PAGE_STATS
    hsize_t nhits, nmisses, nbypasses;
#endif /* H5FD_SEC2_PAGE_STATS */
} H5FD_sec2_t;


/*
 * This driver supports systems that have the lseek64() function by defining
 * some macros here so we don't have to have conditional compilations later
 * throughout the code.
 *
 * HDoff_t:	The datatype for file offsets, the second argument of
 *		the lseek() or lseek64() call.
 *
 */

/*
 * These macros check for overflow of various quantities.  These macros
 * assume that HDoff_t is signed and haddr_t and size_t are unsigned.
 *
 * ADDR_OVERFLOW:	Checks whether a file address of type `haddr_t'
 *			is too large to be represented by the second argument
 *			of the file seek function.
 *
 * SIZE_OVERFLOW:	Checks whether a buffer size of type `hsize_t' is too
 *			large to be represented by the `size_t' type.
 *
 * REGION_OVERFLOW:	Checks whether an address and size pair describe data
 *			which can be addressed entirely by the second
 *			argument of the file seek function.
 */
#define MAXADDR (((haddr_t)1<<(8*sizeof(HDoff_t)-1))-1)
#define ADDR_OVERFLOW(A)	(HADDR_UNDEF==(A) ||			      \
				 ((A) & ~(haddr_t)MAXADDR))
#define SIZE_OVERFLOW(Z)	((Z) & ~(hsize_t)MAXADDR)
#define REGION_OVERFLOW(A,Z)	(ADDR_OVERFLOW(A) || SIZE_OVERFLOW(Z) ||      \
                                 HADDR_UNDEF==(A)+(Z) ||		      \
				 (HDoff_t)((A)+(Z))<(HDoff_t)(A))

/* Prototypes */
static H5FD_t *H5FD_sec2_open(const char *name, unsigned flags, hid_t fapl_id,
			      haddr_t maxaddr);
static herr_t H5FD_sec2_close(H5FD_t *_file);
static int H5FD_sec2_cmp(const H5FD_t *_f1, const H5FD_t *_f2);
static herr_t H5FD_sec2_query(const H5FD_t *_f1, unsigned long *flags);
static haddr_t H5FD_sec2_get_eoa(const H5FD_t *_file, H5FD_mem_t type);
static herr_t H5FD_sec2_set_eoa(H5FD_t *_file, H5FD_mem_t type, haddr_t addr);
static haddr_t H5FD_sec2_get_eof(const H5FD_t *_file);
static herr_t  H5FD_sec2_get_handle(H5FD_t *_file, hid_t fapl, void** file_handle);
static herr_t H5FD_sec2_read(H5FD_t *_file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr,
			     size_t size, void *buf);
static herr_t H5FD_sec2_write(H5FD_t *_file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr,
			      size_t size, const void *buf);
static herr_t H5FD_sec2_flush(H5FD_t *_file, hid_t dxpl_id, unsigned closing);
static herr_t H5FD_sec2_truncate(H5FD_t *_file, hid_t dxpl_id, hbool_t closing);

static const H5FD_class_t H5FD_sec2_g = {
    "sec2",					/*name			*/
    MAXADDR,					/*maxaddr		*/
    H5F_CLOSE_WEAK,				/* fc_degree		*/
    NULL,					/*sb_size		*/
    NULL,					/*sb_encode		*/
    NULL,					/*sb_decode		*/
    0, 						/*fapl_size		*/
    NULL,					/*fapl_get		*/
    NULL,					/*fapl_copy		*/
    NULL, 					/*fapl_free		*/
    0,						/*dxpl_size		*/
    NULL,					/*dxpl_copy		*/
    NULL,					/*dxpl_free		*/
    H5FD_sec2_open,				/*open			*/
    H5FD_sec2_close,				/*close			*/
    H5FD_sec2_cmp,				/*cmp			*/
    H5FD_sec2_query,				/*query			*/
    NULL,					/*get_type_map		*/
    NULL,					/*alloc			*/
    NULL,					/*free			*/
    H5FD_sec2_get_eoa,				/*get_eoa		*/
    H5FD_sec2_set_eoa, 				/*set_eoa		*/
    H5FD_sec2_get_eof,				/*get_eof		*/
    H5FD_sec2_get_handle,                       /*get_handle            */
    H5FD_sec2_read,				/*read			*/
    H5FD_sec2_write,				/*write			*/
    H5FD_sec2_flush,				/*flush			*/
    H5FD_sec2_truncate,				/*truncate		*/
    NULL,                                       /*lock                  */
    NULL,                                       /*unlock                */
    H5FD_FLMAP_SINGLE 				/*fl_map		*/
};

/* Declare a free list to manage the H5FD_sec2_t struct */
H5FL_DEFINE_STATIC(H5FD_sec2_t);


/*--------------------------------------------------------------------------
NAME
   H5FD_sec2_init_interface -- Initialize interface-specific information
USAGE
    herr_t H5FD_sec2_init_interface()

RETURNS
    Non-negative on success/Negative on failure
DESCRIPTION
    Initializes any interface-specific data or routines.  (Just calls
    H5FD_sec2_init currently).

--------------------------------------------------------------------------*/
static herr_t
H5FD_sec2_init_interface(void)
{
    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5FD_sec2_init_interface)

    FUNC_LEAVE_NOAPI(H5FD_sec2_init())
} /* H5FD_sec2_init_interface() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_init
 *
 * Purpose:	Initialize this driver by registering the driver with the
 *		library.
 *
 * Return:	Success:	The driver ID for the sec2 driver.
 *		Failure:	Negative.
 *
 * Programmer:	Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5FD_sec2_init(void)
{
    hid_t ret_value;            /* Return value */

    FUNC_ENTER_NOAPI(H5FD_sec2_init, FAIL)

    if(H5I_VFL != H5I_get_type(H5FD_SEC2_g))
        H5FD_SEC2_g = H5FD_register(&H5FD_sec2_g, sizeof(H5FD_class_t), FALSE);

    /* Set return value */
    ret_value = H5FD_SEC2_g;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_init() */


/*---------------------------------------------------------------------------
 * Function:	H5FD_sec2_term
 *
 * Purpose:	Shut down the VFD
 *
 * Return:	<none>
 *
 * Programmer:  Quincey Koziol
 *              Friday, Jan 30, 2004
 *
 *---------------------------------------------------------------------------
 */
void
H5FD_sec2_term(void)
{
    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5FD_sec2_term)

    /* Reset VFL ID */
    H5FD_SEC2_g = 0;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5FD_sec2_term() */


/*-------------------------------------------------------------------------
 * Function:	H5Pset_fapl_sec2
 *
 * Purpose:	Modify the file access property list to use the H5FD_SEC2
 *		driver defined in this source file.  There are no driver
 *		specific properties.
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Robb Matzke
 *		Thursday, February 19, 1998
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pset_fapl_sec2(hid_t fapl_id)
{
    H5P_genplist_t *plist;      /* Property list pointer */
    herr_t ret_value;           /* Return value */

    FUNC_ENTER_API(H5Pset_fapl_sec2, FAIL)
    H5TRACE1("e", "i", fapl_id);

    if(NULL == (plist = H5P_object_verify(fapl_id, H5P_FILE_ACCESS)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list")

    ret_value = H5P_set_driver(plist, H5FD_SEC2, NULL);

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Pset_fapl_sec2() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_open
 *
 * Purpose:	Create and/or opens a Unix file as an HDF5 file.
 *
 * Return:	Success:	A pointer to a new file data structure. The
 *				public fields will be initialized by the
 *				caller, which is always H5FD_open().
 *		Failure:	NULL
 *
 * Programmer:	Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static H5FD_t *
H5FD_sec2_open(const char *name, unsigned flags, hid_t fapl_id, haddr_t maxaddr)
{
    H5FD_sec2_t	*file = NULL;   /* sec2 VFD info */
    int		fd = (-1);      /* File descriptor */
    int		o_flags;        /* Flags for open() call */
#ifdef _WIN32
    HFILE filehandle;
    struct _BY_HANDLE_FILE_INFORMATION fileinfo;
#endif
    h5_stat_t	sb;
    H5FD_t	*ret_value;     /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5FD_sec2_open)
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Called, name = '%s'\n", FUNC, name);
#endif /* H5FD_SEC2_PAGE_DEBUG */

    /* Sanity check on file offsets */
    HDcompile_assert(sizeof(HDoff_t) >= sizeof(size_t));

    /* Check arguments */
    if(!name || !*name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, NULL, "invalid file name")
    if(0 == maxaddr || HADDR_UNDEF == maxaddr)
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, NULL, "bogus maxaddr")
    if(ADDR_OVERFLOW(maxaddr))
        HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, NULL, "bogus maxaddr")

    /* Build the open flags */
    o_flags = (H5F_ACC_RDWR & flags) ? O_RDWR : O_RDONLY;
    if(H5F_ACC_TRUNC & flags)
        o_flags |= O_TRUNC;
    if(H5F_ACC_CREAT & flags)
        o_flags |= O_CREAT;
    if(H5F_ACC_EXCL & flags)
        o_flags |= O_EXCL;

    /* Open the file */
    if((fd = HDopen(name, o_flags, 0666)) < 0) {
        int myerrno = errno;

        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, NULL, "unable to open file: name = '%s', errno = %d, error message = '%s', flags = %x, o_flags = %x", name, myerrno, HDstrerror(myerrno), flags, (unsigned)o_flags);
    } /* end if */
    if(HDfstat(fd, &sb) < 0)
        HSYS_GOTO_ERROR(H5E_FILE, H5E_BADFILE, NULL, "unable to fstat file")

    /* Create the new file struct */
    if(NULL == (file = H5FL_CALLOC(H5FD_sec2_t)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "unable to allocate file struct")

    file->fd = fd;
    H5_ASSIGN_OVERFLOW(file->eof, sb.st_size, h5_stat_size_t, haddr_t);
#ifdef _WIN32
    filehandle = _get_osfhandle(fd);
    (void)GetFileInformationByHandle((HANDLE)filehandle, &fileinfo);
    file->fileindexhi = fileinfo.nFileIndexHigh;
    file->fileindexlo = fileinfo.nFileIndexLow;
#else /* _WIN32 */
    file->device = sb.st_dev;
#ifdef H5_VMS
    file->inode[0] = sb.st_ino[0];
    file->inode[1] = sb.st_ino[1];
    file->inode[2] = sb.st_ino[2];
#else
    file->inode = sb.st_ino;
#endif /*H5_VMS*/

#endif /* _WIN32 */

    /* Retain a copy of the name used to open the file, for possible error reporting */
    HDstrncpy(file->filename, name, sizeof(file->filename));
    file->filename[sizeof(file->filename) - 1] = '\0';

/* Set up page buffering information */
{
    unsigned u;

    file->pages = (void *)H5MM_malloc((size_t)PAGE_SIZE * NPAGES);
    for(u = 0; u < NPAGES; u++) {
        file->page[u] = (unsigned char *)file->pages + (size_t)(PAGE_SIZE * u);
        file->page_addr[u] = HADDR_UNDEF;
        file->page_dirty[u] = FALSE;
    } /* end for */
#ifdef H5FD_SEC2_PAGE_STATS
    file->nhits = 0;
    file->nmisses = 0;
    file->nbypasses = 0;
#endif /* H5FD_SEC2_PAGE_STATS */
}

    /* Check for non-default FAPL */
    if(H5P_FILE_ACCESS_DEFAULT != fapl_id) {
        H5P_genplist_t      *plist;      /* Property list pointer */

        /* Get the FAPL */
        if(NULL == (plist = (H5P_genplist_t *)H5I_object(fapl_id)))
            HGOTO_ERROR(H5E_VFL, H5E_BADTYPE, NULL, "not a file access property list")

        /* This step is for h5repart tool only. If user wants to change file driver from
         * family to sec2 while using h5repart, this private property should be set so that
         * in the later step, the library can ignore the family driver information saved
         * in the superblock.
         */
        if(H5P_exist_plist(plist, H5F_ACS_FAMILY_TO_SEC2_NAME) > 0)
            if(H5P_get(plist, H5F_ACS_FAMILY_TO_SEC2_NAME, &file->fam_to_sec2) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTGET, NULL, "can't get property of changing family to sec2")
    } /* end if */

    /* Set return value */
    ret_value = (H5FD_t*)file;

done:
    if(NULL == ret_value) {
        if(fd >= 0)
            HDclose(fd);
        if(file)
            file = H5FL_FREE(H5FD_sec2_t, file);
    } /* end if */

#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Leaving, ret_value = %p\n", FUNC, ret_value);
#endif /* H5FD_SEC2_PAGE_DEBUG */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_open() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_close
 *
 * Purpose:	Closes a Unix file.
 *
 * Return:	Success:	0
 *		Failure:	-1, file not closed.
 *
 * Programmer:	Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_sec2_close(H5FD_t *_file)
{
    H5FD_sec2_t	*file = (H5FD_sec2_t *)_file;
    herr_t ret_value = SUCCEED;                 /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5FD_sec2_close)

    /* Sanity check */
    HDassert(file);

/* Release page buffering information */
/* Flush any dirty pages */
if(H5FD_sec2_flush(_file, H5P_DEFAULT, TRUE) < 0)
    HGOTO_ERROR(H5E_IO, H5E_CANTFLUSH, FAIL, "can't flush pages to file")

H5MM_xfree(file->pages);
#ifdef H5FD_SEC2_PAGE_STATS
HDfprintf(stderr, "%s: file->nhits = %Hu, file->nmisses = %Hu, file->nbypasses = %Hu\n", FUNC, file->nhits, file->nmisses, file->nbypasses);
#endif /* H5FD_SEC2_PAGE_STATS */

    /* Close the underlying file */
    if(HDclose(file->fd) < 0)
        HSYS_GOTO_ERROR(H5E_IO, H5E_CANTCLOSEFILE, FAIL, "unable to close file")

    /* Release the file info */
    file = H5FL_FREE(H5FD_sec2_t, file);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_close() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_cmp
 *
 * Purpose:	Compares two files belonging to this driver using an
 *		arbitrary (but consistent) ordering.
 *
 * Return:	Success:	A value like strcmp()
 *		Failure:	never fails (arguments were checked by the
 *				caller).
 *
 * Programmer:	Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static int
H5FD_sec2_cmp(const H5FD_t *_f1, const H5FD_t *_f2)
{
    const H5FD_sec2_t	*f1 = (const H5FD_sec2_t *)_f1;
    const H5FD_sec2_t	*f2 = (const H5FD_sec2_t *)_f2;
    int ret_value = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5FD_sec2_cmp)

#ifdef _WIN32
    if(f1->fileindexhi < f2->fileindexhi) HGOTO_DONE(-1)
    if(f1->fileindexhi > f2->fileindexhi) HGOTO_DONE(1)

    if(f1->fileindexlo < f2->fileindexlo) HGOTO_DONE(-1)
    if(f1->fileindexlo > f2->fileindexlo) HGOTO_DONE(1)

#else /* _WIN32 */
#ifdef H5_DEV_T_IS_SCALAR
    if(f1->device < f2->device) HGOTO_DONE(-1)
    if(f1->device > f2->device) HGOTO_DONE(1)
#else /* H5_DEV_T_IS_SCALAR */
    /* If dev_t isn't a scalar value on this system, just use memcmp to
     * determine if the values are the same or not.  The actual return value
     * shouldn't really matter...
     */
    if(HDmemcmp(&(f1->device),&(f2->device),sizeof(dev_t)) < 0) HGOTO_DONE(-1)
    if(HDmemcmp(&(f1->device),&(f2->device),sizeof(dev_t)) > 0) HGOTO_DONE(1)
#endif /* H5_DEV_T_IS_SCALAR */

#ifndef H5_VMS
    if(f1->inode < f2->inode) HGOTO_DONE(-1)
    if(f1->inode > f2->inode) HGOTO_DONE(1)
#else
    if(HDmemcmp(&(f1->inode), &(f2->inode), 3 * sizeof(ino_t)) < 0) HGOTO_DONE(-1)
    if(HDmemcmp(&(f1->inode), &(f2->inode), 3 * sizeof(ino_t)) > 0) HGOTO_DONE(1)
#endif /*H5_VMS*/

#endif /* _WIN32 */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_cmp() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_query
 *
 * Purpose:	Set the flags that this VFL driver is capable of supporting.
 *              (listed in H5FDpublic.h)
 *
 * Return:	Success:	non-negative
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Friday, August 25, 2000
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_sec2_query(const H5FD_t *_file, unsigned long *flags /* out */)
{
    const H5FD_sec2_t	*file = (const H5FD_sec2_t *)_file;    /* sec2 VFD info */

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5FD_sec2_query)

    /* Set the VFL feature flags that this driver supports */
    if(flags) {
        *flags = 0;
        *flags |= H5FD_FEAT_AGGREGATE_METADATA; /* OK to aggregate metadata allocations */
        *flags |= H5FD_FEAT_ACCUMULATE_METADATA; /* OK to accumulate metadata for faster writes */
        *flags |= H5FD_FEAT_DATA_SIEVE;       /* OK to perform data sieving for faster raw data reads & writes */
        *flags |= H5FD_FEAT_AGGREGATE_SMALLDATA; /* OK to aggregate "small" raw data allocations */
        *flags |= H5FD_FEAT_POSIX_COMPAT_HANDLE; /* VFD handle is POSIX I/O call compatible */

        /* Check for flags that are set by h5repart */
        if(file->fam_to_sec2)
            *flags |= H5FD_FEAT_IGNORE_DRVRINFO; /* Ignore the driver info when file is opened (which eliminates it) */
    } /* end if */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD_sec2_query() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_get_eoa
 *
 * Purpose:	Gets the end-of-address marker for the file. The EOA marker
 *		is the first address past the last byte allocated in the
 *		format address space.
 *
 * Return:	Success:	The end-of-address marker.
 *		Failure:	HADDR_UNDEF
 *
 * Programmer:	Robb Matzke
 *              Monday, August  2, 1999
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD_sec2_get_eoa(const H5FD_t *_file, H5FD_mem_t UNUSED type)
{
    const H5FD_sec2_t	*file = (const H5FD_sec2_t *)_file;

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5FD_sec2_get_eoa)

    FUNC_LEAVE_NOAPI(file->eoa)
} /* end H5FD_sec2_get_eoa() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_set_eoa
 *
 * Purpose:	Set the end-of-address marker for the file. This function is
 *		called shortly after an existing HDF5 file is opened in order
 *		to tell the driver where the end of the HDF5 data is located.
 *
 * Return:	Success:	0
 *		Failure:	-1
 *
 * Programmer:	Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_sec2_set_eoa(H5FD_t *_file, H5FD_mem_t UNUSED type, haddr_t addr)
{
    H5FD_sec2_t	*file = (H5FD_sec2_t *)_file;

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5FD_sec2_set_eoa)

    file->eoa = addr;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD_sec2_set_eoa() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_get_eof
 *
 * Purpose:	Returns the end-of-file marker, which is the greater of
 *		either the Unix end-of-file or the HDF5 end-of-address
 *		markers.
 *
 * Return:	Success:	End of file address, the first address past
 *				the end of the "file", either the Unix file
 *				or the HDF5 file.
 *		Failure:	HADDR_UNDEF
 *
 * Programmer:	Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD_sec2_get_eof(const H5FD_t *_file)
{
    const H5FD_sec2_t	*file = (const H5FD_sec2_t *)_file;

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5FD_sec2_get_eof)

    FUNC_LEAVE_NOAPI(MAX(file->eof, file->eoa))
} /* end H5FD_sec2_get_eof() */


/*-------------------------------------------------------------------------
 * Function:       H5FD_sec2_get_handle
 *
 * Purpose:        Returns the file handle of sec2 file driver.
 *
 * Returns:        Non-negative if succeed or negative if fails.
 *
 * Programmer:     Raymond Lu
 *                 Sept. 16, 2002
 *
 *-------------------------------------------------------------------------
 */
/* ARGSUSED */
static herr_t
H5FD_sec2_get_handle(H5FD_t *_file, hid_t UNUSED fapl, void **file_handle)
{
    H5FD_sec2_t         *file = (H5FD_sec2_t *)_file;
    herr_t              ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT(H5FD_sec2_get_handle)

    if(!file_handle)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file handle not valid")

    *file_handle = &(file->fd);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_get_handle() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_read_real
 *
 * Purpose:	Reads SIZE bytes of data from FILE beginning at address ADDR
 *		into buffer BUF according to data transfer properties in
 *		DXPL_ID.
 *
 * Return:	Success:	Zero. Result is stored in caller-supplied
 *				buffer BUF.
 *		Failure:	-1, Contents of buffer BUF are undefined.
 *
 * Programmer:	Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_sec2_read_real(const H5FD_sec2_t *file, haddr_t addr, size_t size, void *buf/*out*/)
{
    herr_t              ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5FD_sec2_read_real)

    HDassert(file && file->pub.cls);
    HDassert(buf);

    /*
     * Read data, being careful of interrupted system calls, partial results,
     * and the end of the file.
     */
    while(size > 0) {
        ssize_t		nbytes;         /* Number of bytes read */

        do {
            nbytes = HDpread(file->fd, buf, size, (HDoff_t)addr);
        } while(-1 == nbytes && EINTR == errno);
        if(-1 == nbytes) { /* error */
            int myerrno = errno;
            time_t mytime = HDtime(NULL);
            HDoff_t myoffset = HDlseek(file->fd, (HDoff_t)0, SEEK_CUR);

            HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "file read failed: time = %s, filename = '%s', file descriptor = %d, errno = %d, error message = '%s', buf = %p, size = %lu, offset = %llu", HDctime(&mytime), file->filename, file->fd, myerrno, HDstrerror(myerrno), buf, (unsigned long)size, (unsigned long long)myoffset);
        } /* end if */
        if(0 == nbytes) {
            /* end of file but not end of format address space */
            HDmemset(buf, 0, size);
            break;
        } /* end if */
        HDassert(nbytes >= 0);
        HDassert((size_t)nbytes <= size);
        H5_CHECK_OVERFLOW(nbytes, ssize_t, size_t);
        size -= (size_t)nbytes;
        H5_CHECK_OVERFLOW(nbytes, ssize_t, haddr_t);
        addr += (haddr_t)nbytes;
        buf = (unsigned char *)buf + nbytes;
    } /* end while */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_read_real() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_write_real
 *
 * Purpose:	Writes SIZE bytes of data to FILE beginning at address ADDR
 *		from buffer BUF according to data transfer properties in
 *		DXPL_ID.
 *
 * Return:	Success:	Zero
 *		Failure:	-1
 *
 * Programmer:	Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_sec2_write_real(H5FD_sec2_t *file, haddr_t addr, size_t size, const void *buf)
{
    herr_t              ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5FD_sec2_write_real)

    HDassert(file && file->pub.cls);
    HDassert(buf);

    /*
     * Write the data, being careful of interrupted system calls and partial
     * results
     */
    while(size > 0) {
        ssize_t		nbytes;         /* Number of bytes to write */

        do {
            nbytes = HDpwrite(file->fd, buf, size, (HDoff_t)addr);
        } while(-1 == nbytes && EINTR == errno);
        if(-1 == nbytes) { /* error */
            int myerrno = errno;
            time_t mytime = HDtime(NULL);
            HDoff_t myoffset = HDlseek(file->fd, (HDoff_t)0, SEEK_CUR);

            HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "file write failed: time = %s, filename = '%s', file descriptor = %d, errno = %d, error message = '%s', buf = %p, size = %lu, offset = %llu", HDctime(&mytime), file->filename, file->fd, myerrno, HDstrerror(myerrno), buf, (unsigned long)size, (unsigned long long)myoffset);
        } /* end if */
        HDassert(nbytes > 0);
        HDassert((size_t)nbytes <= size);
        H5_CHECK_OVERFLOW(nbytes, ssize_t, size_t);
        size -= (size_t)nbytes;
        H5_CHECK_OVERFLOW(nbytes, ssize_t, haddr_t);
        addr += (haddr_t)nbytes;
        buf = (const char *)buf + nbytes;
    } /* end while */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_write_real() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_page_flush
 *
 * Purpose:	Evict a given page from the page cache
 *
 * Return:	Success:	Zero
 *		Failure:	-1
 *
 * Programmer:	Quincey Koziol
 *              Wednesday, November 24, 2010
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_sec2_page_flush(H5FD_sec2_t *file, unsigned page_idx)
{
    herr_t      ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5FD_sec2_page_flush)

    /* Check if page is dirty */
    if(file->page_dirty[page_idx]) {
        size_t size;            /* Size to write */

        /* Sanity check */
        HDassert(H5F_addr_defined(file->page_addr[page_idx]));

        /* Check for page at end of file */
        if((file->page_addr[page_idx] + PAGE_SIZE) > file->eof)
            size = (size_t)(file->eof - file->page_addr[page_idx]);
        else
            size = PAGE_SIZE;

        /* Flush page */
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s:%u page write to file->page_addr[%u] = %a, size = %Zu\n", FUNC, __LINE__, page_idx, file->page_addr[page_idx], size);
#endif /* H5FD_SEC2_PAGE_DEBUG */
        if(H5FD_sec2_write_real(file, file->page_addr[page_idx], size, file->page[page_idx]) < 0)
            HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "can't write page to file")

        /* Make page as clean */
        file->page_dirty[page_idx] = FALSE;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_page_flush() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_page_remove
 *
 * Purpose:	Remove a given page from the page cache
 *
 * Return:	Success:	Zero
 *		Failure:	-1
 *
 * Programmer:	Quincey Koziol
 *              Wednesday, November 24, 2010
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_sec2_page_remove(H5FD_sec2_t *file, unsigned page_idx)
{
    unsigned npages = (NPAGES - (page_idx + 1)); /* Number of pages to move */
    void *tmp_page = file->page[page_idx];      /* Pointer to page's area in the buffer */

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5FD_sec2_page_remove)

#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: removing page at %a, eof = %a\n", "H5FD_sec2_page_remove", file->page_addr[page_idx], file->eof);
#endif /* H5FD_SEC2_PAGE_DEBUG */
    /* Move existing pages up */
    HDmemmove(&file->page[page_idx], &file->page[page_idx + 1], npages * sizeof(void *));
    HDmemmove(&file->page_addr[page_idx], &file->page_addr[page_idx + 1], npages * sizeof(haddr_t));
    HDmemmove(&file->page_dirty[page_idx], &file->page_dirty[page_idx + 1], npages * sizeof(hbool_t));

    /* Reset information on last page */
    file->page[NPAGES - 1] = tmp_page;
    file->page_addr[NPAGES - 1] = HADDR_UNDEF;
    file->page_dirty[NPAGES - 1] = FALSE;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD_sec2_page_remove() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_page_promote
 *
 * Purpose:	Promote a given page in the page cache
 *
 * Return:	Success:	Zero
 *		Failure:	-1
 *
 * Programmer:	Quincey Koziol
 *              Wednesday, November 24, 2010
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_sec2_page_promote(H5FD_sec2_t *file, unsigned page_idx, unsigned *new_page_idx)
{
    void *tmp_page;             /* Temporary buffer pointer for page */
    haddr_t tmp_addr;           /* Temporary address for page */
    hbool_t tmp_dirty;          /* Temporary dirty flag for page */

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5FD_sec2_page_promote)

#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: promoting page #%u, at %a\n", "H5FD_sec2_page_promote", page_idx, file->page_addr[page_idx]);
#endif /* H5FD_SEC2_PAGE_DEBUG */

    /* Move the page forward in the "cache", if it's not already at the front */
    if(page_idx > 0) {
        tmp_page = file->page[page_idx];
        tmp_addr = file->page_addr[page_idx];
        tmp_dirty = file->page_dirty[page_idx];

        /* Move existing pages down in LRU scheme */
        HDmemmove(&file->page[1], &file->page[0], (page_idx * sizeof(void *)));
        HDmemmove(&file->page_addr[1], &file->page_addr[0], (page_idx * sizeof(haddr_t)));
        HDmemmove(&file->page_dirty[1], &file->page_dirty[0], (page_idx * sizeof(hbool_t)));
        file->page[0] = tmp_page;
        file->page_addr[0] = tmp_addr;
        file->page_dirty[0] = tmp_dirty;

        /* Set the new page's index */
        *new_page_idx = 0;
    } /* end if */
    else
        /* Leave page where it is */
        *new_page_idx = page_idx;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD_sec2_page_promote() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_read
 *
 * Purpose:	Reads SIZE bytes of data from FILE beginning at address ADDR
 *		into buffer BUF according to data transfer properties in
 *		DXPL_ID.
 *
 * Return:	Success:	Zero. Result is stored in caller-supplied
 *				buffer BUF.
 *		Failure:	-1, Contents of buffer BUF are undefined.
 *
 * Programmer:	Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
/* ARGSUSED */
static herr_t
H5FD_sec2_read(H5FD_t *_file, H5FD_mem_t UNUSED type, hid_t UNUSED dxpl_id,
    haddr_t addr, size_t size, void *_buf/*out*/)
{
    H5FD_sec2_t	*file = (H5FD_sec2_t *)_file;   /* Alias for file pointer */
    unsigned u;                         /* Local index variable */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5FD_sec2_read)
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Called: addr = %a, size = %Zu\n", FUNC, addr, size);
#endif /* H5FD_SEC2_PAGE_DEBUG */

    HDassert(file && file->pub.cls);
    HDassert(_buf);

    /* Check for overflow conditions */
    if(!H5F_addr_defined(addr))
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "addr undefined, addr = %llu", (unsigned long long)addr)
    if(REGION_OVERFLOW(addr, size))
        HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL, "addr overflow, addr = %llu", (unsigned long long)addr)
    if((addr + size) > file->eoa)
        HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL, "addr overflow, addr = %llu", (unsigned long long)addr)

    /* Fill requests up to half the page cache from page buffers */
    if(size <= ((PAGE_SIZE * NPAGES) / 2)) {
        unsigned char *buf = (unsigned char *)_buf;

        /* Fill request from page buffers */
        while(size > 0) {
            size_t page_off;        /* Offset within the page */
            size_t page_amt;        /* Amount to copy from page */
            hbool_t found_page = FALSE;

            /* Look for page that overlaps with start of buffer */
            for(u = 0; u < NPAGES; u++) {
                /* Does current page overlap with [current] beginning of buffer */
                if(H5F_addr_le(file->page_addr[u], addr) && H5F_addr_lt(addr, (file->page_addr[u] + PAGE_SIZE))) {
                    found_page = TRUE;
                    break;
                } /* end if */
            } /* end for */

            /* Did we find a page that fulfills [part of] request? */
            if(found_page) {    /* Yes - fulfill request */
                /* Sanity check */
                HDassert(u < NPAGES);

                /* Promote the page in the eviction scheme */
                if(H5FD_sec2_page_promote(file, u, &u) < 0)
                    HGOTO_ERROR(H5E_IO, H5E_CANTMOVE, FAIL, "can't promote page in eviction scheme")

#ifdef H5FD_SEC2_PAGE_STATS
                /* Increment # of hits */
                file->nhits++;
#endif /* H5FD_SEC2_PAGE_STATS */
            } /* end if */
            else {      /* No - bring new page into memory */
                /* Flush page to evict */
                if(H5FD_sec2_page_flush(file, NPAGES - 1) < 0)
                    HGOTO_ERROR(H5E_IO, H5E_CANTFLUSH, FAIL, "can't flush page to file")

#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: evicting page at file->page_addr[%u] = %a\n", FUNC, (NPAGES - 1), file->page_addr[(NPAGES - 1)]);
#endif /* H5FD_SEC2_PAGE_DEBUG */
                {
                    void *tmp_page = file->page[NPAGES - 1];

                    /* Move existing pages down in LRU scheme */
                    HDmemmove(&file->page[1], &file->page[0], (NPAGES - 1) * sizeof(void *));
                    HDmemmove(&file->page_addr[1], &file->page_addr[0], (NPAGES - 1) * sizeof(haddr_t));
                    HDmemmove(&file->page_dirty[1], &file->page_dirty[0], (NPAGES - 1) * sizeof(hbool_t));
                    file->page[0] = tmp_page;
                }

                /* Set page location */
                u = 0;

                /* Compute address of new page */
                file->page_addr[u] = (addr / PAGE_SIZE) * PAGE_SIZE;

                /* Read a page of data */
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s:%u page read from file->page_addr[%u] = %a\n", FUNC, __LINE__, u, file->page_addr[u]);
#endif /* H5FD_SEC2_PAGE_DEBUG */
                if(H5FD_sec2_read_real(file, file->page_addr[u], PAGE_SIZE, file->page[u]) < 0)
                    HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "can't read page from file")
                file->page_dirty[u] = FALSE;

#ifdef H5FD_SEC2_PAGE_STATS
                /* Increment # of misses */
                file->nmisses++;
#endif /* H5FD_SEC2_PAGE_STATS */
            } /* end else */

            /* Compute offset of info within page */
            page_off = (size_t)(addr - file->page_addr[u]);

            /* Compute amount to copy */
            if(size < (PAGE_SIZE - page_off))
                page_amt = size;
            else
                page_amt = (PAGE_SIZE - page_off);

            /* Copy data from page into buffer */
            HDmemcpy(buf, ((unsigned char *)file->page[u] + page_off), page_amt);

            /* Increment buffer counter & decrement amount left */
            buf += page_amt;
            size -= page_amt;
            addr += page_amt;
        } /* end while */
    } /* end if */
    else {
        haddr_t end = addr + size;      /* End of buffer to write */

        /* Sanity check */
        HDassert((end - addr) > PAGE_SIZE);

#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Large read bypassing cache\n", FUNC);
#endif /* H5FD_SEC2_PAGE_DEBUG */
#ifdef H5FD_SEC2_PAGE_STATS
        /* Increment # of page buffer bypasses */
        file->nbypasses++;
#endif /* H5FD_SEC2_PAGE_STATS */

        /* Read data */
        if(H5FD_sec2_read_real(file, addr, size, _buf) < 0)
            HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "can't read data from file")

        /* Check to see if large read intersects with any of the [dirty] pages */
        for(u = 0; u < NPAGES; u++) {
            /* Check for valid, but dirty page */
            if(H5F_addr_defined(file->page_addr[u]) && file->page_dirty[u]) {
                haddr_t page_end = file->page_addr[u] + PAGE_SIZE;  /* End of current page */

#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: dirty page[%u] at: %a\n", FUNC, u, file->page_addr[u]);
#endif /* H5FD_SEC2_PAGE_DEBUG */
                /* Does buffer intersect current page */
                if((H5F_addr_le(addr, file->page_addr[u]) && H5F_addr_gt(end, file->page_addr[u]))
                        || (H5F_addr_lt(addr, page_end) && H5F_addr_ge(end, page_end))) {
                    size_t page_off;    /* Offset of overlap within the page */
                    size_t buf_off;     /* Offset of overlap within the buffer */
                    size_t overlap;     /* Length of overlap within the page */

#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Large read intersects with dirty page at: %a\n", FUNC, file->page_addr[u]);
#endif /* H5FD_SEC2_PAGE_DEBUG */

                    /* Check if large read encompasses entire page */
                    if(H5F_addr_le(addr, file->page_addr[u]) && H5F_addr_gt(end, page_end)) {
                        /* Set up offsets & lengths */
                        page_off = (size_t)0;
                        buf_off = (size_t)(file->page_addr[u] - addr);
                        overlap = PAGE_SIZE;
                    } /* end if */
                    else {
                        /* Check for overlapping the beginning of the page */
                        if(H5F_addr_le(addr, file->page_addr[u]) && H5F_addr_gt(end, file->page_addr[u])) {
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Partial read of dirty page at %a - overlap beginning of page\n", FUNC, file->page_addr[u]);
#endif /* H5FD_SEC2_PAGE_DEBUG */
                            /* Set up offsets & lengths */
                            page_off = (size_t)0;
                            buf_off = (size_t)(file->page_addr[u] - addr);
                            overlap = (size_t)(end - file->page_addr[u]);
                        } /* end if */
                        else {
                            /* Sanity check */
                            HDassert(H5F_addr_lt(addr, page_end) && H5F_addr_ge(end, page_end));
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Partial read of dirty page at %a - overlap end of page\n", FUNC, file->page_addr[u]);
#endif /* H5FD_SEC2_PAGE_DEBUG */
                            /* Set up offsets & lengths */
                            page_off = (size_t)(addr - file->page_addr[u]);
                            buf_off = (size_t)0;
                            overlap = (size_t)(page_end - addr);
                        } /* end else */
                    } /* end else */
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: page_off = %Zu, buf_off = %Zu, overlap = %Zu\n", FUNC, page_off, buf_off, overlap);
#endif /* H5FD_SEC2_PAGE_DEBUG */

#ifdef QAK
                    /* Promote the page in the eviction scheme */
                    if(H5FD_sec2_page_promote(file, u, &u) < 0)
                        HGOTO_ERROR(H5E_IO, H5E_CANTMOVE, FAIL, "can't promote page in eviction scheme")
#endif /* QAK */

                    /* Update page with information from buffer */
                    HDmemcpy((unsigned char *)_buf + buf_off, (unsigned char *)file->page[u] + page_off, overlap);
                } /* end if */
            } /* end if */
        } /* end for */
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_read() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_write
 *
 * Purpose:	Writes SIZE bytes of data to FILE beginning at address ADDR
 *		from buffer BUF according to data transfer properties in
 *		DXPL_ID.
 *
 * Return:	Success:	Zero
 *		Failure:	-1
 *
 * Programmer:	Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
/* ARGSUSED */
static herr_t
H5FD_sec2_write(H5FD_t *_file, H5FD_mem_t UNUSED type, hid_t UNUSED dxpl_id, haddr_t addr,
		size_t size, const void *_buf)
{
    H5FD_sec2_t	*file = (H5FD_sec2_t *)_file;   /* Alias for file structure */
    unsigned u;                                 /* Local index variable */
    herr_t ret_value = SUCCEED;                 /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5FD_sec2_write)
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Called: addr = %a, size = %Zu\n", FUNC, addr, size);
#endif /* H5FD_SEC2_PAGE_DEBUG */

    HDassert(file && file->pub.cls);
    HDassert(_buf);

    /* Check for overflow conditions */
    if(!H5F_addr_defined(addr))
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "addr undefined, addr = %llu", (unsigned long long)addr)
    if(REGION_OVERFLOW(addr, size))
        HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL, "addr overflow, addr = %llu, size = %llu", (unsigned long long)addr, (unsigned long long)size)
    if((addr + size) > file->eoa)
        HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL, "addr overflow, addr = %llu, size = %llu, eoa = %llu", (unsigned long long)addr, (unsigned long long)size, (unsigned long long)file->eoa)

    /* Fulfill requests up to half the page cache from page buffers */
    if(size <= ((PAGE_SIZE * NPAGES) / 2)) {
        const unsigned char *buf = (const unsigned char *)_buf;

        /* Write request to page buffers */
        while(size > 0) {
            size_t page_off;        /* Offset within the page */
            size_t page_amt;        /* Amount to copy from page */
            hbool_t found_page = FALSE;

            /* Look for page that overlaps with start of buffer */
            for(u = 0; u < NPAGES; u++) {
                /* Does current page overlap with [current] beginning of buffer */
                if(H5F_addr_le(file->page_addr[u], addr) && H5F_addr_lt(addr, (file->page_addr[u] + PAGE_SIZE))) {
                    found_page = TRUE;
                    break;
                } /* end if */
            } /* end for */

            /* Did we find a page that fulfills [part of] request? */
            if(found_page) {    /* Yes - fulfill request */
                /* Sanity check */
                HDassert(u < NPAGES);

                /* Promote the page in the eviction scheme */
                if(H5FD_sec2_page_promote(file, u, &u) < 0)
                    HGOTO_ERROR(H5E_IO, H5E_CANTMOVE, FAIL, "can't promote page in eviction scheme")

#ifdef H5FD_SEC2_PAGE_STATS
                /* Increment # of hits */
                file->nhits++;
#endif /* H5FD_SEC2_PAGE_STATS */
            } /* end if */
            else {      /* No - bring new page into memory */
                /* Flush page to evict */
                if(H5FD_sec2_page_flush(file, NPAGES - 1) < 0)
                    HGOTO_ERROR(H5E_IO, H5E_CANTFLUSH, FAIL, "can't flush page to file")

#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: evicting page at file->page_addr[%u] = %a\n", FUNC, (NPAGES - 1), file->page_addr[(NPAGES - 1)]);
#endif /* H5FD_SEC2_PAGE_DEBUG */
                {
                    void *tmp_page = file->page[NPAGES - 1];

                    /* Move existing pages down in LRU scheme */
                    HDmemmove(&file->page[1], &file->page[0], (NPAGES - 1) * sizeof(void *));
                    HDmemmove(&file->page_addr[1], &file->page_addr[0], (NPAGES - 1) * sizeof(haddr_t));
                    HDmemmove(&file->page_dirty[1], &file->page_dirty[0], (NPAGES - 1) * sizeof(hbool_t));
                    file->page[0] = tmp_page;
                }

                /* Set page location */
                u = 0;

                /* Compute address of new page */
                file->page_addr[u] = (addr / PAGE_SIZE) * PAGE_SIZE;

#ifdef H5FD_SEC2_PAGE_STATS
                /* Increment # of misses */
                file->nmisses++;
#endif /* H5FD_SEC2_PAGE_STATS */
            } /* end else */

            /* Compute offset of info within page */
            page_off = (size_t)(addr - file->page_addr[u]);

            /* Compute amount to copy */
            if(size < (PAGE_SIZE - page_off))
                page_amt = size;
            else
                page_amt = (PAGE_SIZE - page_off);

            /* Check for partial page write of new page */
            if(!found_page && page_amt < PAGE_SIZE) {
                /* Read the existing page of data from the file */
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s:%u page read from file->page_addr[%u] = %a\n", FUNC, __LINE__, u, file->page_addr[u]);
#endif /* H5FD_SEC2_PAGE_DEBUG */
                if(H5FD_sec2_read_real(file, file->page_addr[u], PAGE_SIZE, file->page[u]) < 0)
                    HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL, "can't read page from file")
            } /* end if */

            /* Copy data from buffer into page */
            HDmemcpy(((unsigned char *)file->page[u] + page_off), buf, page_amt);

            /* Mark page dirty */
            file->page_dirty[u] = TRUE;

            /* Increment buffer counter & decrement amount left */
            buf += page_amt;
            size -= page_amt;
            addr += page_amt;
        } /* end while */
    } /* end if */
    else {
        haddr_t end = addr + size;      /* End of buffer to write */

        /* Sanity check */
        HDassert((end - addr) > PAGE_SIZE);

#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Large write bypassing cache\n", FUNC);
#endif /* H5FD_SEC2_PAGE_DEBUG */
#ifdef H5FD_SEC2_PAGE_STATS
        /* Increment # of page buffer bypasses */
        file->nbypasses++;
#endif /* H5FD_SEC2_PAGE_STATS */

        /* Check to see if large write intersects with any of the pages */
        for(u = 0; u < NPAGES; u++) {
retry:
            /* Check for valid page */
            if(H5F_addr_defined(file->page_addr[u])) {
                haddr_t page_end = file->page_addr[u] + PAGE_SIZE;  /* End of current page */

#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: page[%u] at: %a\n", FUNC, u, file->page_addr[u]);
#endif /* H5FD_SEC2_PAGE_DEBUG */
                /* Does buffer intersect current page */
                if((H5F_addr_le(addr, file->page_addr[u]) && H5F_addr_gt(end, file->page_addr[u]))
                        || (H5F_addr_lt(addr, page_end) && H5F_addr_ge(end, page_end))) {
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Large write intersects with page at: %a\n", FUNC, file->page_addr[u]);
#endif /* H5FD_SEC2_PAGE_DEBUG */

                    /* Check if large write invalidates entire page */
                    if(H5F_addr_le(addr, file->page_addr[u]) && H5F_addr_gt(end, page_end)) {
                        /* Remove page */
                        if(H5FD_sec2_page_remove(file, u) < 0)
                            HGOTO_ERROR(H5E_IO, H5E_CANTREMOVE, FAIL, "can't remove page from cache")

                        /* Re-try, with updated page information */
                        goto retry;
                    } /* end if */
                    else {
                        size_t page_off;    /* Offset of overlap within the page */
                        size_t buf_off;     /* Offset of overlap within the buffer */
                        size_t overlap;     /* Length of overlap within the page */
                        
                        /* Check for overlapping the beginning of the page */
                        if(H5F_addr_le(addr, file->page_addr[u]) && H5F_addr_gt(end, file->page_addr[u])) {
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Partial update of page at %a - overlap beginning of page\n", FUNC, file->page_addr[u]);
#endif /* H5FD_SEC2_PAGE_DEBUG */
                            /* Set up offsets & lengths */
                            page_off = (size_t)0;
                            buf_off = (size_t)(file->page_addr[u] - addr);
                            overlap = (size_t)(end - file->page_addr[u]);
                        } /* end if */
                        else {
                            /* Sanity check */
                            HDassert(H5F_addr_lt(addr, page_end) && H5F_addr_ge(end, page_end));
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Partial update of page at %a - overlap end of page\n", FUNC, file->page_addr[u]);
#endif /* H5FD_SEC2_PAGE_DEBUG */
                            /* Set up offsets & lengths */
                            page_off = (size_t)(addr - file->page_addr[u]);
                            buf_off = (size_t)0;
                            overlap = (size_t)(page_end - addr);
                        } /* end else */
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: page_off = %Zu, buf_off = %Zu, overlap = %Zu\n", FUNC, page_off, buf_off, overlap);
#endif /* H5FD_SEC2_PAGE_DEBUG */

#ifdef QAK
                        /* Promote the page in the eviction scheme */
                        if(H5FD_sec2_page_promote(file, u, &u) < 0)
                            HGOTO_ERROR(H5E_IO, H5E_CANTMOVE, FAIL, "can't promote page in eviction scheme")
#endif /* QAK */

                        /* Update page with information from buffer */
                        HDmemcpy((unsigned char *)file->page[u] + page_off, (const unsigned char *)_buf + buf_off, overlap);

                        /* Note that we _don't_ Mark page as dirty here:
                         * if the page was already dirty, we haven't changed
                         * anything, and if the page was already clean, it doesn't
                         * need to be marked as dirty, since its data will be
                         * in sync with what's in the file. -QAK
                         */
                    } /* end else */
                } /* end if */
            } /* end if */
        } /* end for */

        /* Read data */
        if(H5FD_sec2_write_real(file, addr, size, _buf) < 0)
            HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL, "can't write data to file")

        /* Advance address offset, for correct eof update */
        addr += size;
    } /* end else */

    /* Update eof */
    if(addr > file->eof)
        file->eof = addr;

done:
#ifdef H5FD_SEC2_PAGE_DEBUG
HDfprintf(stderr, "%s: Leaving, file->eof = %a\n", FUNC, file->eof);
#endif /* H5FD_SEC2_PAGE_DEBUG */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_write() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_flush
 *
 * Purpose:	Flush any cached data
 *
 * Return:	Success:	Non-negative
 *
 *		Failure:	Negative
 *
 * Programmer:	Quincey Koziol
 *              Monday, November 22, 2010
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD_sec2_flush(H5FD_t *_file, hid_t UNUSED dxpl_id, unsigned UNUSED closing)
{
    H5FD_sec2_t	*file = (H5FD_sec2_t*)_file;
    herr_t ret_value = SUCCEED;                 /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5FD_sec2_flush)

    /* Sanity check */
    HDassert(file);

/* Release page buffering information */
{
    unsigned u;

    /* Flush any dirty pages */
    for(u = 0; u < NPAGES; u++) {
        /* Flush page */
        if(H5FD_sec2_page_flush(file, u) < 0)
            HGOTO_ERROR(H5E_IO, H5E_CANTFLUSH, FAIL, "can't flush page to file")
    } /* end for */
}

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_flush() */


/*-------------------------------------------------------------------------
 * Function:	H5FD_sec2_truncate
 *
 * Purpose:	Makes sure that the true file size is the same (or larger)
 *		than the end-of-address.
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative
 *
 * Programmer:	Robb Matzke
 *              Wednesday, August  4, 1999
 *
 *-------------------------------------------------------------------------
 */
/* ARGSUSED */
static herr_t
H5FD_sec2_truncate(H5FD_t *_file, hid_t UNUSED dxpl_id, hbool_t UNUSED closing)
{
    H5FD_sec2_t *file = (H5FD_sec2_t *)_file;
    herr_t ret_value = SUCCEED;                 /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5FD_sec2_truncate)

    HDassert(file);

    /* Extend the file to make sure it's large enough */
    if(!H5F_addr_eq(file->eoa, file->eof)) {
#ifdef _WIN32
        HFILE filehandle;   /* Windows file handle */
        LARGE_INTEGER li;   /* 64-bit integer for SetFilePointer() call */

        /* Map the posix file handle to a Windows file handle */
        filehandle = _get_osfhandle(file->fd);

        /* Translate 64-bit integers into form Windows wants */
        /* [This algorithm is from the Windows documentation for SetFilePointer()] */
        li.QuadPart = (LONGLONG)file->eoa;
        (void)SetFilePointer((HANDLE)filehandle, li.LowPart, &li.HighPart, FILE_BEGIN);
        if(SetEndOfFile((HANDLE)filehandle) == 0)
            HGOTO_ERROR(H5E_IO, H5E_SEEKERROR, FAIL, "unable to extend file properly")
#else /* _WIN32 */
#ifdef H5_VMS
        /* Reset seek offset to the beginning of the file, so that the file isn't
         * re-extended later.  This may happen on Open VMS. */
        if(-1 == HDlseek(file->fd, (HDoff_t)0, SEEK_SET))
            HSYS_GOTO_ERROR(H5E_IO, H5E_SEEKERROR, FAIL, "unable to seek to proper position")
#endif

        if(-1 == HDftruncate(file->fd, (HDoff_t)file->eoa))
            HSYS_GOTO_ERROR(H5E_IO, H5E_SEEKERROR, FAIL, "unable to extend file properly")
#endif /* _WIN32 */

        /* Update the eof value */
        file->eof = file->eoa;

{
    unsigned u;

    /* Remove pages that are now after the end of the file */
    for(u = 0; u < NPAGES; u++) {
        while(H5F_addr_defined(file->page_addr[u]) && file->page_addr[u] >= file->eof) {
            /* Remove page */
            if(H5FD_sec2_page_remove(file, u) < 0)
                HGOTO_ERROR(H5E_IO, H5E_CANTREMOVE, FAIL, "can't remove page from cache")
        } /* end if */
    } /* end for */
}
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_truncate() */

