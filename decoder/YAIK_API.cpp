// ImageDecoder.cpp : This file contains the 'main' function. Program execution begins and ends there.
//


// #define USE_DEBUG_IMAGE		(1)


#include "YAIK_functions.h"

// TODO LUT System, for now just ust the big table thing...
extern void InitLUT();
extern unsigned char* GetLUTBase4Bit(int planeType);
extern unsigned char* GetLUTBase3Bit(int planeType);

//---------------------------------------------------------------------------------------------
// TEMPORARY STUFF !!!!
//---------------------------------------------------------------------------------------------

void StartCounter();
double GetCounter();

#define CHECK_MEM		_CrtCheckMemory();
#ifdef CHECK_MEM
#include <Windows.h>
#endif

//---------------------------------------------------------------------------------------------

#define STBI_ONLY_PNG

#define STB_IMAGE_IMPLEMENTATION
#include "../external/stb_image/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb_image/stb_image_write.h"

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

#define NO_CONVERSION

void slowYCoCg2RGB(u8 Y, s8 Co, s8 Cg, u8& R, u8& G, u8& B) {
#ifdef NO_CONVERSION
	R = Y;
	G = Co;
	B = Cg;
#else
	int Coi = Co*2;
	int Cgi = Cg*2;

	int tmp = Y - Cgi / 2;
	G = Cgi + tmp;
	B = tmp - Coi / 2;
	R = B + Coi;
#endif
}

void slowRGB2YCoCg(u8 R, u8 G, u8 B, u8& Y, u8& Co, u8&Cg) {
#ifdef NO_CONVERSION
	Y  = R;
	Co = G;
	Cg = B;
#else
	// -255..+255
	Co = (R - B) / 2;					// /2 to fit 8 bit.
	int tmp = B + Co / 2;
	Cg = (G - tmp) / 2;					// /2 to fit 8 bit.
	Y = tmp + Cg / 2;
#endif
}

void debug1BitAsPngLinearOrSwizzled(const char* dump, u8* buffer, int w, int h, int stridePixel, bool swizzled) {
	unsigned char* data = new unsigned char[w * h * 3];

	for (int y=0; y < h;y++) {
		for (int x=0; x < w;x++) {
			int idx = (x + y*w) * 3;

			int v;
			if (!swizzled) {
				int pos = x + (stridePixel*y);
				v = (buffer[pos >> 3] & (1 << (pos & 7))) ? 255 : 0;
			} else {
				int xT = x>>3;
				int yT = y>>3;
				u8* pBuff = &buffer[((xT*64) + (yT*((stridePixel>>3)*64)))>>3];
				v = pBuff[y & 7] & (1 << (x&7)) ? 255 : 0;
			}

			data[idx    ] = v;
			data[idx + 1] = v;
			data[idx + 2] = v;
		}
	}

	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(dump, w, h, 3, data, w * 3);

	delete[] data;
}

void debug8BitAsPngLinear(const char* dump, u8* buffer, int w, int h, int strideByte, bool swizzled) {
	unsigned char* data = new unsigned char[w * h * 3];

	int tileWide = (strideByte>>3);

	// Linear
	for (int y=0; y < h;y++) {
		for (int x=0; x < w;x++) {
			int idx = (x + y*w) * 3;

			int v;
			if (!swizzled) {
				int pos = x + (strideByte*y);
				v = buffer[pos];
			} else {
				int xT = x>>3;
				int yT = y>>3;
				u8* pBuff = &buffer[(xT<<3) + (yT*tileWide*64)];
				v = pBuff[((y&7) << 3) + (x&7)];
			}

			data[idx    ] = v;
			data[idx + 1] = v;
			data[idx + 2] = v;
		}
	}

	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(dump, w, h, 3, data, w * 3);

	delete[] data;
}

void dumpColorMapSwizzle(const char* name, u8* alpha, u8* palRGB, int colCount, BoundingBox box) {
	int w = box.w;
	int h = box.h;
	int s = w*h;

	u8* exportB = new u8[s*3];

	for (int y=0; y<h;y++) {
		for (int x=0; x < w; x++) {
			int n = x + y*w;

			int insideTileX = n&7;
			int insideTileY = (n>>3)&7;
			int tileID      = n>>6;
			int ty          = tileID / (w>>3);
			int tx			= tileID % (w>>3);

			int pixIdx = ((ty*8 + insideTileY)*w) + (tx*8 + insideTileX);

			if ((!alpha) || alpha[n]) {
				int idx = alpha[n]*3;
				exportB[pixIdx*3]    = palRGB[idx  ]; 		
				exportB[pixIdx*3 +1] = palRGB[idx+1]; 		
				exportB[pixIdx*3 +2] = palRGB[idx+2]; 		
			} else {
				int col = ((x & 8)^(y&8)) ? 255 : 128;
				exportB[pixIdx*3]    = col; 
				exportB[pixIdx*3 +1] = 0; 		
				exportB[pixIdx*3 +2] = col;
			}
		}
	}

	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(name, w, h, 3, exportB, w*3);
	
	delete[] exportB;
}

void dumpColorMap8Swizzle(const char* name, u8* array, BoundingBox box) {
	int w = box.w;
	int h = box.h;
	int s = w*h;

	u8* exportB = new u8[s*3];

	for (int y=0; y<h;y++) {
		for (int x=0; x < w; x++) {
			int n = x + y*w;

			int insideTileX = n&7;
			int insideTileY = (n>>3)&7;
			int tileID      = n>>6;
			int ty          = tileID / (w>>3);
			int tx			= tileID % (w>>3);

			int pixIdx = ((ty*8 + insideTileY)*w) + (tx*8 + insideTileX);

			int vIn = array[n];

			int idx = array[n]*3;
			exportB[pixIdx*3]    = vIn;
			exportB[pixIdx*3 +1] = vIn; 		
			exportB[pixIdx*3 +2] = vIn;
		}
	}

	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(name, w, h, 3, exportB, w*3);
	
	delete[] exportB;
}

void dumpColorMap888Swizzle(const char* name, u8* arrayR, u8* arrayG, u8* arrayB, BoundingBox box) {
	int w = box.w;
	int h = box.h;
	int s = w*h;

	u8* exportB = new u8[s*3];

	for (int y=0; y<h;y++) {
		for (int x=0; x < w; x++) {
			int n = x + y*w;

			int insideTileX = n&7;
			int insideTileY = (n>>3)&7;
			int tileID      = n>>6;
			int ty          = tileID / (w>>3);
			int tx			= tileID % (w>>3);

			int pixIdx = ((ty*8 + insideTileY)*w) + (tx*8 + insideTileX);

			exportB[pixIdx*3]    = arrayR[n];
			exportB[pixIdx*3 +1] = arrayG[n]; 		
			exportB[pixIdx*3 +2] = arrayB[n];
		}
	}

	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(name, w, h, 3, exportB, w*3);
	
	delete[] exportB;
}



void debugRGBAsPng(const char* dump, u8* buffer, int w, int h, int channel) {
	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(dump, w, h, channel, buffer, w * channel);
}

void* allocateTemp(YAIK_Instance* pCtx, u32 size) {
	void* p= new u8[size];
	memset(p,0,size);
	return p;
}

void releaseMemory(YAIK_Instance* pCtx, void* ptr) {
	delete[] ptr;
}

u8* decompressData(YAIK_Instance* pCtx, u8* dataIn, u32 size, u32 expectedSize) {
	u8* tmp = (u8*)allocateTemp(pCtx, expectedSize * 2);
	size_t outsize = ZSTD_decompress(tmp, expectedSize*2, dataIn, size);
	return tmp;
}

void kassert(bool validCond) {
	if (!validCond) {
		printf("Error");
		while (1) {
		}
	}
}

YAIK_ERROR_CODE	gErrorCode = YAIK_NO_ERROR;

void			SetErrorCode(YAIK_ERROR_CODE error) {
	// Sticky bit...
	if (gErrorCode == YAIK_NO_ERROR) {
		gErrorCode = error;
	}
}

#include <stdio.h>

//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------




/*
	TODO : Y,Co,Cg plane must allocate 1 line more of tile.
	And copy the last line of the previous into the first line of the next one.
	==> Do not have to handle the corner case for bottom : same code as everything else.

	TODO : Handle the last TILE on the RIGHT differently.
		. Could may be reproduce the same thing as Y axis duplicate.
		. pCtx->width+1 during decompression. ?
			. Warning possible trouble.
			
*/

void blendHorizL(int mask, u8* tile) {
	// On top grid element 0.
	switch ((mask>>1) & 0x7) {
	case 0x0:
		// Skip...
		break;
	case 0x1:
		tile[1] = (tile[0] + tile[2]) >> 1;
		break;
	case 0x2:
		tile[2] = (tile[1] + tile[3]) >> 1;
		break;
	case 0x3:
		tile[1] = ((tile[0]*171) + (tile[3]* 85) + 127) >> 8;
		tile[2] = ((tile[0]* 85) + (tile[3]*171) + 127) >> 8;
		break;
	case 0x4:
		tile[3] = (tile[2] + tile[4]) >> 1;
		break;
	case 0x5:
		tile[1] = (tile[0] + tile[2]) >> 1;
		tile[3] = (tile[2] + tile[4]) >> 1;
		break;
	case 0x6:
		tile[2] = ((tile[1]*171) + (tile[4]* 85) + 127) >> 8;
		tile[3] = ((tile[1]* 85) + (tile[4]*171) + 127) >> 8;
		break;
	case 0x7:
		tile[1] = (((tile[0]* 3) +  tile[4]    ) +   1) >> 2;
		tile[2] =   (tile[0]     +  tile[4]           ) >> 1;
		tile[3] =  ( tile[0]     + (tile[4]*  3) + 1  ) >> 2;
		break;
	}
}

void blendHorizR(int mask, u8* tile) {
	// On top grid element 0.
	switch ((mask>>5) & 0x7) {
	case 0x0:
		// Skip...
		break;
	case 0x1:
		tile[5] = (tile[4] + tile[6]) >> 1;
		break;
	case 0x2:
		tile[6] = (tile[5] + tile[7]) >> 1;
		break;
	case 0x3:
		tile[5] = ((tile[4]*171) + (tile[7]* 85) + 127) >> 8;
		tile[6] = ((tile[4]* 85) + (tile[7]*171) + 127) >> 8;
		break;
	case 0x4:
		tile[7] = (tile[6] + tile[64]) >> 1;
		break;
	case 0x5:
		tile[5] = (tile[4] + tile[6]) >> 1;
		tile[7] = (tile[6] + tile[64]) >> 1;
		break;
	case 0x6:
		tile[6] = ((tile[5]*171) + (tile[64]* 85) + 127) >> 8;
		tile[7] = ((tile[5]* 85) + (tile[64]*171) + 127) >> 8;
		break;
	case 0x7:
		tile[5] = (((tile[4]* 3) +  tile[64]    ) +   1) >> 2;
		tile[6] =   (tile[4]     +  tile[64]           ) >> 1;
		tile[7] =  ( tile[4]     + (tile[64]*  3) + 1  ) >> 2;
		break;
	}
}

void	YAIK_Instance::FillGradient() {
	// Y Plane
	int TX = this->maskBBox.x>>3;
	int TY = this->maskBBox.y>>3;
	int TW = this->maskBBox.w>>3;
	int TH = this->maskBBox.h>>3;
	int TT = TW * TH;

	int startPos = (TX + TY * TW);
	u8* mask	= &this->mipMapMask[startPos<<3];
	u8* tileY	= &this->planeY    [startPos<<6];

	// Do that during the decoding of LUT ?

	for (int n=0; n < TT; n++) {
		// For each tile.

		// OPTIMIZE : Detect 0 pixel in tile, skip (read 64 bit)

		// Left 4 block
//		u8 msk = mask[];
//		assert(msk & 1);


		// Right
	}
}
void	YAIK_Instance::ComputeCoCg		() {
}
void	YAIK_Instance::ConvertYCoCg2RGB() {
}





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

void decompress1BitTiled			(YAIK_Instance* pCtx, MipmapHeader* header, u8* data_uncompressed) {
	int shift = header->mipmapLevel;
	int tileWidth = 1 << shift;
	BoundingBox bbox = header->bbox;
	bbox.x <<= shift;
	bbox.y <<= shift;
	bbox.w <<= shift;
	bbox.h <<= shift;
	pCtx->maskBBox = bbox;

#if (USE_DEBUG_IMAGE)
	debug1BitAsPngLinearOrSwizzled("DebugDecomp//Input_1bit_mipmap.png", data_uncompressed, header->bbox.w, header->bbox.h,header->bbox.w,/*LINEAR*/false);
#endif
	pCtx->mipMapMask = (u8*)allocateTemp(pCtx, (bbox.w * bbox.h) >> 3);

	u8* src = data_uncompressed;
	u8* dst = pCtx->mipMapMask;

	switch (tileWidth) {
	case 64:
	{
		printf("TODO SWIZZLE 64");
		while (1) {};

/*
		u32* dst32 = (u32*)dst;
		// Map relative to x,y
		for (int y = 0; y < bbox.h; y++) {
			for (int x = 0; x < (bbox.w >> 6); x++) {
				// Each bit read
				int sft = (x & 7);
				u32 v = (src[x >> 3] & (1 << sft)) ? 0xFFFFFFFF : 0;
				// Write 8 byte.
				*dst32++ = v;
				*dst32++ = v;
			}

			if ((y & 0x3F) == 0x3F) {
				src += ((bbox.w >> 6) >> 3);
			}
		}
*/
	}
	break;
	case 32:
	{
		printf("TODO SWIZZLE 32");
		while (1) {};
/*
		u32* dst32 = (u32*)dst;
		// Map relative to x,y
		for (int y = 0; y < bbox.h; y++) {
			for (int x = 0; x < (bbox.w >> 5); x++) {
				// Each bit read
				int sft = (x & 7);
				u32 v = (src[x >> 3] & (1 << sft)) ? 0xFFFFFFFF : 0;
				// Write 4 byte.
				*dst32++ = v;
			}

			if ((y & 0x1F) == 0x1F) {
				src += ((bbox.w >> 5) >> 3);
			}
		}
*/
	}
	break;
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
	case 8:
	{
		/* SLOW, LINEAR...
		// Map relative to x,y
		for (int y = 0; y < bbox.h; y++) {
			for (int x = 0; x < (bbox.w >> 3); x++) {
				// Each bit read
				int sft = (x & 7);
				u8 v = (src[x >> 3] & (1 << sft)) ? 255 : 0;
				// Write 1 byte.
				*dst++ = v;
			}

			if ((y & 7) == 7) {
				src += ((bbox.w >> 3) >> 3);
			}
		}*/
		printf("TODO SWIZZLE 8");
		while (1) {};
	}
	break;
	default:
	{
		SetErrorCode(YAIK_INVALID_MIPMAP_LEVEL);
	}
	}
#if (USE_DEBUG_IMAGE)
	debug1BitAsPngLinearOrSwizzled("DebugDecomp//Output_1bit_swizzle_mipmap.png", pCtx->mipMapMask, bbox.w, bbox.h,bbox.w,true/*SWIZZLED*/);
#endif
#undef USE_DEBUG
}

void createDefaultMipmap			(YAIK_Instance* pCtx, BoundingBox& bbox) {
	int length = ((bbox.w * bbox.h) + 7)>>3;
	pCtx->mipMapMask	= (u8*)allocateTemp(pCtx, length);
	pCtx->maskBBox		= bbox;
	memset(pCtx->mipMapMask, 255, length);
}

/*
-----------------------------------------------------------------------------
2. Decompression technique for Alpha
-----------------------------------------------------------------------------
 */

void decompress1BitMaskNonAlign(YAIK_Instance* pCtx, BoundingBox& header_bbox, u8 r, u8 g, u8 b, u8* data_uncompressed) {
	if ((header_bbox.h > 0) && (header_bbox.w > 0)) {
		// No empty surface allowed.
		// Heap Buffer Allocator :
		// [Target Buffer Mask	w*h]
		u8* mipmapChannel	= pCtx->mipMapMask;		// Different bounding box !!!
		u32 strideMipmap	= pCtx->maskBBox.w;
		BoundingBox colBBox = header_bbox;

		// Tricky : mipmap parsing in mipmap space is using color bounding box !
		u32 mipmapPos		= (colBBox.x - pCtx->maskBBox.x) + (strideMipmap * (colBBox.y - pCtx->maskBBox.y));
		u32 colPos			= 0;
		u8* colorMaskStream = data_uncompressed;

		BoundingBox bbox = pCtx->maskBBox;

		int dstWidth  = pCtx->width;
		int pixFormat = pCtx->isRGBA ? 4 : 3;
		int strideRGB = (pCtx->width - colBBox.w) * pixFormat;

		u8* pRGB = &pCtx->targetRGBA[((colBBox.y * dstWidth) + colBBox.x) * pixFormat];
		int endY = colBBox.y + colBBox.h;

		int validInSrc = 0;

		for (int y = colBBox.y; y <= endY; y++) {
			int backupMipmapPos	= mipmapPos;
			int mipmapPosE		= mipmapPos + colBBox.w;

			// [TODO] Possible optimization using left/right span
			// [TODO] Possible optimization with pos = (pos<<1) | (pos>>7); // bit moving from 1..128 loop power of two
			//                                 + *colorStreamMask increment when '1' (predecrement first)
			//                                    colP += pos & 1;
			// [TODO] RGBA can use a 32 bit write. (Alpha not merged yet)
			do {
				u8 mskMPos	= (1 << (mipmapPos & 7));
				u8* pDst	= &mipmapChannel[mipmapPos >> 3];
				u8 mipV		= *pDst & mskMPos;
				if (mipV != 0) {
					validInSrc++;
					u8 vSrc = colorMaskStream[colPos >> 3] & (1<< (colPos & 7));
					if (vSrc) {
						// Clear Bit from original.
						*pDst &= ~mskMPos;
						// Fill RGB into Target.
						pRGB[0] = r;
						pRGB[1] = g;
						pRGB[2] = b;
					}
					colPos++;
				}
				mipmapPos++;
				pRGB += pixFormat;
			} while (mipmapPos != mipmapPosE);
			
			mipmapPos = backupMipmapPos + strideMipmap;
			pRGB += strideRGB;
		}

		if (1) {
			static int count = 0; char buff[2000]; sprintf(buff,"DebugDecomp//DebugRGBColor%i.png",count);
			debugRGBAsPng(buff, pCtx->targetRGBA, pCtx->width, pCtx->height, pixFormat);
			count++;
		}
//		debug1BitAsPng("DebugDecomp//MaskPostColor.png", pCtx->mipMapMask, bbox.w, bbox.h);
	}
}

void decompress1BitMaskAlign8NoMask(YAIK_Instance* pCtx, BoundingBox& header_bbox, u8* data_uncompressed) {
	BoundingBox bbox = header_bbox;
	u32 width  = pCtx->width;
	u32 height = pCtx->height;

	//Heap Temp Allocator
	//	[Alloc for stream decompression of size]
	pCtx->alphaChannel = (u8*)allocateTemp(pCtx, width * height);

	if ((bbox.h > 0) && (bbox.w > 0)) {
		// No empty surface allowed.

		// 8 pixel aligned.
		assert((bbox.x & 7) == 0);
		assert((bbox.w & 7) == 0);

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
}

void decompress6BitTo8BitAlphaNoMask(YAIK_Instance* pCtx, AlphaHeader* header, bool useInverseValues, u8* data_uncompressed) {
	if ((header->bbox.h > 0) && (header->bbox.w > 0)) {
		// No empty surface allowed.
		// Heap Buffer Allocator :
		// [Target Buffer Mask	w*h]
		pCtx->alphaChannel	= (u8*)allocateTemp(pCtx, pCtx->width * pCtx->height);
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
}

void decompress6BitTo8BitAlphaUsingMipmapMask(YAIK_Instance* pCtx, AlphaHeader* header, bool useInverseValues, u8* data_uncompressed) {
	if ((header->bbox.h > 0) && (header->bbox.w > 0)) {
		// No empty surface allowed.
		// Heap Buffer Allocator :
		// [Target Buffer Mask	w*h]
		pCtx->alphaChannel = (u8*)allocateTemp(pCtx, pCtx->width * pCtx->height);
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
}

void decompress8BitTo8BitAlphaNoMask(YAIK_Instance* pCtx, AlphaHeader* header, u8* data_uncompressed) {
	// TODO : Reuse from previous alpha stuff.
	// Support only through memcpy.
	if ((header->bbox.h > 0) && (header->bbox.w > 0)) {
		// No empty surface allowed.
		// Heap Buffer Allocator :
		// [Target Buffer Mask	w*h]
		pCtx->alphaChannel = (u8*)allocateTemp(pCtx, pCtx->width * pCtx->height);
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
}

// TODO : Remap 8 Bit Alpha mask to RGBA 32 bit memory (Later in the process) when doing convert YCoCg full to RGBA final.
//		  Problem : Will do the conversion ONLY on Mipmap bounding bbox. If smaller than full image, need to fill zeros.
//			=== DO YCoCg->RGB IN PLACE !!! Save bandwidth, no copy. ===

/*
-----------------------------------------------------------------------------
2. Decompression technique for Color
-----------------------------------------------------------------------------
	Stream need to be decompressed anyway to 1:1 scale to maintain 'mask' when updating list of available pixels or not.
	1 Bit mask good too.

	Trick : bounding box need to be a multiple of 8. -> Allow decompression of block on byte alignment.
	Then memcpy to put block at the correct place.
*/
#if 0
u8* mask;			// 1 Bit full screen mask.
u8* stream;			// 1 Bit stream of pixels.
int streamBitCount = wBBoxCol * hBBoxCol;

u8  PIXELFORMAT; // 3 or 4
u8* rgbaBuff;

u8 r, g, b;
// TODO Convert into YCoCg
u8 Y, Co, Cg;

int xBBoxCol, yBBoxCol, wBBoxCol, hBBoxCol;
int x1BBoxCol = xBBoxCol + wBBoxCol;
int y1BBoxCol = yBBoxCol + hBBoxCol;

int dy = yBBoxCol * widthImg;
int dx = xBBoxCol;

u8* stream;
int posBitStream = 0;

// TODO : possibility to main a span list for X ?
//			Try to skip as much as possible at each pass. (left, right or complete line)
//			Scan of mipmapMask the first time and build left/right table (or compress ?)
//			Update scan if we delete first or last pixel of scanline. (dx value == scan edge)

while (dy < y1BBoxCol) {

	// TODO OPTIMIZE : Load from scan table edge left/right

	dx = xBBoxCol;
	while (dx < x1BBoxCol) {
		int pixPos = dx + dy;
		int bytePos = pixPos >> 3;
		u8 maskBit = (1 << (pixPos & 7));

		// Empty pixel slot in destination ?
		if (mask[bytePos] & maskBit) {
			// If valid pixel in mask => Write RGB.
			if (stream[posBitStream >> 3] & (1 << (posBitStream & 7))) {
				// Remove pixel slot from mask.
				mask[bytePos] |= ~maskBit;

				// TODO OPTIMIZE : if (dx == scanStart || dx == scanEnd) { recomputeLine = true; }

				// Pixel is in MASK, next pixel in stream.
				int adr = pixPos * PIXELFORMAT; // RGBA Adress.
				rgbBuff[adr++] = Y;
				rgbBuff[adr++] = Co;
				rgbBuff[adr] = Cg;
			}

			posBitStream++;
		}
		dx++;
	}

	// TODO OPTIMIZE : if (recomputeLine) { ... recompute scanStart / end and update table }

	dy += widthImg;
}


/*
-----------------------------------------------------------------------------
3. Decompression technique for RGB Smooth Map
-----------------------------------------------------------------------------
	Have a bounding box for 1/4 Res 1 Bit smooth map.
	Reuse code from 1. with limit of 8 aligned pixels in smooth map. :-)
*/
u8* pSrc;

int gridX; // Offset 0..3 : Position in 1:1 for RGB grid when decompress.
int gridY; // Offset 0..3 : Position in 1:1 for RGB grid when decompress.

// Grid sampling in 1/4 size bit map.
int quartGX = gridX >> 1; // 0 or 1
int quartGY = gridY >> 1; // 0 or 1
int quartW = (widthImg >> 1);
int stepY = 2 * quartW;	// Quarter res, then skip 2 lines.
int endY = smoothBBy1 * quartW;

int fullY = ((smoothBBy * 2) + gridY);
for (int y = ((smoothBBy + quartGY) * quartW); y < endY;) {
	for (int x = smoothBBx; x < smoothBBx1; x += 2) {
		int pixPos = x + y + quartGX;
		int bytePos = pixPos >> 3;
		u8 maskBit = (1 << (pixPos & 7));
		if (mask[bytePos] & maskBit) {
			u8 r = *pSrc++;
			u8 g = *pSrc++;
			u8 b = *pSrc++;

			// TODO Conversion RGB -> YCoCg

			int adr = (((x * 2) + gridX) + fullY) * PIXELFORMAT; // RGBA Adress.
			rgbBuff[adr++] = Y;
			rgbBuff[adr++] = Co;
			rgbBuff[adr] = Cg;
		}
	}
	y += stepY;
	fullY += widthImg;
}
#endif

void generateFullMap() {
	// Not implemented.
	kassert(false);
}


#define TAG_MIPMAP		(0x4d50494d)
#define TAG_ALPHA		(0x4d504c41)
#define TAG_COLOR		(0x4c4f4355)
#define TAG_SMOOTH		(0x50414d53)
#define TAG_PLANE		(0x544e4c50)
#define TAG_GRADTILE	(0x4c495447)

void wrongOrder() {
	kassert(false); // Not implemented.
}

YAIK_ERROR_CODE YAIK_GetErrorCode(const char** messageOut) {
	if (messageOut) {
		*messageOut = "TO DO IMPLEMENT ERROR STRING";
	}
	return gErrorCode;
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
		gLibrary.instances = NULL;
		return maxDecodeThreadContext * sizeof(YAIK_Instance);
	} else {
		SetErrorCode(YAIK_INVALID_CONTEXT_COUNT);
		return 0;
	}
}

YAIK_LIB		YAIK_Init			(void* memory, u8 maxDecodeThreadContext) {

	// [TODO] Find a way to check that maxDecodeThreadContext is the same as previous call.

	if (memory) {
		// Return the amount of memory required to perform library init.
		gLibrary.totalSlotCount = maxDecodeThreadContext;
		gLibrary.freeSlotCount	= maxDecodeThreadContext;
		gLibrary.instances		= (YAIK_Instance*)memory;
		for (int n = 0; n < maxDecodeThreadContext; n++) {
			gLibrary.freeStack[n] = &gLibrary.instances[n];
		}

		InitLUT();

		return &gLibrary;
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

bool			YAIK_DecodeImagePre	(YAIK_LIB libMemory, void* sourceStreamAligned, u32 streamLength, DecodedImage& fillSize) {
	fillSize.hasAlpha		= false;	// Fill later, reset for now.
	fillSize.hasAlpha1Bit	= false;	// Fill later, reset for now.
	fillSize.outputImage	= NULL;		// User can NOT put a buffer in there yet anyway.
	fillSize.width			= 0;
	fillSize.height			= 0;
	fillSize.internalTag	= NULL;
	fillSize.tempMemory		= NULL;
	fillSize.temporaryMemoryAmount = 0;

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
				fillSize.width		= pHeader->width;
				fillSize.height		= pHeader->height;
				fillSize.hasAlpha	= pHeader->infoMask & FileHeader::BIT_ALPHA_CHANNEL ? true : false;

				// [TODO]
				fillSize.temporaryMemoryAmount = 0;

				// Check user miss between Pre and real decode.
				memset(instanceCtx, 0, sizeof(YAIK_Instance));
				instanceCtx->srcCheck		= sourceStreamAligned;
				instanceCtx->srcLength		= streamLength;
				instanceCtx->isRGBA			= fillSize.hasAlpha;
				instanceCtx->mapRGB			= NULL;
				instanceCtx->mipMapMask		= NULL;

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

bool			YAIK_DecodeImage	(void* sourceStreamAligned, u32 streamLength, DecodedImage& context) {
	YAIK_Instance*	pCtx		= (YAIK_Instance*)context.internalTag;
	FileHeader*		pHeader		= (FileHeader*)sourceStreamAligned;

	pCtx->width			= pHeader->width;
	pCtx->height		= pHeader->height;
	pCtx->targetRGBA	= context.outputImage;

	pHeader++; // Skip header now.

	if (!pCtx) 
	{ SetErrorCode(YAIK_DECIMG_INVALIDCTX); goto error; }

	if ((pCtx->srcCheck != sourceStreamAligned) || (pCtx->srcLength != streamLength)) 
	{ SetErrorCode(YAIK_DECIMG_DIFFSTREAM); goto error; }

	if (pCtx->targetRGBA == NULL) 
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
				decompress1BitTiled(pCtx,pHeader, (u8*)&pHeader[1]);

				state = 1;
			} else {
				wrongOrder(); goto error;
			}
			break;
		}
		case TAG_ALPHA:
		{
			bool error = false;
			AlphaHeader* pHeader = (AlphaHeader*)pBlock;

			if (state >= 2) {
				wrongOrder(); goto error;
			}

			// --- Decompress Alpha Block ---
			u8* dataUncompressed = decompressData(pCtx, (u8*)&pHeader[1], pHeader->streamSize, pHeader->expectedDecompressionSize);

			if (state == 0) {
				switch (pHeader->parameters & 7) {
				case AlphaHeader::IS_1_BIT_FULL:
					decompress1BitMaskAlign8NoMask			(pCtx, pHeader->bbox, dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_FULL:
					decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, false,dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_FULL_INVERSE:
					decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, true, dataUncompressed);
					break;
				case AlphaHeader::IS_8_BIT_FULL:
					decompress8BitTo8BitAlphaNoMask			(pCtx, pHeader, dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_USEMIPMAPMASK:
				case AlphaHeader::IS_6_BIT_USEMIPMAPMASK_INVERSE:
				case AlphaHeader::IS_1_BIT_USEMIPMAPMASK:
					SetErrorCode(YAIK_ALPHA_FORMAT_IMPOSSIBLE);
					assert(false);
					error = true;
					break;
				default:
					SetErrorCode(YAIK_INVALID_ALPHA_FORMAT);
					assert(false);
					error = true;
				}
			} else if (state == 1) {
				switch (pHeader->parameters & 7) {
				case AlphaHeader::IS_1_BIT_FULL:
					decompress1BitMaskAlign8NoMask			(pCtx, pHeader->bbox, dataUncompressed);
					break;
				case AlphaHeader::IS_1_BIT_USEMIPMAPMASK:
					SetErrorCode(YAIK_ALPHA_UNSUPPORTED_YET);
					break;
				case AlphaHeader::IS_6_BIT_FULL:
					decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, false,dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_FULL_INVERSE:
					decompress6BitTo8BitAlphaNoMask			(pCtx, pHeader, true, dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_USEMIPMAPMASK:
					decompress6BitTo8BitAlphaUsingMipmapMask(pCtx, pHeader, false,dataUncompressed);
					break;
				case AlphaHeader::IS_6_BIT_USEMIPMAPMASK_INVERSE:
					decompress6BitTo8BitAlphaUsingMipmapMask(pCtx, pHeader, true, dataUncompressed);
					break;
				case AlphaHeader::IS_8_BIT_FULL:
					decompress8BitTo8BitAlphaNoMask			(pCtx, pHeader, dataUncompressed);
					break;
				default:
					SetErrorCode(YAIK_INVALID_ALPHA_FORMAT);
					error = true;
					assert(false);
				}
			}

			// TODO : Support if encoder use ALPHA as MIPMAP ? -> Conversion from Alpha to Swizzled mask.
				
			releaseMemory(pCtx,dataUncompressed);

			#if (USE_DEBUG_IMAGE)
			int err = stbi_write_png("DebugDecomp\\DumpAlphaChannel.png",pCtx->width, pCtx->height, 1, pCtx->alphaChannel, pCtx->width);
			#endif
				
			state = 2;
			break;
		}
		case TAG_COLOR:
		{
			if (state <= 2) {
				UniqueColorHeader* pHeader = (UniqueColorHeader*)pBlock;
				u8* after = (u8*)(&pHeader[1]);

				//
				// For decoding performance reason, 
				// target buffer must map bbox full surface for now.
				// But format allow later extensions...
				//
				assert(pHeader->bbox.w == pCtx->maskBBox.w);
				assert(pHeader->bbox.h == pCtx->maskBBox.h);
				assert(pHeader->bbox.x == pCtx->maskBBox.x);
				assert(pHeader->bbox.y == pCtx->maskBBox.y);

				// RGB Palette (Read in place)
				u8* palette			= &after[-3];										// Index 0 never used. 
				int colorCount		= pHeader->colorCount ? pHeader->colorCount : 256;	// 0 = 256
				u8* wPal			= &palette[1];

				for (int n=1;n<colorCount;n++) {
					// [TODO] COULD BE BUGGY... Redo 'proper in place'
					slowRGB2YCoCg(wPal[0],wPal[1],wPal[2],wPal[0],wPal[1],wPal[2]);
					wPal += 3;
				}

				if (!pCtx->mipMapMask) {
					createDefaultMipmap(pCtx,pHeader->bbox);
				}

				u8* dataIndex		= &palette[colorCount * 3];
				// --- Decompress Index Block ---
				u8* dataUncompressed= decompressData(pCtx, dataIndex, pHeader->streamSize, pHeader->expectedDecompressionSizeM);
				u8* indexDataStart	= dataUncompressed;
				// --- Decompress Span Block ---
				LineSpan* tbl		= (LineSpan*)decompressData(pCtx, &dataIndex[pHeader->streamSize], pHeader->streamSizeE, pHeader->expectedDecompressionSkippers);
				LineSpan* tblStart	= tbl;
				LineSpan* tblEnd	= &tbl[pHeader->expectedDecompressionSkippers / sizeof(LineSpan)];

				// RGB/RGBA Dest
				u8* mipmapP			= pCtx->mipMapMask;
				// Tile order...
				int streamPos		= 0;
				int endStream		= pHeader->bbox.w*pHeader->bbox.h;
				u8* dataUncompressedE = &dataUncompressed[pHeader->expectedDecompressionSizeM];

				// Interleaved pixel case...
				u8* pY  = &pCtx->targetRGBA[0];
				u8* pCo = &pCtx->targetRGBA[pCtx->width * pCtx->height];
				u8* pCg = &pCtx->targetRGBA[pCtx->width * pCtx->height * 2];

				/*
				if (posCopy < mipmapBbox.h * mipmapBbox.w) {
					int sizeCopy = (mipmapBbox.h * mipmapBbox.w) - posCopy;
					memcpy(pBS,&mapPixel[posCopy],sizeCopy);
					pBS += sizeCopy;
					filledSpan->deltaStart	= posCopy - lastPostCopy;
					filledSpan->length		= sizeCopy;
					filledSpan++;
				}
				*/

				int pos = 0;
				while (tbl != tblEnd) {
					pos += tbl->startDelta;
					u8* endIndexData = &indexDataStart[tbl->length];
					tbl++;
					int posIn = pos;
					while (indexDataStart < endIndexData) {
						int vIdx = (*indexDataStart++);
						if (vIdx) {
							u8* pPal = &palette[vIdx * 3];
							mipmapP[posIn>>3] &= ~(1<<(posIn & 7)); // Reset bit to 0.
							pY [posIn] = pPal[0];
							pCo[posIn] = pPal[1];
							pCg[posIn] = pPal[2];
						}
						posIn++;
					}
				}

				releaseMemory(pCtx,dataUncompressed);
				releaseMemory(pCtx,tblStart);

				#if (USE_DEBUG_IMAGE)
				// TODO RGB seperate plane...
//				debugRGBAsPng ("Color0Full.png",pCtx->targetRGBA, pCtx->width, pCtx->height, pixelFormat);
				dumpColorMap8Swizzle("DebugDecomp\\YPlane.png" ,pY ,pHeader->bbox);
				dumpColorMap8Swizzle("DebugDecomp\\CoPlane.png",pCo,pHeader->bbox);
				dumpColorMap8Swizzle("DebugDecomp\\CgPlane.png",pCg,pHeader->bbox);
				dumpColorMap888Swizzle("DebugDecomp\\Color0Full.png",pY,pCo,pCg,pHeader->bbox);
				debug1BitAsPngLinearOrSwizzled("DebugDecomp\\PostColorMap.png",pCtx->mipMapMask, pCtx->maskBBox.w, pCtx->maskBBox.h,pCtx->maskBBox.w,/*SWIZZLE*/true);
				#endif
			} else {
				wrongOrder(); goto error;
			}
			state = 3;
			break;
		}
		case TAG_SMOOTH:
		{
			if (state <= 3) {
				HeaderSmoothMap* pHeader = (HeaderSmoothMap*)pBlock;
				u8* after = (u8*)(&pHeader[1]);

				state = 4;

				if (!pCtx->mipMapMask) {
					BoundingBox bbox;
					bbox.x = 0;
					bbox.y = 0;
					bbox.w = pCtx->width;
					bbox.h = pCtx->height;
					createDefaultMipmap(pCtx,bbox);
				}

				// Verify that bounding box alignement match decompression implementation.
				// verifyBoundingBox(bbox,4,1);

				// --- Decompress Index Block ---
				int expectedSize = (pHeader->bbox.w * pHeader->bbox.h) >> 3; 
				u8* dataUncompressedQuarterBitmap = decompressData(pCtx, after, pHeader->streamSize, expectedSize);
				u8* allocQuarterBitmap = dataUncompressedQuarterBitmap;

				// --- Decompress Index Block ---
				u8* dataUncompressedRGBStream	= decompressData(pCtx, &after[pHeader->streamSize], pHeader->rgbStreamSize, pHeader->expectedRGBStreamSize);
				u8* allocRGBStream				= dataUncompressedRGBStream;

				BoundingBox bbox = pHeader->bbox;

				// Mipmap 1 bit mask
				int tileW		= pCtx->width >> 3;

				// Use GridX,GridY Here !!!!
				int gridX =  pHeader->grid & 3;
				int gridY = (pHeader->grid>>2) & 3;
				int offsetXYGrid = gridX + (gridY<<3);
				
				// Compute position inside MIPMAP box from the 1/4 size full screen bbox.
				int xPosRelMipmap = (bbox.x<<1) - pCtx->maskBBox.x; // Half position in full image space - Position of Mipmap block.
				int yPosRelMipmap = (bbox.y<<1) - pCtx->maskBBox.y;
				// Swizzle space, top-left
				int offsetTopLeft = (((xPosRelMipmap>>3) + ((yPosRelMipmap>>3)*(pCtx->maskBBox.w>>3)))<<6);

				u8* planeY		= &pCtx->targetRGBA[offsetXYGrid + offsetTopLeft];
				u8* planeCo		= &pCtx->targetRGBA[(pCtx->width * pCtx->height)     + offsetXYGrid + offsetTopLeft];
				u8* planeCg		= &pCtx->targetRGBA[(pCtx->width * pCtx->height * 2) + offsetXYGrid + offsetTopLeft];
				u8* mask		= &pCtx->mipMapMask[(gridY<<1) + (((xPosRelMipmap>>3)*64) + (((yPosRelMipmap>>3)*64*(pCtx->maskBBox.w>>3))>>3) )];

				#if (USE_DEBUG_IMAGE)
				{
					debug1BitAsPngLinearOrSwizzled("DebugDecomp\\PreGradientMask.png",mask, pHeader->bbox.w*2, pHeader->bbox.h*2,pCtx->maskBBox.w,/*SWIZZLE*/true);
				}
				#endif

				// FOR DEBUG ONLY, CLEAR THE CURRENT WORK BUFFER : memset(pCtx->targetRGBA,0,pCtx->width * pCtx->height * 3);

				// Depend on grid position
				u8 mask1 = ~(0x01<<gridX);
				u8 mask2 = ~(0x10<<gridX);
				u8 mask3 = ~(0x11<<gridX);

				#if (USE_DEBUG_IMAGE)
				// TODO RGB seperate plane...
				debug1BitAsPngLinearOrSwizzled("DebugDecomp\\SmoothMap.png",dataUncompressedQuarterBitmap, pHeader->bbox.w, pHeader->bbox.h,pHeader->bbox.w,/*LINEAR*/false);
				#endif

				// Bounding box align 8 pixel in 1/4 size : align 16 pixel in screen space. (no pb with tileX+=2 loop)
				BoundingBox boundXTile;
				/*
				boundXTile.x		= ((bbox.x<<1) - pCtx->maskBBox.x)>>3;	// Quarter -> x4 (= 8 pixels)
				boundXTile.w		= bbox.w>>2;							// Quarter -> x4 (= 8 pixels)
				*/
				boundXTile.x		= 0;	// Quarter -> x4 (= 8 pixels)
				boundXTile.w		= bbox.w>>2;							// Quarter -> x4 (= 8 pixels)
				int endBoundXTile	= (boundXTile.x + boundXTile.w);		// But our loop is about 8 pixel in screen space, treat 4 pixel at a time...
				
				assert((endBoundXTile & 1) == 0); // Always a odd number of tile (pair)

				int pos = 0;
				for (int y=0; y<bbox.h; y+=2) {
					int dstY		= (y<<1)+((bbox.y<<1)-pCtx->maskBBox.y); // TODO Optimize Y variable.

					int YInsideT	= dstY & 7;
					int YInsideTByte= YInsideT << 3;
					int tileY		= (y>>3) * tileW;		// Dst tile pos.
//					printf("-----\n");
					for (int tileX=boundXTile.x; tileX<endBoundXTile; tileX+=2) {
						u8 v = dataUncompressedQuarterBitmap[pos] & 0x55;
//						printf("x:%i, y:%i yL:%i = %x \n", (tileX*8)+pCtx->maskBBox.x, dstY+pCtx->maskBBox.y, (y<<1), dataUncompressedQuarterBitmap[pos]);

						if (v) {
//							printf("FOUND\n");
							// 4 Bit bitmap per tile in screen space.
							for (int blk =0; blk < 128; blk += 64) {
								u8 v0 = (v >> (blk>>4)) & 0x5;
								if (v0) { // xRxL
									int posPix = ((tileY + tileX)<<6)		// Tile position in bitmap.	(tileX<<1 because v0 4 bit = 2 tile in screen space width !)
												| blk						// X Pixel position inside tile.
												| YInsideTByte;				// Y Pixel position inside tile.

									u8* pY = &planeY [posPix];
									u8* pCo= &planeCo[posPix];
									u8* pCg= &planeCg[posPix];

									//
									// The fast compact punch card writer...
									//
									switch (v0) {
									case 0x0: 
									case 0x2:
										// Do nothing.
										break;
									case 0x1:
									case 0x3:
										pY [0]	= *dataUncompressedRGBStream++;
										pCo[0]	= *dataUncompressedRGBStream++;
										pCg[0]	= *dataUncompressedRGBStream++;
					/*
										printf("x:%i, y:%i = [%i,%i,%i]\n",
											(blk ? 8 : 0) + (tileX*8)+pCtx->maskBBox.x,
											dstY+pCtx->maskBBox.y,
											pY[0],pCo[0],pCg[0]);
					*/
										mask[posPix>>3] &= mask1;
										break;
									case 0x4:
									case 0x6: 
										pY [4]	= *dataUncompressedRGBStream++;
										pCo[4]	= *dataUncompressedRGBStream++;
										pCg[4]	= *dataUncompressedRGBStream++;
					/*

										printf("x:%i, y:%i = [%i,%i,%i]\n",
											(blk ? 8 : 0) + 4 + (tileX*8)+pCtx->maskBBox.x,
											dstY+pCtx->maskBBox.y,
											pY[4],pCo[4],pCg[4]);
					*/
										mask[posPix>>3] &= mask2;
										break;
									case 0x5:
									case 0x7: 
										pY [0]	= *dataUncompressedRGBStream++;
										pCo[0]	= *dataUncompressedRGBStream++;
										pCg[0]	= *dataUncompressedRGBStream++;
										pY [4]	= *dataUncompressedRGBStream++;
										pCo[4]	= *dataUncompressedRGBStream++;
										pCg[4]	= *dataUncompressedRGBStream++;
					/*

										printf("x:%i, y:%i = [%i,%i,%i]\n",
											(blk ? 8 : 0) + (tileX*8)+pCtx->maskBBox.x,
											dstY+pCtx->maskBBox.y,
											pY[0],pCo[0],pCg[0]);

										printf("x:%i, y:%i = [%i,%i,%i]\n",
											(blk ? 8 : 0) + 4 + (tileX*8)+pCtx->maskBBox.x,
											dstY+pCtx->maskBBox.y,
											pY[4],pCo[4],pCg[4]);
					*/
										mask[posPix>>3] &= mask3;
										break;
									}
								}
							}
						}
						pos++;
					}
					pos += bbox.w>>3;
				}


				#if (USE_DEBUG_IMAGE)
				{
					u8* pY  = &pCtx->targetRGBA[0];
					u8* pCo = &pCtx->targetRGBA[pCtx->width * pCtx->height];
					u8* pCg = &pCtx->targetRGBA[pCtx->width * pCtx->height * 2];

					dumpColorMap888Swizzle("DebugDecomp\\ColorGradientPixels.png",pY,pCo,pCg,pCtx->maskBBox);
					debug1BitAsPngLinearOrSwizzled("DebugDecomp\\PostGradientMap.png",mask, pHeader->bbox.w*2, pHeader->bbox.h*2,pCtx->maskBBox.w,/*SWIZZLE*/true);
					debug1BitAsPngLinearOrSwizzled("DebugDecomp\\PostGradientMapComplete.png",pCtx->mipMapMask, pCtx->maskBBox.w, pCtx->maskBBox.h,pCtx->maskBBox.w,/*SWIZZLE*/true);

				}
				#endif

				releaseMemory(pCtx,allocQuarterBitmap);
				releaseMemory(pCtx,allocRGBStream);

			} else {
				wrongOrder(); goto error;
			}

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
					createDefaultMipmap(pCtx,bbox);
				}

				if (!pCtx->mapRGB) {
					pCtx->strideRGBMap  = (pCtx->width>>2)+1;
					int size			=  pCtx->strideRGBMap * ((pCtx->height>>2)+1);
					pCtx->mapRGB		= (u8*)allocateTemp(pCtx,size * 3);
					int sizeMask		= (size+7) >>3;
					pCtx->mapRGBMask	= (u8*)allocateTemp(pCtx,sizeMask);
					// We do not care about garbage in mapRGB.
					// mapRGBMask will tell us if we have valid data or not.
					memset(pCtx->mapRGBMask, 0, sizeMask);
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

				u8* dataUncompressedTilebitmap = (u8*)decompressData(pCtx, after, pHeader->streamBitmapSize, sizeBitmap);
				after += pHeader->streamBitmapSize;
				u8* dataUncompressedRGB        = (u8*)decompressData(pCtx, after, pHeader->streamRGBSize   , pHeader->streamRGBSizeUncompressed);


				// -------------------------------------------------------------------
				//  Generate Tile
				// -------------------------------------------------------------------
				u8* pR = &pCtx->targetRGBA[0];
				u8* pG = &pCtx->targetRGBA[pCtx->width * pCtx->height];
				u8* pB = &pCtx->targetRGBA[pCtx->width * pCtx->height * 2];

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

				// -------------------------------------------------------------------
				//  Free automatic.
				// -------------------------------------------------------------------

				BoundingBox bb;
				bb.x=0; bb.y=0;
				bb.w=512; bb.h=720;
				// printf("Color Count Stream : %i\n",pHeader->streamRGBSizeUncompressed/3);
//				dumpColorMap888Swizzle("GradientTest.png",pR,pG,pB,bb);
//				debugRGBAsPng("RGBMap.png",pCtx->mapRGB, (pCtx->width>>2)+1, ((pCtx->height>>2)+1), 3);
			}
		}
			break;
		case TAG_PLANE:
		{
			if (state <= 5) {
				PlaneTile* pHeader = (PlaneTile*)pBlock;
				u8* after = (u8*)(&pHeader[1]);

				state = 5;

				BoundingBox bbox = pHeader->bbox;

				// Mipmap 1 bit mask
				u8* mipmapP		= pCtx->mipMapMask;
				int tileW		= pCtx->maskBBox.w >> 3;
				BoundingBox boundXTile;

				int xPosRelMipmap = bbox.x - pCtx->maskBBox.x; // Half position in full image space - Position of Mipmap block.
				int yPosRelMipmap = bbox.y - pCtx->maskBBox.y;

				bbox.x			= xPosRelMipmap;
				bbox.y			= yPosRelMipmap;
				boundXTile.x	= xPosRelMipmap>>3;
				boundXTile.y	= yPosRelMipmap>>3;
				boundXTile.w	= bbox.w>>3;
				boundXTile.h	= bbox.h>>3;

				int blockCount = boundXTile.w * boundXTile.h * 2;

				u16* dataUncompressedTileMap = (u16*)decompressData(pCtx, after, pHeader->streamSizeTileMap, blockCount);

				// Skip Tile Map
				after += pHeader->streamSizeTileMap;

				u8* dataUncompressedTileStream = decompressData(pCtx, after, pHeader->streamSizeTileStream, pHeader->expectedSizeTileStream);
				u8* idStream = dataUncompressedTileStream;

				u8* srcPlane = NULL;
				switch ((pHeader->format >> 2) & 0x3) {
				// Y,Co,Cg
				case 0: srcPlane = &pCtx->targetRGBA[0]; break;
				case 1: srcPlane = NULL; // &pCtx->targetRGBA[(pCtx->width * pCtx->height)]; 
					break;
				case 2: srcPlane = NULL; // &pCtx->targetRGBA[(pCtx->width * pCtx->height * 2)]; 
					break;
				case 3: 
					srcPlane = NULL;
					SetErrorCode(YAIK_INVALID_PLANE_ID);
				}

				if (srcPlane) {
				int topLeftTile	= (boundXTile.y*boundXTile.w + boundXTile.x);
				u8* pTile		= &srcPlane[topLeftTile<<6];
				u64* pMaskTile	= (u64*)&mipmapP [topLeftTile];
				int boundYTileEnd = boundXTile.y + boundXTile.h;
				u8  indexSwap   = 0;
				u16* pInfoTile	= (u16*)&dataUncompressedTileMap;

				u8* tileLUTBase = GetLUTBase4Bit((pHeader->format >> 2) & 0x3);
				int amount = 0;

				static int counter = 0;
				char buffer[2000]; sprintf(buffer,"DebugDecomp\\PostGradientMapComplete%i.png",counter);
				debug1BitAsPngLinearOrSwizzled(buffer,pCtx->mipMapMask, pCtx->maskBBox.w, pCtx->maskBBox.h,pCtx->maskBBox.w,/*SWIZZLE*/true);

				int idPos    = 0;
				for (int y=boundXTile.y; y < boundYTileEnd; y++) {
					u8* lineTile		= pTile;
					u8* lineTileE		= &pTile[boundXTile.w<<6];
					u64* lineMask		= pMaskTile;
					u16* pParseInfoTile	= pInfoTile;

					printf("Y:%i\n",(y*8)+192);

					while (lineTile < lineTileE) {
						int xc = ((lineTile - pTile)>>3)+240;

						u64 mask64		= *lineMask++;

						// printf("X:%i -> %i\n",xc,BitCount(mask64));
						printf("%i-",idPos);

						u16 tileLUTID	= *pParseInfoTile++;

						// Skip complete tile...
						if (mask64 == 0ULL) {
							lineTile += 64;
							continue;
						}

						indexSwap		= 0x0;					// TODO. No compression for now.
						u8* LUT			= &tileLUTBase[tileLUTID << 4];

						int start = 4;
						if ((mask64 & 0xFFFFFFFF) == 0) {
							lineTile  += 4;
							mask64   >>= 32;
							start      = 4;
						} else {
							start	   = 0;
						}

						for (int n=start; n < 8; n++) {
							// 8 Pixel mask.
							u8 readMask = (u8)mask64;
							mask64 >>= 8;

							if (readMask) {
								// TODO : Can optimize the case with start odd or even ?
								for (int m=0; m < 2; m++) {
									u8 idStreamV = (idStream[idPos>>1]) ^ indexSwap;
									u8 lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
									switch (readMask & 0xF) {
									case 0x0:
										lineTile += 4;
										readMask = readMask>>4;
										break;
									case 0x1:
										amount++;
										lineTile[0] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0x2:
										amount++;
										lineTile[1] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0x3:
										amount++;
										amount++;
										lineTile[0] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[1] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0x4:
										amount++;
										lineTile[2] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0x5:
										amount++;
										amount++;
										lineTile[0] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[2] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0x6:
										amount++;
										amount++;
										lineTile[1] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[2] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0x7:
										amount++;
										amount++;
										amount++;
										lineTile[0] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[1] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[2] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0x8:
										amount++;
										lineTile[4] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0x9:
										amount++;
										amount++;
										lineTile[0] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[4] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0xA:
										amount++;
										amount++;
										lineTile[1] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[4] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0xB:
										amount++;
										amount++;
										amount++;
										lineTile[0] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[1] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[4] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0xC:
										amount++;
										amount++;
										lineTile[2] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[4] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0xD:
										amount++;
										amount++;
										amount++;
										lineTile[0] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[2] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[4] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0xE:
										amount++;
										amount++;
										amount++;
										lineTile[1] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[2] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[4] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									case 0xF:
										amount++;
										amount++;
										amount++;
										amount++;
										lineTile[0] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[1] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[2] = LUT[lutIndex]; idPos++;
										idStreamV = idStream[idPos>>1] ^ indexSwap; lutIndex = (idStreamV >> ((idPos&1)<<2)) & 0xF;
										lineTile[4] = LUT[lutIndex]; idPos++;
										readMask = readMask>>4;
										lineTile += 4;
										break;
									}
								}
							} else {
								lineTile += 8;
							}
						}
					}

					pTile			+= tileW<<6;
					pMaskTile		+= tileW;
					pParseInfoTile	+= tileW;
				}

				}
//				printf("fff %i", amount);

			} else {
				wrongOrder(); goto error;
			}
			break;
		}

		// Other todo....

		default:
			SetErrorCode(YAIK_INVALID_TAG_ID); goto error;
		}

		// Skip to the next block using tag info.
		pBlock = (HeaderBase*)endBlock;
	}

	}

	/*
	pCtx->FillGradient		();
	pCtx->ComputeCoCg		();
	pCtx->ConvertYCoCg2RGB	();
	*/

	gLibrary.FreeInstance(pCtx);
	return true;
error:
	return false;
}
