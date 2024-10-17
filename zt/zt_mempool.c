#include "ztlib.h"

#define INT64CONST(x)  (x##LL)
#define UINT64CONST(x) (x##ULL)

/*
 * Flags for pg_malloc_extended and palloc_extended, deliberately named
 * the same as the backend flags.
 */
#define MCXT_ALLOC_HUGE			0x01	/* allow huge allocation (> 1 GB) not
										 * actually used for frontends */
#define MCXT_ALLOC_NO_OOM		0x02	/* no failure if out-of-memory */
#define MCXT_ALLOC_ZERO			0x04	/* zero allocated memory */

/*
 * MaxAllocSize, MaxAllocHugeSize
 *		Quasi-arbitrary limits on size of allocations.
 *
 * Note:
 *		There is no guarantee that smaller allocations will succeed, but
 *		larger requests will be summarily denied.
 *
 * palloc() enforces MaxAllocSize, chosen to correspond to the limiting size
 * of varlena objects under TOAST.  See VARSIZE_4B() and related macros in
 * postgres.h.  Many datatypes assume that any allocatable size can be
 * represented in a varlena header.  This limit also permits a caller to use
 * an "int" variable for an index into or length of an allocation.  Callers
 * careful to avoid these hazards can access the higher limit with
 * MemoryContextAllocHuge().  Both limits permit code to assume that it may
 * compute twice an allocation's size without overflow.
 */
#define MaxAllocSize	((Size) 0x3fffffff) /* 1 gigabyte - 1 */

#define AllocSizeIsValid(size)	((Size) (size) <= MaxAllocSize)

										  /* Must be less than SIZE_MAX */
#define MaxAllocHugeSize	(SIZE_MAX / 2)

#define InvalidAllocSize	SIZE_MAX

#define AllocHugeSizeIsValid(size)	((Size) (size) <= MaxAllocHugeSize)

/*
 * PointerIsValid
 *		True iff pointer is valid.
 */
#define PointerIsValid(pointer) ((const void*)(pointer) != NULL)

/*
 * Max
 *		Return the maximum of two numbers.
 */
#define MaxPG(x, y)		((x) > (y) ? (x) : (y))

 /*
  * Min
  *		Return the minimum of two numbers.
  */
#define MinPG(x, y)		((x) < (y) ? (x) : (y))

/* Get a bit mask of the bits set in non-long aligned addresses */
#define LONG_ALIGN_MASK (sizeof(long) - 1)

#define MEMSET_LOOP_LIMIT 1024

/*
 * MemSetAligned is the same as MemSet except it omits the test to see if
 * "start" is word-aligned.  This is okay to use if the caller knows a-priori
 * that the pointer is suitably aligned (typically, because he just got it
 * from palloc(), which always delivers a max-aligned pointer).
 */
#define MemSetAligned(start, val, len) \
	do \
	{ \
		long   *_start = (long *) (start); \
		int		_val = (val); \
		Size	_len = (len); \
\
		if ((_len & LONG_ALIGN_MASK) == 0 && \
			_val == 0 && \
			_len <= MEMSET_LOOP_LIMIT && \
			MEMSET_LOOP_LIMIT != 0) \
		{ \
			long *_stop = (long *) ((char *) _start + _len); \
			while (_start < _stop) \
				*_start++ = 0; \
		} \
		else \
			memset(_start, _val, _len); \
	} while (0)

/*
 * Hints to the compiler about the likelihood of a branch. Both likely() and
 * unlikely() return the boolean value of the contained expression.
 *
 * These should only be used sparingly, in very hot code paths. It's very easy
 * to mis-estimate likelihoods.
 */
#if __GNUC__ >= 3
#define likely(x)	__builtin_expect((x) != 0, 1)
#define unlikely(x) __builtin_expect((x) != 0, 0)
#else
#define likely(x)	((x) != 0)
#define unlikely(x) ((x) != 0)
#endif

/* ----------------
 * Alignment macros: align a length or address appropriately for a given type.
 * The fooALIGN() macros round up to a multiple of the required alignment,
 * while the fooALIGN_DOWN() macros round down.  The latter are more useful
 * for problems like "how many X-sized structures will fit in a page?".
 *
 * NOTE: TYPEALIGN[_DOWN] will not work if ALIGNVAL is not a power of 2.
 * That case seems extremely unlikely to be needed in practice, however.
 *
 * NOTE: MAXIMUM_ALIGNOF, and hence MAXALIGN(), intentionally exclude any
 * larger-than-8-byte types the compiler might have.
 * ----------------
 */

#define TYPEALIGN(ALIGNVAL,LEN)  \
	(((uintptr_t) (LEN) + ((ALIGNVAL) - 1)) & ~((uintptr_t) ((ALIGNVAL) - 1)))

#define MAXIMUM_ALIGNOF			8
#define MAXALIGN(LEN)			TYPEALIGN(MAXIMUM_ALIGNOF, (LEN))

/*
 * Type MemoryContextData is declared in nodes/memnodes.h.  Most users
 * of memory allocation should just treat it as an abstract type, so we
 * do not provide the struct contents here.
 */
typedef struct MemoryContextData* MemoryContext;

typedef void (*MemoryStatsPrintFunc) (MemoryContext context, void* passthru,
	const char* stats_string,
	bool print_to_stderr);

/*
 * MemoryContextCounters
 *		Summarization state for MemoryContextStats collection.
 *
 * The set of counters in this struct is biased towards AllocSet; if we ever
 * add any context types that are based on fundamentally different approaches,
 * we might need more or different counters here.  A possible API spec then
 * would be to print only nonzero counters, but for now we just summarize in
 * the format historically used by AllocSet.
 */
typedef struct MemoryContextCounters
{
	Size		nblocks;		/* Total number of malloc blocks */
	Size		freechunks;		/* Total number of free chunks */
	Size		totalspace;		/* Total bytes requested from malloc */
	Size		freespace;		/* The unused portion of totalspace */
} MemoryContextCounters;

/*
 * MemoryContext
 *		A logical context in which memory allocations occur.
 *
 * MemoryContext itself is an abstract type that can have multiple
 * implementations.
 * The function pointers in MemoryContextMethods define one specific
 * implementation of MemoryContext --- they are a virtual function table
 * in C++ terms.
 *
 * Node types that are actual implementations of memory contexts must
 * begin with the same fields as MemoryContextData.
 *
 * Note: for largely historical reasons, typedef MemoryContext is a pointer
 * to the context struct rather than the struct type itself.
 */
typedef struct MemoryContextMethods
{
	/*
	 * Function to handle memory allocation requests of 'size' to allocate
	 * memory into the given 'context'.  The function must handle flags
	 * MCXT_ALLOC_HUGE and MCXT_ALLOC_NO_OOM.  MCXT_ALLOC_ZERO is handled by
	 * the calling function.
	 */
	void* (*alloc) (MemoryContext context, Size size, int flags);

	/* call this free_p in case someone #define's free() */
	void		(*free_p) (void* pointer);

	/*
	 * Function to handle a size change request for an existing allocation.
	 * The implementation must handle flags MCXT_ALLOC_HUGE and
	 * MCXT_ALLOC_NO_OOM.  MCXT_ALLOC_ZERO is handled by the calling function.
	 */
	void* (*realloc) (void* pointer, Size size, int flags);

	/*
	 * Invalidate all previous allocations in the given memory context and
	 * prepare the context for a new set of allocations.  Implementations may
	 * optionally free() excess memory back to the OS during this time.
	 */
	void		(*reset) (MemoryContext context);

	/* Free all memory consumed by the given MemoryContext. */
	void		(*delete_context) (MemoryContext context);

	/* Return the MemoryContext that the given pointer belongs to. */
	MemoryContext(*get_chunk_context) (void* pointer);

	/*
	 * Return the number of bytes consumed by the given pointer within its
	 * memory context, including the overhead of alignment and chunk headers.
	 */
	Size(*get_chunk_space) (void* pointer);

	/*
	 * Return true if the given MemoryContext has not had any allocations
	 * since it was created or last reset.
	 */
	bool		(*is_empty) (MemoryContext context);
	void		(*stats) (MemoryContext context,
		MemoryStatsPrintFunc printfunc, void* passthru,
		MemoryContextCounters* totals,
		bool print_to_stderr);
#ifdef MEMORY_CONTEXT_CHECKING

	/*
	 * Perform validation checks on the given context and raise any discovered
	 * anomalies as WARNINGs.
	 */
	void		(*check) (MemoryContext context);
#endif
} MemoryContextMethods;

/*
 * MemoryContextMethodID
 *		A unique identifier for each MemoryContext implementation which
 *		indicates the index into the mcxt_methods[] array. See mcxt.c.
 *
 * For robust error detection, ensure that MemoryContextMethodID has a value
 * for each possible bit-pattern of MEMORY_CONTEXT_METHODID_MASK, and make
 * dummy entries for unused IDs in the mcxt_methods[] array.  We also try
 * to avoid using bit-patterns as valid IDs if they are likely to occur in
 * garbage data, or if they could falsely match on chunks that are really from
 * malloc not palloc.  (We can't tell that for most malloc implementations,
 * but it happens that glibc stores flag bits in the same place where we put
 * the MemoryContextMethodID, so the possible values are predictable for it.)
 */
typedef enum MemoryContextMethodID
{
	MCTX_0_RESERVED_UNUSEDMEM_ID,	/* 0000 occurs in never-used memory */
	MCTX_1_RESERVED_GLIBC_ID,	/* glibc malloc'd chunks usually match 0001 */
	MCTX_2_RESERVED_GLIBC_ID,	/* glibc malloc'd chunks > 128kB match 0010 */
	MCTX_ASET_ID,
	MCTX_GENERATION_ID,
	MCTX_SLAB_ID,
	MCTX_ALIGNED_REDIRECT_ID,
	MCTX_BUMP_ID,
	MCTX_8_UNUSED_ID,
	MCTX_9_UNUSED_ID,
	MCTX_10_UNUSED_ID,
	MCTX_11_UNUSED_ID,
	MCTX_12_UNUSED_ID,
	MCTX_13_UNUSED_ID,
	MCTX_14_UNUSED_ID,
	MCTX_15_RESERVED_WIPEDMEM_ID	/* 1111 occurs in wipe_mem'd memory */
} MemoryContextMethodID;

static void BogusFree(void* pointer) {}
static void* BogusRealloc(void* pointer, Size size, int flags) { return NULL; }
static MemoryContext BogusGetChunkContext(void* pointer) { return NULL; }
static Size BogusGetChunkSpace(void* pointer) { return 0; }

/* These functions implement the MemoryContext API for AllocSet context. */
static void* AllocSetAlloc(MemoryContext context, Size size, int flags);
static void AllocSetFree(void* pointer);
static void* AllocSetRealloc(void* pointer, Size size, int flags);
static void AllocSetReset(MemoryContext context);
static void AllocSetDelete(MemoryContext context);
static MemoryContext AllocSetGetChunkContext(void* pointer);
static Size AllocSetGetChunkSpace(void* pointer);
static bool AllocSetIsEmpty(MemoryContext context);
static void AllocSetStats(MemoryContext context,
	MemoryStatsPrintFunc printfunc, void* passthru,
	MemoryContextCounters* totals,
	bool print_to_stderr);
#ifdef MEMORY_CONTEXT_CHECKING
static void AllocSetCheck(MemoryContext context);
#endif

/* These functions implement the MemoryContext API for Generation context. */
static void* GenerationAlloc(MemoryContext context, Size size, int flags) { return NULL; }
static void GenerationFree(void* pointer) {}
static void* GenerationRealloc(void* pointer, Size size, int flags) { return NULL; }
static void GenerationReset(MemoryContext context) {}
static void GenerationDelete(MemoryContext context) {}
static MemoryContext GenerationGetChunkContext(void* pointer) { return NULL; }
static Size GenerationGetChunkSpace(void* pointer) { return 0; }
static bool GenerationIsEmpty(MemoryContext context) { return true; }
static void GenerationStats(MemoryContext context,
	MemoryStatsPrintFunc printfunc, void* passthru,
	MemoryContextCounters* totals,
	bool print_to_stderr) {}
#ifdef MEMORY_CONTEXT_CHECKING
static void GenerationCheck(MemoryContext context) {}
#endif


/* These functions implement the MemoryContext API for Slab context. */
static void* SlabAlloc(MemoryContext context, Size size, int flags) { return NULL; }
static void SlabFree(void* pointer) {}
static void* SlabRealloc(void* pointer, Size size, int flags) { return NULL; }
static void SlabReset(MemoryContext context) {}
static void SlabDelete(MemoryContext context) {}
static MemoryContext SlabGetChunkContext(void* pointer) { return NULL; }
static Size SlabGetChunkSpace(void* pointer) { return 0; }
static bool SlabIsEmpty(MemoryContext context) { return true; }
static void SlabStats(MemoryContext context,
	MemoryStatsPrintFunc printfunc, void* passthru,
	MemoryContextCounters* totals,
	bool print_to_stderr) {}
#ifdef MEMORY_CONTEXT_CHECKING
static void SlabCheck(MemoryContext context) {}
#endif

/*
 * These functions support the implementation of palloc_aligned() and are not
 * part of a fully-fledged MemoryContext type.
 */
static void AlignedAllocFree(void* pointer) {}
static void* AlignedAllocRealloc(void* pointer, Size size, int flags) { return NULL; }
static MemoryContext AlignedAllocGetChunkContext(void* pointer) { return NULL; }
static Size AlignedAllocGetChunkSpace(void* pointer) { return 0; }

/* These functions implement the MemoryContext API for the Bump context. */
static void* BumpAlloc(MemoryContext context, Size size, int flags) { return NULL; }
static void BumpFree(void* pointer) {}
static void* BumpRealloc(void* pointer, Size size, int flags) { return NULL; }
static void BumpReset(MemoryContext context) {}
static void BumpDelete(MemoryContext context) {}
static MemoryContext BumpGetChunkContext(void* pointer) { return NULL; }
static Size BumpGetChunkSpace(void* pointer) { return 0; }
static bool BumpIsEmpty(MemoryContext context) { return true; }
static void BumpStats(MemoryContext context, MemoryStatsPrintFunc printfunc,
	void* passthru, MemoryContextCounters* totals,
	bool print_to_stderr) {}
#ifdef MEMORY_CONTEXT_CHECKING
static void BumpCheck(MemoryContext context) {}
#endif

#define BOGUS_MCTX(id) \
	[id].free_p = BogusFree, \
	[id].realloc = BogusRealloc, \
	[id].get_chunk_context = BogusGetChunkContext, \
	[id].get_chunk_space = BogusGetChunkSpace

static const MemoryContextMethods mcxt_methods[] = {
	/* aset.c */
	[MCTX_ASET_ID] .alloc = AllocSetAlloc,
	[MCTX_ASET_ID].free_p = AllocSetFree,
	[MCTX_ASET_ID].realloc = AllocSetRealloc,
	[MCTX_ASET_ID].reset = AllocSetReset,
	[MCTX_ASET_ID].delete_context = AllocSetDelete,
	[MCTX_ASET_ID].get_chunk_context = AllocSetGetChunkContext,
	[MCTX_ASET_ID].get_chunk_space = AllocSetGetChunkSpace,
	[MCTX_ASET_ID].is_empty = AllocSetIsEmpty,
	[MCTX_ASET_ID].stats = AllocSetStats,
#ifdef MEMORY_CONTEXT_CHECKING
	[MCTX_ASET_ID].check = AllocSetCheck,
#endif

	/* generation.c */
	[MCTX_GENERATION_ID].alloc = GenerationAlloc,
	[MCTX_GENERATION_ID].free_p = GenerationFree,
	[MCTX_GENERATION_ID].realloc = GenerationRealloc,
	[MCTX_GENERATION_ID].reset = GenerationReset,
	[MCTX_GENERATION_ID].delete_context = GenerationDelete,
	[MCTX_GENERATION_ID].get_chunk_context = GenerationGetChunkContext,
	[MCTX_GENERATION_ID].get_chunk_space = GenerationGetChunkSpace,
	[MCTX_GENERATION_ID].is_empty = GenerationIsEmpty,
	[MCTX_GENERATION_ID].stats = GenerationStats,
#ifdef MEMORY_CONTEXT_CHECKING
	[MCTX_GENERATION_ID].check = GenerationCheck,
#endif

	/* slab.c */
	[MCTX_SLAB_ID].alloc = SlabAlloc,
	[MCTX_SLAB_ID].free_p = SlabFree,
	[MCTX_SLAB_ID].realloc = SlabRealloc,
	[MCTX_SLAB_ID].reset = SlabReset,
	[MCTX_SLAB_ID].delete_context = SlabDelete,
	[MCTX_SLAB_ID].get_chunk_context = SlabGetChunkContext,
	[MCTX_SLAB_ID].get_chunk_space = SlabGetChunkSpace,
	[MCTX_SLAB_ID].is_empty = SlabIsEmpty,
	[MCTX_SLAB_ID].stats = SlabStats,
#ifdef MEMORY_CONTEXT_CHECKING
	[MCTX_SLAB_ID].check = SlabCheck,
#endif

	/* alignedalloc.c */
	[MCTX_ALIGNED_REDIRECT_ID].alloc = NULL,	/* not required */
	[MCTX_ALIGNED_REDIRECT_ID].free_p = AlignedAllocFree,
	[MCTX_ALIGNED_REDIRECT_ID].realloc = AlignedAllocRealloc,
	[MCTX_ALIGNED_REDIRECT_ID].reset = NULL,	/* not required */
	[MCTX_ALIGNED_REDIRECT_ID].delete_context = NULL,	/* not required */
	[MCTX_ALIGNED_REDIRECT_ID].get_chunk_context = AlignedAllocGetChunkContext,
	[MCTX_ALIGNED_REDIRECT_ID].get_chunk_space = AlignedAllocGetChunkSpace,
	[MCTX_ALIGNED_REDIRECT_ID].is_empty = NULL, /* not required */
	[MCTX_ALIGNED_REDIRECT_ID].stats = NULL,	/* not required */
#ifdef MEMORY_CONTEXT_CHECKING
	[MCTX_ALIGNED_REDIRECT_ID].check = NULL,	/* not required */
#endif

	/* bump.c */
	[MCTX_BUMP_ID].alloc = BumpAlloc,
	[MCTX_BUMP_ID].free_p = BumpFree,
	[MCTX_BUMP_ID].realloc = BumpRealloc,
	[MCTX_BUMP_ID].reset = BumpReset,
	[MCTX_BUMP_ID].delete_context = BumpDelete,
	[MCTX_BUMP_ID].get_chunk_context = BumpGetChunkContext,
	[MCTX_BUMP_ID].get_chunk_space = BumpGetChunkSpace,
	[MCTX_BUMP_ID].is_empty = BumpIsEmpty,
	[MCTX_BUMP_ID].stats = BumpStats,
#ifdef MEMORY_CONTEXT_CHECKING
	[MCTX_BUMP_ID].check = BumpCheck,
#endif


	/*
	 * Reserved and unused IDs should have dummy entries here.  This allows us
	 * to fail cleanly if a bogus pointer is passed to pfree or the like.  It
	 * seems sufficient to provide routines for the methods that might get
	 * invoked from inspection of a chunk (see MCXT_METHOD calls below).
	 */
	BOGUS_MCTX(MCTX_1_RESERVED_GLIBC_ID),
	BOGUS_MCTX(MCTX_2_RESERVED_GLIBC_ID),
	BOGUS_MCTX(MCTX_8_UNUSED_ID),
	BOGUS_MCTX(MCTX_9_UNUSED_ID),
	BOGUS_MCTX(MCTX_10_UNUSED_ID),
	BOGUS_MCTX(MCTX_11_UNUSED_ID),
	BOGUS_MCTX(MCTX_12_UNUSED_ID),
	BOGUS_MCTX(MCTX_13_UNUSED_ID),
	BOGUS_MCTX(MCTX_14_UNUSED_ID),
	BOGUS_MCTX(MCTX_0_RESERVED_UNUSEDMEM_ID),
	BOGUS_MCTX(MCTX_15_RESERVED_WIPEDMEM_ID)
};

#undef BOGUS_MCTX

/*
 * The number of bits that 8-byte memory chunk headers can use to encode the
 * MemoryContextMethodID.
 */
#define MEMORY_CONTEXT_METHODID_BITS 4
#define MEMORY_CONTEXT_METHODID_MASK \
	((((uint64) 1) << MEMORY_CONTEXT_METHODID_BITS) - 1)

/*
 * The maximum allowed value that MemoryContexts can store in the value
 * field.  Must be 1 less than a power of 2.
 */
#define MEMORYCHUNK_MAX_VALUE			UINT64CONST(0x3FFFFFFF)

/*
 * The maximum distance in bytes that a MemoryChunk can be offset from the
 * block that is storing the chunk.  Must be 1 less than a power of 2.
 */
#define MEMORYCHUNK_MAX_BLOCKOFFSET		UINT64CONST(0x3FFFFFFF)

/*
 * As above, but mask out the lowest-order (always zero) bit as this is shared
 * with the MemoryChunkGetValue field.
 */
#define MEMORYCHUNK_BLOCKOFFSET_MASK 	UINT64CONST(0x3FFFFFFE)

/* define the least significant base-0 bit of each portion of the hdrmask */
#define MEMORYCHUNK_EXTERNAL_BASEBIT	MEMORY_CONTEXT_METHODID_BITS
#define MEMORYCHUNK_VALUE_BASEBIT		(MEMORYCHUNK_EXTERNAL_BASEBIT + 1)
#define MEMORYCHUNK_BLOCKOFFSET_BASEBIT	(MEMORYCHUNK_VALUE_BASEBIT + 29)

/*
 * A magic number for storing in the free bits of an external chunk.  This
 * must mask out the bits used for storing the MemoryContextMethodID and the
 * external bit.
 */
#define MEMORYCHUNK_MAGIC		(UINT64CONST(0xB1A8DB858EB6EFBA) >> \
								 MEMORYCHUNK_VALUE_BASEBIT << \
								 MEMORYCHUNK_VALUE_BASEBIT)

typedef struct MemoryChunk
{
#ifdef MEMORY_CONTEXT_CHECKING
	Size		requested_size;
#endif

	/* bitfield for storing details about the chunk */
	uint64		hdrmask;		/* must be last */
} MemoryChunk;

/* Get the MemoryChunk from the pointer */
#define PointerGetMemoryChunk(p) \
	((MemoryChunk *) ((char *) (p) - sizeof(MemoryChunk)))
/* Get the pointer from the MemoryChunk */
#define MemoryChunkGetPointer(c) \
	((void *) ((char *) (c) + sizeof(MemoryChunk)))

/* Get the MemoryChunk from the pointer */
#define PointerGetMemoryChunk(p) \
	((MemoryChunk *) ((char *) (p) - sizeof(MemoryChunk)))
/* Get the pointer from the MemoryChunk */
#define MemoryChunkGetPointer(c) \
	((void *) ((char *) (c) + sizeof(MemoryChunk)))

/* private macros for making the inline functions below more simple */
#define HdrMaskIsExternal(hdrmask) \
	((hdrmask) & (((uint64) 1) << MEMORYCHUNK_EXTERNAL_BASEBIT))
#define HdrMaskGetValue(hdrmask) \
	(((hdrmask) >> MEMORYCHUNK_VALUE_BASEBIT) & MEMORYCHUNK_MAX_VALUE)

/*
 * Shift the block offset down to the 0th bit position and mask off the single
 * bit that's shared with the MemoryChunkGetValue field.
 */
#define HdrMaskBlockOffset(hdrmask) \
	(((hdrmask) >> MEMORYCHUNK_BLOCKOFFSET_BASEBIT) & MEMORYCHUNK_BLOCKOFFSET_MASK)

/* For external chunks only, check the magic number matches */
#define HdrMaskCheckMagic(hdrmask) \
	(MEMORYCHUNK_MAGIC == \
	 ((hdrmask) >> MEMORYCHUNK_VALUE_BASEBIT << MEMORYCHUNK_VALUE_BASEBIT))

/*
 * MemoryChunkSetHdrMask
 *		Store the given 'block', 'chunk_size' and 'methodid' in the given
 *		MemoryChunk.
 *
 * The number of bytes between 'block' and 'chunk' must be <=
 * MEMORYCHUNK_MAX_BLOCKOFFSET.
 * 'value' must be <= MEMORYCHUNK_MAX_VALUE.
 * Both 'chunk' and 'block' must be MAXALIGNed pointers.
 */
static inline void
MemoryChunkSetHdrMask(MemoryChunk* chunk, void* block,
	Size value, MemoryContextMethodID methodid)
{
	Size		blockoffset = (char*)chunk - (char*)block;
#if 0
	Assert((char*)chunk >= (char*)block);
	Assert((blockoffset & MEMORYCHUNK_BLOCKOFFSET_MASK) == blockoffset);
	Assert(value <= MEMORYCHUNK_MAX_VALUE);
	Assert((int)methodid <= MEMORY_CONTEXT_METHODID_MASK);
#endif 
	chunk->hdrmask = (((uint64)blockoffset) << MEMORYCHUNK_BLOCKOFFSET_BASEBIT) |
		(((uint64)value) << MEMORYCHUNK_VALUE_BASEBIT) |
		methodid;
}

/*
 * MemoryChunkSetHdrMaskExternal
 *		Set 'chunk' as an externally managed chunk.  Here we only record the
 *		MemoryContextMethodID and set the external chunk bit.
 */
static inline void
MemoryChunkSetHdrMaskExternal(MemoryChunk* chunk,
	MemoryContextMethodID methodid)
{
#if 0
	Assert((int)methodid <= MEMORY_CONTEXT_METHODID_MASK);
#endif 
	chunk->hdrmask = MEMORYCHUNK_MAGIC | (((uint64)1) << MEMORYCHUNK_EXTERNAL_BASEBIT) |
		methodid;
}

/*
 * MemoryChunkIsExternal
 *		Return true if 'chunk' is marked as external.
 */
static inline bool
MemoryChunkIsExternal(MemoryChunk* chunk)
{
	/*
	 * External chunks should always store MEMORYCHUNK_MAGIC in the upper
	 * portion of the hdrmask, check that nothing has stomped on that.
	 */
#if 0
	Assert(!HdrMaskIsExternal(chunk->hdrmask) ||
		HdrMaskCheckMagic(chunk->hdrmask));
#endif 
	return HdrMaskIsExternal(chunk->hdrmask);
}

/*
 * MemoryChunkGetValue
 *		For non-external chunks, returns the value field as it was set in
 *		MemoryChunkSetHdrMask.
 */
static inline Size
MemoryChunkGetValue(MemoryChunk* chunk)
{
#if 0
	Assert(!HdrMaskIsExternal(chunk->hdrmask));
#endif 
	return HdrMaskGetValue(chunk->hdrmask);
}

/*
 * MemoryChunkGetBlock
 *		For non-external chunks, returns the pointer to the block as was set
 *		in MemoryChunkSetHdrMask.
 */
static inline void*
MemoryChunkGetBlock(MemoryChunk* chunk)
{
#if 0
	Assert(!HdrMaskIsExternal(chunk->hdrmask));
#endif 
	return (void*)((char*)chunk - HdrMaskBlockOffset(chunk->hdrmask));
}


 /*
  * GetMemoryChunkMethodID
  *		Return the MemoryContextMethodID from the uint64 chunk header which
  *		directly precedes 'pointer'.
  */
static inline MemoryContextMethodID
GetMemoryChunkMethodID(const void* pointer)
{
	uint64		header;

	/*
	 * Try to detect bogus pointers handed to us, poorly though we can.
	 * Presumably, a pointer that isn't MAXALIGNED isn't pointing at an
	 * allocated chunk.
	 */
#if 0
	Assert(pointer == (const void*)MAXALIGN(pointer));

	/* Allow access to the uint64 header */
	VALGRIND_MAKE_MEM_DEFINED((char*)pointer - sizeof(uint64), sizeof(uint64));
#endif 
	header = *((const uint64*)((const char*)pointer - sizeof(uint64)));
#if 0
	/* Disallow access to the uint64 header */
	VALGRIND_MAKE_MEM_NOACCESS((char*)pointer - sizeof(uint64), sizeof(uint64));
#endif 
	return (MemoryContextMethodID)(header & MEMORY_CONTEXT_METHODID_MASK);
}

/*
 * Call the given function in the MemoryContextMethods for the memory context
 * type that 'pointer' belongs to.
 */
#define MCXT_METHOD(pointer, method) \
	mcxt_methods[GetMemoryChunkMethodID(pointer)].method


/* cleanup all internal definitions */
#undef MEMORYCHUNK_BLOCKOFFSET_MASK
#undef MEMORYCHUNK_EXTERNAL_BASEBIT
#undef MEMORYCHUNK_VALUE_BASEBIT
#undef MEMORYCHUNK_BLOCKOFFSET_BASEBIT
#undef MEMORYCHUNK_MAGIC
#undef HdrMaskIsExternal
#undef HdrMaskGetValue
#undef HdrMaskBlockOffset
#undef HdrMaskCheckMagic

/*-------------------------------------------------------------------------
 *
 * memnodes.h
 *	  POSTGRES memory context node definitions.
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/nodes/memnodes.h
 *
 *-------------------------------------------------------------------------
 */

/*
 * A memory context can have callback functions registered on it.  Any such
 * function will be called once just before the context is next reset or
 * deleted.  The MemoryContextCallback struct describing such a callback
 * typically would be allocated within the context itself, thereby avoiding
 * any need to manage it explicitly (the reset/delete action will free it).
 */
typedef void (*MemoryContextCallbackFunction) (void* arg);

typedef struct MemoryContextCallback
{
	MemoryContextCallbackFunction func; /* function to call */
	void* arg;			/* argument to pass it */
	struct MemoryContextCallback* next; /* next in list of callbacks */
} MemoryContextCallback;


typedef enum NodeTag
{
	T_Invalid = 0,
	T_AllocSetContext = 469,
	T_GenerationContext = 470,
	T_SlabContext = 471,
	T_BumpContext = 472
} NodeTag;

/*
 * The first field of a node of any type is guaranteed to be the NodeTag.
 * Hence the type of any node can be gotten by casting it to Node. Declaring
 * a variable to be of Node * (instead of void *) can also facilitate
 * debugging.
 */
typedef struct Node
{
	NodeTag		type;
} Node;

#define nodeTag(nodeptr)		(((const Node*)(nodeptr))->type)

#define IsA(nodeptr,_type_)		(nodeTag(nodeptr) == T_##_type_)

/*
 * pg_node_attr() - Used in node definitions to set extra information for
 * gen_node_support.pl
 *
 * Attributes can be attached to a node as a whole (place the attribute
 * specification on the first line after the struct's opening brace)
 * or to a specific field (place it at the end of that field's line).  The
 * argument is a comma-separated list of attributes.  Unrecognized attributes
 * cause an error.
 *
 * Valid node attributes:
 *
 * - abstract: Abstract types are types that cannot be instantiated but that
 *   can be supertypes of other types.  We track their fields, so that
 *   subtypes can use them, but we don't emit a node tag, so you can't
 *   instantiate them.
 *
 * - custom_copy_equal: Has custom implementations in copyfuncs.c and
 *   equalfuncs.c.
 *
 * - custom_read_write: Has custom implementations in outfuncs.c and
 *   readfuncs.c.
 *
 * - custom_query_jumble: Has custom implementation in queryjumblefuncs.c.
 *
 * - no_copy: Does not support copyObject() at all.
 *
 * - no_equal: Does not support equal() at all.
 *
 * - no_copy_equal: Shorthand for both no_copy and no_equal.
 *
 * - no_query_jumble: Does not support JumbleQuery() at all.
 *
 * - no_read: Does not support nodeRead() at all.
 *
 * - nodetag_only: Does not support copyObject(), equal(), jumbleQuery()
 *   outNode() or nodeRead().
 *
 * - special_read_write: Has special treatment in outNode() and nodeRead().
 *
 * - nodetag_number(VALUE): assign the specified nodetag number instead of
 *   an auto-generated number.  Typically this would only be used in stable
 *   branches, to give a newly-added node type a number without breaking ABI
 *   by changing the numbers of existing node types.
 *
 * Node types can be supertypes of other types whether or not they are marked
 * abstract: if a node struct appears as the first field of another struct
 * type, then it is the supertype of that type.  The no_copy, no_equal,
 * no_query_jumble and no_read node attributes are automatically inherited
 * from the supertype.  (Notice that nodetag_only does not inherit, so it's
 * not quite equivalent to a combination of other attributes.)
 *
 * Valid node field attributes:
 *
 * - array_size(OTHERFIELD): This field is a dynamically allocated array with
 *   size indicated by the mentioned other field.  The other field is either a
 *   scalar or a list, in which case the length of the list is used.
 *
 * - copy_as(VALUE): In copyObject(), replace the field's value with VALUE.
 *
 * - copy_as_scalar: In copyObject(), copy the field as a scalar value
 *   (e.g. a pointer) even if it is a node-type pointer.
 *
 * - equal_as_scalar: In equal(), compare the field as a scalar value
 *   even if it is a node-type pointer.
 *
 * - equal_ignore: Ignore the field for equality.
 *
 * - equal_ignore_if_zero: Ignore the field for equality if it is zero.
 *   (Otherwise, compare normally.)
 *
 * - query_jumble_ignore: Ignore the field for the query jumbling.  Note
 *   that typmod and collation information are usually irrelevant for the
 *   query jumbling.
 *
 * - query_jumble_location: Mark the field as a location to track.  This is
 *   only allowed for integer fields that include "location" in their name.
 *
 * - read_as(VALUE): In nodeRead(), replace the field's value with VALUE.
 *
 * - read_write_ignore: Ignore the field for read/write.  This is only allowed
 *   if the node type is marked no_read or read_as() is also specified.
 *
 * - write_only_relids, write_only_nondefault_pathtarget, write_only_req_outer:
 *   Special handling for Path struct; see there.
 *
 */
#define pg_node_attr(...)

/* only GCC supports the unused attribute */
#ifdef __GNUC__
#define pg_attribute_unused() __attribute__((unused))
#else
#define pg_attribute_unused()
#endif

/*
 * Forcing a function not to be inlined can be useful if it's the slow path of
 * a performance-critical function, or should be visible in profiles to allow
 * for proper cost attribution.  Note that unlike the pg_attribute_XXX macros
 * above, this should be placed before the function's return type and name.
 */
 /* GCC and Sunpro support noinline via __attribute__ */
#if (defined(__GNUC__) && __GNUC__ > 2) || defined(__SUNPRO_C)
#define pg_noinline __attribute__((noinline))
/* msvc via declspec */
#elif defined(_MSC_VER)
#define pg_noinline __declspec(noinline)
#else
#define pg_noinline
#endif

/*
 * Append PG_USED_FOR_ASSERTS_ONLY to definitions of variables that are only
 * used in assert-enabled builds, to avoid compiler warnings about unused
 * variables in assert-disabled builds.
 */
#ifdef USE_ASSERT_CHECKING
#define PG_USED_FOR_ASSERTS_ONLY
#else
#define PG_USED_FOR_ASSERTS_ONLY pg_attribute_unused()
#endif

typedef struct MemoryContextData
{
	pg_node_attr(abstract)		/* there are no nodes of this type */

	NodeTag		type;			/* identifies exact kind of context */
	/* these two fields are placed here to minimize alignment wastage: */
	bool		isReset;		/* T = no space alloced since last reset */
	bool		allowInCritSection; /* allow palloc in critical section */
	Size		mem_allocated;	/* track memory allocated for this context */
	const MemoryContextMethods* methods;	/* virtual function table */
	MemoryContext parent;		/* NULL if no parent (toplevel context) */
	MemoryContext firstchild;	/* head of linked list of children */
	MemoryContext prevchild;	/* previous child of same parent */
	MemoryContext nextchild;	/* next child of same parent */
	const char* name;			/* context name (just for debugging) */
	const char* ident;			/* context ID if any (just for debugging) */
	MemoryContextCallback* reset_cbs;	/* list of reset/delete callbacks */
} MemoryContextData;

/*
 * MemoryContextIsValid
 *		True iff memory context is valid.
 *
 * Add new context types to the set accepted by this macro.
 */
#define MemoryContextIsValid(context) \
	((context) != NULL && \
	 (IsA((context), AllocSetContext) || \
	  IsA((context), SlabContext) || \
	  IsA((context), GenerationContext) || \
	  IsA((context), BumpContext)))


 /*
  * Recommended default alloc parameters, suitable for "ordinary" contexts
  * that might hold quite a lot of data.
  */
#define ALLOCSET_DEFAULT_MINSIZE   0
#define ALLOCSET_DEFAULT_INITSIZE  (8 * 1024)
#define ALLOCSET_DEFAULT_MAXSIZE   (8 * 1024 * 1024)
#define ALLOCSET_DEFAULT_SIZES \
	ALLOCSET_DEFAULT_MINSIZE, ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE

  /*
   * Recommended alloc parameters for "small" contexts that are never expected
   * to contain much data (for example, a context to contain a query plan).
   */
#define ALLOCSET_SMALL_MINSIZE	 0
#define ALLOCSET_SMALL_INITSIZE  (1 * 1024)
#define ALLOCSET_SMALL_MAXSIZE	 (8 * 1024)
#define ALLOCSET_SMALL_SIZES \
	ALLOCSET_SMALL_MINSIZE, ALLOCSET_SMALL_INITSIZE, ALLOCSET_SMALL_MAXSIZE

   /*
	* Recommended alloc parameters for contexts that should start out small,
	* but might sometimes grow big.
	*/
#define ALLOCSET_START_SMALL_SIZES \
	ALLOCSET_SMALL_MINSIZE, ALLOCSET_SMALL_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE


	/*
	 * Threshold above which a request in an AllocSet context is certain to be
	 * allocated separately (and thereby have constant allocation overhead).
	 * Few callers should be interested in this, but tuplesort/tuplestore need
	 * to know it.
	 */
#define ALLOCSET_SEPARATE_THRESHOLD  8192

#define SLAB_DEFAULT_BLOCK_SIZE		(8 * 1024)
#define SLAB_LARGE_BLOCK_SIZE		(8 * 1024 * 1024)

/*
 * MemoryContextAllocationFailure
 *		For use by MemoryContextMethods implementations to handle when malloc
 *		returns NULL.  The behavior is specific to whether MCXT_ALLOC_NO_OOM
 *		is in 'flags'.
 */
void*
MemoryContextAllocationFailure(MemoryContext context, Size size, int flags)
{
#if 0
	if ((flags & MCXT_ALLOC_NO_OOM) == 0)
	{
		MemoryContextStats(TopMemoryContext);
		ereport(ERROR,
			(errcode(ERRCODE_OUT_OF_MEMORY),
				errmsg("out of memory"),
				errdetail("Failed on request of size %zu in memory context \"%s\".",
					size, context->name)));
	}
#endif 
	return NULL;
}

/*
 * Array giving the position of the left-most set bit for each possible
 * byte value.  We count the right-most position as the 0th bit, and the
 * left-most the 7th bit.  The 0th entry of the array should not be used.
 *
 * Note: this is not used by the functions in pg_bitutils.h when
 * HAVE__BUILTIN_CLZ is defined, but we provide it anyway, so that
 * extensions possibly compiled with a different compiler can use it.
 */
const uint8 pg_leftmost_one_pos[256] = {
	0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
};

/*-------------------------------------------------------------------------
 *
 * aset.c
 *	  Allocation set definitions.
 *
 * AllocSet is our standard implementation of the abstract MemoryContext
 * type.
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/mmgr/aset.c
 *
 * NOTE:
 *	This is a new (Feb. 05, 1999) implementation of the allocation set
 *	routines. AllocSet...() does not use OrderedSet...() any more.
 *	Instead it manages allocations in a block pool by itself, combining
 *	many small allocations in a few bigger blocks. AllocSetFree() normally
 *	doesn't free() memory really. It just add's the free'd area to some
 *	list for later reuse by AllocSetAlloc(). All memory blocks are free()'d
 *	at once on AllocSetReset(), which happens when the memory context gets
 *	destroyed.
 *				Jan Wieck
 *
 *	Performance improvement from Tom Lane, 8/99: for extremely large request
 *	sizes, we do want to be able to give the memory back to free() as soon
 *	as it is pfree()'d.  Otherwise we risk tying up a lot of memory in
 *	freelist entries that might never be usable.  This is specially needed
 *	when the caller is repeatedly repalloc()'ing a block bigger and bigger;
 *	the previous instances of the block were guaranteed to be wasted until
 *	AllocSetReset() under the old way.
 *
 *	Further improvement 12/00: as the code stood, request sizes in the
 *	midrange between "small" and "large" were handled very inefficiently,
 *	because any sufficiently large free chunk would be used to satisfy a
 *	request, even if it was much larger than necessary.  This led to more
 *	and more wasted space in allocated chunks over time.  To fix, get rid
 *	of the midrange behavior: we now handle only "small" power-of-2-size
 *	chunks as chunks.  Anything "large" is passed off to malloc().  Change
 *	the number of freelists to change the small/large boundary.
 *
 *-------------------------------------------------------------------------
 */
#if 0
#include "postgres.h"

#include "port/pg_bitutils.h"
#include "utils/memdebug.h"
#include "utils/memutils.h"
#include "utils/memutils_internal.h"
#include "utils/memutils_memorychunk.h"
#endif 

/*--------------------
 * Chunk freelist k holds chunks of size 1 << (k + ALLOC_MINBITS),
 * for k = 0 .. ALLOCSET_NUM_FREELISTS-1.
 *
 * Note that all chunks in the freelists have power-of-2 sizes.  This
 * improves recyclability: we may waste some space, but the wasted space
 * should stay pretty constant as requests are made and released.
 *
 * A request too large for the last freelist is handled by allocating a
 * dedicated block from malloc().  The block still has a block header and
 * chunk header, but when the chunk is freed we'll return the whole block
 * to malloc(), not put it on our freelists.
 *
 * CAUTION: ALLOC_MINBITS must be large enough so that
 * 1<<ALLOC_MINBITS is at least MAXALIGN,
 * or we may fail to align the smallest chunks adequately.
 * 8-byte alignment is enough on all currently known machines.  This 8-byte
 * minimum also allows us to store a pointer to the next freelist item within
 * the chunk of memory itself.
 *
 * With the current parameters, request sizes up to 8K are treated as chunks,
 * larger requests go into dedicated blocks.  Change ALLOCSET_NUM_FREELISTS
 * to adjust the boundary point; and adjust ALLOCSET_SEPARATE_THRESHOLD in
 * memutils.h to agree.  (Note: in contexts with small maxBlockSize, we may
 * set the allocChunkLimit to less than 8K, so as to avoid space wastage.)
 *--------------------
 */

#define ALLOC_MINBITS		3	/* smallest chunk size is 8 bytes */
#define ALLOCSET_NUM_FREELISTS	11
#define ALLOC_CHUNK_LIMIT	(1 << (ALLOCSET_NUM_FREELISTS-1+ALLOC_MINBITS))
/* Size of largest chunk that we use a fixed size for */
#define ALLOC_CHUNK_FRACTION	4
/* We allow chunks to be at most 1/4 of maxBlockSize (less overhead) */

/*--------------------
 * The first block allocated for an allocset has size initBlockSize.
 * Each time we have to allocate another block, we double the block size
 * (if possible, and without exceeding maxBlockSize), so as to reduce
 * the bookkeeping load on malloc().
 *
 * Blocks allocated to hold oversize chunks do not follow this rule, however;
 * they are just however big they need to be to hold that single chunk.
 *
 * Also, if a minContextSize is specified, the first block has that size,
 * and then initBlockSize is used for the next one.
 *--------------------
 */

#define ALLOC_BLOCKHDRSZ	MAXALIGN(sizeof(AllocBlockData))
#define ALLOC_CHUNKHDRSZ	sizeof(MemoryChunk)

typedef struct AllocBlockData *AllocBlock;	/* forward reference */

/*
 * AllocPointer
 *		Aligned pointer which may be a member of an allocation set.
 */
typedef void *AllocPointer;

/*
 * AllocFreeListLink
 *		When pfreeing memory, if we maintain a freelist for the given chunk's
 *		size then we use a AllocFreeListLink to point to the current item in
 *		the AllocSetContext's freelist and then set the given freelist element
 *		to point to the chunk being freed.
 */
typedef struct AllocFreeListLink
{
	MemoryChunk *next;
} AllocFreeListLink;

/*
 * Obtain a AllocFreeListLink for the given chunk.  Allocation sizes are
 * always at least sizeof(AllocFreeListLink), so we reuse the pointer's memory
 * itself to store the freelist link.
 */
#define GetFreeListLink(chkptr) \
	(AllocFreeListLink *) ((char *) (chkptr) + ALLOC_CHUNKHDRSZ)

/* Validate a freelist index retrieved from a chunk header */
#define FreeListIdxIsValid(fidx) \
	((fidx) >= 0 && (fidx) < ALLOCSET_NUM_FREELISTS)

/* Determine the size of the chunk based on the freelist index */
#define GetChunkSizeFromFreeListIdx(fidx) \
	((((Size) 1) << ALLOC_MINBITS) << (fidx))


/*
 * AllocSetContext is our standard implementation of MemoryContext.
 *
 * Note: header.isReset means there is nothing for AllocSetReset to do.
 * This is different from the aset being physically empty (empty blocks list)
 * because we will still have a keeper block.  It's also different from the set
 * being logically empty, because we don't attempt to detect pfree'ing the
 * last active chunk.
 */
typedef struct AllocSetContext
{
	MemoryContextData header;	/* Standard memory-context fields */
	/* Info about storage allocated in this context: */
	AllocBlock	blocks;			/* head of list of blocks in this set */
	MemoryChunk *freelist[ALLOCSET_NUM_FREELISTS];	/* free chunk lists */
	/* Allocation parameters for this context: */
	uint32		initBlockSize;	/* initial block size */
	uint32		maxBlockSize;	/* maximum block size */
	uint32		nextBlockSize;	/* next block size to allocate */
	uint32		allocChunkLimit;	/* effective chunk size limit */
	/* freelist this context could be put in, or -1 if not a candidate: */
	int			freeListIndex;	/* index in context_freelists[], or -1 */
} AllocSetContext;

typedef AllocSetContext *AllocSet;

/*
 * AllocBlock
 *		An AllocBlock is the unit of memory that is obtained by aset.c
 *		from malloc().  It contains one or more MemoryChunks, which are
 *		the units requested by palloc() and freed by pfree(). MemoryChunks
 *		cannot be returned to malloc() individually, instead they are put
 *		on freelists by pfree() and re-used by the next palloc() that has
 *		a matching request size.
 *
 *		AllocBlockData is the header data for a block --- the usable space
 *		within the block begins at the next alignment boundary.
 */
typedef struct AllocBlockData
{
	AllocSet	aset;			/* aset that owns this block */
	AllocBlock	prev;			/* prev block in aset's blocks list, if any */
	AllocBlock	next;			/* next block in aset's blocks list, if any */
	char	   *freeptr;		/* start of free space in this block */
	char	   *endptr;			/* end of space in this block */
}			AllocBlockData;

/*
 * AllocPointerIsValid
 *		True iff pointer is valid allocation pointer.
 */
#define AllocPointerIsValid(pointer) PointerIsValid(pointer)

/*
 * AllocSetIsValid
 *		True iff set is valid allocation set.
 */
#define AllocSetIsValid(set) \
	(PointerIsValid(set) && IsA(set, AllocSetContext))

/*
 * AllocBlockIsValid
 *		True iff block is valid block of allocation set.
 */
#define AllocBlockIsValid(block) \
	(PointerIsValid(block) && AllocSetIsValid((block)->aset))

/*
 * We always store external chunks on a dedicated block.  This makes fetching
 * the block from an external chunk easy since it's always the first and only
 * chunk on the block.
 */
#define ExternalChunkGetBlock(chunk) \
	(AllocBlock) ((char *) chunk - ALLOC_BLOCKHDRSZ)

/*
 * Rather than repeatedly creating and deleting memory contexts, we keep some
 * freed contexts in freelists so that we can hand them out again with little
 * work.  Before putting a context in a freelist, we reset it so that it has
 * only its initial malloc chunk and no others.  To be a candidate for a
 * freelist, a context must have the same minContextSize/initBlockSize as
 * other contexts in the list; but its maxBlockSize is irrelevant since that
 * doesn't affect the size of the initial chunk.
 *
 * We currently provide one freelist for ALLOCSET_DEFAULT_SIZES contexts
 * and one for ALLOCSET_SMALL_SIZES contexts; the latter works for
 * ALLOCSET_START_SMALL_SIZES too, since only the maxBlockSize differs.
 *
 * Ordinarily, we re-use freelist contexts in last-in-first-out order, in
 * hopes of improving locality of reference.  But if there get to be too
 * many contexts in the list, we'd prefer to drop the most-recently-created
 * contexts in hopes of keeping the process memory map compact.
 * We approximate that by simply deleting all existing entries when the list
 * overflows, on the assumption that queries that allocate a lot of contexts
 * will probably free them in more or less reverse order of allocation.
 *
 * Contexts in a freelist are chained via their nextchild pointers.
 */
#define MAX_FREE_CONTEXTS 100	/* arbitrary limit on freelist length */

/* Obtain the keeper block for an allocation set */
#define KeeperBlock(set) \
	((AllocBlock) (((char *) set) + MAXALIGN(sizeof(AllocSetContext))))

/* Check if the block is the keeper block of the given allocation set */
#define IsKeeperBlock(set, block) ((block) == (KeeperBlock(set)))

typedef struct AllocSetFreeList
{
	int			num_free;		/* current list length */
	AllocSetContext *first_free;	/* list header */
} AllocSetFreeList;

/* context_freelists[0] is for default params, [1] for small params */
static AllocSetFreeList context_freelists[2] =
{
	{
		0, NULL
	},
	{
		0, NULL
	}
};

/* ----------
 * AllocSetFreeIndex -
 *
 *		Depending on the size of an allocation compute which freechunk
 *		list of the alloc set it belongs to.  Caller must have verified
 *		that size <= ALLOC_CHUNK_LIMIT.
 * ----------
 */
static inline int
AllocSetFreeIndex(Size size)
{
	int			idx;

	if (size > (1 << ALLOC_MINBITS))
	{
		/*----------
		 * At this point we must compute ceil(log2(size >> ALLOC_MINBITS)).
		 * This is the same as
		 *		pg_leftmost_one_pos32((size - 1) >> ALLOC_MINBITS) + 1
		 * or equivalently
		 *		pg_leftmost_one_pos32(size - 1) - ALLOC_MINBITS + 1
		 *
		 * However, for platforms without intrinsic support, we duplicate the
		 * logic here, allowing an additional optimization.  It's reasonable
		 * to assume that ALLOC_CHUNK_LIMIT fits in 16 bits, so we can unroll
		 * the byte-at-a-time loop in pg_leftmost_one_pos32 and just handle
		 * the last two bytes.
		 *
		 * Yes, this function is enough of a hot-spot to make it worth this
		 * much trouble.
		 *----------
		 */
#ifdef HAVE_BITSCAN_REVERSE
		idx = pg_leftmost_one_pos32((uint32) size - 1) - ALLOC_MINBITS + 1;
#else
		uint32		t,
					tsize;
#if 0
		/* Statically assert that we only have a 16-bit input value. */
		StaticAssertDecl(ALLOC_CHUNK_LIMIT < (1 << 16),
						 "ALLOC_CHUNK_LIMIT must be less than 64kB");
#endif 
		tsize = size - 1;
		t = tsize >> 8;
		idx = t ? pg_leftmost_one_pos[t] + 8 : pg_leftmost_one_pos[tsize];
		idx -= ALLOC_MINBITS - 1;
#endif
#if 0
		Assert(idx < ALLOCSET_NUM_FREELISTS);
#endif 
	}
	else
		idx = 0;

	return idx;
}

static inline bool
MemoryContextCheckSize(MemoryContext context, Size size, int flags)
{
	bool ret = true;
	if (unlikely(!AllocSizeIsValid(size)))
	{
		if (!(flags & MCXT_ALLOC_HUGE) || !AllocHugeSizeIsValid(size))
		{
			//MemoryContextSizeFailure(context, size, flags);
			ret = false;
		}
	}
	return ret;
}

/*
 * Public routines
 */

 /*
  * MemoryContextCreate
  *		Context-type-independent part of context creation.
  *
  * This is only intended to be called by context-type-specific
  * context creation routines, not by the unwashed masses.
  *
  * The memory context creation procedure goes like this:
  *	1.  Context-type-specific routine makes some initial space allocation,
  *		including enough space for the context header.  If it fails,
  *		it can ereport() with no damage done.
  *	2.	Context-type-specific routine sets up all type-specific fields of
  *		the header (those beyond MemoryContextData proper), as well as any
  *		other management fields it needs to have a fully valid context.
  *		Usually, failure in this step is impossible, but if it's possible
  *		the initial space allocation should be freed before ereport'ing.
  *	3.	Context-type-specific routine calls MemoryContextCreate() to fill in
  *		the generic header fields and link the context into the context tree.
  *	4.  We return to the context-type-specific routine, which finishes
  *		up type-specific initialization.  This routine can now do things
  *		that might fail (like allocate more memory), so long as it's
  *		sure the node is left in a state that delete will handle.
  *
  * node: the as-yet-uninitialized common part of the context header node.
  * tag: NodeTag code identifying the memory context type.
  * method_id: MemoryContextMethodID of the context-type being created.
  * parent: parent context, or NULL if this will be a top-level context.
  * name: name of context (must be statically allocated).
  *
  * Context routines generally assume that MemoryContextCreate can't fail,
  * so this can contain Assert but not elog/ereport.
  */
static void
MemoryContextCreate(MemoryContext node,
	NodeTag tag,
	MemoryContextMethodID method_id,
	MemoryContext parent,
	const char* name)
{
#if 0
	/* Creating new memory contexts is not allowed in a critical section */
	Assert(CritSectionCount == 0);
#endif 
	/* Initialize all standard fields of memory context header */
	node->type = tag;
	node->isReset = true;
	node->methods = &mcxt_methods[method_id];
	node->parent = parent;
	node->firstchild = NULL;
	node->mem_allocated = 0;
	node->prevchild = NULL;
	node->name = name;
	node->ident = NULL;
	node->reset_cbs = NULL;

	/* OK to link node into context tree */
	if (parent)
	{
		node->nextchild = parent->firstchild;
		if (parent->firstchild != NULL)
			parent->firstchild->prevchild = node;
		parent->firstchild = node;
		/* inherit allowInCritSection flag from parent */
		node->allowInCritSection = parent->allowInCritSection;
	}
	else
	{
		node->nextchild = NULL;
		node->allowInCritSection = false;
	}
#if 0
	VALGRIND_CREATE_MEMPOOL(node, 0, false);
#endif 
}

/*
 * AllocSetContextCreateInternal
 *		Create a new AllocSet context.
 *
 * parent: parent context, or NULL if top-level context
 * name: name of context (must be statically allocated)
 * minContextSize: minimum context size
 * initBlockSize: initial allocation block size
 * maxBlockSize: maximum allocation block size
 *
 * Most callers should abstract the context size parameters using a macro
 * such as ALLOCSET_DEFAULT_SIZES.
 *
 * Note: don't call this directly; go through the wrapper macro
 * AllocSetContextCreate.
 */
static MemoryContext
AllocSetContextCreateInternal(MemoryContext parent,
							  const char *name,
							  Size minContextSize,
							  Size initBlockSize,
							  Size maxBlockSize)
{
	int			freeListIndex;
	Size		firstBlockSize;
	AllocSet	set;
	AllocBlock	block;
#if 0
	/* ensure MemoryChunk's size is properly maxaligned */
	StaticAssertDecl(ALLOC_CHUNKHDRSZ == MAXALIGN(ALLOC_CHUNKHDRSZ),
					 "sizeof(MemoryChunk) is not maxaligned");
	/* check we have enough space to store the freelist link */
	StaticAssertDecl(sizeof(AllocFreeListLink) <= (1 << ALLOC_MINBITS),
					 "sizeof(AllocFreeListLink) larger than minimum allocation size");

	/*
	 * First, validate allocation parameters.  Once these were regular runtime
	 * tests and elog's, but in practice Asserts seem sufficient because
	 * nobody varies their parameters at runtime.  We somewhat arbitrarily
	 * enforce a minimum 1K block size.  We restrict the maximum block size to
	 * MEMORYCHUNK_MAX_BLOCKOFFSET as MemoryChunks are limited to this in
	 * regards to addressing the offset between the chunk and the block that
	 * the chunk is stored on.  We would be unable to store the offset between
	 * the chunk and block for any chunks that were beyond
	 * MEMORYCHUNK_MAX_BLOCKOFFSET bytes into the block if the block was to be
	 * larger than this.
	 */
	Assert(initBlockSize == MAXALIGN(initBlockSize) &&
		   initBlockSize >= 1024);
	Assert(maxBlockSize == MAXALIGN(maxBlockSize) &&
		   maxBlockSize >= initBlockSize &&
		   AllocHugeSizeIsValid(maxBlockSize)); /* must be safe to double */
	Assert(minContextSize == 0 ||
		   (minContextSize == MAXALIGN(minContextSize) &&
			minContextSize >= 1024 &&
			minContextSize <= maxBlockSize));
	Assert(maxBlockSize <= MEMORYCHUNK_MAX_BLOCKOFFSET);
#endif 
	/*
	 * Check whether the parameters match either available freelist.  We do
	 * not need to demand a match of maxBlockSize.
	 */
#if 0
	if (minContextSize == ALLOCSET_DEFAULT_MINSIZE &&
		initBlockSize == ALLOCSET_DEFAULT_INITSIZE)
		freeListIndex = 0;
	else if (minContextSize == ALLOCSET_SMALL_MINSIZE &&
			 initBlockSize == ALLOCSET_SMALL_INITSIZE)
		freeListIndex = 1;
	else
#endif 
		freeListIndex = -1;

	/*
	 * If a suitable freelist entry exists, just recycle that context.
	 */
#if 0
	if (freeListIndex >= 0)
	{
		AllocSetFreeList *freelist = &context_freelists[freeListIndex];

		if (freelist->first_free != NULL)
		{
			/* Remove entry from freelist */
			set = freelist->first_free;
			freelist->first_free = (AllocSet) set->header.nextchild;
			freelist->num_free--;

			/* Update its maxBlockSize; everything else should be OK */
			set->maxBlockSize = maxBlockSize;

			/* Reinitialize its header, installing correct name and parent */
			MemoryContextCreate((MemoryContext) set,
								T_AllocSetContext,
								MCTX_ASET_ID,
								parent,
								name);

			((MemoryContext) set)->mem_allocated =
				KeeperBlock(set)->endptr - ((char *) set);

			return (MemoryContext) set;
		}
	}
#endif 
	/* Determine size of initial block */
	firstBlockSize = MAXALIGN(sizeof(AllocSetContext)) +
		ALLOC_BLOCKHDRSZ + ALLOC_CHUNKHDRSZ;
	if (minContextSize != 0)
		firstBlockSize = MaxPG(firstBlockSize, minContextSize);
	else
		firstBlockSize = MaxPG(firstBlockSize, initBlockSize);

	/*
	 * Allocate the initial block.  Unlike other aset.c blocks, it starts with
	 * the context header and its block header follows that.
	 */
	set = (AllocSet) malloc(firstBlockSize);
	if (set == NULL)
	{
#if 0
		if (TopMemoryContext)
			MemoryContextStats(TopMemoryContext);
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory"),
				 errdetail("Failed while creating memory context \"%s\".",
						   name)));
#endif 
		return NULL;
	}

	/*
	 * Avoid writing code that can fail between here and MemoryContextCreate;
	 * we'd leak the header/initial block if we ereport in this stretch.
	 */

	/* Fill in the initial block's block header */
	block = KeeperBlock(set);
	block->aset = set;
	block->freeptr = ((char *) block) + ALLOC_BLOCKHDRSZ;
	block->endptr = ((char *) set) + firstBlockSize;
	block->prev = NULL;
	block->next = NULL;
#if 0
	/* Mark unallocated space NOACCESS; leave the block header alone. */
	VALGRIND_MAKE_MEM_NOACCESS(block->freeptr, block->endptr - block->freeptr);
#endif 
	/* Remember block as part of block list */
	set->blocks = block;

	/* Finish filling in aset-specific parts of the context header */
	MemSetAligned(set->freelist, 0, sizeof(set->freelist));

	set->initBlockSize = (uint32) initBlockSize;
	set->maxBlockSize = (uint32) maxBlockSize;
	set->nextBlockSize = (uint32) initBlockSize;
	set->freeListIndex = freeListIndex;

	/*
	 * Compute the allocation chunk size limit for this context.  It can't be
	 * more than ALLOC_CHUNK_LIMIT because of the fixed number of freelists.
	 * If maxBlockSize is small then requests exceeding the maxBlockSize, or
	 * even a significant fraction of it, should be treated as large chunks
	 * too.  For the typical case of maxBlockSize a power of 2, the chunk size
	 * limit will be at most 1/8th maxBlockSize, so that given a stream of
	 * requests that are all the maximum chunk size we will waste at most
	 * 1/8th of the allocated space.
	 *
	 * Also, allocChunkLimit must not exceed ALLOCSET_SEPARATE_THRESHOLD.
	 */
#if 0
	StaticAssertStmt(ALLOC_CHUNK_LIMIT == ALLOCSET_SEPARATE_THRESHOLD,
					 "ALLOC_CHUNK_LIMIT != ALLOCSET_SEPARATE_THRESHOLD");
#endif 
	/*
	 * Determine the maximum size that a chunk can be before we allocate an
	 * entire AllocBlock dedicated for that chunk.  We set the absolute limit
	 * of that size as ALLOC_CHUNK_LIMIT but we reduce it further so that we
	 * can fit about ALLOC_CHUNK_FRACTION chunks this size on a maximally
	 * sized block.  (We opt to keep allocChunkLimit a power-of-2 value
	 * primarily for legacy reasons rather than calculating it so that exactly
	 * ALLOC_CHUNK_FRACTION chunks fit on a maximally sized block.)
	 */
	set->allocChunkLimit = ALLOC_CHUNK_LIMIT;
	while ((Size) (set->allocChunkLimit + ALLOC_CHUNKHDRSZ) >
		   (Size) ((maxBlockSize - ALLOC_BLOCKHDRSZ) / ALLOC_CHUNK_FRACTION))
		set->allocChunkLimit >>= 1;

	/* Finally, do the type-independent part of context creation */
	MemoryContextCreate((MemoryContext) set,
						T_AllocSetContext,
						MCTX_ASET_ID,
						parent,
						name);

	((MemoryContext) set)->mem_allocated = firstBlockSize;

	return (MemoryContext) set;
}

/*
 * AllocSetReset
 *		Frees all memory which is allocated in the given set.
 *
 * Actually, this routine has some discretion about what to do.
 * It should mark all allocated chunks freed, but it need not necessarily
 * give back all the resources the set owns.  Our actual implementation is
 * that we give back all but the "keeper" block (which we must keep, since
 * it shares a malloc chunk with the context header).  In this way, we don't
 * thrash malloc() when a context is repeatedly reset after small allocations,
 * which is typical behavior for per-tuple contexts.
 */
static void
AllocSetReset(MemoryContext context)
{
	AllocSet	set = (AllocSet) context;
	AllocBlock	block;
	Size		keepersize PG_USED_FOR_ASSERTS_ONLY;
#if 0
	Assert(AllocSetIsValid(set));
#endif 

#ifdef MEMORY_CONTEXT_CHECKING
	/* Check for corruption and leaks before freeing */
	AllocSetCheck(context);
#endif

	/* Remember keeper block size for Assert below */
	keepersize = KeeperBlock(set)->endptr - ((char *) set);

	/* Clear chunk freelists */
	MemSetAligned(set->freelist, 0, sizeof(set->freelist));

	block = set->blocks;

	/* New blocks list will be just the keeper block */
	set->blocks = KeeperBlock(set);

	while (block != NULL)
	{
		AllocBlock	next = block->next;

		if (IsKeeperBlock(set, block))
		{
			/* Reset the block, but don't return it to malloc */
			char	   *datastart = ((char *) block) + ALLOC_BLOCKHDRSZ;

#if 0
#ifdef CLOBBER_FREED_MEMORY
			wipe_mem(datastart, block->freeptr - datastart);
#else
			/* wipe_mem() would have done this */
			VALGRIND_MAKE_MEM_NOACCESS(datastart, block->freeptr - datastart);
#endif
#endif 
			block->freeptr = datastart;
			block->prev = NULL;
			block->next = NULL;
		}
		else
		{
			/* Normal case, release the block */
			context->mem_allocated -= block->endptr - ((char *) block);
#if 0
#ifdef CLOBBER_FREED_MEMORY
			wipe_mem(block, block->freeptr - ((char *) block));
#endif
#endif 
			free(block);
		}
		block = next;
	}
#if 0
	Assert(context->mem_allocated == keepersize);
#endif 
	/* Reset block size allocation sequence, too */
	set->nextBlockSize = set->initBlockSize;
}

#if 0
/*
 * MemoryContextResetOnly
 *		Release all space allocated within a context.
 *		Nothing is done to the context's descendant contexts.
 */
void
MemoryContextResetOnly(MemoryContext context)
{
#if 0
	Assert(MemoryContextIsValid(context));
#endif 
	/* Nothing to do if no pallocs since startup or last reset */
	if (!context->isReset)
	{
		MemoryContextCallResetCallbacks(context);

		/*
		 * If context->ident points into the context's memory, it will become
		 * a dangling pointer.  We could prevent that by setting it to NULL
		 * here, but that would break valid coding patterns that keep the
		 * ident elsewhere, e.g. in a parent context.  So for now we assume
		 * the programmer got it right.
		 */

		context->methods->reset(context);
		context->isReset = true;
#if 0
		VALGRIND_DESTROY_MEMPOOL(context);
		VALGRIND_CREATE_MEMPOOL(context, 0, false);
#endif 
	}
}
#endif 
/*
 * AllocSetDelete
 *		Frees all memory which is allocated in the given set,
 *		in preparation for deletion of the set.
 *
 * Unlike AllocSetReset, this *must* free all resources of the set.
 */
static void
AllocSetDelete(MemoryContext context)
{
	AllocSet	set = (AllocSet) context;
	AllocBlock	block = set->blocks;
	Size		keepersize PG_USED_FOR_ASSERTS_ONLY;
#if 0
	Assert(AllocSetIsValid(set));
#endif
#ifdef MEMORY_CONTEXT_CHECKING
	/* Check for corruption and leaks before freeing */
	AllocSetCheck(context);
#endif

	/* Remember keeper block size for Assert below */
	keepersize = KeeperBlock(set)->endptr - ((char *) set);

#if 0
	/*
	 * If the context is a candidate for a freelist, put it into that freelist
	 * instead of destroying it.
	 */
	if (set->freeListIndex >= 0)
	{
		AllocSetFreeList *freelist = &context_freelists[set->freeListIndex];

		/*
		 * Reset the context, if it needs it, so that we aren't hanging on to
		 * more than the initial malloc chunk.
		 */
		if (!context->isReset)
			MemoryContextResetOnly(context);

		/*
		 * If the freelist is full, just discard what's already in it.  See
		 * comments with context_freelists[].
		 */
		if (freelist->num_free >= MAX_FREE_CONTEXTS)
		{
			while (freelist->first_free != NULL)
			{
				AllocSetContext *oldset = freelist->first_free;

				freelist->first_free = (AllocSetContext *) oldset->header.nextchild;
				freelist->num_free--;

				/* All that remains is to free the header/initial block */
				free(oldset);
			}
#if 0
			Assert(freelist->num_free == 0);
#endif 
		}

		/* Now add the just-deleted context to the freelist. */
		set->header.nextchild = (MemoryContext) freelist->first_free;
		freelist->first_free = set;
		freelist->num_free++;

		return;
	}
#endif 
	/* Free all blocks, except the keeper which is part of context header */
	while (block != NULL)
	{
		AllocBlock	next = block->next;

		if (!IsKeeperBlock(set, block))
			context->mem_allocated -= block->endptr - ((char *) block);

#ifdef CLOBBER_FREED_MEMORY
		wipe_mem(block, block->freeptr - ((char *) block));
#endif

		if (!IsKeeperBlock(set, block))
			free(block);

		block = next;
	}
#if 0
	Assert(context->mem_allocated == keepersize);
#endif 
	/* Finally, free the context header, including the keeper block */
	free(set);
}

/*
 * Helper for AllocSetAlloc() that allocates an entire block for the chunk.
 *
 * AllocSetAlloc()'s comment explains why this is separate.
 */
pg_noinline
static void *
AllocSetAllocLarge(MemoryContext context, Size size, int flags)
{
	AllocSet	set = (AllocSet) context;
	AllocBlock	block;
	MemoryChunk *chunk;
	Size		chunk_size;
	Size		blksize;
	bool        size_is_good;
	/* validate 'size' is within the limits for the given 'flags' */
	size_is_good = MemoryContextCheckSize(context, size, flags);

	if (!size_is_good)
		return NULL;

#ifdef MEMORY_CONTEXT_CHECKING
	/* ensure there's always space for the sentinel byte */
	chunk_size = MAXALIGN(size + 1);
#else
	chunk_size = MAXALIGN(size);
#endif

	blksize = chunk_size + ALLOC_BLOCKHDRSZ + ALLOC_CHUNKHDRSZ;
	block = (AllocBlock) malloc(blksize);
	if (block == NULL)
		return MemoryContextAllocationFailure(context, size, flags);

	context->mem_allocated += blksize;

	block->aset = set;
	block->freeptr = block->endptr = ((char *) block) + blksize;

	chunk = (MemoryChunk *) (((char *) block) + ALLOC_BLOCKHDRSZ);

	/* mark the MemoryChunk as externally managed */
	MemoryChunkSetHdrMaskExternal(chunk, MCTX_ASET_ID);

#ifdef MEMORY_CONTEXT_CHECKING
	chunk->requested_size = size;
	/* set mark to catch clobber of "unused" space */
	Assert(size < chunk_size);
	set_sentinel(MemoryChunkGetPointer(chunk), size);
#endif
#ifdef RANDOMIZE_ALLOCATED_MEMORY
	/* fill the allocated space with junk */
	randomize_mem((char *) MemoryChunkGetPointer(chunk), size);
#endif

	/*
	 * Stick the new block underneath the active allocation block, if any, so
	 * that we don't lose the use of the space remaining therein.
	 */
	if (set->blocks != NULL)
	{
		block->prev = set->blocks;
		block->next = set->blocks->next;
		if (block->next)
			block->next->prev = block;
		set->blocks->next = block;
	}
	else
	{
		block->prev = NULL;
		block->next = NULL;
		set->blocks = block;
	}
#if 0
	/* Ensure any padding bytes are marked NOACCESS. */
	VALGRIND_MAKE_MEM_NOACCESS((char *) MemoryChunkGetPointer(chunk) + size,
							   chunk_size - size);

	/* Disallow access to the chunk header. */
	VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);
#endif 
	return MemoryChunkGetPointer(chunk);
}

/*
 * Small helper for allocating a new chunk from a chunk, to avoid duplicating
 * the code between AllocSetAlloc() and AllocSetAllocFromNewBlock().
 */
static inline void *
AllocSetAllocChunkFromBlock(MemoryContext context, AllocBlock block,
							Size size, Size chunk_size, int fidx)
{
	MemoryChunk *chunk;

	chunk = (MemoryChunk *) (block->freeptr);
#if 0
	/* Prepare to initialize the chunk header. */
	VALGRIND_MAKE_MEM_UNDEFINED(chunk, ALLOC_CHUNKHDRSZ);
#endif
	block->freeptr += (chunk_size + ALLOC_CHUNKHDRSZ);
#if 0
	Assert(block->freeptr <= block->endptr);
#endif 
	/* store the free list index in the value field */
	MemoryChunkSetHdrMask(chunk, block, fidx, MCTX_ASET_ID);

#ifdef MEMORY_CONTEXT_CHECKING
	chunk->requested_size = size;
	/* set mark to catch clobber of "unused" space */
	if (size < chunk_size)
		set_sentinel(MemoryChunkGetPointer(chunk), size);
#endif
#ifdef RANDOMIZE_ALLOCATED_MEMORY
	/* fill the allocated space with junk */
	randomize_mem((char *) MemoryChunkGetPointer(chunk), size);
#endif
#if 0
	/* Ensure any padding bytes are marked NOACCESS. */
	VALGRIND_MAKE_MEM_NOACCESS((char *) MemoryChunkGetPointer(chunk) + size,
							   chunk_size - size);

	/* Disallow access to the chunk header. */
	VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);
#endif 
	return MemoryChunkGetPointer(chunk);
}

/*
 * Helper for AllocSetAlloc() that allocates a new block and returns a chunk
 * allocated from it.
 *
 * AllocSetAlloc()'s comment explains why this is separate.
 */
pg_noinline
static void *
AllocSetAllocFromNewBlock(MemoryContext context, Size size, int flags,
						  int fidx)
{
	AllocSet	set = (AllocSet) context;
	AllocBlock	block;
	Size		availspace;
	Size		blksize;
	Size		required_size;
	Size		chunk_size;

	/* due to the keeper block set->blocks should always be valid */
#if 0
	Assert(set->blocks != NULL);
#endif 
	block = set->blocks;
	availspace = block->endptr - block->freeptr;

	/*
	 * The existing active (top) block does not have enough room for the
	 * requested allocation, but it might still have a useful amount of space
	 * in it.  Once we push it down in the block list, we'll never try to
	 * allocate more space from it. So, before we do that, carve up its free
	 * space into chunks that we can put on the set's freelists.
	 *
	 * Because we can only get here when there's less than ALLOC_CHUNK_LIMIT
	 * left in the block, this loop cannot iterate more than
	 * ALLOCSET_NUM_FREELISTS-1 times.
	 */
	while (availspace >= ((1 << ALLOC_MINBITS) + ALLOC_CHUNKHDRSZ))
	{
		AllocFreeListLink *link;
		MemoryChunk *chunk;
		Size		availchunk = availspace - ALLOC_CHUNKHDRSZ;
		int			a_fidx = AllocSetFreeIndex(availchunk);

		/*
		 * In most cases, we'll get back the index of the next larger freelist
		 * than the one we need to put this chunk on.  The exception is when
		 * availchunk is exactly a power of 2.
		 */
		if (availchunk != GetChunkSizeFromFreeListIdx(a_fidx))
		{
			a_fidx--;
#if 0
			Assert(a_fidx >= 0);
#endif 
			availchunk = GetChunkSizeFromFreeListIdx(a_fidx);
		}

		chunk = (MemoryChunk *) (block->freeptr);
#if 0
		/* Prepare to initialize the chunk header. */
		VALGRIND_MAKE_MEM_UNDEFINED(chunk, ALLOC_CHUNKHDRSZ);
#endif 
		block->freeptr += (availchunk + ALLOC_CHUNKHDRSZ);
		availspace -= (availchunk + ALLOC_CHUNKHDRSZ);

		/* store the freelist index in the value field */
		MemoryChunkSetHdrMask(chunk, block, a_fidx, MCTX_ASET_ID);
#ifdef MEMORY_CONTEXT_CHECKING
		chunk->requested_size = InvalidAllocSize;	/* mark it free */
#endif
		/* push this chunk onto the free list */
		link = GetFreeListLink(chunk);

#if 0
		VALGRIND_MAKE_MEM_DEFINED(link, sizeof(AllocFreeListLink));
#endif 
		link->next = set->freelist[a_fidx];
#if 0
		VALGRIND_MAKE_MEM_NOACCESS(link, sizeof(AllocFreeListLink));
#endif 
		set->freelist[a_fidx] = chunk;
	}

	/*
	 * The first such block has size initBlockSize, and we double the space in
	 * each succeeding block, but not more than maxBlockSize.
	 */
	blksize = set->nextBlockSize;
	set->nextBlockSize <<= 1;
	if (set->nextBlockSize > set->maxBlockSize)
		set->nextBlockSize = set->maxBlockSize;

	/* Choose the actual chunk size to allocate */
	chunk_size = GetChunkSizeFromFreeListIdx(fidx);
#if 0
	Assert(chunk_size >= size);
#endif 
	/*
	 * If initBlockSize is less than ALLOC_CHUNK_LIMIT, we could need more
	 * space... but try to keep it a power of 2.
	 */
	required_size = chunk_size + ALLOC_BLOCKHDRSZ + ALLOC_CHUNKHDRSZ;
	while (blksize < required_size)
		blksize <<= 1;

	/* Try to allocate it */
	block = (AllocBlock) malloc(blksize);

	/*
	 * We could be asking for pretty big blocks here, so cope if malloc fails.
	 * But give up if there's less than 1 MB or so available...
	 */
	while (block == NULL && blksize > 1024 * 1024)
	{
		blksize >>= 1;
		if (blksize < required_size)
			break;
		block = (AllocBlock) malloc(blksize);
	}

	if (block == NULL)
		return MemoryContextAllocationFailure(context, size, flags);

	context->mem_allocated += blksize;

	block->aset = set;
	block->freeptr = ((char *) block) + ALLOC_BLOCKHDRSZ;
	block->endptr = ((char *) block) + blksize;
#if 0
	/* Mark unallocated space NOACCESS. */
	VALGRIND_MAKE_MEM_NOACCESS(block->freeptr,
							   blksize - ALLOC_BLOCKHDRSZ);
#endif 
	block->prev = NULL;
	block->next = set->blocks;
	if (block->next)
		block->next->prev = block;
	set->blocks = block;

	return AllocSetAllocChunkFromBlock(context, block, size, chunk_size, fidx);
}

/*
 * AllocSetAlloc
 *		Returns a pointer to allocated memory of given size or raises an ERROR
 *		on allocation failure, or returns NULL when flags contains
 *		MCXT_ALLOC_NO_OOM.
 *
 * No request may exceed:
 *		MAXALIGN_DOWN(SIZE_MAX) - ALLOC_BLOCKHDRSZ - ALLOC_CHUNKHDRSZ
 * All callers use a much-lower limit.
 *
 * Note: when using valgrind, it doesn't matter how the returned allocation
 * is marked, as mcxt.c will set it to UNDEFINED.  In some paths we will
 * return space that is marked NOACCESS - AllocSetRealloc has to beware!
 *
 * This function should only contain the most common code paths.  Everything
 * else should be in pg_noinline helper functions, thus avoiding the overhead
 * of creating a stack frame for the common cases.  Allocating memory is often
 * a bottleneck in many workloads, so avoiding stack frame setup is
 * worthwhile.  Helper functions should always directly return the newly
 * allocated memory so that we can just return that address directly as a tail
 * call.
 */
void *
AllocSetAlloc(MemoryContext context, Size size, int flags)
{
	AllocSet	set = (AllocSet) context;
	AllocBlock	block;
	MemoryChunk *chunk;
	int			fidx;
	Size		chunk_size;
	Size		availspace;
#if 0
	Assert(AllocSetIsValid(set));

	/* due to the keeper block set->blocks should never be NULL */
	Assert(set->blocks != NULL);
#endif 
	/*
	 * If requested size exceeds maximum for chunks we hand the request off to
	 * AllocSetAllocLarge().
	 */
	if (size > set->allocChunkLimit)
		return AllocSetAllocLarge(context, size, flags);

	/*
	 * Request is small enough to be treated as a chunk.  Look in the
	 * corresponding free list to see if there is a free chunk we could reuse.
	 * If one is found, remove it from the free list, make it again a member
	 * of the alloc set and return its data address.
	 *
	 * Note that we don't attempt to ensure there's space for the sentinel
	 * byte here.  We expect a large proportion of allocations to be for sizes
	 * which are already a power of 2.  If we were to always make space for a
	 * sentinel byte in MEMORY_CONTEXT_CHECKING builds, then we'd end up
	 * doubling the memory requirements for such allocations.
	 */
	fidx = AllocSetFreeIndex(size);
	chunk = set->freelist[fidx];
	if (chunk != NULL)
	{
		AllocFreeListLink *link = GetFreeListLink(chunk);
#if 0
		/* Allow access to the chunk header. */
		VALGRIND_MAKE_MEM_DEFINED(chunk, ALLOC_CHUNKHDRSZ);

		Assert(fidx == MemoryChunkGetValue(chunk));

		/* pop this chunk off the freelist */
		VALGRIND_MAKE_MEM_DEFINED(link, sizeof(AllocFreeListLink));
#endif 
		set->freelist[fidx] = link->next;
#if 0
		VALGRIND_MAKE_MEM_NOACCESS(link, sizeof(AllocFreeListLink));
#endif 
#ifdef MEMORY_CONTEXT_CHECKING
		chunk->requested_size = size;
		/* set mark to catch clobber of "unused" space */
		if (size < GetChunkSizeFromFreeListIdx(fidx))
			set_sentinel(MemoryChunkGetPointer(chunk), size);
#endif
#ifdef RANDOMIZE_ALLOCATED_MEMORY
		/* fill the allocated space with junk */
		randomize_mem((char *) MemoryChunkGetPointer(chunk), size);
#endif
#if 0
		/* Ensure any padding bytes are marked NOACCESS. */
		VALGRIND_MAKE_MEM_NOACCESS((char *) MemoryChunkGetPointer(chunk) + size,
								   GetChunkSizeFromFreeListIdx(fidx) - size);

		/* Disallow access to the chunk header. */
		VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);
#endif 
		return MemoryChunkGetPointer(chunk);
	}

	/*
	 * Choose the actual chunk size to allocate.
	 */
	chunk_size = GetChunkSizeFromFreeListIdx(fidx);
#if 0
	Assert(chunk_size >= size);
#endif 
	block = set->blocks;
	availspace = block->endptr - block->freeptr;

	/*
	 * If there is enough room in the active allocation block, we will put the
	 * chunk into that block.  Else must start a new one.
	 */
	if (unlikely(availspace < (chunk_size + ALLOC_CHUNKHDRSZ)))
		return AllocSetAllocFromNewBlock(context, size, flags, fidx);

	/* There's enough space on the current block, so allocate from that */
	return AllocSetAllocChunkFromBlock(context, block, size, chunk_size, fidx);
}

/*
 * AllocSetFree
 *		Frees allocated memory; memory is removed from the set.
 */
void
AllocSetFree(void *pointer)
{
	AllocSet	set;
	MemoryChunk *chunk = PointerGetMemoryChunk(pointer);
#if 0
	/* Allow access to the chunk header. */
	VALGRIND_MAKE_MEM_DEFINED(chunk, ALLOC_CHUNKHDRSZ);
#endif 
	if (MemoryChunkIsExternal(chunk))
	{
		/* Release single-chunk block. */
		AllocBlock	block = ExternalChunkGetBlock(chunk);
#if 0
		/*
		 * Try to verify that we have a sane block pointer: the block header
		 * should reference an aset and the freeptr should match the endptr.
		 */
		if (!AllocBlockIsValid(block) || block->freeptr != block->endptr)
			elog(ERROR, "could not find block containing chunk %p", chunk);
#endif 
		set = block->aset;

#ifdef MEMORY_CONTEXT_CHECKING
		{
			/* Test for someone scribbling on unused space in chunk */
			Assert(chunk->requested_size < (block->endptr - (char *) pointer));
			if (!sentinel_ok(pointer, chunk->requested_size))
				elog(WARNING, "detected write past chunk end in %s %p",
					 set->header.name, chunk);
		}
#endif

		/* OK, remove block from aset's list and free it */
		if (block->prev)
			block->prev->next = block->next;
		else
			set->blocks = block->next;
		if (block->next)
			block->next->prev = block->prev;

		set->header.mem_allocated -= block->endptr - ((char *) block);

#ifdef CLOBBER_FREED_MEMORY
		wipe_mem(block, block->freeptr - ((char *) block));
#endif
		free(block);
	}
	else
	{
		AllocBlock	block = MemoryChunkGetBlock(chunk);
		int			fidx;
		AllocFreeListLink *link;

		/*
		 * In this path, for speed reasons we just Assert that the referenced
		 * block is good.  We can also Assert that the value field is sane.
		 * Future field experience may show that these Asserts had better
		 * become regular runtime test-and-elog checks.
		 */
#if 0
		Assert(AllocBlockIsValid(block));
#endif 
		set = block->aset;

		fidx = MemoryChunkGetValue(chunk);
#if 0
		Assert(FreeListIdxIsValid(fidx));
#endif 
		link = GetFreeListLink(chunk);

#ifdef MEMORY_CONTEXT_CHECKING
		/* Test for someone scribbling on unused space in chunk */
		if (chunk->requested_size < GetChunkSizeFromFreeListIdx(fidx))
			if (!sentinel_ok(pointer, chunk->requested_size))
				elog(WARNING, "detected write past chunk end in %s %p",
					 set->header.name, chunk);
#endif

#ifdef CLOBBER_FREED_MEMORY
		wipe_mem(pointer, GetChunkSizeFromFreeListIdx(fidx));
#endif
		/* push this chunk onto the top of the free list */
		// VALGRIND_MAKE_MEM_DEFINED(link, sizeof(AllocFreeListLink));
		link->next = set->freelist[fidx];
		// VALGRIND_MAKE_MEM_NOACCESS(link, sizeof(AllocFreeListLink));
		set->freelist[fidx] = chunk;

#ifdef MEMORY_CONTEXT_CHECKING

		/*
		 * Reset requested_size to InvalidAllocSize in chunks that are on free
		 * list.
		 */
		chunk->requested_size = InvalidAllocSize;
#endif
	}
}

/*
 * AllocSetRealloc
 *		Returns new pointer to allocated memory of given size or NULL if
 *		request could not be completed; this memory is added to the set.
 *		Memory associated with given pointer is copied into the new memory,
 *		and the old memory is freed.
 *
 * Without MEMORY_CONTEXT_CHECKING, we don't know the old request size.  This
 * makes our Valgrind client requests less-precise, hazarding false negatives.
 * (In principle, we could use VALGRIND_GET_VBITS() to rediscover the old
 * request size.)
 */
void *
AllocSetRealloc(void *pointer, Size size, int flags)
{
#if 0
	AllocBlock	block;
	AllocSet	set;
	MemoryChunk *chunk = PointerGetMemoryChunk(pointer);
	Size		oldchksize;
	int			fidx;
#if 0
	/* Allow access to the chunk header. */
	VALGRIND_MAKE_MEM_DEFINED(chunk, ALLOC_CHUNKHDRSZ);
#endif 
	if (MemoryChunkIsExternal(chunk))
	{
		/*
		 * The chunk must have been allocated as a single-chunk block.  Use
		 * realloc() to make the containing block bigger, or smaller, with
		 * minimum space wastage.
		 */
		Size		chksize;
		Size		blksize;
		Size		oldblksize;

		block = ExternalChunkGetBlock(chunk);
#if 0
		/*
		 * Try to verify that we have a sane block pointer: the block header
		 * should reference an aset and the freeptr should match the endptr.
		 */
		if (!AllocBlockIsValid(block) || block->freeptr != block->endptr)
			elog(ERROR, "could not find block containing chunk %p", chunk);
#endif 
		set = block->aset;

		/* only check size in paths where the limits could be hit */
		MemoryContextCheckSize((MemoryContext) set, size, flags);

		oldchksize = block->endptr - (char *) pointer;

#ifdef MEMORY_CONTEXT_CHECKING
		/* Test for someone scribbling on unused space in chunk */
		Assert(chunk->requested_size < oldchksize);
		if (!sentinel_ok(pointer, chunk->requested_size))
			elog(WARNING, "detected write past chunk end in %s %p",
				 set->header.name, chunk);
#endif

#ifdef MEMORY_CONTEXT_CHECKING
		/* ensure there's always space for the sentinel byte */
		chksize = MAXALIGN(size + 1);
#else
		chksize = MAXALIGN(size);
#endif

		/* Do the realloc */
		blksize = chksize + ALLOC_BLOCKHDRSZ + ALLOC_CHUNKHDRSZ;
		oldblksize = block->endptr - ((char *) block);

		block = (AllocBlock) realloc(block, blksize);
		if (block == NULL)
		{
			/* Disallow access to the chunk header. */
			// VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);
			return MemoryContextAllocationFailure(&set->header, size, flags);
		}

		/* updated separately, not to underflow when (oldblksize > blksize) */
		set->header.mem_allocated -= oldblksize;
		set->header.mem_allocated += blksize;

		block->freeptr = block->endptr = ((char *) block) + blksize;

		/* Update pointers since block has likely been moved */
		chunk = (MemoryChunk *) (((char *) block) + ALLOC_BLOCKHDRSZ);
		pointer = MemoryChunkGetPointer(chunk);
		if (block->prev)
			block->prev->next = block;
		else
			set->blocks = block;
		if (block->next)
			block->next->prev = block;

#if 0
#ifdef MEMORY_CONTEXT_CHECKING
#ifdef RANDOMIZE_ALLOCATED_MEMORY

		/*
		 * We can only randomize the extra space if we know the prior request.
		 * When using Valgrind, randomize_mem() also marks memory UNDEFINED.
		 */
		if (size > chunk->requested_size)
			randomize_mem((char *) pointer + chunk->requested_size,
						  size - chunk->requested_size);
#else

		/*
		 * If this is an increase, realloc() will have marked any
		 * newly-allocated part (from oldchksize to chksize) UNDEFINED, but we
		 * also need to adjust trailing bytes from the old allocation (from
		 * chunk->requested_size to oldchksize) as they are marked NOACCESS.
		 * Make sure not to mark too many bytes in case chunk->requested_size
		 * < size < oldchksize.
		 */
#ifdef USE_VALGRIND
		if (Min(size, oldchksize) > chunk->requested_size)
			VALGRIND_MAKE_MEM_UNDEFINED((char *) pointer + chunk->requested_size,
										Min(size, oldchksize) - chunk->requested_size);
#endif
#endif

		chunk->requested_size = size;
		/* set mark to catch clobber of "unused" space */
		Assert(size < chksize);
		set_sentinel(pointer, size);
#else							/* !MEMORY_CONTEXT_CHECKING */

		/*
		 * We may need to adjust marking of bytes from the old allocation as
		 * some of them may be marked NOACCESS.  We don't know how much of the
		 * old chunk size was the requested size; it could have been as small
		 * as one byte.  We have to be conservative and just mark the entire
		 * old portion DEFINED.  Make sure not to mark memory beyond the new
		 * allocation in case it's smaller than the old one.
		 */
		VALGRIND_MAKE_MEM_DEFINED(pointer, Min(size, oldchksize));
#endif

		/* Ensure any padding bytes are marked NOACCESS. */
		VALGRIND_MAKE_MEM_NOACCESS((char *) pointer + size, chksize - size);

		/* Disallow access to the chunk header . */
		VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);
#endif 
		return pointer;
	}

	block = MemoryChunkGetBlock(chunk);

	/*
	 * In this path, for speed reasons we just Assert that the referenced
	 * block is good. We can also Assert that the value field is sane. Future
	 * field experience may show that these Asserts had better become regular
	 * runtime test-and-elog checks.
	 */
#if 0
	Assert(AllocBlockIsValid(block));
#endif 
	set = block->aset;

	fidx = MemoryChunkGetValue(chunk);
#if 0
	Assert(FreeListIdxIsValid(fidx));
#endif 
	oldchksize = GetChunkSizeFromFreeListIdx(fidx);

#ifdef MEMORY_CONTEXT_CHECKING
	/* Test for someone scribbling on unused space in chunk */
	if (chunk->requested_size < oldchksize)
		if (!sentinel_ok(pointer, chunk->requested_size))
			elog(WARNING, "detected write past chunk end in %s %p",
				 set->header.name, chunk);
#endif

	/*
	 * Chunk sizes are aligned to power of 2 in AllocSetAlloc().  Maybe the
	 * allocated area already is >= the new size.  (In particular, we will
	 * fall out here if the requested size is a decrease.)
	 */
	if (oldchksize >= size)
	{
#if 0
#ifdef MEMORY_CONTEXT_CHECKING
		Size		oldrequest = chunk->requested_size;

#ifdef RANDOMIZE_ALLOCATED_MEMORY
		/* We can only fill the extra space if we know the prior request */
		if (size > oldrequest)
			randomize_mem((char *) pointer + oldrequest,
						  size - oldrequest);
#endif

		chunk->requested_size = size;

		/*
		 * If this is an increase, mark any newly-available part UNDEFINED.
		 * Otherwise, mark the obsolete part NOACCESS.
		 */
		if (size > oldrequest)
			VALGRIND_MAKE_MEM_UNDEFINED((char *) pointer + oldrequest,
										size - oldrequest);
		else
			VALGRIND_MAKE_MEM_NOACCESS((char *) pointer + size,
									   oldchksize - size);

		/* set mark to catch clobber of "unused" space */
		if (size < oldchksize)
			set_sentinel(pointer, size);
#else							/* !MEMORY_CONTEXT_CHECKING */

		/*
		 * We don't have the information to determine whether we're growing
		 * the old request or shrinking it, so we conservatively mark the
		 * entire new allocation DEFINED.
		 */
		VALGRIND_MAKE_MEM_NOACCESS(pointer, oldchksize);
		VALGRIND_MAKE_MEM_DEFINED(pointer, size);
#endif

		/* Disallow access to the chunk header. */
		VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);
#endif 
		return pointer;
	}
	else
	{
		/*
		 * Enlarge-a-small-chunk case.  We just do this by brute force, ie,
		 * allocate a new chunk and copy the data.  Since we know the existing
		 * data isn't huge, this won't involve any great memcpy expense, so
		 * it's not worth being smarter.  (At one time we tried to avoid
		 * memcpy when it was possible to enlarge the chunk in-place, but that
		 * turns out to misbehave unpleasantly for repeated cycles of
		 * palloc/repalloc/pfree: the eventually freed chunks go into the
		 * wrong freelist for the next initial palloc request, and so we leak
		 * memory indefinitely.  See pgsql-hackers archives for 2007-08-11.)
		 */
		AllocPointer newPointer;
		Size		oldsize;

		/* allocate new chunk (this also checks size is valid) */
		newPointer = AllocSetAlloc((MemoryContext) set, size, flags);

		/* leave immediately if request was not completed */
		if (newPointer == NULL)
		{
			/* Disallow access to the chunk header. */
			// VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);
			return MemoryContextAllocationFailure((MemoryContext) set, size, flags);
		}

		/*
		 * AllocSetAlloc() may have returned a region that is still NOACCESS.
		 * Change it to UNDEFINED for the moment; memcpy() will then transfer
		 * definedness from the old allocation to the new.  If we know the old
		 * allocation, copy just that much.  Otherwise, make the entire old
		 * chunk defined to avoid errors as we copy the currently-NOACCESS
		 * trailing bytes.
		 */
		// VALGRIND_MAKE_MEM_UNDEFINED(newPointer, size);
#ifdef MEMORY_CONTEXT_CHECKING
		oldsize = chunk->requested_size;
#else
		oldsize = oldchksize;
		// VALGRIND_MAKE_MEM_DEFINED(pointer, oldsize);
#endif

		/* transfer existing data (certain to fit) */
		memcpy(newPointer, pointer, oldsize);

		/* free old chunk */
		AllocSetFree(pointer);

		return newPointer;
	}
#endif
	return NULL;
}

/*
 * AllocSetGetChunkContext
 *		Return the MemoryContext that 'pointer' belongs to.
 */
MemoryContext
AllocSetGetChunkContext(void *pointer)
{
	MemoryChunk *chunk = PointerGetMemoryChunk(pointer);
	AllocBlock	block;
	AllocSet	set;
#if 0
	/* Allow access to the chunk header. */
	VALGRIND_MAKE_MEM_DEFINED(chunk, ALLOC_CHUNKHDRSZ);
#endif 
	if (MemoryChunkIsExternal(chunk))
		block = ExternalChunkGetBlock(chunk);
	else
		block = (AllocBlock) MemoryChunkGetBlock(chunk);
#if 0
	/* Disallow access to the chunk header. */
	VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);

	Assert(AllocBlockIsValid(block));
#endif 
	set = block->aset;

	return &set->header;
}

/*
 * AllocSetGetChunkSpace
 *		Given a currently-allocated chunk, determine the total space
 *		it occupies (including all memory-allocation overhead).
 */
Size
AllocSetGetChunkSpace(void *pointer)
{
	MemoryChunk *chunk = PointerGetMemoryChunk(pointer);
	int			fidx;
#if 0
	/* Allow access to the chunk header. */
	VALGRIND_MAKE_MEM_DEFINED(chunk, ALLOC_CHUNKHDRSZ);
#endif 
	if (MemoryChunkIsExternal(chunk))
	{
		AllocBlock	block = ExternalChunkGetBlock(chunk);
#if 0
		/* Disallow access to the chunk header. */
		VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);

		Assert(AllocBlockIsValid(block));
#endif 
		return block->endptr - (char *) chunk;
	}

	fidx = MemoryChunkGetValue(chunk);
#if 0
	Assert(FreeListIdxIsValid(fidx));

	/* Disallow access to the chunk header. */
	VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);
#endif 
	return GetChunkSizeFromFreeListIdx(fidx) + ALLOC_CHUNKHDRSZ;
}

/*
 * AllocSetIsEmpty
 *		Is an allocset empty of any allocated space?
 */
bool
AllocSetIsEmpty(MemoryContext context)
{
#if 0
	Assert(AllocSetIsValid(context));
#endif 
	/*
	 * For now, we say "empty" only if the context is new or just reset. We
	 * could examine the freelists to determine if all space has been freed,
	 * but it's not really worth the trouble for present uses of this
	 * functionality.
	 */
	if (context->isReset)
		return true;
	return false;
}

/*
 * AllocSetStats
 *		Compute stats about memory consumption of an allocset.
 *
 * printfunc: if not NULL, pass a human-readable stats string to this.
 * passthru: pass this pointer through to printfunc.
 * totals: if not NULL, add stats about this context into *totals.
 * print_to_stderr: print stats to stderr if true, elog otherwise.
 */
void
AllocSetStats(MemoryContext context,
			  MemoryStatsPrintFunc printfunc, void *passthru,
			  MemoryContextCounters *totals, bool print_to_stderr)
{
	AllocSet	set = (AllocSet) context;
	Size		nblocks = 0;
	Size		freechunks = 0;
	Size		totalspace;
	Size		freespace = 0;
	AllocBlock	block;
	int			fidx;
#if 0
	Assert(AllocSetIsValid(set));
#endif 
	/* Include context header in totalspace */
	totalspace = MAXALIGN(sizeof(AllocSetContext));

	for (block = set->blocks; block != NULL; block = block->next)
	{
		nblocks++;
		totalspace += block->endptr - ((char *) block);
		freespace += block->endptr - block->freeptr;
	}
	for (fidx = 0; fidx < ALLOCSET_NUM_FREELISTS; fidx++)
	{
		Size		chksz = GetChunkSizeFromFreeListIdx(fidx);
		MemoryChunk *chunk = set->freelist[fidx];

		while (chunk != NULL)
		{
			AllocFreeListLink *link = GetFreeListLink(chunk);
#if 0
			/* Allow access to the chunk header. */
			VALGRIND_MAKE_MEM_DEFINED(chunk, ALLOC_CHUNKHDRSZ);
			Assert(MemoryChunkGetValue(chunk) == fidx);
			VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);
#endif 
			freechunks++;
			freespace += chksz + ALLOC_CHUNKHDRSZ;

			// VALGRIND_MAKE_MEM_DEFINED(link, sizeof(AllocFreeListLink));
			chunk = link->next;
			// VALGRIND_MAKE_MEM_NOACCESS(link, sizeof(AllocFreeListLink));
		}
	}
#if 0
	if (printfunc)
	{
		char		stats_string[200];

		snprintf(stats_string, sizeof(stats_string),
				 "%zu total in %zu blocks; %zu free (%zu chunks); %zu used",
				 totalspace, nblocks, freespace, freechunks,
				 totalspace - freespace);
		printfunc(context, passthru, stats_string, print_to_stderr);
	}
#endif 
	if (totals)
	{
		totals->nblocks += nblocks;
		totals->freechunks += freechunks;
		totals->totalspace += totalspace;
		totals->freespace += freespace;
	}
}


#ifdef MEMORY_CONTEXT_CHECKING

/*
 * AllocSetCheck
 *		Walk through chunks and check consistency of memory.
 *
 * NOTE: report errors as WARNING, *not* ERROR or FATAL.  Otherwise you'll
 * find yourself in an infinite loop when trouble occurs, because this
 * routine will be entered again when elog cleanup tries to release memory!
 */
void
AllocSetCheck(MemoryContext context)
{
	AllocSet	set = (AllocSet) context;
	const char *name = set->header.name;
	AllocBlock	prevblock;
	AllocBlock	block;
	Size		total_allocated = 0;

	for (prevblock = NULL, block = set->blocks;
		 block != NULL;
		 prevblock = block, block = block->next)
	{
		char	   *bpoz = ((char *) block) + ALLOC_BLOCKHDRSZ;
		long		blk_used = block->freeptr - bpoz;
		long		blk_data = 0;
		long		nchunks = 0;
		bool		has_external_chunk = false;

		if (IsKeeperBlock(set, block))
			total_allocated += block->endptr - ((char *) set);
		else
			total_allocated += block->endptr - ((char *) block);

		/*
		 * Empty block - empty can be keeper-block only
		 */
		if (!blk_used)
		{
			if (!IsKeeperBlock(set, block))
				elog(WARNING, "problem in alloc set %s: empty block %p",
					 name, block);
		}

		/*
		 * Check block header fields
		 */
		if (block->aset != set ||
			block->prev != prevblock ||
			block->freeptr < bpoz ||
			block->freeptr > block->endptr)
			elog(WARNING, "problem in alloc set %s: corrupt header in block %p",
				 name, block);

		/*
		 * Chunk walker
		 */
		while (bpoz < block->freeptr)
		{
			MemoryChunk *chunk = (MemoryChunk *) bpoz;
			Size		chsize,
						dsize;

			/* Allow access to the chunk header. */
			VALGRIND_MAKE_MEM_DEFINED(chunk, ALLOC_CHUNKHDRSZ);

			if (MemoryChunkIsExternal(chunk))
			{
				chsize = block->endptr - (char *) MemoryChunkGetPointer(chunk); /* aligned chunk size */
				has_external_chunk = true;

				/* make sure this chunk consumes the entire block */
				if (chsize + ALLOC_CHUNKHDRSZ != blk_used)
					elog(WARNING, "problem in alloc set %s: bad single-chunk %p in block %p",
						 name, chunk, block);
			}
			else
			{
				int			fidx = MemoryChunkGetValue(chunk);

				if (!FreeListIdxIsValid(fidx))
					elog(WARNING, "problem in alloc set %s: bad chunk size for chunk %p in block %p",
						 name, chunk, block);

				chsize = GetChunkSizeFromFreeListIdx(fidx); /* aligned chunk size */

				/*
				 * Check the stored block offset correctly references this
				 * block.
				 */
				if (block != MemoryChunkGetBlock(chunk))
					elog(WARNING, "problem in alloc set %s: bad block offset for chunk %p in block %p",
						 name, chunk, block);
			}
			dsize = chunk->requested_size;	/* real data */

			/* an allocated chunk's requested size must be <= the chsize */
			if (dsize != InvalidAllocSize && dsize > chsize)
				elog(WARNING, "problem in alloc set %s: req size > alloc size for chunk %p in block %p",
					 name, chunk, block);

			/* chsize must not be smaller than the first freelist's size */
			if (chsize < (1 << ALLOC_MINBITS))
				elog(WARNING, "problem in alloc set %s: bad size %zu for chunk %p in block %p",
					 name, chsize, chunk, block);

			/*
			 * Check for overwrite of padding space in an allocated chunk.
			 */
			if (dsize != InvalidAllocSize && dsize < chsize &&
				!sentinel_ok(chunk, ALLOC_CHUNKHDRSZ + dsize))
				elog(WARNING, "problem in alloc set %s: detected write past chunk end in block %p, chunk %p",
					 name, block, chunk);

			/* if chunk is allocated, disallow access to the chunk header */
			if (dsize != InvalidAllocSize)
				VALGRIND_MAKE_MEM_NOACCESS(chunk, ALLOC_CHUNKHDRSZ);

			blk_data += chsize;
			nchunks++;

			bpoz += ALLOC_CHUNKHDRSZ + chsize;
		}

		if ((blk_data + (nchunks * ALLOC_CHUNKHDRSZ)) != blk_used)
			elog(WARNING, "problem in alloc set %s: found inconsistent memory block %p",
				 name, block);

		if (has_external_chunk && nchunks > 1)
			elog(WARNING, "problem in alloc set %s: external chunk on non-dedicated block %p",
				 name, block);
	}

	Assert(total_allocated == context->mem_allocated);
}

#endif							/* MEMORY_CONTEXT_CHECKING */


MemPoolContext zt_mempool_create(const char* mempool_name, U32 minContextSize, U32 initBlockSize, U32 maxBlockSize)
{
	MemoryContext cxt;

	if (0 == initBlockSize)
		initBlockSize = ALLOCSET_DEFAULT_INITSIZE;
	if (0 == maxBlockSize)
		maxBlockSize = ALLOCSET_DEFAULT_MAXSIZE;

	cxt = AllocSetContextCreateInternal(NULL, mempool_name, minContextSize, initBlockSize, maxBlockSize);

	return (MemPoolContext)cxt;
}

void zt_mempool_destroy(MemPoolContext cxt)
{
	if (cxt)
	{
		MemoryContext context = (MemoryContext)cxt;
		context->methods->delete_context(context);
	}
}

void* zt_palloc(MemPoolContext cxt, size_t size)
{
	/* duplicates MemoryContextAlloc to avoid increased overhead */
	void* ret = NULL;

	if (cxt)
	{
		MemoryContext context = (MemoryContext)cxt;

		context->isReset = false;

		/*
		 * For efficiency reasons, we purposefully offload the handling of
		 * allocation failures to the MemoryContextMethods implementation as this
		 * allows these checks to be performed only when an actual malloc needs to
		 * be done to request more memory from the OS.  Additionally, not having
		 * to execute any instructions after this call allows the compiler to use
		 * the sibling call optimization.  If you're considering adding code after
		 * this call, consider making it the responsibility of the 'alloc'
		 * function instead.
		 */
		ret = context->methods->alloc(context, size, 0);
	}

	return ret;
}

void* zt_palloc0(MemPoolContext cxt, size_t size)
{
	/* duplicates MemoryContextAlloc to avoid increased overhead */
	void* ret = NULL;

	if (cxt)
	{
		MemoryContext context = (MemoryContext)cxt;

		context->isReset = false;

		/*
		 * For efficiency reasons, we purposefully offload the handling of
		 * allocation failures to the MemoryContextMethods implementation as this
		 * allows these checks to be performed only when an actual malloc needs to
		 * be done to request more memory from the OS.  Additionally, not having
		 * to execute any instructions after this call allows the compiler to use
		 * the sibling call optimization.  If you're considering adding code after
		 * this call, consider making it the responsibility of the 'alloc'
		 * function instead.
		 */
		ret = context->methods->alloc(context, size, 0);

		if (ret)
		{
			MemSetAligned(ret, 0, size);
		}
	}

	return ret;
}

void zt_pfree(void* pointer)
{
	if (pointer)
	{
		MCXT_METHOD(pointer, free_p) (pointer);
	}
}
