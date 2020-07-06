// ###################################################################################################################
//  [Public header for user of YAIK Library]
//  C Style interface
// ###################################################################################################################

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
typedef uint8_t              u8;
typedef int8_t               s8;
typedef uint16_t            u16;
typedef int16_t             s16;
typedef uint32_t            u32;
typedef int32_t             s32;
typedef uint64_t            u64;



// ###################################################################################################################
//  Parameters for APIs.
// ###################################################################################################################

// Wrap Internal Context into type.
typedef void*   YAIK_LIB;
typedef void*   YAIK_INSTANCE;

// [Forward Declaration]
struct YAIK_SDecodedImage;
struct YAIK_SMemAlloc;

// ###################################################################################################################
//  APIs (NOT multithread safe)
// ###################################################################################################################

/*  Initialize the library.
    If you plan to use multiple threads to decode images, setup the maximum amount of threads you plan to use.
    It will limit the number of call you can make to YAIK_DecodeImagePer in parallel.

    Optionnally, you can pass your own memory allocator. (NULL will provide default)
    Note :  The memory allocator passed here is ONLY FOR THE LIBRARY ITSELF, NOT FOR IMAGE DECODING.
            IMAGE DECODING ALLOCATION SCHEME IS SEPERATED.                                                          */
YAIK_LIB        YAIK_Init                   (u8                     maxDecodeThreadContext
                                            ,YAIK_SMemAlloc*        libraryMemAllocator);

/*  Now that the context is created, user must pass the tables needed for decoder.
    Those tables are big enough and can updated with compressor version without changing the binary.
    So API allows to setup LUT data seperatly from YAIK_Init and not internally.                                    
    
    Note : LUT Values are freed when YAIK_Release is called.
    Note : Memory allocator can be set to NULL, will internally use default.
    Note : If user passes a memory allocator, the context is copied. So YAIK_SMemAlloc* itself can be destroyed.
           But the content of the YAIK_SMemAlloc MUST STAY VALID for as long as the library is alive.
*/
void            YAIK_AssignLUT              (YAIK_LIB               lib
                                            ,u8*                    lutData
                                            ,u32                    lutDataLength);

/*  Release the library                                                                                             */
void            YAIK_Release                (YAIK_LIB               lib);

// ###################################################################################################################
//  APIs (Multithread safe)
// ###################################################################################################################

/*  First, call to query image size. As user provide the end buffer, we need to first return information to do so.
    This call ALSO allocate an internal context : A call to YAIK_DecodeImagePre MUST BE FOLLOWED 
    by a call to YAIK_DecodeImage.                                                                                  
    
	IMPORTANT : Do not provide the memory allocator in this call, information will be overwritten here.
	            Memory allocator has to be provided at the second stage.

    Note : The YAIK image stream must be aligned to 4 bytes in memory.                                              */
bool            YAIK_DecodeImagePre         (YAIK_LIB               libMemory
                                            ,void*                  sourceStreamAligned
                                            ,u32                    streamLength
                                            ,YAIK_SDecodedImage*    getUserInfo);

/*  Then call to decompress the stream into your buffer. It will also release the context in any case
    Note : Could have copied the values from YAIK_DecodeImagePre call with 'sourceStreamAligned' and 'length' 
    inside the internal context. But by API design, we force the user to understand that the stream can not 
    be freed until this call.

    Before doing the call, the user will provide (through the YAIK_SDecodedImage struct) :
    - The target buffer for the decoded image (MUST)
    - Callback function to write in a custom manner to the target buffer (OPTIONNAL, See imageBuilderFunc)
    - Provide a memory allocator. (OPTIONNAL)                                                                       
    
    Obviously a call to YAIK_DecodeImage is impossible without a call to YAIK_DecodeImagePre first.                 */
bool            YAIK_DecodeImage            (void*                  sourceStreamAligned
                                            ,u32                    streamLength
                                            ,YAIK_SDecodedImage*    context);

enum YAIK_ERROR_CODE {
    // PUBLIC ERRORS
    YAIK_NO_ERROR = 0,                      // [NO ERROR]
    YAIK_INVALID_LIBRARYCTX,                // Invalid Library Context.
    YAIK_MALLOC_FAIL,                       // Impossible to allocate the needed memory.
    YAIK_INVALID_CONTEXT_COUNT,             // Impossible to init library with 0 max context.
    YAIK_INIT_FAIL,                         // NULL pointer was passed for memory allocation.
    YAIK_RELEASE_EMPTY_LIBRARY,             // Try to release a library which was not allocated.
    YAIK_INVALID_STREAM,                    // Try to use a stream that is NULL or too small.
    YAIK_INVALID_HEADER,                    // Input File format not recognized.
    YAIK_NO_EMPTYDECODE_SLOT,               // No more available slot for decoding. => Decoding too many images at the same time, or not enough decode context (See YAIK_Init)
    YAIK_DECIMG_INVALIDCTX,                 // Try to call YAIK_SDecodeImage with an invalid context.
    YAIK_DECIMG_DIFFSTREAM,                 // Stream lenght or pointer changed between called to YAIK_DecodeImagePre and YAIK_DecodeImage.
    YAIK_DECIMG_BUFFERNOTSET,               // RGBA buffer from user is not set.
    YAIK_INVALID_CONTEXT_MEMALLOCATOR,      // Invalid custom setup of the memory allocator.
    YAIK_INVALID_DECOMPRESSION,             // Invalid stream that could not be decompressed by the compression library.
    YAIK_INVALID_LUT,                       // Problem decompressing the LUT file.

    // INTERNAL ERRORS
    YAIK_DECOMPRESSION_CREATE_FAIL,         
    YAIK_INVALID_MIPMAP_LEVEL,
    YAIK_ALPHA_FORMAT_IMPOSSIBLE,
    YAIK_INVALID_ALPHA_FORMAT,
    YAIK_ALPHA_UNSUPPORTED_YET,
    YAIK_INVALID_TAG_ID,
    YAIK_INVALID_PLANE_ID,
};

/*  Get error Code to check for failure in any API of YAIK.
    This is a 'sticky' error code : the first call to fail will set the code and stay until user read it.

    If multiple failure occurs (multiple call to APIs), it is only the first error code that the user is going
    to see, not the last one. It will be reset by calling the function.
	
	The error code may be lost if multiple thread decode at the same time and both generate an error.
	Probability is very low, yet a thread error could shadow/override a completely different thread error.
	
	Obviously, a thread could decode with no error but call to this function return a error for another thread.
	PLEASE CHECK the decoder boolean return value for successfull completion before checking the error code.
*/
YAIK_ERROR_CODE YAIK_GetErrorCode           ();

// ###################################################################################################################
//  Structure definition and user-customization
// ###################################################################################################################

// -------------------------------------------------------------------------------------------------------------------
// ---- User Custom functions & context for memory allocator (OPTIONNAL) ---------------------------------------------
// -------------------------------------------------------------------------------------------------------------------
typedef void*   (*YAIK_allocFunc) (void* customContext, size_t size     );
typedef void    (*YAIK_freeFunc ) (void* customContext, void*  address  );
// Note : YAIK_freeFunc MUST support NULL ptr as a valid parameter.
struct YAIK_SMemAlloc {
	YAIK_SMemAlloc():customAlloc(0),customFree(0),customContext(0) {}

    YAIK_allocFunc  customAlloc;
    YAIK_freeFunc   customFree;
    void*           customContext; 
};
// -------------------------------------------------------------------------------------------------------------------



// -------------------------------------------------------------------------------------------------------------------
// ---- YAIK_SDecodedImage Structure used by API calls.        -------------------------------------------------------
// -------------------------------------------------------------------------------------------------------------------
// [Forward Declaration]
struct YAIK_SCustomDataSource;

/*  This function pointer type is for making your own custom writer of the decoded image to the memory.
    (See YAIK_DecodeImage function and DecodedImage structure).
    It can be setup inside the YAIK_SDecodedImage structure.                                                    */
typedef void    (*imageBuilderFunc)(struct YAIK_SDecodedImage* userInfo, struct YAIK_SCustomDataSource* sourceImageInternal );

struct YAIK_SDecodedImage {
    u16                 width;                  // 1. data filled by Pre function.
    u16                 height;                 // 1. data filled by Pre function.
    bool                hasAlpha;               // 1. data filled by Pre function.

    imageBuilderFunc    customImageOutput;      // 2. Pre function set to NULL by default, override after Pre function.
                                                // Allow to write any custom image exported if format is different from 
                                                // standard RGB888 or RGBA8888 format. (Ex. export to 565 or reverse BGR order)
    void*               userContextCustomImage; // 2. When using a custom image output, user can set his own context if needed.

    YAIK_SMemAlloc      userMemoryAllocator;    // 2. User can override the default value to put own memory allocation scheme.

    u8*                 outputImage;            // 2. Buffer allocated by user, 3. Filled by decoder function.
    s32                 outputImageStride;      // 2. Value set by user, allow to have YAIK put the imagine INSIDE another buffer at a precise location.

    bool                hasAlpha1Bit;           // 3. More info for user if needed.
    YAIK_INSTANCE       internalTag;            // [NEVER TOUCH, filled by YAIK_DecodeImagePre]
};
// -------------------------------------------------------------------------------------------------------------------



// -------------------------------------------------------------------------------------------------------------------
// --------- User Custom feature to transfer end result in own format (OPTIONNAL) ----------------------------------- 
// -------------------------------------------------------------------------------------------------------------------
/*  This is the structure received by the callback function (See imageBuilderFunc) when making your own custom writer.                      
    User will receive the internal buffers and copy the RGB(A) values to its own buffer
    See internal description in detail. If your target image size is not a multiple of 8, care must be taken.       */
struct YAIK_SCustomDataSource {
    // - Source Image is composed of tiles of 8x8 pixels for R,G,B plane.
    // - The tile order is from left to right, then top to bottom.
    // - First tile is coordinate 0,0 of the image.
    // - So internally, an image is ALWAYS a multiple of 8 pixels.
    // - Obviously, R,G,B planes are seperated. Not interleaved.
    // - If Alpha plane exist (planeA != NULL), it is a simple 8 bit left to right, top to bottom linear buffer.
    // - Values are simple 8 bit 0..255 range as usual.
    u8*                 planeR;
    u8*                 planeG;
    u8*                 planeB;
    u8*                 planeA;

    // Distance in byte to the next TILE line.
    s32                 strideR;
    s32                 strideG;
    s32                 strideB;
    // Distance in byte to the next line.
    s32                 strideA;
};
// -------------------------------------------------------------------------------------------------------------------

#endif // YAIK_PUBLIC_HEADER
