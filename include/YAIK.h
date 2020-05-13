//--------------------------------------------------------------------------------------------------------------------
//  [Public header for user of YAIK Library]
//  C Style interface
//--------------------------------------------------------------------------------------------------------------------

#ifndef YAIK_PUBLIC_HEADER
#define YAIK_PUBLIC_HEADER

// NULL definition
#include <stddef.h>

// Allow use 'bool' type if not in C++
#ifndef __cplusplus
	#include "stdbool.h"
#endif

// Wrap standard integer type definition into a shorter easier to maintain type.
#include <inttypes.h>
typedef uint8_t				u8;
typedef int8_t				s8;
typedef uint16_t			u16;
typedef int16_t				s16;
typedef uint32_t			u32;
typedef int32_t				s32;
typedef uint64_t			u64;

// Wrap Decoder context into type.
typedef void*				YAIK_LIB;

struct DecodedImage {
	u16		width;					// 1. data filled by Pre function.
	u16		height;					// 1. data filled by Pre function.
	u32		temporaryMemoryAmount;	// 1. data filled by Pre function.

	u8*		tempMemory;				// 2. Buffer allocated by user. 3. Filled by decoder function.
	u8*		outputImage;			// 2. Buffer allocated by user, 3. Filled by decoder function.

	bool	hasAlpha;				// 3. Image Format : RGB or RGBA ? (Decoder function)
	bool	hasAlpha1Bit;			// 3. More info for user if needed.

	void*	internalTag;			// [NEVER TOUCH]
};

//--------------------------------------------------------------------------------------------------------------------
//  NOT multithread safe APIs
//--------------------------------------------------------------------------------------------------------------------

/* 	Get the memory amount needed for the library itself, use return value to allocate memory.
	The library does NOT use any memory allocation.																	*/
u32				YAIK_GetLibraryMemoryAmount	(u8 maxDecodeThreadContext);

/*	Pass context to initialize the library. (Allow user to put his own info into the library context)
	Call only once !																								*/
YAIK_LIB		YAIK_Init					(void* libMemory, u8 maxDecodeThreadContext);

/*	Release the library																								*/
void			YAIK_Release				(YAIK_LIB lib);

//--------------------------------------------------------------------------------------------------------------------
//  From here multithread safe
//--------------------------------------------------------------------------------------------------------------------

/*	Call first to get image size.																					*/
bool			YAIK_DecodeImagePre			(YAIK_LIB libMemory, void* sourceStreamAligned, u32 streamLength, DecodedImage& getUserInfo);

/*	Call second to decompress the stream into your buffer. And release the YAIK_INST context in any case
	Note : Could have copied the values from YAIK_DecodeImagePre call with 'sourceStreamAligned' and 'length' 
	inside the internal context. But by API design, we force the user to understand that the stream can not 
	be freed until this call.																						*/
bool			YAIK_DecodeImage			(void* sourceStreamAligned, u32 streamLength, DecodedImage & context);

//--------------------------------------------------------------------------------------------------------------------
//  Multithread safe.
//--------------------------------------------------------------------------------------------------------------------

enum YAIK_ERROR_CODE {
	YAIK_NO_ERROR = 0,			// [NO ERROR]
	YAIK_INVALID_CONTEXT_COUNT,	// Impossible to init library with 0 max context.
	YAIK_INIT_FAIL,				// NULL pointer was passed for memory allocation.
	YAIK_RELEASE_EMPTY_LIBRARY,	// Try to release a library which was not allocated.
	YAIK_INVALID_STREAM,		// Try to use a stream that is NULL or too small.
	YAIK_INVALID_HEADER,		// Input File format not recognized.
	YAIK_NO_EMPTYDECODE_SLOT,	// No more available slot for decoding. => Decoding too many images at the same time, or not enough decode context (See YAIK_Init)
	YAIK_DECIMG_INVALIDCTX,		// Try to call DecodeImage with an invalid context.
	YAIK_DECIMG_DIFFSTREAM,		// Stream lenght or pointer changed between called to Pre and DecodeImage.
	YAIK_INVALID_MIPMAP_LEVEL,	// Internal to encoder/decoder size.
	YAIK_ALPHA_FORMAT_IMPOSSIBLE,	// Try to use mipmap mask for alpha decompression when mipmap mask WAS NOT in the file.
	YAIK_INVALID_ALPHA_FORMAT,	// Unknown alpha format. type=7
	YAIK_ALPHA_UNSUPPORTED_YET,	// Try to use 1 Bit + MipmapMask, not implemented yet.
	YAIK_INVALID_TAG_ID,		// Unknown chunk format.
	YAIK_DECIMG_BUFFERNOTSET,	// RGBA buffer from user is not set.
	YAIK_INVALID_PLANE_ID,		// Invalid Plane ID in Plane Chunk.
};

/* Get Error Code if failure in any API																				*/
YAIK_ERROR_CODE	YAIK_GetErrorCode			(const char** messageOut);

#endif // YAIK_PUBLIC_HEADER
