#include "YAIK_functions.h"

//---------------------------------------------------------------------------------------------
// TEMPORARY STUFF !!!!
//---------------------------------------------------------------------------------------------

#ifdef YAIK_DEVEL
	void StartCounter();
	double GetCounter();

	#define CHECK_MEM		_CrtCheckMemory();
	#ifdef CHECK_MEM
	#include <Windows.h>
	#endif

	#include <stdio.h>
	#define PRINTF	printf;
#else
	#define PRINTF	; 
#endif

// memset, memcpy
#include <memory.h>

#include "../external/zstd/zstd.h"

int BitCount(uint64_t v) {
	int count = 0;
	while (v != 0) {
		if (v & 1) {
			count++;
		}
		v >>= 1;
	}
	return count;
}

void releaseMemory(YAIK_Instance* pCtx, void* ptr) {
//	delete[] ptr;
}

YAIK_ERROR_CODE	gErrorCode = YAIK_NO_ERROR;
void			SetErrorCode(YAIK_ERROR_CODE error) {
	// Sticky bit...
	if (gErrorCode == YAIK_NO_ERROR) {
		gErrorCode = error;
	}
}

// TODO : Remap 8 Bit Alpha mask to RGBA 32 bit memory (Later in the process) when doing convert YCoCg full to RGBA final.
//		  Problem : Will do the conversion ONLY on Mipmap bounding bbox. If smaller than full image, need to fill zeros.
//			=== DO YCoCg->RGB IN PLACE !!! Save bandwidth, no copy. ===

#define TAG_MIPMAP		(0x4d50494d)
#define TAG_ALPHA		(0x4d504c41)
#define TAG_COLOR		(0x4c4f4355)
#define TAG_SMOOTH		(0x50414d53)
#define TAG_PLANE		(0x544e4c50)
#define TAG_GRADTILE	(0x4c495447)

void wrongOrder() {
	kassert(false); // Not implemented.
}

YAIK_ERROR_CODE YAIK_GetErrorCode() {
	YAIK_ERROR_CODE res = gErrorCode;
	gErrorCode = YAIK_NO_ERROR;
	return res;
}

YAIK_Library	gLibrary;

YAIK_Instance* YAIK_Library::AllocInstance() {
	// [TODO : Use MUTEX ? ATOMIC]. What happen if go too far ? (Use 257 entries with GUARD NULL ?)
	if (gLibrary.freeSlotCount > 0) {
		return freeStack[--gLibrary.freeSlotCount];
	} else {
		return NULL;
	}
}

void			YAIK_Library::FreeInstance(YAIK_Instance* inst) {
	// [TODO : Support multithreading...]
	kassert(inst != NULL); // CALLER CHECK !
	freeStack[gLibrary.freeSlotCount++] = inst;
}

u32				YAIK_GetLibraryMemoryAmount(u8 maxDecodeThreadContext) {
	if (maxDecodeThreadContext) {
		SetErrorCode(YAIK_NO_ERROR);
		gLibrary.instances			= NULL;
		gLibrary.totalSlotCount		= maxDecodeThreadContext;
		gLibrary.freeSlotCount		= 0;

		return maxDecodeThreadContext * sizeof(YAIK_Instance);
	} else {
		SetErrorCode(YAIK_INVALID_CONTEXT_COUNT);
		return 0;
	}
}

YAIK_LIB		YAIK_Init			(void* memory, u8 maxDecodeThreadContext) {
	if (gLibrary.freeSlotCount != 0 || (gLibrary.totalSlotCount != maxDecodeThreadContext)) {
		SetErrorCode(YAIK_INVALID_CONTEXT_COUNT);
		return NULL;
	}

	if (memory) {
		// Return the amount of memory required to perform library init.
		gLibrary.totalSlotCount = maxDecodeThreadContext;
		gLibrary.freeSlotCount	= maxDecodeThreadContext;
		gLibrary.instances		= (YAIK_Instance*)memory;
		for (int n = 0; n < maxDecodeThreadContext; n++) {
			gLibrary.freeStack[n] = &gLibrary.instances[n];
		}

		if (InitLUT()) {
			return &gLibrary;
		} else {
			SetErrorCode(YAIK_INIT_FAIL);
			return NULL;
		}
	} else {
		SetErrorCode(YAIK_INIT_FAIL);
		return NULL;
	}
}

void			YAIK_Release		(YAIK_LIB lib) {
	if (lib) {
		// !!! DONT : delete[] gLibrary.instances; --> User allocated buffer passed to the API !!!
		// Free your own stuff, but there should be no need.
	} else {
		gErrorCode = YAIK_RELEASE_EMPTY_LIBRARY;
	}
}

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

u8*  AllocateMem (YAIK_SMemAlloc* allocCtx, size_t size) {
	u8* res = (u8*)allocCtx->customAlloc(allocCtx, size);
	if (!res) {
		SetErrorCode(YAIK_MALLOC_FAIL);
	}
	return res;
}

void FreeMem	 (YAIK_SMemAlloc* allocCtx, void*  ptr ) {
	if (ptr) { allocCtx->customFree(allocCtx, ptr); }
}

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

void kassert(bool validCond) {
	if (!validCond) {
		PRINTF("Error");
		while (1) {
		}
	}
}

bool			YAIK_DecodeImage	(void* sourceStreamAligned, u32 streamLength, YAIK_SDecodedImage* context) {
	YAIK_Instance*	pCtx		= (YAIK_Instance*)context->internalTag;
	FileHeader*		pHeader		= (FileHeader*)sourceStreamAligned;
	YAIK_SMemAlloc& allocCtx	= pCtx->allocCtx;
	
	pCtx->width			= pHeader->width;
	pCtx->height		= pHeader->height;
	pCtx->tileWidth     = ((pCtx->width + 7)>>3);
	pCtx->tileHeight    = ((pCtx->height+ 7)>>3);

	int singlePlaneSize = (pCtx->tileWidth * pCtx->tileHeight) << 6;

	pHeader++; // Skip header now.

	if (!pCtx) 
	{ SetErrorCode(YAIK_DECIMG_INVALIDCTX); goto error; }

	if (context->userMemoryAllocator.customAlloc == NULL)
	{ SetErrorCode(YAIK_INVALID_CONTEXT_MEMALLOCATOR); goto error; }
	if (context->userMemoryAllocator.customFree  == NULL)
	{ SetErrorCode(YAIK_INVALID_CONTEXT_MEMALLOCATOR); goto error; }

	// Now we can use the memory allocator.
	pCtx->allocCtx	= context->userMemoryAllocator;
	allocCtx	= pCtx->allocCtx;

	pCtx->planeR		= (u8*)allocCtx.customAlloc(&allocCtx,singlePlaneSize * 3);
	pCtx->planeG		= pCtx->planeR + singlePlaneSize;
	pCtx->planeB		= pCtx->planeG + singlePlaneSize;

	if (pCtx->planeR == NULL) 
	{ SetErrorCode(YAIK_MALLOC_FAIL); goto error; }

	// --- Convert Own Memory Allocator into ZStd Allocator
	ZSTD_customMem mem;
	mem.customAlloc		= pCtx->allocCtx.customAlloc;
	mem.customFree		= pCtx->allocCtx.customFree;
	mem.opaque			= pCtx->allocCtx.customContext;
	// --- Pass the allocator and build a ZStd Context.
	pCtx->decompCtx		= ZSTD_createDCtx_advanced(mem); // Same as ZSTD_createDCtx(); but using custom memory allocator.

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
		pBlock++;
		u8* endBlock = (&(((u8*)pBlock)[local.length]));

		switch (local.tag.tag32) {
		case TAG_MIPMAP:
		{
			if (state == 0) {
				MipmapHeader* pHeader = (MipmapHeader*)pBlock;

				// Decompress mipmap : decompress1BitMaskAlign8()
				if ( ! Decompress1BitTiled(pCtx,pHeader, (u8*)&pHeader[1])) {
					goto error;
				}

				state = 1;
			} else {
				wrongOrder(); goto error;
			}
			break;
		}
		case TAG_ALPHA:
		{
			AlphaHeader* pHeader = (AlphaHeader*)pBlock;

			if (state >= 2) {
				wrongOrder(); goto error;
			}

			// --- Decompress Alpha Block ---
			u8* dataUncompressed = DecompressData(pCtx, (u8*)&pHeader[1], pHeader->streamSize, pHeader->expectedDecompressionSize);

			bool resultOp = true;
			if (state == 0) {
				switch (pHeader->parameters & 7) {
				case AlphaHeader::IS_1_BIT_FULL:
					resultOp = Decompress1BitMaskAlign8NoMask			(pCtx, pHeader->bbox, dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_FULL:
					resultOp = Decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, false,dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_FULL_INVERSE:
					resultOp = Decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, true, dataUncompressed);
					break;
				case AlphaHeader::IS_8_BIT_FULL:
					resultOp = Decompress8BitTo8BitAlphaNoMask			(pCtx, pHeader, dataUncompressed);
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
				switch (pHeader->parameters & 7) {
				case AlphaHeader::IS_1_BIT_FULL:
					resultOp = Decompress1BitMaskAlign8NoMask			(pCtx, pHeader->bbox, dataUncompressed);
					break;
				case AlphaHeader::IS_1_BIT_USEMIPMAPMASK:
					SetErrorCode(YAIK_ALPHA_UNSUPPORTED_YET);
					break;
				case AlphaHeader::IS_6_BIT_FULL:
					resultOp = Decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, false,dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_FULL_INVERSE:
					resultOp = Decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, true, dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_USEMIPMAPMASK:
					resultOp = Decompress6BitTo8BitAlphaUsingMipmapMask(pCtx, pHeader, false,dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_USEMIPMAPMASK_INVERSE:
					resultOp = Decompress6BitTo8BitAlphaUsingMipmapMask(pCtx, pHeader, true, dataUncompressed);
					break;
				case AlphaHeader::IS_8_BIT_FULL:
					resultOp = Decompress8BitTo8BitAlphaNoMask			(pCtx, pHeader, dataUncompressed);
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
				
			releaseMemory(pCtx,dataUncompressed);

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

				if (!pCtx->mapRGB) {
					pCtx->strideRGBMap  = (pCtx->width>>2)+1;
					int size			=  pCtx->strideRGBMap * ((pCtx->height>>2)+1);
					pCtx->mapRGB		= (u8*)AllocateMem(&allocCtx,size * 3);
					int sizeMask		= (size+7) >>3;
					pCtx->mapRGBMask	= (u8*)AllocateMem(&allocCtx,sizeMask);
					int sizeTileMap     = (((pCtx->width+7)>>3)*(((pCtx->height+7)>>3)*4))>>3;
					pCtx->tile4x4Mask   = (u8*)AllocateMem(&allocCtx,sizeTileMap); // Map 1 bit per 4x4 tile : know if pixel are used or not by gradient decode.

					if ((pCtx->mapRGB == NULL) || (pCtx->mapRGBMask == NULL) || (pCtx->tile4x4Mask == NULL)) {
						goto error;
					}

					// We do not care about garbage in mapRGB.
					// mapRGBMask will tell us if we have valid data or not.
					memset(pCtx->mapRGBMask, 0, sizeMask   );
					memset(pCtx->tile4x4Mask,0, sizeTileMap);
				}

				// -------------------------------------------------------------------
				//  Decompress RGB Stream + Tile Map
				// -------------------------------------------------------------------
				int XShift =  pHeader->format     & 7;
				int YShift = (pHeader->format>>3) & 7;

				// Compute Swizzling parameters.
				u32 bigTileX, bigTileY, bitCount; // RESULT PARAMETER
				HeaderGradientTile::getSwizzleSize(XShift,YShift,bigTileX,bigTileY,bitCount);

				int xBBTileCount= ((pCtx->width +(bigTileX-1)) / bigTileX); // OPTIMIZE : getSwizzleSize return bitShift too and remove division.
				int yBBTileCount= ((pCtx->height+(bigTileY-1)) / bigTileY);
				int sizeBitmap	= (xBBTileCount * yBBTileCount * bitCount) >> 3; // No need for rounding, bitCount garantees multibyte alignment.

				u8* dataUncompressedTilebitmap = (u8*)DecompressData(pCtx, after, pHeader->streamBitmapSize, sizeBitmap);
				after += pHeader->streamBitmapSize;
				u8* dataUncompressedRGB        = (u8*)DecompressData(pCtx, after, pHeader->streamRGBSize   , pHeader->streamRGBSizeUncompressed);

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
				}

				FreeMem(&allocCtx,dataUncompressedRGB);
				FreeMem(&allocCtx,dataUncompressedTilebitmap);

				BoundingBox bb;
				bb.x=0; bb.y=0;
				bb.w=512; bb.h=720;
				// printf("Color Count Stream : %i\n",pHeader->streamRGBSizeUncompressed/3);
				// DumpColorMap888Swizzle("GradientTest.png",pCtx->planeR,pCtx->planeG,pCtx->planeB,bb);
				// DebugRGBAsPng("RGBMap.png",pCtx->mapRGB, (pCtx->width>>2)+1, ((pCtx->height>>2)+1), 3);
			}
		}
			break;

		// Other todo....

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

	gLibrary.FreeInstance(pCtx);
	return true;
error:
	return false;
}
