/*
 * opencl_common.h
 *
 * A common header for OpenCL device code
 * --
 * Copyright 2011-2014 (C) KaiGai Kohei <kaigai@kaigai.gr.jp>
 * Copyright 2014 (C) The PG-Strom Development Team
 *
 * This software is an extension of PostgreSQL; You can use, copy,
 * modify or distribute it under the terms of 'LICENSE' included
 * within this package.
 */
#ifndef OPENCL_COMMON_H
#define OPENCL_COMMON_H

/*
 * OpenCL intermediator always adds -DOPENCL_DEVICE_CODE on kernel build,
 * but not for the host code, so this #if ... #endif block is available
 * only OpenCL device code.
 */
#ifdef OPENCL_DEVICE_CODE
#pragma OPENCL EXTENSION cl_khr_fp64 : enable

/* NULL definition */
#define NULL	((void *) 0UL)

/* Misc definitions */
#define FLEXIBLE_ARRAY_MEMBER
#define offsetof(TYPE, FIELD)   ((uintptr_t) &((TYPE *)0)->FIELD)
#define lengthof(ARRAY)			(sizeof(ARRAY) / sizeof((ARRAY)[0]))

/* basic type definitions */
typedef bool		cl_bool;
typedef char		cl_char;
typedef uchar		cl_uchar;
typedef short		cl_short;
typedef ushort		cl_ushort;
typedef int			cl_int;
typedef uint		cl_uint;
typedef long		cl_long;
typedef ulong		cl_ulong;
typedef float		cl_float;
typedef double		cl_double;
#if HOSTPTRLEN == 8
typedef cl_ulong	hostptr_t;
#elif HOSTPTRLEN == 4
typedef cl_uint		hostptr_t;
#else
#error unexpected host pointer length
#endif	/* HOSTPTRLEN */

/*
 * Alignment macros
 */
#define TYPEALIGN(ALIGNVAL,LEN)	\
	(((uintptr_t) (LEN) + ((ALIGNVAL) - 1)) & ~((uintptr_t) ((ALIGNVAL) - 1)))

/*
 * Simplified varlena support.
 *
 * Unlike host code, device code cannot touch external and/or compressed
 * toast datum. All the format device code can understand is usual
 * in-memory form; 4-bytes length is put on the head and contents follows.
 * So, it is a responsibility of host code to decompress the toast values
 * if device code may access compressed varlena.
 * In case when device code touches unsupported format, calculation result
 * shall be postponed to calculate on the host side.
 *
 * Note that it is harmless to have external and/or compressed toast datam
 * unless it is NOT referenced in the device code. It can understand the
 * length of these values, unlike contents.
 */
typedef struct {
	int		vl_len;
	char	vl_dat[1];
} varlena;
#define VARHDRSZ			((int) sizeof(cl_int))
#define VARDATA(PTR)		(((varlena *)(PTR))->vl_dat)
#define VARSIZE(PTR)		(((varlena *)(PTR))->vl_len)
#define VARSIZE_EXHDR(PTR)	(VARSIZE(PTR) - VARHDRSZ)


#define VARATT_IS_4B(PTR) \
	((((varattrib_1b *) (PTR))->va_header & 0x01) == 0x00)
#define VARATT_IS_4B_U(PTR) \
	((((varattrib_1b *) (PTR))->va_header & 0x03) == 0x00)
#define VARATT_IS_4B_C(PTR) \
	((((varattrib_1b *) (PTR))->va_header & 0x03) == 0x02)
#define VARATT_IS_1B(PTR) \
	((((varattrib_1b *) (PTR))->va_header & 0x01) == 0x01)
#define VARATT_IS_1B_E(PTR) \
	((((varattrib_1b *) (PTR))->va_header) == 0x01)
#define VARATT_NOT_PAD_BYTE(PTR) \
	(*((cl_uchar *) (PTR)) != 0)

#define VARSIZE_ANY(PTR)							\
	(VARATT_IS_1B_E(PTR) ? VARSIZE_EXTERNAL(PTR) :	\
	 (VARATT_IS_1B(PTR) ? VARSIZE_1B(PTR) :			\
	  VARSIZE_4B(PTR)))

#else	/* OPENCL_DEVICE_CODE */
#include "access/htup_details.h"
#include "storage/itemptr.h"
#define __global	/* address space qualifier is noise on host */
#endif

/*
 * Error code definition
 */
#define StromError_Success				0	/* OK */
#define StromError_RowFiltered			1	/* Row-clause was false */
#define StromError_RowReCheck			2	/* To be checked on the host */
#define StromError_ServerNotReady		100	/* OpenCL server is not ready */
#define StromError_BadRequestMessage	101	/* Bad request message */
#define StromError_OpenCLInternal		102	/* OpenCL internal error */
#define StromError_OutOfSharedMemory	105	/* out of shared memory */
#define StromError_DivisionByZero		200	/* Division by zero */

/* significant error; that abort transaction on the host code */
#define StromErrorIsSignificant(errcode)	((errcode) >= 100 || (errcode) < 0)

/*
 * kern_parambuf
 *
 * Const and Parameter buffer. It stores constant values during a particular
 * scan, so it may make sense if it is obvious length of kern_parambuf is
 * less than constant memory (NOTE: not implemented yet).
 */
typedef struct {
	cl_uint		length;		/* total length of parambuf */
	cl_uint		nparams;	/* number of parameters */
	cl_uint		poffset[FLEXIBLE_ARRAY_MEMBER];	/* offset of params */
} kern_parambuf;

/*
 * kern_resultbuf
 *
 * Output buffer to write back calculation results on a parciular chunk.
 * 'errcode' informs a significant error that shall raise an error on
 * host side and abort transactions. 'results' informs row-level status.
 */
typedef struct {
	cl_uint		nitems;		/* number of results being written */
	cl_int		errcode;	/* chunk-level error */
	cl_int		results[FLEXIBLE_ARRAY_MEMBER];
} kern_resultbuf;

/*
 * Data type definitions for row oriented data format
 * ---------------------------------------------------
 */
#ifdef OPENCL_DEVICE_CODE
/*
 * we need to e-define HeapTupleData and HeapTupleHeaderData and
 * t_infomask related stuff
 */
typedef struct {
	struct {
		cl_ushort	bi_hi;
		cl_ushort	bi_lo;
	} ip_blkid;
	cl_ushort		ip_posid;
} ItemPointerData;

typedef struct HeapTupleData {
	cl_uint			t_len;
	ItemPointerData	t_self;
	cl_uint			t_tableOid;
	hostptr_t		t_data;		/* !HOSTONLY! pointer to htup on the host */
} HeapTupleData;

typedef struct {
	union {
		struct {
			cl_uint	t_xmin;		/* inserting xact ID */
			cl_uint	t_xmax;		/* deleting or locking xact ID */
			union {
				cl_uint	t_cid;	/* inserting or deleting command ID, or both */
				cl_uint	t_xvac;	/* old-style VACUUM FULL xact ID */
			} t_field3;
		} t_heap;
		struct {
			cl_uint	datum_len_;	/* varlena header (do not touch directly!) */
			cl_uint	datum_typmod;	/* -1, or identifier of a record type */
			cl_uint	datum_typeid;	/* composite type OID, or RECORDOID */
		} t_datum;
	} t_choice;

	ItemPointerData	t_ctid;			/* current TID of this or newer tuple */

	cl_ushort		t_infomask2;	/* number of attributes + various flags */
	cl_ushort		t_infomask;		/* various flag bits, see below */
	cl_uchar		t_hoff;			/* sizeof header incl. bitmap, padding */
	/* ^ - 23 bytes - ^ */
	cl_uchar		t_bits[1];		/* bitmap of NULLs -- VARIABLE LENGTH */
} HeapTupleHeaderData;

#define att_isnull(ATT, BITS) (!((BITS)[(ATT) >> 3] & (1 << ((ATT) & 0x07))))

/*
 * information stored in t_infomask:
 */
#define HEAP_HASNULL			0x0001	/* has null attribute(s) */
#define HEAP_HASVARWIDTH		0x0002	/* has variable-width attribute(s) */
#define HEAP_HASEXTERNAL		0x0004	/* has external stored attribute(s) */
#define HEAP_HASOID				0x0008	/* has an object-id field */
#define HEAP_XMAX_KEYSHR_LOCK	0x0010	/* xmax is a key-shared locker */
#define HEAP_COMBOCID			0x0020	/* t_cid is a combo cid */
#define HEAP_XMAX_EXCL_LOCK		0x0040	/* xmax is exclusive locker */
#define HEAP_XMAX_LOCK_ONLY		0x0080	/* xmax, if valid, is only a locker */

#endif

/*
 * alignment for pg-strom
 */
#define STROMALIGN_LEN			16
#define STROMALIGN(LEN)			TYPEALIGN(STROMALIGN_LEN,LEN)
#define STROMALIGN_DOWN(LEN)	TYPEALIGN_DOWN(STROMALIGN_LEN,LEN)

/*
 * kern_colmeta
 *
 * It stores metadata of columns being on row-store because tuple with NULL
 * values does not have always constant 
 */
#define KERN_COLMETA_ATTNOTNULL			0x01
#define KERN_COLMETA_ATTREFERENCED		0x02
typedef struct {
	/* set of KERN_COLMETA_* flags */
	cl_uchar		flags;
	/* alignment; 1,2,4 or 8, not characters in pg_attribute */
	cl_char			attalign;
	/* length of attribute */
	cl_short		attlen;
	/* offset to null-map and column-array from the head of column-store */
	cl_uint			cs_ofs;
} kern_colmeta;

/*
 * kern_row_store
 *
 * It stores records in row-format.
 *
 * +-----------------+
 * | length          |
 * +-----------------+ o--+
 * | ncols (= M)     |    | The array of tuple offset begins from colmeta[N].
 * +-----------------+    | It points a particular variable length region 
 * | nrows (= N)     |    | from the tail.
 * +-----------------+    |
 * | colmeta[0]      |    |
 * | colmeta[1]      |    |
 * |    :            |    |
 * | colmeta[M-1]    |    |
 * +-----------------+ <--+
 * | tuples[0]       |
 * | tuples[1]       |
 * | tuples[2] o----------+ offset from the head of this row-store
 * |    :            |    |
 * | tuples[N-1]     |    |
 * +-----------------+    |
 * |      :          |    |
 * | free area       |    |
 * |      :          |    |
 * +-----------------+ <------ current usage of this row-store
 * | (N-1)th rs_tuple|    |
 * +-----------------+    |
 * |      :          |    |
 * |      :          |    |
 * +-----------------+ <--+
 * | 2nd rs_tuple    |
 * +-----------------+
 * | 1st rs_tuple    |
 * +-----------------+
 * |      :          |
 * | 0th rs_tuple    |
 * |      :          |
 * +-----------------+
 */
typedef struct {
	cl_uint			length;	/* length of this kernel row_store */
	cl_uint			ncols;	/* number of columns in the source relation */
	cl_uint			nrows;	/* number of rows in this store */
	kern_colmeta	colmeta[FLEXIBLE_ARRAY_MEMBER];	/* metadata of columns */
} kern_row_store;

/*
 * rs_tuple
 *
 * HeapTuple representation in row-store. Even though most of metadata in
 * the HeapTupleData / HeapTupleHeaderData are not used in device kernel,
 * we put them together because it enables to avoid tuple re-construction
 * on the host side that has limited computing power.
 */
typedef struct {
	HeapTupleData		htup;
	HeapTupleHeaderData	data;
} rs_tuple;

static inline __global rs_tuple *
kern_rowstore_get_tuple(__global kern_row_store *krstore, cl_uint rindex)
{
	cl_uint	   *p_offset;

	if (rindex >= krstore->nrows)
		return NULL;
	p_offset = (cl_uint *)(&krstore->colmeta[krstore->ncols]);
	if (p_offset[rindex] == 0)
		return NULL;
	return (__global rs_tuple *)((uintptr_t)krstore + p_offset[rindex]);
}

/*
 * Data type definitions for column oriented data format
 * ---------------------------------------------------
 */

/*
 * kern_column_store
 *
 * It stores arrays in column-format
 * +-----------------+
 * | length          |
 * +-----------------+
 * | ncols (=M)      |
 * +-----------------+
 * | nrows (=N)      |
 * +-----------------+
 * | colmeta[0]      |
 * | colmeta[1]   o-------+ colmeta[j].cs_ofs points an offset of the column-
 * |    :            |    | array in this store.
 * | colmeta[M-1]    |    |
 * +-----------------+    | (char *)(kcs) + colmeta[j].cs_ofs points is
 * | column array    |    | the address of column array.
 * | for column-0    |    |
 * +-----------------+ <--+
 * | +---------------|
 * | | Nulls map     | If colmeta[j].atthasnull is TRUE, a bitmap shall be
 * | |               | put in front of the column array. Its length is aligned
 * | +---------------| to STROMALIGN_LEN
 * | | array of      |
 * | | column-1      |
 * | |               |
 * +-+---------------+
 * |      :          |
 * |      :          |
 * +-----------------+
 * | column array    |
 * | for column-(M-1)|
 * +-----------------+
 */
typedef struct {
	cl_uint			length;	/* length of this kernel column-store */
	cl_uint			ncols;	/* number of columns in this store */
	cl_uint			nrows;  /* number of records in this store */
	kern_colmeta	colmeta[FLEXIBLE_ARRAY_MEMBER]; /* metadata of columns */
} kern_column_store;

/*
 * kern_toastbuf
 *
 * The kernel toast buffer has number of columns and per-column directory
 * in its header region. The per-column directory points the starting offset
 * from the head of kern_toastbuf.
 *
 * +--------------+
 * | magic        | magic number; a value that should not be a length of
 * +--------------+ other buffer object (like, row- or column-store)
 * | ncols        | number of columns in this buffer
 * +--------------+
 * | coldir[0]    |
 * | coldir[1]  o-------+ In case when a varlena reference (offset=120) of
 * |   :          |     | column-1, it has to reference coldir[1] to get
 * | coldir[N-1]  |     | offset of per-column varlena buffer.
 * +--------------+     | Then, it adds per-datum offset to reach the
 * |   :          |     | address of variable.
 * |   :          |     |
 * +--------------+  <--+
 * |   :          |  )
 * +--------------+  +120
 * |'Hello!'      |
 * +--------------+
 * |   :          |
 * |   :          |
 * +--------------+
 */
#define TOASTBUF_MAGIC		0xffffffff	/* should not be length of buffers */

typedef struct {
	cl_uint			magic;	/* = TOASTBUF_MAGIC */
	cl_uint			ncols;
	cl_uint			coldir[FLEXIBLE_ARRAY_MEMBER];
} kern_toastbuf;

#ifdef OPENCL_DEVICE_CODE

/* template for native types */
#define STROMCL_SIMPLE_DATATYPE_TEMPLATE(NAME,BASE)	\
	typedef struct {								\
		BASE	value;								\
		bool	isnull;								\
	} pg_##NAME##_t;

#define STROMCL_VARLENA_DATATYPE_TEMPLACE(NAME)		\
	STROMCL_SIMPLE_DATATYPE_TEMPLATE(NAME, __global varlena *)

#define STROMCL_SIMPLE_VARREF_TEMPLATE(NAME,BASE)			\
	static pg_##NAME##_t									\
	pg_##NAME##_vref(__global kern_column_store *kcs,		\
					 cl_uint colidx,						\
					 cl_uint rowidx)						\
	{														\
		pg_##NAME##_t result;								\
		__global BASE *addr									\
			= kern_get_datum(kcs,colidx,rowidx);			\
															\
		if (!addr)											\
			result.isnull = true;							\
		else												\
		{													\
			result.isnull = false;							\
			result.value = *addr;							\
		}													\
		return result;										\
	}

#define STROMCL_VARLENA_VARREF_TEMPLATE(NAME)				\
	static pg_##NAME##_t									\
	pg_##NAME##_vref(__global kern_column_store *kcs,		\
					 __global kern_toastbuf *toast,			\
					 cl_uint colidx,						\
					 cl_uint rowidx)						\
	{														\
		pg_##NAME##_t result;								\
		__global cl_uint *p_offset							\
			= kern_get_datum(kcs,colidx,rowidx);			\
															\
		if (!vl_offset)										\
			result.isnull = true;							\
		else												\
		{													\
			cl_uint	offset = *p_offset;						\
															\
			if (toast->magic == TOASTBUF_MAGIC)				\
				offset += toast->coldir[colidx];			\
			result.isnull = false;							\
			result.value = ((__global varlena *)			\
							((uintptr_t)toast + offset));	\
		}													\
		return result;										\
	}

#define STROMCL_SIMPLE_PARAMREF_TEMPLATE(NAME,BASE)			\
	static pg_##NAME##_t									\
	pg_##NAME##_param(__global kern_parambuf *kparam,		\
					  cl_uint param_id)						\
	{														\
		pg_##NAME##_t result;								\
		__global BASE *addr;								\
															\
		if (param_id < kparam->nparams &&					\
			kparam->poffset[param_id] > 0)					\
		{													\
			result.value = *((__global BASE *)				\
							 ((uintptr_t)kparam +			\
							  kparam->poffset[param_id]));	\
			result.isnull = false;							\
		}													\
		else												\
			result.isnull = true;							\
															\
		return result;										\
	}

#define STROMCL_VARLENA_PARAMREF_TEMPLATE(NAME)				\
	static pg_##NAME##_t									\
	pg_##NAME##_param(__global kern_parambuf *kparam,		\
					  cl_uint param_id)						\
	{														\
		pg_##NAME##_t result;								\
		__global varlena *addr;								\
															\
		if (param_id < kparam->nparams &&					\
			kparam->poffset[param_id] > 0)					\
		{													\
			result.value = (__global varlena *)				\
				((uintptr_t)kparam + kparam->poffset[param_id]); \
			result.isnull = false;							\
		}													\
		else												\
			result.isnull = true;							\
															\
		return result;										\
	}

#define STROMCL_SIMPLE_TYPE_TEMPLATE(NAME,BASE)		\
	STROMCL_SIMPLE_DATATYPE_TEMPLATE(NAME,BASE)		\
	STROMCL_SIMPLE_VARREF_TEMPLATE(NAME,BASE)		\
	STROMCL_SIMPLE_PARAMREF_TEMPLATE(NAME,BASE)

#define STROMCL_VRALENA_TYPE_TEMPLATE(NAME)			\
	STROMCL_VARLENA_DATATYPE_TEMPLATE(NAME)			\
	STROMCL_VARLENA_VARREF_TEMPLATE(NAME)			\
	STROMCL_VARLENA_PARAMREF_TEMPLATE(NAME)


/*
 * Common function to translate a row-store into column-store.
 *
 * The kern_row_to_column() translates records on the supplied row-store
 * into columns array on the supplied column-store. 
 * Data format using HeapTuple takes unignorable cost if we try to calculate
 * offset of the referenced value from head of the tuple for each reference.
 * Our column-oriented format does not take such a limitation, so our logic
 * prefers column-store anyway.
 * You need to pay attention three prerequisites of this function.
 * 1. The caller must set up header portion of kern_column_store prior to
 *    kernel invocation. This function assumes each kern_colmeta entry has
 *    appropriate offset from the head.
 * 2. The caller must allocate sizeof(cl_uchar) * get_local_size(0) bytes
 *    of local memory, and gives it as the last argument. Null-bitmap takes
 *    bit-operation depending on the neighbor thread.
 * 3. Local work-group size has to be multiplexer of 8.
 */
static void
kern_row_to_column(__global kern_row_store *krs,
				   __global kern_column_store *kcs,
				   cl_uint ncols,
				   __constant cl_ushort *attnums,
				   __local cl_char *workbuf)
{
	__global rs_tuple  *rs_tup;
	size_t		local_id = get_local_id(0);
	cl_uint		maxatt = attnums[ncols - 1];
	cl_uint		offset;
	cl_uint		i, j;

	/* fetch a rs_tuple on the row-store */
	rs_tup = kern_rowstore_get_tuple(krs, get_global_id(0));

	offset = rs_tup->data.t_hoff;
	for (i=0, j=0; i < maxatt; i++)
	{
		cl_bool	isnull;

		if (!rs_tup || ((rs_tup->data.t_infomask & HEAP_HASNULL) != 0 &&
						att_isnull(i, rs_tup->data.t_bits)))
			isnull = true;
		else
		{
			__global kern_colmeta  *colmeta = &krs->colmeta[i];

			isnull = false;
			if (colmeta->attlen > 0)
				offset = TYPEALIGN(colmeta->attalign, offset);
			else if (VARATT_NOT_PAD_BYTE((char *)&rs_tup->data + offset))
				offset = TYPEALIGN(colmeta->attalign, offset);

			if (i == attnums[j])
			{
				__global kern_colmeta *colmeta = &kcs->colmeta[j];
				__global char  *src;
				__global char  *dest;

				src = ((__global char *)&rs_tup->data) + offset;
				dest = ((__global char *)kcs) + colmeta->cs_ofs;
				/* adjust destination address by nullmap, if needed */
				if ((colmeta->flags & KERN_COLMETA_ATTNOTNULL) == 0)
					dest += STROMALIGN((get_global_size(0) + 7) / 8);
				/* adjust destination address for exact slot on column-array */
				dest += get_global_id(0) * (colmeta->attlen > 0
											? colmeta->attlen
											: sizeof(cl_uint));
				/*
				 * Copy a datum from a field of rs_tuple into column-array
				 * of kern_column_store. In case of variable length-field,
				 * column-array will have offset to body of the variable
				 * length field in the toast buffer. The source row-store
				 * will also perform as a toast buffer after the translation.
				 *
				 * NOTE: Also note that we assume fixed length variable has
				 * 1, 2, 4, 8 or 16-bytes length. Elsewhere, it should be
				 * a variable length field.
				 */
				switch (colmeta->attlen)
				{
					case 1:
						*((cl_char *)dest) = *((cl_char *)src);
						break;
					case 2:
						*((cl_short *)dest) = *((cl_short *)src);
						break;
					case 4:
						*((cl_int *)dest) = *((cl_int *)src);
						break;
					case 8:
						*((cl_long *)dest) = *((cl_long *)src);
						break;
					case 16:
						*((cl_long *)dest) = *((cl_long *)src);
						*(((cl_long *)dest) + 1) = *(((cl_long *)src) + 1);
						break;
					default:
						*((cl_uint *)dest) = (cl_uint)((uintptr_t)src -
													   (uintptr_t)rs_tup);
						break;
				}
			}
		}

		/*
		 * Calculation of nullmap if this column is the target to be moved.
		 * Because it takes per bit operation using interaction with neighbor
		 * work-item, we use local working memory for reduction.
		 */
		if (i == attnums[j])
		{
			__global kern_colmeta *colmeta = &kcs->colmeta[j];
			__global cl_uchar *p_nullmap;

			if ((colmeta->flags & KERN_COLMETA_ATTNOTNULL) == 0)
			{
				workbuf[local_id]
					= (isnull ? (1 << (local_id & 0x07)) : 0);
				barrier(CLK_LOCAL_MEM_FENCE);
				if ((local_id & 0x01) == 0)
					workbuf[local_id] |= workbuf[local_id + 1];
				barrier(CLK_LOCAL_MEM_FENCE);
				if ((local_id & 0x03) == 0)
					workbuf[local_id] |= workbuf[local_id + 2];
				barrier(CLK_LOCAL_MEM_FENCE);
				if ((local_id & 0x07) == 0)
					workbuf[local_id] |= workbuf[local_id + 4];
				barrier(CLK_LOCAL_MEM_FENCE);
				/* put a nullmap */
				if ((local_id & 0x07) == 0 && get_global_id(0) < krs->nrows)
				{
					*((cl_uchar *)kcs +
					  colmeta->cs_ofs +
					  (get_global_id(0) >> 3)) = workbuf[local_id];
				}
			}
			j++;
		}
	}
}

/*
 * kern_get_datum
 *
 * Reference to a particular datum on the supplied column store.
 * It returns NULL, If it is a null-value in context of SQL. Elsewhere,
 * it returns a pointer towards global memory.
 */
static __global void *
kern_get_datum(__global kern_column_store *kcs,
			   cl_uint colidx,
			   cl_uint rowidx)
{
	__global kern_colmeta *colmeta;
	cl_uint		offset;

	if (colidx >= kcs->ncols || rowidx >= kcs->nrows)
		return NULL;

	colmeta = &kcs->colmeta[colidx];
	offset = colmeta->cs_ofs;
	if ((colmeta->flags & KERN_COLMETA_ATTNOTNULL) == 0)
	{
		__global cl_uchar  *p_nullmap = (__global cl_uchar *)kcs + offset;

		if ((p_nullmap[rowidx >> 3] & (1 << (rowidx & 0x07))) != 0)
			return NULL;
		offset += STROMALIGN((kcs->nrows + 7) >> 3);
	}

	if (colmeta->attlen > 0)
		offset += colmeta->attlen * rowidx;
	else
		offset += sizeof(cl_uint) * rowidx;

	return (__global void *)((uintptr_t)kcs + offset);
}

/* ------------------------------------------------------------
 *
 * Declarations of common built-in types and functions
 *
 * ------------------------------------------------------------
 */

/*
 * pg_bool_t is built-in data type
 */
STROMCL_SIMPLE_TYPE_TEMPLATE(bool, bool)

/*
 * Functions for BooleanTest
 */
static inline pg_bool_t
pgfn_bool_is_true(pg_bool_t result)
{
	result.value = (!result.isnull && result.value);
	result.isnull = false;
	return result;
}

static inline pg_bool_t
pgfn_bool_is_not_true(pg_bool_t result)
{
	result.value = (result.isnull || !result.value);
	result.isnull = false;
	return result;
}

static inline pg_bool_t
pgfn_bool_is_false(pg_bool_t result)
{
	result.value = (!result.isnull && !result.value);
	result.isnull = false;
	return result;
}

static inline pg_bool_t
pgfn_bool_is_not_false(pg_bool_t result)
{
	result.value = (result.isnull || result.value);
	result.isnull = false;
	return result;
}

static inline pg_bool_t
pgfn_bool_is_unknown(pg_bool_t result)
{
	result.value = result.isnull;
	result.isnull = false;
	return result;
}

static inline pg_bool_t
pgfn_bool_is_not_unknown(pg_bool_t result)
{
	result.value = !result.isnull;
	result.isnull = false;
	return result;
}

/*
 * Functions for BoolOp (EXPR_AND and EXPR_OR shall be constructed on demand)
 */
static inline pg_bool_t
pgfn_boolop_not(pg_bool_t result)
{
	result.value = !result.value;
	/* if null is given, result is also null */
	return result;
}

#endif	/* OPENCL_DEVICE_CODE */
#endif	/* OPENCL_COMMON_H */
