#include "YAIK_functions.h"


#ifdef YAIK_DEVEL
	//---------------------------------------------------------------------------------------------
	// TEMPORARY STUFF For debug.
	//---------------------------------------------------------------------------------------------
	#include <Windows.h>
	#define CHECK_MEM		_CrtCheckMemory();

#include <stdio.h>
	#define PRINTF	printf
#else
	#define PRINTF
#endif

// memset, memcpy
#include <memory.h>
// 
#include "../external/zstd/zstd.h"

YAIK_ERROR_CODE	gErrorCode = YAIK_NO_ERROR;
YAIK_Library	gLibrary;

//-------------------------------------------------------------------------------------

YAIK_ERROR_CODE YAIK_GetErrorCode	() {
	YAIK_ERROR_CODE res = gErrorCode;
	gErrorCode = YAIK_NO_ERROR;
	return res;
}

void			SetErrorCode(YAIK_ERROR_CODE error) {
	// Sticky bit...
	if (gErrorCode == YAIK_NO_ERROR) {
		gErrorCode = error;
	}
}

//-------------------------------------------------------------------------------------
// Wrapper function to use memory allocators.
// and other function used in this source file.
//-------------------------------------------------------------------------------------
u8*  AllocateMem (YAIK_SMemAlloc* allocCtx, size_t size) {
	u8* res = (u8*)allocCtx->customAlloc(allocCtx, size);
	if (!res) {
		// Set sticky error code internally if return NULL.
		SetErrorCode(YAIK_MALLOC_FAIL);
	}
	return res;
}

void FreeMem	 (YAIK_SMemAlloc* allocCtx, void*  ptr ) {
	// NULL SAFE. Allow cleaner code path
	if (ptr) { allocCtx->customFree(allocCtx, ptr); }
}

YAIK_Instance* YAIK_Library::AllocInstance() {
	// [TODO : Use MUTEX ? ATOMIC]. What happen if go too far ? (Use 257 entries with GUARD NULL ?)
	if (gLibrary.freeSlotCount > 0) {
		return freeStack[--gLibrary.freeSlotCount];
	} else {
		return NULL;
	}
}

void			YAIK_Library::FreeInstance(YAIK_Instance* inst) {
	// [TODO : Support multithreading... : MUTEX/ATOMIC]
	kassert(inst != NULL); // CALLER CHECK !
	if (inst) {
		freeStack[gLibrary.freeSlotCount++] = inst;
	}
}

void kassert(bool validCond) {
	if (!validCond) {
		PRINTF("Error");
		while (1) {
		}
	}
}

// -----------------------------------------------------------------------------------------------
//		YAIK_Init
// -----------------------------------------------------------------------------------------------
YAIK_LIB		YAIK_Init			(u8 maxDecodeThreadContext, YAIK_SMemAlloc* allocator) {
	if (maxDecodeThreadContext) {
		SetErrorCode(YAIK_NO_ERROR);
		if (allocator == NULL) {
			// Setup Default Memory Allocator.
			gLibrary.libraryAllocator.customContext	= NULL;
			gLibrary.libraryAllocator.customAlloc	= internal_allocFunc;
			gLibrary.libraryAllocator.customFree	= internal_freeFunc;
		} else {
			// Setup User Memory Allocator.
			gLibrary.libraryAllocator = *allocator;
		}
		gLibrary.instances			= NULL;
		gLibrary.totalSlotCount		= maxDecodeThreadContext;
		gLibrary.freeSlotCount		= 0;

		for (int n=0; n < 4; n++) {
			gLibrary.LUT3D_BitFormat[n] = NULL;
			// gLibrary.LUT2D_BitFormat[n] = NULL; DEPRECATED.
		}

		u8* memory = AllocateMem(&gLibrary.libraryAllocator, maxDecodeThreadContext * sizeof(YAIK_Instance) );

		if (memory) {
			// Return the amount of memory required to perform library init.
			gLibrary.totalSlotCount = maxDecodeThreadContext;
			gLibrary.freeSlotCount	= maxDecodeThreadContext;
			gLibrary.instances		= (YAIK_Instance*)memory;
			for (int n = 0; n < maxDecodeThreadContext; n++) {
				gLibrary.freeStack[n] = &gLibrary.instances[n];
			}

			return &gLibrary;
		} else {
			SetErrorCode(YAIK_INIT_FAIL);
			return NULL;
		}
	} else {
		SetErrorCode(YAIK_INVALID_CONTEXT_COUNT);
		return NULL;
	}
}

// -----------------------------------------------------------------------------------------------
//		YAIK_AssignLUT
// -----------------------------------------------------------------------------------------------

void			YAIK_AssignLUT		(YAIK_LIB lib, u8* lutDataCompressed, u32 lutDataCompressedLength) {
	if (lib) {
		// First, header the beginning of LUT file.
		LUTHeader*	pHeader		= (LUTHeader*)lutDataCompressed;
		u8*			stream		= (u8*)&pHeader[1];
		u32			streamSize	= lutDataCompressedLength - sizeof(LUTHeader);
		YAIK_Library* pLib		= (YAIK_Library*)lib;

		int itemSize		= 0;
		int maxItemCount	= 0;
		int maxPatternCount = 0;
		int patternCount	= 0;
		int allocSize;
		bool is3DTbl = false;

		if (pHeader->lutH[0] == 'L' && pHeader->lutH[1]=='U') {
			switch (pHeader->lutH[2]) {
			case 'L' : 
				itemSize		= 3;
				is3DTbl			= true;
				maxItemCount	= 256;	// MAX LUT TABLE AMOUNT
				maxPatternCount = 64;	// Not 48 ! Leave empty space. Fully adressible space is NOT 48 pattern but 64.
				patternCount	= 6;
				/*
					[For each 3 to 6 Bit]
						[Table 0 Pattern 0  ]
							[V0][V1][V2]      <--- Length of 8/16/32/64 entries.
							...
							...
						[Table 0 Pattern 1  ]
						[Table 0 Pattern ...]
						[Table 0 Pattern 47 ]
						----
						[Empty Space, equivalent to pattern 48..63 ]
						----
						[Table 1 Pattern 0  ]
						[Table 1 Pattern 1  ]
						[Table 1 Pattern ...]
						[Table 1 Pattern 47 ]
						----
						[Empty Space, equivalent to pattern 48..63 ]
						----
						...
						...
						...
						----
						[Table 255 Pattern 0  ]
						[Table 255 Pattern 1  ]
						[Table 255 Pattern ...]
						[Table 255 Pattern 47 ]
						----
						[Empty Space, equivalent to pattern 48..63 ]

						We allocate more in case we read further than 8/16/32/64 entries (+256 entries).
						So 3D LUT Decoder do not need to check memory when reading pixel per pixel during decode.
						It is just a precaution against voluntary attacks or bad encoding. Should never happen anyway.
				 */

				// [Copy paste code to avoid compiler warning about overflow.]
				allocSize		= (((64+32+16+8)*itemSize) * maxItemCount * maxPatternCount) + (256*itemSize);
				break;
			case '2' :
				itemSize		= 2;
				is3DTbl			= false;
				maxItemCount	= 2048;	// MAX LUT TABLE AMOUNT (Accessible in DECODER stream). But this loader ALLOWS 256 TABLE MAX ANYWAY
				maxPatternCount = 8;
				patternCount	= 2;
				/*
					[For each 3 to 6 Bit]
						[Table 0 Pattern 0  ]
							[V0][V1]			<--- Length of 8/16/32/64 entries. 
							...
							...
						[Table 0 Pattern 1  ]
						[Table 0 Pattern ...]
						[Table 0 Pattern 7  ]
						----
						[Table 1 Pattern 0  ]
						[Table 1 Pattern 1  ]
						[Table 1 Pattern ...]
						[Table 1 Pattern 7  ]
						----
						...
						...
						...
						----
						[Table 2047 Pattern 0  ]
						[Table 2047 Pattern 1  ]
						[Table 2047 Pattern ...]
						[Table 2047 Pattern 47 ]

						We allocate more in case we read further than 8/16/32/64 entries (+256 entries).
						So 3D LUT Decoder do not need to check memory when reading pixel per pixel during decode.
						It is just a precaution against voluntary attacks or bad encoding. Should never happen anyway.
				 */

				// [Copy paste code to avoid compiler warning about overflow.]
				allocSize		= (((64+32+16+8)*itemSize) * maxItemCount * maxPatternCount) + (256*itemSize);

				// 2D LUT DEPRECATED FOR NOW.
				SetErrorCode(YAIK_INVALID_LUT);
				return; // break;
			default:
				// Unsupported LUT Type.
				SetErrorCode(YAIK_INVALID_LUT);
				return;
			}
		} else {
			// Not a valid LUT File.
			SetErrorCode(YAIK_INVALID_LUT);
			return;
		}

		u32			expectedSize= (pHeader->entryCount+1) * (itemSize*(64+32+16+8));
		if (expectedSize != streamSize) {
			SetErrorCode(YAIK_INVALID_LUT);
			return;
		}
		
		// Values in file are u8...
		// Actually, for SECURITY REASON, we will allocate the WHOLE possible adressable space
		// from any stream. It will guarantee that no buffer overflow is possible on that side.
		// We also add 256x3 byte to make sure that NO INDEX STREAM PATCHED with malicious or corrupted data don't read outside of the space.
		//

		u8* memory = AllocateMem(&gLibrary.libraryAllocator, allocSize); 

		if (memory) {
			u8* pFill = memory;

			for (int bit=3; bit<=6; bit++) {
				pLib->LUT3D_BitFormat[bit-3] = pFill;
				/*if (is3DTbl) {   DEPRECATED, ONLY 3D SUPPORTED NOW.
				 
				} else {
					pLib->LUT2D_BitFormat[bit-3] = pFill;
				} */

				// Note : See 2D Table, we support up to 2048 entries in 2D case, but this header version
				// limit to load 256 Tables, even in 2D mode.
				for (int e=0; e <= pHeader->entryCount; e++) {
					u8* originalX = &stream   [0];
					u8* originalY = &stream   [1<<bit];
					u8* originalZ = is3DTbl ? &originalY[1<<bit] : NULL;

#ifdef YAIK_DEVEL
					for (int n=0; n < (1<<bit); n++) {
						if ((originalX[n] > 128) || (originalY[n] > 128) || (originalZ && (originalZ[n] > 128))) {
							PRINTF("ERROR");
							while (true) {
							}
						}
					}
#endif
					
					// Note : Code performance is poor, no unrolled loop.
					//        But run only once per file loaded.
					//        And keep code easier to maintain and generated code compact.
					for (int pat=0; pat < patternCount; pat++) {
						u8* TblX;
						u8* TblY;
						u8* TblZ;

						switch (pat) {
						case 0: // Do nothing. 
							TblX = originalX;
							TblY = originalY;
							TblZ = originalZ;
							break;
						case 1:
							if (is3DTbl) {
								// X[ZY] For 3D
								TblX = originalX;
								TblY = originalZ;
								TblZ = originalY;
							} else {
								// XY->YX for 2D
								TblX = originalY;
								TblY = originalX;
								TblZ = originalZ;
							}
							break;

						// Further cases are only reachable for 3D LUT...
						case 2: // [YX]Z
							TblX = originalY;
							TblY = originalX;
							TblZ = originalZ;
							break;
						case 3: // YZX
							TblX = originalY;
							TblY = originalZ;
							TblZ = originalX;
							break;
						case 4: // ZXY
							TblX = originalZ;
							TblY = originalX;
							TblZ = originalY;
							break;
						case 5: // ZYX
							TblX = originalZ;
							TblY = originalY;
							TblZ = originalX;
							break;
						DEFAULT_UNREACHABLE;
						}

						// case  0: 
						for (int idx=0; idx < (1<<bit); idx++) {
							// 3 Value
							*pFill++ = TblX[idx];
							*pFill++ = TblY[idx];
							if (is3DTbl) {
								*pFill++ = TblZ[idx];
							}
						}
						// case  1:
						for (int idx=0; idx < (1<<bit); idx++) {
							// 3 Value
							*pFill++ = 128 - TblX[idx];
							*pFill++ = TblY[idx];
							if (is3DTbl) {
								*pFill++ = TblZ[idx];
							}
						}
						// case  2:
						for (int idx=0; idx < (1<<bit); idx++) {
							// 3 Value
							*pFill++ = TblX[idx];
							*pFill++ = 128 - TblY[idx];
							if (is3DTbl) {
								*pFill++ = TblZ[idx];
							}
						}
						// case  3:
						for (int idx=0; idx < (1<<bit); idx++) {
							// 3 Value
							*pFill++ = 128 - TblX[idx];
							*pFill++ = 128 - TblY[idx];
							if (is3DTbl) {
								*pFill++ = TblZ[idx];
							}
						}

						if (is3DTbl) {
							// case  4:
							for (int idx=0; idx < (1<<bit); idx++) {
								// 3 Value
								*pFill++ = TblX[idx];
								*pFill++ = TblY[idx];
								*pFill++ = 128 - TblZ[idx];
							}
							// case  5:
							for (int idx=0; idx < (1<<bit); idx++) {
								// 3 Value
								*pFill++ = 128 - TblX[idx];
								*pFill++ = TblY[idx];
								*pFill++ = 128 - TblZ[idx];
							}
							// case  6:
							for (int idx=0; idx < (1<<bit); idx++) {
								// 3 Value
								*pFill++ = TblX[idx];
								*pFill++ = 128 - TblY[idx];
								*pFill++ = 128 - TblZ[idx];
							}
							// case  7:
							for (int idx=0; idx < (1<<bit); idx++) {
								// 3 Value
								*pFill++ = 128 - TblX[idx];
								*pFill++ = 128 - TblY[idx];
								*pFill++ = 128 - TblZ[idx];
							}
						}
					}

					if (is3DTbl) {
						// Fill empty space in 3D, none in 2D.
						memset(pFill, 251, 16*(3*(1<<bit))); // Fill GARBAGE FOR NOW.
						pFill  += 16*(3*(1<<bit));
					}

					stream += ((1<<bit)*itemSize); // +24,+48,+96,+192 in 3D, +16,+32,+64,+128 in 2D
				}
			}
		} else {
			SetErrorCode(YAIK_MALLOC_FAIL);
		}
	} else {
		SetErrorCode(YAIK_INVALID_LIBRARYCTX);
	}
}

// -----------------------------------------------------------------------------------------------
//		YAIK_Release
// -----------------------------------------------------------------------------------------------
void			YAIK_Release		(YAIK_LIB lib) {
	if (lib) {
		YAIK_Library* pLib = (YAIK_Library*)lib;
		FreeMem(&pLib->libraryAllocator,pLib->instances);
	} else {
		SetErrorCode(YAIK_RELEASE_EMPTY_LIBRARY);
	}
}

// -----------------------------------------------------------------------------------------------
//		YAIK_DecodeImagePre
// -----------------------------------------------------------------------------------------------
bool			YAIK_DecodeImagePre	(YAIK_LIB libMemory, void* sourceStreamAligned, u32 streamLength, YAIK_SDecodedImage* pFill) {
	YAIK_SDecodedImage& fillSize = *pFill;

	fillSize.hasAlpha		= false;	// Fill later, reset for now.
	fillSize.hasAlpha1Bit	= false;	// Fill later, reset for now.
	fillSize.outputImage	= NULL;		// User can NOT put a buffer in there yet anyway.
	fillSize.width			= 0;
	fillSize.height			= 0;
	fillSize.internalTag	= NULL;

	FileHeader* pHeader = (FileHeader*)sourceStreamAligned;
	if (pHeader && (streamLength > sizeof(FileHeader))) {
		if ((pHeader->tag.tag8[0] == 'Y') &&
			(pHeader->tag.tag8[1] == 'A') &&
			(pHeader->tag.tag8[2] == 'I') &&
			(pHeader->tag.tag8[3] == 'K')) {

			YAIK_Instance* instanceCtx		= gLibrary.AllocInstance();
			fillSize.internalTag			= instanceCtx;

			if (instanceCtx) {
				// All user need for now.
				fillSize.width								= pHeader->width;
				fillSize.height								= pHeader->height;
				fillSize.hasAlpha							= pHeader->infoMask & FileHeader::BIT_ALPHA_CHANNEL ? true : false;

				// Reset everything in new context, especially pointers clean.
				memset(instanceCtx, 0, sizeof(YAIK_Instance));

				// Check user miss between Pre and real decode.
				instanceCtx->srcCheck						= sourceStreamAligned;
				instanceCtx->srcLength						= streamLength;
				instanceCtx->isRGBA							= fillSize.hasAlpha;

				// Provide Default Image builder. (ALWAYS => SAVE ON PTR TEST !)
				fillSize.customImageOutput					= internal_imageBuilderFunc;
				fillSize.userContextCustomImage				= NULL;

				// Provide Default Memory Allocator (ALWAYS => SAVE ON PTR TEST !)
				fillSize.userMemoryAllocator.customAlloc	= internal_allocFunc;
				fillSize.userMemoryAllocator.customFree		= internal_freeFunc;

				fillSize.userMemoryAllocator.customContext	= NULL;

				return true;
			} else {
				SetErrorCode(YAIK_NO_EMPTYDECODE_SLOT);
			}
		} else {
			SetErrorCode(YAIK_INVALID_HEADER);
		}
	} else {
		SetErrorCode(YAIK_INVALID_STREAM);
	}

	return false;
}

// -----------------------------------------------------------------------------------------------
//		YAIK_DecodeImage
// -----------------------------------------------------------------------------------------------

/* [Wrapper to handle allocation, decompression and errors.]
   We rely on ZStd internal concerning security if attacker create bad stream. */
u8* DecompressData(YAIK_Instance* pCtx, u8* dataIn, u32 size, u32 expectedSize) {
	u8* res = (u8*)AllocateMem(&pCtx->allocCtx,expectedSize);
	if (res) {
		size_t result = ZSTD_decompressDCtx((ZSTD_DCtx*)pCtx->decompCtx, res, expectedSize, dataIn, size);
		if (expectedSize != result) {
			SetErrorCode	(YAIK_INVALID_DECOMPRESSION);
			FreeMem			(&pCtx->allocCtx,res);
			res = NULL;
		}
	}
	return res;
}

/* [Function to update masks internal bitmap when we transition from Single RGB Plane work to seperate R,G,B plane work]
   To optimize performance, when we work on decompressing 3 planes at the same time, we only update the mask
   as a single monochrome plane. 
   
   Once the decoder starts to work on different planes (2 or 1 plane only), we need to have mask updated seperately : 
   this function duplicate the monochrome mask to all 3 planes to be then updated individually.
 */
void UpdateTileAndRGBMask(YAIK_Instance* pCtx) {
	// We use a flag, once transition as been done, it is done.
	// But transition can occur at various places inside the codec, thus making it as a function,
	// called where possible transition could be necessary if it did not occured before.

	if (pCtx->singleRGB) {
		pCtx->singleRGB = false;
		// Duplicate RGB Loaded pixel bitmap for G and B channel now. (until now it was RGB within a single bit-map)
		memcpy(&pCtx->mapRGBMask[pCtx->sizeMapMask   ],pCtx->mapRGBMask,pCtx->sizeMapMask);
		memcpy(&pCtx->mapRGBMask[pCtx->sizeMapMask<<1],pCtx->mapRGBMask,pCtx->sizeMapMask);
		// Duplicate Processed map of 4x4 tile. (until now it was RGB within a single bit-map)
		memcpy(&pCtx->tile4x4Mask[pCtx->tile4x4MaskSize   ],pCtx->tile4x4Mask,pCtx->tile4x4MaskSize);
		memcpy(&pCtx->tile4x4Mask[pCtx->tile4x4MaskSize<<1],pCtx->tile4x4Mask,pCtx->tile4x4MaskSize);
	}
}

void Debug_RGBandTILE(YAIK_Instance* pCtx, const char* nameRGB, const char* nameMask) {
#if 1
	#ifdef YAIK_DEVEL
	BoundingBox bb;
	bb.x=0; bb.y=0;
	bb.w=pCtx->width; bb.h=pCtx->height;
	DumpColorMap888Swizzle(nameRGB,pCtx->planeR,pCtx->planeG,pCtx->planeB,bb);
	Dump4x4TileMap(nameMask, pCtx->tile4x4Mask, pCtx->tile4x4Mask,pCtx->tile4x4Mask, pCtx->width>>2, pCtx->height>>2);
	#endif
#endif
}

// --------------------------------------------------------
// 32 bit constant defining the tags in the stream.
#define TAG_MIPMAP		(0x4d50494d)
#define TAG_ALPHA		(0x4d504c41)
#define TAG_GRADTILE	(0x4c495447)
#define TAG_TILE3D		(0x4c544433)
#define TAG_TILE1D		(0x4c544431)

// DEPRECATED FOR NOW.
// #define TAG_PLANE		(0x544e4c50)
// #define TAG_COLOR		(0x4c4f4355)
// #define TAG_SMOOTH		(0x50414d53)
// #define TAG_TILE2D		(0x4c544432)

// Handle Wrong Tag order if needed.
void wrongTagOrder() {
	kassert(false); // Not implemented.
}
// --------------------------------------------------------

// --------------------------------------------------------
//  Wrapping of memory allocator into a tracker of allocation
//  Allows to figure out right away if memory leak occurs
//  within YAIK while doing dev and adding/fixing features.
//  Automatically removed in release.
// --------------------------------------------------------
#ifdef YAIK_DEVEL
void*	mem_buffers[200];
int     mem_allocCount = 0;
YAIK_SMemAlloc mem_wrapperAlloc;

void*   checkAllocFunc (void* customContext, size_t size     ) {
    /* TEST FAILURE POINT.
	if (mem_allocCount == 32) {
		mem_allocCount++;
		return NULL; 
	} // Force failure.
    */
	mem_buffers[mem_allocCount] = mem_wrapperAlloc.customAlloc(customContext,size);
	return mem_buffers[mem_allocCount++];
}

void    checkFreeFunc  (void* customContext, void*  address  ) {
	for (int n=0; n < mem_allocCount; n++) {
		if (mem_buffers[n] == address) {
			mem_buffers[n] = NULL;
			mem_wrapperAlloc.customFree(customContext,address);
			return;
		}
	}

	//Delete wrong invalid pointer or double delete.
	printf("MEMORY CHECK : FREE %p NOT FOUND.", address);
}

void	checkAllocationEnd() {
	for (int n=0; n < mem_allocCount; n++) {
		if (mem_buffers[n]) {
			printf("LEAK AT %i (adr %p)\n",n,mem_buffers[n]);
		}
	}

	// Reset for another decode...
	mem_allocCount = 0;
}

#else
#define checkAllocationEnd
#endif
// --------------------------------------------------------


bool			YAIK_DecodeImage	(void* sourceStreamAligned, u32 streamLength, YAIK_SDecodedImage* context) {
	bool			res			= false;
	YAIK_Instance*	pCtx		= (YAIK_Instance*)context->internalTag;
	FileHeader*		pHeader		= (FileHeader*)sourceStreamAligned;
	YAIK_SMemAlloc& allocCtx	= pCtx->allocCtx;
	u8*				endStream   = ((u8*)sourceStreamAligned) + streamLength;
	
	pCtx->width					= pHeader->width;
	pCtx->height				= pHeader->height;
	pCtx->tileWidth				= RDIV8(pCtx->width);
	pCtx->tileHeight			= RDIV8(pCtx->height);
	
	int singlePlaneSize			= MUL64(pCtx->tileWidth * pCtx->tileHeight);

	pHeader++; // Skip header now.

	if (!pCtx) 
	{ SetErrorCode(YAIK_DECIMG_INVALIDCTX); goto error; }

	if (context->userMemoryAllocator.customAlloc == NULL)
	{ SetErrorCode(YAIK_INVALID_CONTEXT_MEMALLOCATOR); goto error; }
	if (context->userMemoryAllocator.customFree  == NULL)
	{ SetErrorCode(YAIK_INVALID_CONTEXT_MEMALLOCATOR); goto error; }

	#ifdef YAIK_DEVEL
	// -------------------------------------------------------------
	// Wrap the allocator into Memory Leak Check Allocator
	mem_wrapperAlloc	= context->userMemoryAllocator;
	context->userMemoryAllocator.customAlloc	= checkAllocFunc;
	context->userMemoryAllocator.customFree		= checkFreeFunc;
	// -------------------------------------------------------------
	#endif

	// Now we can use the memory allocator.
	pCtx->allocCtx				= context->userMemoryAllocator;
	allocCtx					= pCtx->allocCtx;

	pCtx->planeR				= (u8*)allocCtx.customAlloc(&allocCtx,singlePlaneSize * 3);
	pCtx->planeG				= pCtx->planeR + singlePlaneSize;
	pCtx->planeB				= pCtx->planeG + singlePlaneSize;

	if (pCtx->planeR == NULL) 
	{ SetErrorCode(YAIK_MALLOC_FAIL); goto error; }

#ifdef YAIK_DEVEL
	// Release Version does not need to clean. Garbage is OK as it will be overwritten.
	memset(pCtx->planeR,0,singlePlaneSize * 3);
#endif

	// --- Convert Own Memory Allocator into ZStd Allocator
	ZSTD_customMem mem;
	mem.customAlloc				= pCtx->allocCtx.customAlloc;
	mem.customFree				= pCtx->allocCtx.customFree;
	mem.opaque					= pCtx->allocCtx.customContext;
	// --- Pass the allocator and build a ZStd Context.
	pCtx->decompCtx				= ZSTD_createDCtx_advanced(mem); // Same as ZSTD_createDCtx(); but using custom memory allocator.

	if (pCtx->decompCtx == NULL) 
	{ SetErrorCode(YAIK_DECOMPRESSION_CREATE_FAIL); goto error; }

	if ((pCtx->srcCheck != sourceStreamAligned) || (pCtx->srcLength != streamLength)) 
	{ SetErrorCode(YAIK_DECIMG_DIFFSTREAM); goto error; }

	if (context->outputImage == NULL) 
	{ SetErrorCode(YAIK_DECIMG_BUFFERNOTSET); goto error; }

	// Read block until last 4 byte DEADBEEF.
	{
	HeaderBase* pBlock = (HeaderBase*)pHeader;

	int state = 0;

	while (pBlock->tag.tag32 != 0xDEADBEEF) {
		HeaderBase local = *pBlock;

		PRINTF("BLOCK %x, Size : %i\n",pBlock->tag.tag32, pBlock->length);

		pBlock++;
		u8* endBlock = (&(((u8*)pBlock)[local.length]));

		// Make sure we do not read outside of the memory stream for each block.
		if (endBlock > endStream) {
			SetErrorCode(YAIK_INVALID_TAG_ID);
			goto error;
		}

		switch (local.tag.tag32) {
		case TAG_MIPMAP:
		{
			if (state == 0) {
				MipmapHeader* pHeader = (MipmapHeader*)pBlock;

				// Decompress mipmap : decompress1BitMaskAlign8()
				u8* streamStart = (u8*)&pHeader[1];
				u32 length      = endBlock - streamStart; 
				if ( ! Decompress1BitTiled(pCtx,pHeader, streamStart, length)) {
					goto error;
				}

				state = 1;
			} else {
				wrongTagOrder(); goto error;
			}
			break;
		}
		case TAG_ALPHA:
		{
			AlphaHeader* pHeader = (AlphaHeader*)pBlock;

			if (state >= 2) {
				wrongTagOrder(); goto error;
			}

			// --- Decompress Alpha Block ---
			u8* dataUncompressed = DecompressData(pCtx, (u8*)&pHeader[1], pHeader->streamSize, pHeader->expectedDecompressionSize);

			bool resultOp = true;
			if (state == 0) {
				switch (MOD8(pHeader->parameters)) {
				case AlphaHeader::IS_1_BIT_FULL:
					resultOp = Decompress1BitMaskAlign8NoMask			(pCtx, pHeader->bbox, dataUncompressed, pHeader->expectedDecompressionSize);
					break;
				case AlphaHeader::IS_6_BIT_FULL:
					resultOp = Decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, false,dataUncompressed, pHeader->expectedDecompressionSize);
					break;
				case AlphaHeader::IS_6_BIT_FULL_INVERSE:
					resultOp = Decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, true, dataUncompressed, pHeader->expectedDecompressionSize);
					break;
				case AlphaHeader::IS_8_BIT_FULL:
					resultOp = Decompress8BitTo8BitAlphaNoMask			(pCtx, pHeader, dataUncompressed, pHeader->expectedDecompressionSize);
					break;
				case AlphaHeader::IS_6_BIT_USEMIPMAPMASK:
				case AlphaHeader::IS_6_BIT_USEMIPMAPMASK_INVERSE:
				case AlphaHeader::IS_1_BIT_USEMIPMAPMASK:
					SetErrorCode(YAIK_ALPHA_FORMAT_IMPOSSIBLE);
					kassert(false);
					resultOp = false;
					break;
				default:
					SetErrorCode(YAIK_INVALID_ALPHA_FORMAT);
					kassert(false);
					resultOp = false;
				}
			} else if (state == 1) {
				switch (MOD8(pHeader->parameters)) {
				case AlphaHeader::IS_1_BIT_FULL:
					resultOp = Decompress1BitMaskAlign8NoMask			(pCtx, pHeader->bbox, dataUncompressed, pHeader->expectedDecompressionSize);
					break;
				case AlphaHeader::IS_1_BIT_USEMIPMAPMASK:
					SetErrorCode(YAIK_ALPHA_UNSUPPORTED_YET);
					break;
				case AlphaHeader::IS_6_BIT_FULL:
					resultOp = Decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, false,dataUncompressed, pHeader->expectedDecompressionSize);
					break;
				case AlphaHeader::IS_6_BIT_FULL_INVERSE:
					resultOp = Decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, true, dataUncompressed, pHeader->expectedDecompressionSize);
					break;
				case AlphaHeader::IS_6_BIT_USEMIPMAPMASK:
					resultOp = Decompress6BitTo8BitAlphaUsingMipmapMask(pCtx, pHeader, false,dataUncompressed, pHeader->expectedDecompressionSize);
					break;
				case AlphaHeader::IS_6_BIT_USEMIPMAPMASK_INVERSE:
					resultOp = Decompress6BitTo8BitAlphaUsingMipmapMask(pCtx, pHeader, true, dataUncompressed, pHeader->expectedDecompressionSize);
					break;
				case AlphaHeader::IS_8_BIT_FULL:
					resultOp = Decompress8BitTo8BitAlphaNoMask			(pCtx, pHeader, dataUncompressed, pHeader->expectedDecompressionSize);
					break;
				default:
					SetErrorCode(YAIK_INVALID_ALPHA_FORMAT);
					resultOp = false;
					kassert(false);
				}
			}

			// Decompression routine failed...
			if (!resultOp) { goto error; }

			// TODO : Support if encoder use ALPHA as MIPMAP ? -> Conversion from Alpha to Swizzled mask.
				
			FreeMem(&allocCtx,dataUncompressed);

			#if (USE_DEBUG_IMAGE)
			int err = stbi_write_png("DebugDecomp\\DumpAlphaChannel.png",pCtx->width, pCtx->height, 1, pCtx->alphaChannel, pCtx->width);
			#endif
				
			state = 2;
			break;
		}
		case TAG_GRADTILE:
		{
			if (state <= 4) {
				HeaderGradientTile* pHeader = (HeaderGradientTile*)pBlock;
				u8* after = (u8*)(&pHeader[1]);

				state = 4;

				/*
				if (!pCtx->mipMapMask) {
					BoundingBox bbox;
					bbox.x = 0;
					bbox.y = 0;
					bbox.w = pCtx->width;
					bbox.h = pCtx->height;
					if ( ! CreateDefaultMipmap(pCtx,bbox) ) {
						goto error;
					};
				}
				*/

				if (!pCtx->mapRGB) {
					pCtx->strideRGBMap  = DIV4(pCtx->width)+1;
					int size			=  pCtx->strideRGBMap * (DIV4(pCtx->height)+1);
					pCtx->mapRGB		= (u8*)AllocateMem(&allocCtx,size * 3);
					int sizeMask		= RDIV8(size);
					pCtx->mapRGBMask	= (u8*)AllocateMem(&allocCtx,sizeMask * 3);
					pCtx->sizeMapMask   = sizeMask;
					pCtx->tile4x4MaskSize= DIV8((RDIV16(pCtx->width)<<2)*(RDIV8(pCtx->height)<<1));
					pCtx->tile4x4Mask   = (u8*)AllocateMem(&allocCtx,pCtx->tile4x4MaskSize*3); // Map 1 bit per 4x4 tile : know if pixel are used or not by gradient decode.
					pCtx->singleRGB		= true;
					if ((pCtx->mapRGB == NULL) || (pCtx->mapRGBMask == NULL) || (pCtx->tile4x4Mask == NULL)) {
						// Memory Free happen in context free.
						goto error;
					}

					// We do not care about garbage in mapRGB.
					// mapRGBMask will tell us if we have valid data or not.
					memset(pCtx->mapRGBMask, 0, sizeMask   );
					memset(pCtx->tile4x4Mask,0, pCtx->tile4x4MaskSize);
				}

				if (pHeader->plane != 7) {
					UpdateTileAndRGBMask(pCtx);
				}

				// -------------------------------------------------------------------
				//  Decompress RGB Stream + Tile Map
				// -------------------------------------------------------------------
				int XShift = MOD8(     pHeader->format );
				int YShift = MOD8(DIV8(pHeader->format));

				// Compute Swizzling parameters.
				u32 bigTileX, bigTileY, bitCount; // RESULT PARAMETER
				HeaderGradientTile::getSwizzleSize(XShift,YShift,bigTileX,bigTileY,bitCount);

				int xBBTileCount= ((pCtx->width +(bigTileX-1)) / bigTileX); // OPTIMIZE : getSwizzleSize return bitShift too and remove division.
				int yBBTileCount= ((pCtx->height+(bigTileY-1)) / bigTileY);
				int sizeBitmap	= DIV8(xBBTileCount * yBBTileCount * bitCount); // No need for rounding, bitCount garantees multibyte alignment.

				u8* dataUncompressedTilebitmap = (u8*)DecompressData(pCtx, after, pHeader->streamBitmapSize, sizeBitmap);
				after += pHeader->streamBitmapSize;
				u8* dataCustCompressedRGB      = (u8*)DecompressData(pCtx, after, pHeader->streamRGBSizeZStd, pHeader->streamRGBSizeCustomCompressor);

				u8* dataUncompressedRGB        = NULL;
				bool mem_fail = false;
				if (dataCustCompressedRGB) {
					dataUncompressedRGB = (u8*)AllocateMem(&allocCtx, pHeader->streamRGBSizeUncompressed);
					if (dataUncompressedRGB) {
						// (u8* input, int inputSize, int inputBufferSize, u8* output, int outputSize)
						PaletteDecompressor(
							dataCustCompressedRGB,pHeader->streamRGBSizeCustomCompressor,
							pHeader->streamRGBSizeCustomCompressor,
							dataUncompressedRGB,
							pHeader->streamRGBSizeUncompressed,
							pHeader->colorCompression
						);
					} else {
						mem_fail = true;
					}
					FreeMem(&allocCtx,dataCustCompressedRGB);
				} else {
					mem_fail = true;
				}

				if (dataUncompressedRGB && dataUncompressedTilebitmap) {
					// -------------------------------------------------------------------
					//  Generate Tile
					// -------------------------------------------------------------------
					u8* pR = pCtx->planeR;
					u8* pG = pCtx->planeG;
					u8* pB = pCtx->planeB;

					// NOTE : Swizzling parameters are not passed to the functions because they are already optimized
					//        for the specific size.
					// POSSIBLY TODO : Add Assert to check that implementation is matching specs here before call. But unlikely to change.
					switch (pHeader->format) {
					case HeaderGradientTile::TILE_16x16: DecompressGradient16x16(pCtx, dataUncompressedTilebitmap,dataUncompressedRGB, pR, pG, pB, pHeader->plane); break;
					case HeaderGradientTile::TILE_16x8:  DecompressGradient16x8	(pCtx, dataUncompressedTilebitmap,dataUncompressedRGB, pR, pG, pB, pHeader->plane); break;
					case HeaderGradientTile::TILE_8x16:  DecompressGradient8x16	(pCtx, dataUncompressedTilebitmap,dataUncompressedRGB, pR, pG, pB, pHeader->plane); break;
					case HeaderGradientTile::TILE_8x8:	 DecompressGradient8x8	(pCtx, dataUncompressedTilebitmap,dataUncompressedRGB, pR, pG, pB, pHeader->plane); break;
					case HeaderGradientTile::TILE_8x4:	 DecompressGradient8x4	(pCtx, dataUncompressedTilebitmap,dataUncompressedRGB, pR, pG, pB, pHeader->plane); break;
					case HeaderGradientTile::TILE_4x8:	 DecompressGradient4x8	(pCtx, dataUncompressedTilebitmap,dataUncompressedRGB, pR, pG, pB, pHeader->plane); break;
					case HeaderGradientTile::TILE_4x4:	 DecompressGradient4x4	(pCtx, dataUncompressedTilebitmap,dataUncompressedRGB, pR, pG, pB, pHeader->plane); break;
					}
				} else {
					mem_fail = true;
				}

				// Free in any cases, may be some were allocated.
				FreeMem(&allocCtx,dataUncompressedRGB);
				FreeMem(&allocCtx,dataUncompressedTilebitmap);

				if (mem_fail) {
					// Error code already set by AllocateMem when failure : SetErrorCode(YAIK_MALLOC_FAIL);
					goto error;
				}

				Debug_RGBandTILE(pCtx, "GradientTest.png", "Tile4x4.png");
				DebugRGBAsPng   ("RGBMap.png",pCtx->mapRGB, (pCtx->width>>2)+1, ((pCtx->height>>2)+1), 3);
			}
			break;
		}
		case TAG_TILE1D:
		{
			if (state >= 4) {
				state = 5;
				Header1D* pHeader = (Header1D*)pBlock;
				u8* streamTypeCmp = (u8*)&pHeader[1];
				u8* streamPixCmp  = &streamTypeCmp[pHeader->streamTypeCnt];

				u8* typeDecomp      = DecompressData(pCtx,streamTypeCmp,pHeader->streamTypeCnt ,pHeader->streamTypeUncmp );
				u8* allocTypeDecomp = typeDecomp;
				u8* pixStreamDecomp = DecompressData(pCtx,streamPixCmp ,pHeader->streamPixelBit,pHeader->streamPixelUncmp);
				u8* allocPixStream  = pixStreamDecomp;

				bool mem_error = false;
				if (typeDecomp && pixStreamDecomp) {
					UpdateTileAndRGBMask(pCtx);

					Decompress1D(pCtx, &typeDecomp,&pixStreamDecomp,0, pHeader);
					Debug_RGBandTILE(pCtx, "GradientTest.png", "Tile4x4_1DR.png");

					Decompress1D(pCtx, &typeDecomp,&pixStreamDecomp,1, pHeader);
					Debug_RGBandTILE(pCtx, "GradientTest.png", "Tile4x4_1DG.png");

					Decompress1D(pCtx, &typeDecomp,&pixStreamDecomp,2, pHeader);
					Debug_RGBandTILE(pCtx, "GradientTest.png", "Tile4x4_1DB.png");

				} else {
					mem_error = true;
				}

				// Free in any cases, may be some were allocated.
				FreeMem(&allocCtx, allocTypeDecomp);
				FreeMem(&allocCtx, allocPixStream );

				if (mem_error) {
					goto error;
				}
			}
		}
			break;
		case TAG_TILE3D:
		// case TAG_TILE2D: DEPRECATED
		{
			if (state <= 4) {
				state = 4;

				bool is3D = (local.tag.tag32 == TAG_TILE3D);

				HeaderTile3D* pHeader = (HeaderTile3D*)pBlock;
				u8* stream3Bit = (u8*)(&pHeader[1]);
				u8* stream4Bit = &stream3Bit[pHeader->compr3BitSize];
				u8* stream5Bit = &stream4Bit[pHeader->compr4BitSize];
				u8* stream6Bit = &stream5Bit[pHeader->compr5BitSize];
				u8* tileStream = &stream6Bit[pHeader->compr6BitSize];
				u8* colorStream= &tileStream[pHeader->comprTypeSize];
				u8* tmap16_8   = &colorStream[pHeader->comprColorSize];
				u8* tmap8_16   = &tmap16_8  [pHeader->sizeT16_8MapCmp ];
				u8* tmap8_8    = &tmap8_16  [pHeader->sizeT8_16MapCmp ];
				u8* tmap8_4    = &tmap8_8   [pHeader->sizeT8_8MapCmp  ];
				u8* tmap4_8    = &tmap8_4   [pHeader->sizeT8_4MapCmp  ];
				u8* tmap4_4    = &tmap4_8   [pHeader->sizeT4_8MapCmp  ];

				TileParam t3dParam;

				// [SECURITY] Any value decoded OK => Covered by LUT size. 
				//            And any garbage value read inside LUT range is just color stuff -> No need for INDEX range check.
				bool valid3BitStream;
				if (pHeader->stream3BitCnt) {
					t3dParam.stream3Bit	= DecompressData(pCtx,stream3Bit,pHeader->compr3BitSize,pHeader->stream3BitCnt);
					valid3BitStream     = (t3dParam.stream3Bit != NULL);
				} else {
					t3dParam.stream3Bit = NULL;
					valid3BitStream     = true;
				}

				bool valid4BitStream;
				if (pHeader->stream4BitCnt) {
					t3dParam.stream4Bit	= DecompressData(pCtx,stream4Bit,pHeader->compr4BitSize,pHeader->stream4BitCnt);
					valid4BitStream     = (t3dParam.stream4Bit != NULL);
				} else {
					t3dParam.stream4Bit = NULL;
					valid4BitStream     = true;
				}

				bool valid5BitStream;
				if (pHeader->stream5BitCnt) {
					t3dParam.stream5Bit	= DecompressData(pCtx,stream5Bit,pHeader->compr5BitSize,pHeader->stream5BitCnt);
					valid5BitStream     = (t3dParam.stream5Bit != NULL);
				} else {
					t3dParam.stream5Bit	= NULL;
					valid5BitStream     = true;
				}

				bool valid6BitStream;
				if (pHeader->stream6BitCnt) {
					t3dParam.stream6Bit	= DecompressData(pCtx,stream6Bit,pHeader->compr6BitSize,pHeader->stream6BitCnt);
					valid6BitStream     = (t3dParam.stream6Bit != NULL);
				} else {
					t3dParam.stream6Bit	= NULL;
					valid6BitStream     = true;
				}

				// [SECURITY] All possible range from tile value (mode, bit count, tile ID) are acceeding valid memory definition. -> No need for INDEX range check.
				t3dParam.tileStream	= (u16*)DecompressData(pCtx,tileStream ,pHeader->comprTypeSize ,pHeader->streamTypeCnt*sizeof(u16));	// One tile is 2 byte.
				// [SECURITY] Invalid color range will just generate wrong color for decoding in case stream is tampered. -> No need for check either.
				t3dParam.colorStream= DecompressData(pCtx,colorStream,pHeader->comprColorSize,pHeader->streamColorCnt);				// Tile Count x 6

				TileParam t3dParamPtrDelete = t3dParam; // Copy Struct.

				if		(	!valid3BitStream
						||	!valid4BitStream
						||	!valid5BitStream
						||	!valid6BitStream
						||	!t3dParam.tileStream
						||	!t3dParam.colorStream ) {

					// Try to free was has been allocated locally if error.
					FreeMem(&allocCtx,t3dParamPtrDelete.stream3Bit);
					FreeMem(&allocCtx,t3dParamPtrDelete.stream4Bit);
					FreeMem(&allocCtx,t3dParamPtrDelete.stream5Bit);
					FreeMem(&allocCtx,t3dParamPtrDelete.stream6Bit);

					FreeMem(&allocCtx,t3dParamPtrDelete.colorStream);
					FreeMem(&allocCtx,t3dParamPtrDelete.tileStream);

					// Code already set by error code.
					goto error;
				}

				PaletteFullRangeRemapping(t3dParamPtrDelete.colorStream,pHeader->compressionRateColor,pHeader->streamColorCnt);

				bool memErr = false;
				if (pHeader->sizeT16_8Map) {
					t3dParam.currentMap	= DecompressData(pCtx,tmap16_8, pHeader->sizeT16_8MapCmp, pHeader->sizeT16_8Map);
					if (t3dParam.currentMap) {
						Tile3D_16x8(pCtx,pHeader,&t3dParam,gLibrary.LUT3D_BitFormat);
						allocCtx.customFree(&allocCtx,t3dParam.currentMap);
					} else {
						memErr = true;
					}

					Debug_RGBandTILE(pCtx, "GradientTest.png", "Tile4x4_Post16x8.png");
				}


				if (pHeader->sizeT8_16Map) {
					t3dParam.currentMap	= DecompressData(pCtx,tmap8_16, pHeader->sizeT8_16MapCmp, pHeader->sizeT8_16Map);
					if (t3dParam.currentMap) {
						Tile3D_8x16(pCtx,pHeader,&t3dParam,gLibrary.LUT3D_BitFormat);
						allocCtx.customFree(&allocCtx,t3dParam.currentMap);
					} else {
						memErr = true;
					}
					Debug_RGBandTILE(pCtx, "GradientTest.png", "Tile4x4_Post8x16.png");
				}


				if (pHeader->sizeT8_8Map) {
					bool err = false;
					t3dParam.currentMap	= DecompressData(pCtx,tmap8_8,  pHeader->sizeT8_8MapCmp,  pHeader->sizeT8_8Map );
					if (t3dParam.currentMap) {
						if (is3D) {
							Tile3D_8x8(pCtx,pHeader,&t3dParam,gLibrary.LUT3D_BitFormat);
						} else {
							SetErrorCode(YAIK_INVALID_PLANE_ID);
							err = true;

							//-------------------------------------------------------------------------------
							// 2D Correlation mode disabled/deprecated for now.
							//-------------------------------------------------------------------------------
							#if DEPRECATED

							UpdateTileAndRGBMask(pCtx);
							switch (pHeader->component) {
							case 3:
								Tile2D_8x8_RG (pCtx, pHeader, &t3dParam, gLibrary.LUT2D_BitFormat);
								break;
							case 5:
								Tile2D_8x8_RB (pCtx, pHeader, &t3dParam, gLibrary.LUT2D_BitFormat);
								break;
							case 6:
								Tile2D_8x8_GB (pCtx, pHeader, &t3dParam, gLibrary.LUT2D_BitFormat);
								break;
							default:
								SetErrorCode(YAIK_INVALID_PLANE_ID);
								err = true;
								break;
							}

							#endif
						}
					} else {
						memErr = true;
						err    = true;
					}

					FreeMem(&allocCtx,t3dParam.currentMap);
					memErr |= err; // will go to error later, in case earlier error forgot to free things...

					Debug_RGBandTILE(pCtx, "GradientTest.png", "Tile4x4_Post8x8.png");
				}

				if (pHeader->sizeT8_4Map) {
					t3dParam.currentMap	= DecompressData(pCtx,tmap8_4, pHeader->sizeT8_4MapCmp, pHeader->sizeT8_4Map);
					if (t3dParam.currentMap) {
						Tile3D_8x4(pCtx,pHeader,&t3dParam,gLibrary.LUT3D_BitFormat);
						FreeMem(&allocCtx,t3dParam.currentMap);
					} else {
						memErr = true;
					}

					Debug_RGBandTILE(pCtx, "GradientTest.png", "Tile4x4_Post8x4.png");
				}

				if (pHeader->sizeT4_8Map) {
					t3dParam.currentMap	= DecompressData(pCtx,tmap4_8, pHeader->sizeT4_8MapCmp, pHeader->sizeT4_8Map);
					if (t3dParam.currentMap) {
						Tile3D_4x8(pCtx,pHeader,&t3dParam,gLibrary.LUT3D_BitFormat);
						FreeMem(&allocCtx,t3dParam.currentMap);
					} else {
						memErr = true;
					}

					Debug_RGBandTILE(pCtx, "GradientTest.png", "Tile4x4_Post4x8.png");
				}

				if (pHeader->sizeT4_4Map) {
					bool err = false;
					t3dParam.currentMap	= DecompressData(pCtx,tmap4_4, pHeader->sizeT4_4MapCmp, pHeader->sizeT4_4Map);
					if (t3dParam.currentMap) {
						if (is3D) {
							Tile3D_4x4(pCtx,pHeader,&t3dParam,gLibrary.LUT3D_BitFormat);
						} else {
							SetErrorCode(YAIK_INVALID_PLANE_ID);
							err = true;

							//-------------------------------------------------------------------------------
							// 2D Correlation mode disabled/deprecated for now.
							//-------------------------------------------------------------------------------
							#if DEPRECATED
							UpdateTileAndRGBMask(pCtx);

							switch (pHeader->component) {
							case 3:
								Tile2D_4x4_RG (pCtx, pHeader, &t3dParam, gLibrary.LUT2D_BitFormat);
								break;
							case 5:
								Tile2D_4x4_RB (pCtx, pHeader, &t3dParam, gLibrary.LUT2D_BitFormat);
								break;
							case 6:
								Tile2D_4x4_GB (pCtx, pHeader, &t3dParam, gLibrary.LUT2D_BitFormat);
								break;
							default:
								SetErrorCode(YAIK_INVALID_PLANE_ID);
								err = true;
								break;
							}
							#endif
						}
						FreeMem(&allocCtx,t3dParam.currentMap);
					} else {
						memErr = true;
						err = true;
					}
					memErr |= err; // Go to error stuff later, but must make sure first we do not leak stuff.

					Debug_RGBandTILE(pCtx, "GradientTest.png", "Tile4x4_Post4x4.png");
				}

				// Finally free everything, whatever happened.
				FreeMem(&allocCtx,t3dParamPtrDelete.stream3Bit);
				FreeMem(&allocCtx,t3dParamPtrDelete.stream4Bit);
				FreeMem(&allocCtx,t3dParamPtrDelete.stream5Bit);
				FreeMem(&allocCtx,t3dParamPtrDelete.stream6Bit);

				FreeMem(&allocCtx,t3dParamPtrDelete.colorStream);
				FreeMem(&allocCtx,t3dParamPtrDelete.tileStream);

				// Then go to error ONCE we managed to clear everything...
				if (memErr) { goto error; }
			}
			break;
		}
		default:
			SetErrorCode(YAIK_INVALID_TAG_ID); goto error;
		}

		// Skip to the next block using tag info.
		pBlock = (HeaderBase*)endBlock;
	}

	}

	YAIK_SCustomDataSource srcInfo;
	srcInfo.strideA	= pCtx->width;
	srcInfo.strideR	= pCtx->tileWidth<<6;
	srcInfo.strideG	= srcInfo.strideR;
	srcInfo.strideB	= srcInfo.strideR;

	srcInfo.planeA	= pCtx->alphaChannel;
	srcInfo.planeR	= pCtx->planeR;
	srcInfo.planeG	= pCtx->planeG;
	srcInfo.planeB	= pCtx->planeB;

	// Copy, interleave, remix to the target buffer.
	context->customImageOutput(context,&srcInfo);

	res = true;
error:
	// Free all the memory allocated (or not : NULL is OK)
	FreeMem(&allocCtx,pCtx->alphaChannel	);
	FreeMem(&allocCtx,pCtx->mapRGB			);
	FreeMem(&allocCtx,pCtx->mapRGBMask		);
	FreeMem(&allocCtx,pCtx->mipMapMask		);
	FreeMem(&allocCtx,pCtx->planeR			); // Contains R,G,B
	FreeMem(&allocCtx,pCtx->tile4x4Mask		);
	if (pCtx->decompCtx) {
		ZSTD_freeDCtx((ZSTD_DCtx*)pCtx->decompCtx);
	}
	gLibrary.FreeInstance(pCtx);

	// Verify memory leak in DEVEL MODE.
	checkAllocationEnd();
	return res;
}
