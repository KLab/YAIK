#include "YAIK_functions.h"

// memset, memcpy
#include <memory.h>


/*
-----------------------------------------------------------------------------
2. Decompression technique for Alpha
-----------------------------------------------------------------------------
 */

bool Decompress1BitMaskAlign8NoMask(YAIK_Instance* pCtx, BoundingBox& header_bbox, u8* data_uncompressed) {
	BoundingBox bbox = header_bbox;
	u32 width  = pCtx->width;
	u32 height = pCtx->height;

	//Heap Temp Allocator
	//	[Alloc for stream decompression of size]
	pCtx->alphaChannel = (u8*)AllocateMem(&pCtx->allocCtx, width * height);
	if (pCtx->alphaChannel == NULL) { return false; }

	if ((bbox.h > 0) && (bbox.w > 0)) {
		// No empty surface allowed.

		// 8 pixel aligned.
		kassert((bbox.x & 7) == 0);
		kassert((bbox.w & 7) == 0);

		/*	Fill mask with optimization to the length
			and call count to memset and memcpy.

			AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
			AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
			AAAAAA.................BBBBBBBB
			BBBBBB.................BBBBBBBB
			BBBBBB.................CCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
		*/

		u8* pDst = pCtx->alphaChannel;
		// Section [AAAA]
		int stepA = (bbox.x + (bbox.y * width));
		// Section [....]
		int stepD = bbox.w;
		// Section [BBBB]
		int stepB = (width - bbox.w);
		// Section [CCCC]
		int endY1 = (bbox.y + bbox.h);
		int stepC = ((width  - (bbox.x + bbox.w)))
				 + (((height - (endY1)) * width));

		if (stepA) {
			memset(pDst, 0, stepA);
			pDst += stepA;
		}

		int blkCnt = stepD >> 3; // By 8 pixel each.

		// TODO OPTIMIZE [Could optimize loop using ptr]
		int endY = endY1 - 1;
		for (int y = bbox.y; y <= endY; y++) {
			// Section [....]
			// [TODO] Fill with bitmap 1 bit -> 8 bit. memcpy(pDst, mipmapMaskStream, stepD);
			int cnt = blkCnt;
			while (--cnt) {
				int v = *data_uncompressed++;
				*pDst++ = ((v & 0x01) << 31) >> 31; // 0 or 255 write.
				*pDst++ = ((v & 0x02) << 30) >> 31; // 0 or 255 write.
				*pDst++ = ((v & 0x04) << 29) >> 31; // 0 or 255 write.
				*pDst++ = ((v & 0x08) << 28) >> 31; // 0 or 255 write.
				*pDst++ = ((v & 0x10) << 27) >> 31; // 0 or 255 write.
				*pDst++ = ((v & 0x20) << 26) >> 31; // 0 or 255 write.
				*pDst++ = ((v & 0x40) << 25) >> 31; // 0 or 255 write.
				*pDst++ = ((v & 0x80) << 24) >> 31; // 0 or 255 write.
			}

			// Section [BBBB] or [CCCC] (last)
			int stepDo = (y != endY) ? stepB : stepC;
			memset(pDst, 0, stepDo);
			pDst += stepDo;
		}
	}

	return true;
}

bool Decompress6BitTo8BitAlphaNoMask(YAIK_Instance* pCtx, AlphaHeader* header, bool useInverseValues, u8* data_uncompressed) {
	if ((header->bbox.h > 0) && (header->bbox.w > 0)) {
		u32 width  = pCtx->width;
		u32 height = pCtx->height;

		// No empty surface allowed.
		// Heap Buffer Allocator :
		// [Target Buffer Mask	w*h]
		pCtx->alphaChannel = (u8*)AllocateMem(&pCtx->allocCtx, width * height);
		if (pCtx->alphaChannel == NULL) { return false; }

		u8* alphaChannel	= pCtx->alphaChannel;
		u8* alphaMaskStream = data_uncompressed;

		BoundingBox bbox	= header->bbox;
		int dstWidth		= pCtx->width;

		/*	Fill mask with optimization to the length
			and call count to memset and memcpy.

			AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
			AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
			AAAAAA.................BBBBBBBB
			BBBBBB.................BBBBBBBB
			BBBBBB.................CCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
		*/
		u8* pDst = alphaChannel;
		// Section [AAAA]
		int stepA = (bbox.x + (bbox.y * dstWidth));
		// Section [....]
		int stepD = bbox.w;
		// Section [BBBB]
		int stepB = (dstWidth - bbox.w);
		// Section [CCCC]
		int endY1 = (bbox.y + bbox.h);
		int stepC = ((dstWidth - (bbox.x + bbox.w)))
			+ (((pCtx->height - (endY1)) * dstWidth));

		if (stepA) {
			memset(pDst, 0, stepA);
			pDst += stepA;
		}

		int endY = endY1 - 1;
		int state = 0;

		int blockPerLine = bbox.w >> 2; // div 4
		kassert((bbox.w & 3) == 0);

		u8 workV;

		// TODO OPTIMIZE [Could optimize loop using ptr for Y...]
		for (int y = bbox.y; y <= endY; y++) {
			// Decompress by block of 4 pixels
			u8* endBlk = &alphaMaskStream[blockPerLine * 3];

			if (useInverseValues) {
				/**
					Decompress single line of 4 value blocks.
					Stream is array of 6 bit with inverted values (0=>31, 31=>0)
					Value is then upscaled from 6 bit to 8 bit (proper mathematically correct : [abcdef] -> [abcdefab]
					 => Same as (v * 255 / 31)
				 */
				while (alphaMaskStream < endBlk) {
					// bbaaaaaa
					u8 v = *alphaMaskStream++;
					workV = 63 - (v & 0x3F);							//  aaaaaa
					*pDst++ = (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
					// ccccbbbb
					u8 v2 = *alphaMaskStream++;
					workV = 63 - ((v >> 6) | ((v2 & 0xF) << 2));		//  bbbb.bb
					*pDst++ = (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
					// ddddddcc
					v = *alphaMaskStream++;
					workV = 63 - ((v2 >> 4) | ((v & 3) << 4));		//  cc.cccc
					*pDst++ = (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
					workV = 63 - (v >> 2);								//  dddddd
					*pDst++ = (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
				}
			} else {
				/*
					Same as previous block, except that 6 bit values are NOT inverted inside the stream.
				 */
				while (alphaMaskStream < endBlk) {
					// bbaaaaaa
					u8 v = *alphaMaskStream++;
					workV = (v & 0x3F);									//  aaaaaa
					*pDst++ = (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
					// ccccbbbb
					u8 v2 = *alphaMaskStream++;
					workV = ((v >> 6) | ((v2 & 0xF) << 2));				//  bbbb.bb
					*pDst++ = (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
					// ddddddcc
					v = *alphaMaskStream++;
					workV = ((v2 >> 4) | ((v & 3) << 4));				//  cc.cccc
					*pDst++ = (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
					workV = (v >> 2);									//  dddddd
					*pDst++ = (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
				}
			}

			// Section [BBBB] or [CCCC] (last)
			int stepDo = (y != endY) ? stepB : stepC;
			memset(pDst, 0, stepDo);
			pDst += stepDo;
		}
	}

	return true;
}

bool Decompress6BitTo8BitAlphaUsingMipmapMask(YAIK_Instance* pCtx, AlphaHeader* header, bool useInverseValues, u8* data_uncompressed) {
	if ((header->bbox.h > 0) && (header->bbox.w > 0)) {
		u32 width  = pCtx->width;
		u32 height = pCtx->height;

		// No empty surface allowed.
		// Heap Buffer Allocator :
		// [Target Buffer Mask	w*h]
		pCtx->alphaChannel = (u8*)AllocateMem(&pCtx->allocCtx, width * height);
		if (pCtx->alphaChannel == NULL) { return false; }

		u8* alphaChannel  = pCtx->alphaChannel;
		u8* mipmapChannel = pCtx->mipMapMask;		// Different bounding box !!!
		u32 strideMipmap = pCtx->maskBBox.w;
		BoundingBox alphaBBox = header->bbox;

		// Tricky : mipmap parsing in mipmap space is using alpha bounding box !
		u32 mipmapPos = (alphaBBox.x - pCtx->maskBBox.x) + (strideMipmap * (alphaBBox.y - pCtx->maskBBox.y));

		u8* alphaMaskStream = data_uncompressed;

		BoundingBox bbox = header->bbox;
		int dstWidth = pCtx->width;

		/*	Fill mask with optimization to the length
			and call count to memset and memcpy.

			AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
			AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
			AAAAAA.................BBBBBBBB
			BBBBBB.................BBBBBBBB
			BBBBBB.................CCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
		*/
		u8* pDst = alphaChannel;
		// Section [AAAA]
		int stepA = (bbox.x + (bbox.y * dstWidth));
		// Section [....]
		int stepD = bbox.w;
		// Section [BBBB]
		int stepB = (dstWidth - bbox.w);
		// Section [CCCC]
		int endY1 = (bbox.y + bbox.h);
		int stepC = ((dstWidth - (bbox.x + bbox.w)))
			+ (((pCtx->height - (endY1)) * dstWidth));

		if (stepA) {
			memset(pDst, 0, stepA);
			pDst += stepA;
		}

		int endY = endY1 - 1;
		u8  workV;

		int state = 0;
		for (int y = bbox.y; y <= endY; y++) {
			u8 v,v2, writeV;
			int backupMipmapPos	= mipmapPos;
			int mipmapPosE		= mipmapPos + bbox.w;

			// [TODO] Possible optimization using left/right span
			if (useInverseValues) {
				do {
					u8 mipV = mipmapChannel[mipmapPos >> 3] & (1 << (mipmapPos & 7));
					if (mipV == 0) {
						*pDst++ = 0;
					} else {
						switch (state & 3) {
						case 0:
							v		= *alphaMaskStream++;
							workV	= 63 - (v & 0x3F);							//  aaaaaa
							writeV	= (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
							break;
						case 1:
							v2		= *alphaMaskStream++;
							workV	= 63 - ((v >> 6) | ((v2 & 0xF) << 2));		//  bbbb.bb
							writeV	= (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
							break;
						case 2:
							v		= *alphaMaskStream++;
							workV	= 63 - ((v2 >> 4) | ((v & 0x3) << 4));		//  cc.cccc
							writeV	= (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
							break;
						case 3:
							workV	= 63 - (v >> 2);							//  dddddd
							writeV	= (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
							break;
						}
						*pDst++ = writeV;
						state++;
					}
					mipmapPos++;
				} while (mipmapPos != mipmapPosE);
			} else {
				do {
					u8 mipV = mipmapChannel[mipmapPos >> 3] & (1 << (mipmapPos & 7));
					if (mipV == 0) {
						*pDst++ = 0;
					} else {
						switch (state & 3) {
						case 0:
							v		= *alphaMaskStream++;
							workV	= (v & 0x3F);								//  aaaaaa
							writeV	= (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
							break;
						case 1:
							v2		= *alphaMaskStream++;
							workV	= ((v >> 6) | ((v2 & 0xF) << 2));			//  bbbb.bb
							writeV	= (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
							break;
						case 2:
							v		= *alphaMaskStream++;
							workV	= ((v2 >> 4) | ((v & 0x3) << 4));			//  cc.cccc
							writeV	= (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
							break;
						case 3:
							workV	= (v >> 2);									//  dddddd
							writeV	= (workV << 2) | (workV >> 4);				//  6 -> 8 bit.
							break;
						}
						*pDst++ = writeV;
						state++;
					}
					mipmapPos++;
				} while (mipmapPos != mipmapPosE);
			}
			
			mipmapPos = backupMipmapPos + strideMipmap;

			// Section [BBBB] or [CCCC] (last)
			int stepDo = (y != endY) ? stepB : stepC;
			memset(pDst, 0, stepDo);
			pDst += stepDo;
		}
	}
	return true;
}

bool Decompress8BitTo8BitAlphaNoMask(YAIK_Instance* pCtx, AlphaHeader* header, u8* data_uncompressed) {
	// TODO : Reuse from previous alpha stuff.
	// Support only through memcpy.
	if ((header->bbox.h > 0) && (header->bbox.w > 0)) {
		u32 width  = pCtx->width;
		u32 height = pCtx->height;

		// No empty surface allowed.
		// Heap Buffer Allocator :
		// [Target Buffer Mask	w*h]
		pCtx->alphaChannel = (u8*)AllocateMem(&pCtx->allocCtx, width * height);
		if (pCtx->alphaChannel == NULL) { return false; }
		u8* alphaChannel = pCtx->alphaChannel;
		u8* alphaMaskStream = data_uncompressed;

		BoundingBox bbox = header->bbox;
		int dstWidth = pCtx->width;

		/*	Fill mask with optimization to the length
			and call count to memset and memcpy.

			AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
			AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
			AAAAAA.................BBBBBBBB
			BBBBBB.................BBBBBBBB
			BBBBBB.................CCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
			CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
		*/
		u8* pDst = alphaChannel;
		// Section [AAAA]
		int stepA = (bbox.x + (bbox.y * dstWidth));
		// Section [....]
		int stepD = bbox.w;
		// Section [BBBB]
		int stepB = (dstWidth - bbox.w);
		// Section [CCCC]
		int endY1 = (bbox.y + bbox.h);
		int stepC = ((dstWidth - (bbox.x + bbox.w)))
			+ (((pCtx->height - (endY1)) * dstWidth));

		if (stepA) {
			memset(pDst, 0, stepA);
			pDst += stepA;
		}

		int endY = endY1 - 1;
		int state = 0;

		int blockPerLine = bbox.w >> 2; // div 4
		kassert((bbox.w & 3) == 0);

		// TODO OPTIMIZE [Could optimize loop using ptr for Y...]
		for (int y = bbox.y; y <= endY; y++) {
			// Decompress by block of 4 pixels
			memcpy(pDst, alphaMaskStream, stepD);
			pDst += stepD;
			alphaMaskStream += stepD;

			// Section [BBBB] or [CCCC] (last)
			int stepDo = (y != endY) ? stepB : stepC;
			memset(pDst, 0, stepDo);
			pDst += stepDo;
		}
	}
	return true;
}
