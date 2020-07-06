#include "YAIK_functions.h"

// memset, memcpy
#include <memory.h>


/*
-----------------------------------------------------------------------------
1. Decompression technique for Mipmap Mask if any...
-----------------------------------------------------------------------------
	Stream need to be decompressed anyway to 1:1 scale to maintain 'mask' when updating list of available pixels or not.
	1 Bit mask good too.

	Trick : bounding box need to be a multiple of 8. -> Allow decompression of block on byte alignment.
	Then memcpy to put block at the correct place.
*/

/*
Heap Buffer Allocator :
[Target Buffer Mask	w * h]
*/

bool Decompress1BitTiled			(YAIK_Instance* pCtx, MipmapHeader* header, u8* data_uncompressed, u32 dataLength) {
	int shift = header->mipmapLevel;
	int tileWidth = 1 << shift;
	BoundingBox bbox = header->bbox;

	// Tile Count
	int pixCount = bbox.w * bbox.h;
	if (dataLength < ((pixCount + 7)>>3)) {
		SetErrorCode(YAIK_INVALID_MIPMAP_LEVEL);
	}

	bbox.x <<= shift;
	bbox.y <<= shift;
	bbox.w <<= shift;
	bbox.h <<= shift;
	pCtx->maskBBox = bbox;

	if (!CheckInBound2D(pCtx,bbox)) {
		SetErrorCode(YAIK_INVALID_MIPMAP_LEVEL);
	}

#if (USE_DEBUG_IMAGE)
	debug1BitAsPngLinearOrSwizzled("DebugDecomp//Input_1bit_mipmap.png", data_uncompressed, header->bbox.w, header->bbox.h,header->bbox.w,/*LINEAR*/false);
#endif
	pCtx->mipMapMask = (u8*)AllocateMem(&pCtx->allocCtx, (bbox.w * bbox.h) >> 3);
	if (pCtx->mipMapMask == NULL) { return false; }

	u8* src = data_uncompressed;
	u8* dst = pCtx->mipMapMask;

	switch (tileWidth) {
	case 16:
	{

#if 0 // FOR LINEAR MIPMAP
		// Map relative to x,y
		int blockCount     = (bbox.w >> 4)>>2;
		int blockRemainder = (bbox.w >> 4) & 3;
			// -----------------------------
			// FASTER ROUTINE : 2x faster than lower routine.
			// Bit of work to read only 4 bit, no point in big switch case of 8 bit. (Code too big, cache miss, predictor miss)
			// No point in reading odd and even 4 bit : bigger code, I$ miss bigger, same predictor issue too)
			// => Current implementation probably 'best case'
			// -----------------------------

			/*
			for (int y = 0; y < bbox.h; y++) {
				// 4 Bit block src processing.
				for (int bx = 0; bx < blockCount; bx++) {
					u8 mask = src[bx>>1]>>((bx&1)<<2);
					switch (mask & 0xF) {
					// !!! LITTLE ENDIAN FORMAT !!!
					case 0x0: *dst32++ = 0x00000000; *dst32++ = 0x00000000; break;
					case 0x1: *dst32++ = 0x0000FFFF; *dst32++ = 0x00000000; break;
					case 0x2: *dst32++ = 0xFFFF0000; *dst32++ = 0x00000000; break;
					case 0x3: *dst32++ = 0xFFFFFFFF; *dst32++ = 0x00000000; break;
					case 0x4: *dst32++ = 0x00000000; *dst32++ = 0x0000FFFF; break;
					case 0x5: *dst32++ = 0x0000FFFF; *dst32++ = 0x0000FFFF; break;
					case 0x6: *dst32++ = 0xFFFF0000; *dst32++ = 0x0000FFFF; break;
					case 0x7: *dst32++ = 0xFFFFFFFF; *dst32++ = 0x0000FFFF; break;
					case 0x8: *dst32++ = 0x00000000; *dst32++ = 0xFFFF0000; break;
					case 0x9: *dst32++ = 0x0000FFFF; *dst32++ = 0xFFFF0000; break;
					case 0xA: *dst32++ = 0xFFFF0000; *dst32++ = 0xFFFF0000; break;
					case 0xB: *dst32++ = 0xFFFFFFFF; *dst32++ = 0xFFFF0000; break;
					case 0xC: *dst32++ = 0x00000000; *dst32++ = 0xFFFFFFFF; break;
					case 0xD: *dst32++ = 0x0000FFFF; *dst32++ = 0xFFFFFFFF; break;
					case 0xE: *dst32++ = 0xFFFF0000; *dst32++ = 0xFFFFFFFF; break;
					case 0xF: *dst32++ = 0xFFFFFFFF; *dst32++ = 0xFFFFFFFF; break;
					}
				}
				if ((y & 0xF) == 0xF) {
					src += ((bbox.w >> 4) >> 3);
				}
			} 
			
			} else {
			// GENERIC SLOW LINEAR
				u16* dst16 = (u16*)dst;
				for (int y = 0; y < bbox.h; y++) {
					// 4 Bit block src processing.
					for (int x = 0; x < (bbox.w >> 4); x++) {
						// Each bit read
						int sft = (x & 7);
						u16 v = (src[x >> 3] & (1 << sft)) ? 0xFFFF : 0;
						// Write 2 byte.
						*dst16++ = v;
					}

					if ((y & 0xF) == 0xF) {
						src += ((bbox.w >> 4) >> 3);
					}
				}
			}
			
			*/
#endif
		// SWIZZLING.
		int tileWidth = (bbox.w >> 4);
		u64* tile = (u64*)dst;

		u64* tileA = tile;
		u64* tileB = tileA + (tileWidth<<1);
		int pos = 0;
		for (int y = 0; y < bbox.h; y+=16) {
			for (int x = 0; x < tileWidth; x++) {
				u64 v = (src[pos >> 3] & (1 << (pos&7))) ? -1 : 0;
				*tileA++ = v; *tileA++ = v;
				*tileB++ = v; *tileB++ = v;
				pos++;
			}

			tileA += tileWidth<<1;
			tileB += tileWidth<<1;
		}
	}
	break;
	// Unimplemented.
	case 64:
	case 32:
	case 8:
	default:
	{
		SetErrorCode(YAIK_INVALID_MIPMAP_LEVEL);
		return false;
	}
	}
#if (USE_DEBUG_IMAGE)
	debug1BitAsPngLinearOrSwizzled("DebugDecomp//Output_1bit_swizzle_mipmap.png", pCtx->mipMapMask, bbox.w, bbox.h,bbox.w,true/*SWIZZLED*/);
#endif
#undef USE_DEBUG
	return true;
}

bool CreateDefaultMipmap(YAIK_Instance* pCtx, BoundingBox& bbox) {
	int length			= ((bbox.w * bbox.h) + 7)>>3;
	pCtx->mipMapMask	= (u8*)AllocateMem(&pCtx->allocCtx, length);
	if (pCtx->mipMapMask) {
		pCtx->maskBBox	= bbox;
		memset(pCtx->mipMapMask, 255, length);
		return true;
	}
	return false;
}
