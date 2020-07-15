#include "YAIK_functions.h"

// #define DEBUG_LOG

#ifdef DEBUG_LOG
	#include <stdio.h>
	#define DBG_CODED_DEF		int coded = 0;
	#define DBG_CODED(a)		coded |= a;
#else
	#define DBG_CODED_DEF		;
	#define DBG_CODED(a)		;
#endif

// [TODO] : Write document about Gradient representation (PDF ?) and link to documentation folder.
// [TODO] : tile4x4Mask is using a different stride when image is NOT a multiple of 16 pixel. Implementation rely on x8 stride for now. MUST use x16 aligned stride !
//
// Because of swizzling, table representation is larger than bitmap representation. (Don't want image to be x64)
//
// 16x16 : 16 bit table (64x64 pixel) -> Lookup the table by block of 4x4 tile. -> Allow 16/4 skip
// 16x 8 : 32 bit table (64x64 pixel) -> Lookup the table by block of 4x8 tile. -> Allow 
//  8x16 : 32 bit table (64x64 pixel) -> Lookup the table by block of 8x4 tile.
//  8x 8 : 64 bit table (64x64 pixel) -> Lookup the table by block of 8x8 tile.
//  8x 4 : 64 bit table (64x32 pixel) -> Lookup the table by block of 8x8 tile.
//  4x 8 : 64 bit table (32x64 pixel) -> Lookup the table by block of 8x8 tile.
//  4x 4 : 64 bit table (32x32 pixel) -> Lookup the table by block of 8x8 tile.
//

void DecompressGradient16x16(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* planeG, u8* planeB, u8 planeBit) {
	if (planeBit != 7) {
		// Not supported in this version.
#ifdef YAIK_DEVEL
		while (1) {
		}
#else
		return;
#endif
	}

	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u32 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	int idxX;
	int idxY		= 0;
	u16* tileBitmap	= (u16*)dataUncompressedTilebitmap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY16	= (pInstance->strideRGBMap * 4);
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3);
	int strideTile4 = RDIV16(iw);

	// RGB Version Only.

	for (u16 y=0; y<(ih>>4); y+=4) { // 64 pix (4x16)
		idxX = idxY;
		for (u16 x=0; x<(iw>>4); x+=4) { // 64 pix (4x16)
			u16 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				for (u16 yt=y; yt<y+4; yt++) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xF;
					tWord >>=4;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY16;
							continue; 
						}
					}

					int idxX2 = idxY2;
					for (u16 xt=x; xt<x+4; xt++) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							if ((hasRGB[DIV8(idxX2)] & v)==0) {
								hasRGB[DIV8(idxX2)] |= v;
								// Load LT
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+4;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RT
								pCornerRGB[12] = *dataUncompressedRGB++;
								pCornerRGB[13] = *dataUncompressedRGB++;
								pCornerRGB[14] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY16 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY16;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load LB
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx += 4;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RB
								pCornerRGB[12] = *dataUncompressedRGB++;
								pCornerRGB[13] = *dataUncompressedRGB++;
								pCornerRGB[14] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Grad %i,%i [%i,%i,%i][%i,%i,%i]-[%i,%i,%i][%i,%i,%i]\n",
								xt<<4,yt<<4,
								rgbCorner[0][ 0],rgbCorner[0][ 1],rgbCorner[0][ 2],
								rgbCorner[0][12],rgbCorner[0][13],rgbCorner[0][14],
								rgbCorner[1][ 0],rgbCorner[1][ 1],rgbCorner[1][ 2],
								rgbCorner[1][12],rgbCorner[1][13],rgbCorner[1][14]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//

							int idxTilePlane = ((yt*strideTile) + xt)<<(1+6); // Shift 1 because tile 16 to 8, then x64 for pixel count.

							// Left-Right Top
							// Left-Right Bottom
							int idxMapTile4  = xt + (yt<<1)*strideTile4;
							pixelUsed[idxMapTile4            ]	= 0xFF;
							pixelUsed[idxMapTile4+strideTile4]	= 0xFF;

							for (int tileY=0; tileY <16; tileY+=8) {
								u8* tileRL = &planeR[idxTilePlane];
								u8* tileGL = &planeG[idxTilePlane];
								u8* tileBL = &planeB[idxTilePlane];
								for (int ty=tileY; ty < tileY+8; ty++) {
									int nW = 16-ty;
									// Vertical Blend
									int rL = (rgbCorner[0][ 0] * nW) + (rgbCorner[1][ 0] * ty);
									int gL = (rgbCorner[0][ 1] * nW) + (rgbCorner[1][ 1] * ty);
									int bL = (rgbCorner[0][ 2] * nW) + (rgbCorner[1][ 2] * ty);
									int rR = (rgbCorner[0][12] * nW) + (rgbCorner[1][12] * ty);
									int gR = (rgbCorner[0][13] * nW) + (rgbCorner[1][13] * ty);
									int bR = (rgbCorner[0][14] * nW) + (rgbCorner[1][14] * ty);

									// Horizontal blending...
									// 16 Color Line Left Tile
									tileRL[0] = rL>>4;	tileRL[1] = (rL*15 + rR)>>8; tileRL[2] = (rL*7 + rR)>>7; tileRL[3] = (rL*13 + rR*3)>>8; tileRL[4] = (rL*3 + rR)>>6; tileRL[5] = (rL*11 + rR*5)>>8; tileRL[6] = (rL*10 + rR*6)>>8; tileRL[7] = (rL*9 + rR*7)>>8;
									tileGL[0] = gL>>4;	tileGL[1] = (gL*15 + gR)>>8; tileGL[2] = (gL*7 + gR)>>7; tileGL[3] = (gL*13 + gR*3)>>8; tileGL[4] = (gL*3 + gR)>>6; tileGL[5] = (gL*11 + gR*5)>>8; tileGL[6] = (gL*10 + gR*6)>>8; tileGL[7] = (gL*9 + gR*7)>>8;
									tileBL[0] = bL>>4;	tileBL[1] = (bL*15 + bR)>>8; tileBL[2] = (bL*7 + bR)>>7; tileBL[3] = (bL*13 + bR*3)>>8; tileBL[4] = (bL*3 + bR)>>6; tileBL[5] = (bL*11 + bR*5)>>8; tileBL[6] = (bL*10 + bR*6)>>8; tileBL[7] = (bL*9 + bR*7)>>8;

									tileRL[64] = (rL + rR)>>5;	tileRL[65] = (rL*7 + rR*9)>>8; tileRL[66] = (rL*6 + rR*10)>>8; tileRL[67] = (rL*5 + rR*11)>>8; tileRL[68] = (rL + rR*3)>>6; tileRL[69] = (rL*3 + rR*13)>>8; tileRL[70] = (rL + rR*7)>>7; tileRL[71] = (rL + rR*15)>>8;
									tileGL[64] = (gL + gR)>>5;	tileGL[65] = (gL*7 + gR*9)>>8; tileGL[66] = (gL*6 + gR*10)>>8; tileGL[67] = (gL*5 + gR*11)>>8; tileGL[68] = (gL + gR*3)>>6; tileGL[69] = (gL*3 + gR*13)>>8; tileGL[70] = (gL + gR*7)>>7; tileGL[71] = (gL + gR*15)>>8;
									tileBL[64] = (bL + bR)>>5;	tileBL[65] = (bL*7 + bR*9)>>8; tileBL[66] = (bL*6 + bR*10)>>8; tileBL[67] = (bL*5 + bR*11)>>8; tileBL[68] = (bL + bR*3)>>6; tileBL[69] = (bL*3 + bR*13)>>8; tileBL[70] = (bL + bR*7)>>7; tileBL[71] = (bL + bR*15)>>8;

									tileRL += 8;
									tileGL += 8;
									tileBL += 8;
								}
								idxTilePlane += strideTile<<6;
							}
						}
						msk >>= 1;
						idxX2 += 4;
					}
					idxY2 += strideY16;
				}
			}
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY64;
	}
}

void DecompressGradient16x8	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* planeG, u8* planeB, u8 planeBit) {
	if (planeBit != 7) {
		// Not supported in this version.
#ifdef YAIK_DEVEL
		while (1) {
		}
#else
		return;
#endif
	}

	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u32 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	int idxX;
	int idxY		= 0;
	u32* tileBitmap = (u32*)dataUncompressedTilebitmap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY8	= (pInstance->strideRGBMap * 2);
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	int strideTile4 = RDIV16(iw);

	// RGB Version Only.

	for (u16 y=0; y<ih; y+=64) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=64) {
			u32 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 32;
					idxY2  += strideY8<<2; // 8*4
				}

				// If nothing if upper 8 pixel, early jump.
				if ((tWord & 0xFF) == 0) {
					tWord >>= 8;
					yt     += 16;
					idxY2  += strideY8<<1; // 8*2
				}

				for (; yt<y+64; yt+=8) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xF;
					tWord >>=4;

					int idxX2 = idxY2;
					u16 xt=x;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY8;
							continue; 
						}
					} else {
						if ((msk & 3) == 0) {
							msk  >>= 2;
							idxX2 += 8;
							xt    += 32;
						}
					}

					int offUsedPixY = ((yt >> 3)*strideTile4);

					for (; xt<x+64; xt+=16) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							if ((hasRGB[DIV8(idxX2)] & v)==0) {
								hasRGB [DIV8(idxX2)]|= v;
								// Load LT
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+4;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RT
								pCornerRGB[12] = *dataUncompressedRGB++;
								pCornerRGB[13] = *dataUncompressedRGB++;
								pCornerRGB[14] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY8 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY8;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load LB
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx += 4;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RB
								pCornerRGB[12] = *dataUncompressedRGB++;
								pCornerRGB[13] = *dataUncompressedRGB++;
								pCornerRGB[14] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Grad %i,%i [%i,%i,%i][%i,%i,%i]-[%i,%i,%i][%i,%i,%i]\n",
								xt,yt,
								rgbCorner[0][ 0],rgbCorner[0][ 1],rgbCorner[0][ 2],
								rgbCorner[0][12],rgbCorner[0][13],rgbCorner[0][14],
								rgbCorner[1][ 0],rgbCorner[1][ 1],rgbCorner[1][ 2],
								rgbCorner[1][12],rgbCorner[1][13],rgbCorner[1][14]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//


							int idxMapTile4  = (xt >> 4) + offUsedPixY;
							pixelUsed[idxMapTile4] = 0xFF;

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6));
							u8* tileRL = &planeR[idxTilePlane];
							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];
							for (int ty=0; ty < 8; ty++) {
								int nW = 8-ty;
								// Vertical Blend
								int rL = (rgbCorner[0][ 0] * nW) + (rgbCorner[1][ 0] * ty);
								int gL = (rgbCorner[0][ 1] * nW) + (rgbCorner[1][ 1] * ty);
								int bL = (rgbCorner[0][ 2] * nW) + (rgbCorner[1][ 2] * ty);
								int rR = (rgbCorner[0][12] * nW) + (rgbCorner[1][12] * ty);
								int gR = (rgbCorner[0][13] * nW) + (rgbCorner[1][13] * ty);
								int bR = (rgbCorner[0][14] * nW) + (rgbCorner[1][14] * ty);

								// Horizontal blending...
								// 16 Color Line Left Tile
								tileRL[0] = rL>>3;	tileRL[1] = (rL*15 + rR)>>7; tileRL[2] = (rL*7 + rR)>>6; tileRL[3] = (rL*13 + rR*3)>>7; tileRL[4] = (rL*3 + rR)>>5; tileRL[5] = (rL*11 + rR*5)>>7; tileRL[6] = (rL*10 + rR*6)>>7; tileRL[7] = (rL*9 + rR*7)>>7;
								tileGL[0] = gL>>3;	tileGL[1] = (gL*15 + gR)>>7; tileGL[2] = (gL*7 + gR)>>6; tileGL[3] = (gL*13 + gR*3)>>7; tileGL[4] = (gL*3 + gR)>>5; tileGL[5] = (gL*11 + gR*5)>>7; tileGL[6] = (gL*10 + gR*6)>>7; tileGL[7] = (gL*9 + gR*7)>>7;
								tileBL[0] = bL>>3;	tileBL[1] = (bL*15 + bR)>>7; tileBL[2] = (bL*7 + bR)>>6; tileBL[3] = (bL*13 + bR*3)>>7; tileBL[4] = (bL*3 + bR)>>5; tileBL[5] = (bL*11 + bR*5)>>7; tileBL[6] = (bL*10 + bR*6)>>7; tileBL[7] = (bL*9 + bR*7)>>7;

								tileRL[64] = (rL + rR)>>4;	tileRL[65] = (rL*7 + rR*9)>>7; tileRL[66] = (rL*6 + rR*10)>>7; tileRL[67] = (rL*5 + rR*11)>>7; tileRL[68] = (rL + rR*3)>>5; tileRL[69] = (rL*3 + rR*13)>>7; tileRL[70] = (rL + rR*7)>>6; tileRL[71] = (rL + rR*15)>>7;
								tileGL[64] = (gL + gR)>>4;	tileGL[65] = (gL*7 + gR*9)>>7; tileGL[66] = (gL*6 + gR*10)>>7; tileGL[67] = (gL*5 + gR*11)>>7; tileGL[68] = (gL + gR*3)>>5; tileGL[69] = (gL*3 + gR*13)>>7; tileGL[70] = (gL + gR*7)>>6; tileGL[71] = (gL + gR*15)>>7;
								tileBL[64] = (bL + bR)>>4;	tileBL[65] = (bL*7 + bR*9)>>7; tileBL[66] = (bL*6 + bR*10)>>7; tileBL[67] = (bL*5 + bR*11)>>7; tileBL[68] = (bL + bR*3)>>5; tileBL[69] = (bL*3 + bR*13)>>7; tileBL[70] = (bL + bR*7)>>6; tileBL[71] = (bL + bR*15)>>7;

								tileRL += 8;
								tileGL += 8;
								tileBL += 8;
							}
						}
						msk >>= 1;
						idxX2 += 4;
					}
					
					idxY2 += strideY8;
				}
			}
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY64;
	}
}

void DecompressGradient8x16	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* planeG, u8* planeB, u8 planeBit) {
	if (planeBit != 7) {
		// Not supported in this version.
#ifdef YAIK_DEVEL
		while (1) {
		}
#else
		return;
#endif
	}

	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u32 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	int idxX;
	int idxY		= 0;
	u32* tileBitmap	= (u32*)dataUncompressedTilebitmap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY16	= (pInstance->strideRGBMap * 4);
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	int strideTile4 = RDIV16(iw);

	// RGB Version Only.

	for (u16 y=0; y<ih; y+=64) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=64) {
			u32 tWord = *tileBitmap++;

			// Early skip ! 32 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				// If nothing if upper 16 pixel, early jump.
				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 32;
					idxY2  += strideY16<<1; // 16*2
				}

				for (; yt<y+64; yt+=16) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY16;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 8 pixel
						idxX2 += 8;		// 4 Tile of 2 colors
						xt    += 32;	// 32 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 8 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					int offUsedPixY = ((yt >> 3)*strideTile4);

					for (;xt<x+64; xt+=8) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							if ((hasRGB[DIV8(idxX2)] & v)==0) {
								hasRGB [DIV8(idxX2)]|= v;
								// Load LT
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+2;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RT
								pCornerRGB[6] = *dataUncompressedRGB++;
								pCornerRGB[7] = *dataUncompressedRGB++;
								pCornerRGB[8] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY16 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY16;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load LB
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx += 2;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RB
								pCornerRGB[6] = *dataUncompressedRGB++;
								pCornerRGB[7] = *dataUncompressedRGB++;
								pCornerRGB[8] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Grad %i,%i [%i,%i,%i][%i,%i,%i]-[%i,%i,%i][%i,%i,%i]\n",
								xt,yt,
								rgbCorner[0][ 0],rgbCorner[0][ 1],rgbCorner[0][ 2],
								rgbCorner[0][ 6],rgbCorner[0][ 7],rgbCorner[0][ 8],
								rgbCorner[1][ 0],rgbCorner[1][ 1],rgbCorner[1][ 2],
								rgbCorner[1][ 6],rgbCorner[1][ 7],rgbCorner[1][ 8]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6));
							
							u8 v8 = (xt & 8) ? 0xF0 : 0x0F;
							int idxMapTile4  = (xt >> 4) + offUsedPixY;
							pixelUsed[idxMapTile4]				|= v8;
							pixelUsed[idxMapTile4+strideTile4]	|= v8;

							for (int tileY=0; tileY <16; tileY+=8) {
								u8* tileRL = &planeR[idxTilePlane];
								u8* tileGL = &planeG[idxTilePlane];
								u8* tileBL = &planeB[idxTilePlane];
								for (int ty=tileY; ty < tileY+8; ty++) {
									int nW = 16-ty;
									// Vertical Blend
									int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
									int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
									int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
									int rR = (rgbCorner[0][6] * nW) + (rgbCorner[1][6] * ty);
									int gR = (rgbCorner[0][7] * nW) + (rgbCorner[1][7] * ty);
									int bR = (rgbCorner[0][8] * nW) + (rgbCorner[1][8] * ty);

									// Horizontal blending...
									// 16 Color Line Left Tile
									tileRL[0] = rL>>4;	tileRL[1] = (rL*7 + rR)>>7; tileRL[2] = (rL*3 + rR)>>6; tileRL[3] = (rL*5 + rR*3)>>7; tileRL[4] = (rL + rR)>>5; tileRL[5] = (rL*3 + rR*5)>>7; tileRL[6] = (rL + rR*3)>>6; tileRL[7] = (rL + rR*7)>>7;
									tileGL[0] = gL>>4;	tileGL[1] = (gL*7 + gR)>>7; tileGL[2] = (gL*3 + gR)>>6; tileGL[3] = (gL*5 + gR*3)>>7; tileGL[4] = (gL + gR)>>5; tileGL[5] = (gL*3 + gR*5)>>7; tileGL[6] = (gL + gR*3)>>6; tileGL[7] = (gL + gR*7)>>7;
									tileBL[0] = bL>>4;	tileBL[1] = (bL*7 + bR)>>7; tileBL[2] = (bL*3 + bR)>>6; tileBL[3] = (bL*5 + bR*3)>>7; tileBL[4] = (bL + bR)>>5; tileBL[5] = (bL*3 + bR*5)>>7; tileBL[6] = (bL + bR*3)>>6; tileBL[7] = (bL + bR*7)>>7;

									tileRL += 8;
									tileGL += 8;
									tileBL += 8;
								}
								idxTilePlane += strideTile;
							}
						}
						
						msk  >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY16;
				}
			}
			
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY64;
	}
}

void DecompressGradient8x8	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* planeG, u8* planeB, u8 planeBit) {
	if (planeBit != 7) {
		// Not supported in this version.
#ifdef YAIK_DEVEL
		while (1) {
		}
#else
		return;
#endif
	}

	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)dataUncompressedTilebitmap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY8	= (pInstance->strideRGBMap * 2);
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	int strideUsedY = RDIV16(iw);

	// RGB Version Only.

	for (u16 y=0; y<ih; y+=64) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=64) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				// If nothing if upper 32 tile, early jump.
				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 32;
					idxY2  += strideY8<<2; // 4*8
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 16;
					idxY2  += strideY8<<1; // 2*8
				}

				for (; yt<y+64; yt+=8) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY8;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 8 pixel
						idxX2 += 8;		// 4 Tile of 2 colors
						xt    += 32;	// 32 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 8 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					int offUsedPixY = ((yt >> 3)*strideUsedY);

					for (;xt<x+64; xt+=8) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							if ((hasRGB[DIV8(idxX2)] & v)==0) {
								hasRGB [DIV8(idxX2)]|= v;
								// Load LT
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+2;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RT
								pCornerRGB[6] = *dataUncompressedRGB++;
								pCornerRGB[7] = *dataUncompressedRGB++;
								pCornerRGB[8] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY8 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY8;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load LB
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx += 2;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RB
								pCornerRGB[6] = *dataUncompressedRGB++;
								pCornerRGB[7] = *dataUncompressedRGB++;
								pCornerRGB[8] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Grad %i,%i [%i,%i,%i][%i,%i,%i]-[%i,%i,%i][%i,%i,%i]\n",
								xt,yt,
								rgbCorner[0][ 0],rgbCorner[0][ 1],rgbCorner[0][ 2],
								rgbCorner[0][ 6],rgbCorner[0][ 7],rgbCorner[0][ 8],
								rgbCorner[1][ 0],rgbCorner[1][ 1],rgbCorner[1][ 2],
								rgbCorner[1][ 6],rgbCorner[1][ 7],rgbCorner[1][ 8]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//
							int idxUsedPix = (xt >> 4) + offUsedPixY;
							pixelUsed[idxUsedPix] |= (xt & 8) ? 0xF0 : 0x0F;

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6));

//							for (int tileY=0; tileY <16; tileY+=8) {
								u8* tileRL = &planeR[idxTilePlane];
								u8* tileGL = &planeG[idxTilePlane];
								u8* tileBL = &planeB[idxTilePlane];
								for (int ty=0; ty < 8; ty++) {
									int nW = 8-ty;
									// Vertical Blend
									int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
									int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
									int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
									int rR = (rgbCorner[0][6] * nW) + (rgbCorner[1][6] * ty);
									int gR = (rgbCorner[0][7] * nW) + (rgbCorner[1][7] * ty);
									int bR = (rgbCorner[0][8] * nW) + (rgbCorner[1][8] * ty);

									// Horizontal blending...
									// 16 Color Line Left Tile
									tileRL[0] = rL>>3;	tileRL[1] = (rL*7 + rR)>>6; tileRL[2] = (rL*3 + rR)>>5; tileRL[3] = (rL*5 + rR*3)>>6; tileRL[4] = (rL + rR)>>4; tileRL[5] = (rL*3 + rR*5)>>6; tileRL[6] = (rL + rR*3)>>5; tileRL[7] = (rL + rR*7)>>6;
									tileGL[0] = gL>>3;	tileGL[1] = (gL*7 + gR)>>6; tileGL[2] = (gL*3 + gR)>>5; tileGL[3] = (gL*5 + gR*3)>>6; tileGL[4] = (gL + gR)>>4; tileGL[5] = (gL*3 + gR*5)>>6; tileGL[6] = (gL + gR*3)>>5; tileGL[7] = (gL + gR*7)>>6;
									tileBL[0] = bL>>3;	tileBL[1] = (bL*7 + bR)>>6; tileBL[2] = (bL*3 + bR)>>5; tileBL[3] = (bL*5 + bR*3)>>6; tileBL[4] = (bL + bR)>>4; tileBL[5] = (bL*3 + bR*5)>>6; tileBL[6] = (bL + bR*3)>>5; tileBL[7] = (bL + bR*7)>>6;

									tileRL += 8;
									tileGL += 8;
									tileBL += 8;
								}
//								idxTilePlane += strideTile;
//							}
						}
						
						msk  >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY8;
				}
			}
			
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY64;
	}
}

void DecompressGradient8x4	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* planeG, u8* planeB, u8 planeBit) {
	if (planeBit != 7) {
		// Not supported in this version.
#ifdef YAIK_DEVEL
		while (1) {
		}
#else
		return;
#endif
	}

	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	int idxX;
	int idxY		= 0;
	u64* tileBitmap	= (u64*)dataUncompressedTilebitmap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY4	= (pInstance->strideRGBMap    );
	int strideY32	= (pInstance->strideRGBMap * 8); // 8 block of 4 pixel = skip 32.
	int strideTile	= (iw>>3)<<6;
	int strideUsedY = RDIV16(iw);

	// RGB Version Only.

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=64) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 16;
					idxY2  += strideY4<<2; // 4*8
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 8;
					idxY2  += strideY4<<1; // 2*8
				}

				for (; yt<y+32; yt+=4) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 8 pixel
						idxX2 += 8;		// 4 Tile of 2 colors
						xt    += 32;	// 32 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 8 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					int offUsedPixY = ((yt >> 3)*strideUsedY);

					for (;xt<x+64; xt+=8) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							if ((hasRGB[DIV8(idxX2)] & v)==0) {
								hasRGB [DIV8(idxX2)]|= v;
								// Load LT
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+2;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RT
								pCornerRGB[6] = *dataUncompressedRGB++;
								pCornerRGB[7] = *dataUncompressedRGB++;
								pCornerRGB[8] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY4 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY4;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load LB
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx += 2;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RB
								pCornerRGB[6] = *dataUncompressedRGB++;
								pCornerRGB[7] = *dataUncompressedRGB++;
								pCornerRGB[8] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Grad %i,%i [%i,%i,%i][%i,%i,%i]-[%i,%i,%i][%i,%i,%i]\n",
								xt,yt,
								rgbCorner[0][ 0],rgbCorner[0][ 1],rgbCorner[0][ 2],
								rgbCorner[0][ 6],rgbCorner[0][ 7],rgbCorner[0][ 8],
								rgbCorner[1][ 0],rgbCorner[1][ 1],rgbCorner[1][ 2],
								rgbCorner[1][ 6],rgbCorner[1][ 7],rgbCorner[1][ 8]
							);
#endif


							int idxUsedPix = (xt >> 4) + offUsedPixY;
							u8 v8 = 0x03<<((yt & 4)>>1);	// 0 or 2
							pixelUsed[idxUsedPix] |= v8<<((xt & 8)>>1); // Shift 0/2 for Y Axis, then 0/4 for X Axis.

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)) + ((yt & 4)<<3); // ((yt & 4)<<3) = Odd / Even Tile vertically (4x8)

//							for (int tileY=0; tileY <16; tileY+=8) {
								u8* tileRL = &planeR[idxTilePlane];
								u8* tileGL = &planeG[idxTilePlane];
								u8* tileBL = &planeB[idxTilePlane];
								for (int ty=0; ty < 4; ty++) {
									int nW = 4-ty;
									// Vertical Blend
									int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
									int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
									int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
									int rR = (rgbCorner[0][6] * nW) + (rgbCorner[1][6] * ty);
									int gR = (rgbCorner[0][7] * nW) + (rgbCorner[1][7] * ty);
									int bR = (rgbCorner[0][8] * nW) + (rgbCorner[1][8] * ty);

									// Horizontal blending...
									// 16 Color Line Left Tile
									tileRL[0] = rL>>2;	tileRL[1] = (rL*7 + rR)>>5; tileRL[2] = (rL*3 + rR)>>4; tileRL[3] = (rL*5 + rR*3)>>5; tileRL[4] = (rL + rR)>>3; tileRL[5] = (rL*3 + rR*5)>>5; tileRL[6] = (rL + rR*3)>>4; tileRL[7] = (rL + rR*7)>>5;
									tileGL[0] = gL>>2;	tileGL[1] = (gL*7 + gR)>>5; tileGL[2] = (gL*3 + gR)>>4; tileGL[3] = (gL*5 + gR*3)>>5; tileGL[4] = (gL + gR)>>3; tileGL[5] = (gL*3 + gR*5)>>5; tileGL[6] = (gL + gR*3)>>4; tileGL[7] = (gL + gR*7)>>5;
									tileBL[0] = bL>>2;	tileBL[1] = (bL*7 + bR)>>5; tileBL[2] = (bL*3 + bR)>>4; tileBL[3] = (bL*5 + bR*3)>>5; tileBL[4] = (bL + bR)>>3; tileBL[5] = (bL*3 + bR*5)>>5; tileBL[6] = (bL + bR*3)>>4; tileBL[7] = (bL + bR*7)>>5;

									tileRL += 8;
									tileGL += 8;
									tileBL += 8;
								}
//								idxTilePlane += strideTile;
//							}
						}
						
						msk  >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY4;
				}
			}
			
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY32;
	}
}

void DecompressGradient4x8	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* planeG, u8* planeB, u8 planeBit) {
	if (planeBit != 7) {
		// Not supported in this version.
#ifdef YAIK_DEVEL
		while (1) {
		}
#else
		return;
#endif
	}

	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	int idxX;
	int idxY		= 0;
	u64* tileBitmap	= (u64*)dataUncompressedTilebitmap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY8	= (pInstance->strideRGBMap * 2);
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	int strideUsedY = RDIV16(iw);

	// RGB Version Only.

	for (u16 y=0; y<ih; y+=64) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				// If nothing if upper 32 tile, early jump.
				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 32;
					idxY2  += strideY8<<2; // 4*8
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 16;
					idxY2  += strideY8<<1; // 2*8
				}

				for (; yt<y+64; yt+=8) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY8;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;		// 8 Pixel on screen.
					}

					int offUsedPixY = ((yt >> 3)*strideUsedY);

					for (;xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							if ((hasRGB[DIV8(idxX2)] & v)==0) {
								hasRGB [DIV8(idxX2)]|= v;
								// Load LT
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+1;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RT
								pCornerRGB[3] = *dataUncompressedRGB++;
								pCornerRGB[4] = *dataUncompressedRGB++;
								pCornerRGB[5] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY8 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY8;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load LB
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx++;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RB
								pCornerRGB[3] = *dataUncompressedRGB++;
								pCornerRGB[4] = *dataUncompressedRGB++;
								pCornerRGB[5] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Grad %i,%i [%i,%i,%i][%i,%i,%i]-[%i,%i,%i][%i,%i,%i]\n",
								xt,yt,
								rgbCorner[0][ 0],rgbCorner[0][ 1],rgbCorner[0][ 2],
								rgbCorner[0][ 3],rgbCorner[0][ 4],rgbCorner[0][ 5],
								rgbCorner[1][ 0],rgbCorner[1][ 1],rgbCorner[1][ 2],
								rgbCorner[1][ 3],rgbCorner[1][ 4],rgbCorner[1][ 5]
							);
#endif

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)) + (xt & 4);
							
							int idxUsedPix = (xt >> 4) + offUsedPixY;
							int v8 = (0x5)<<(((xt & 0x4)>>2)|((xt & 0x8)>>1));
							pixelUsed[idxUsedPix] |= v8; // Shift 0/1/2/3 for X Axis.

//							for (int tileY=0; tileY <16; tileY+=8) {
								u8* tileRL = &planeR[idxTilePlane];
								u8* tileGL = &planeG[idxTilePlane];
								u8* tileBL = &planeB[idxTilePlane];
								for (int ty=0; ty < 8; ty++) {
									int nW = 8-ty;
									// Vertical Blend
									int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
									int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
									int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
									int rR = (rgbCorner[0][3] * nW) + (rgbCorner[1][3] * ty);
									int gR = (rgbCorner[0][4] * nW) + (rgbCorner[1][4] * ty);
									int bR = (rgbCorner[0][5] * nW) + (rgbCorner[1][5] * ty);

									// Horizontal blending...
									// 16 Color Line Left Tile
									tileRL[0] = rL>>3;	tileRL[1] = (rL*3 + rR)>>5; tileRL[2] = (rL + rR)>>4; tileRL[3] = (rL + rR*3)>>5;
									tileGL[0] = gL>>3;	tileGL[1] = (gL*3 + gR)>>5; tileGL[2] = (gL + gR)>>4; tileGL[3] = (gL + gR*3)>>5;
									tileBL[0] = bL>>3;	tileBL[1] = (bL*3 + bR)>>5; tileBL[2] = (bL + bR)>>4; tileBL[3] = (bL + bR*3)>>5;

									tileRL += 8;
									tileGL += 8;
									tileBL += 8;
								}
//								idxTilePlane += strideTile;
//							}
						}
						
						msk  >>= 1;
						idxX2++;
					}
					
					idxY2 += strideY8;
				}
			}
			
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY64;
	}
}

//
// Internal Implementation for specialized versions.
//
void DecompressGradient4x4RG					(YAIK_Instance* pInstance,u8* dataUncompressedTilebitmap,u8* dataUncompressedRGB,u8* planeR,u8* planeG);												
void DecompressGradient4x4GB					(YAIK_Instance* pInstance,u8* dataUncompressedTilebitmap,u8* dataUncompressedRGB,u8* planeG,u8* planeB);												
void DecompressGradient4x4RB					(YAIK_Instance* pInstance,u8* dataUncompressedTilebitmap,u8* dataUncompressedRGB,u8* planeR,u8* planeB);
void DecompressGradient4x4R						(YAIK_Instance* pInstance,u8* dataUncompressedTilebitmap,u8* dataUncompressedRGB,u8* planeR);
void DecompressGradient4x4G						(YAIK_Instance* pInstance,u8* dataUncompressedTilebitmap,u8* dataUncompressedRGB,u8* planeG);
void DecompressGradient4x4B						(YAIK_Instance* pInstance,u8* dataUncompressedTilebitmap,u8* dataUncompressedRGB,u8* planeB);

void DecompressGradient4x4	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* planeG, u8* planeB, u8 planeBit) {
	switch (planeBit) {
	case 0: return; // Impossible (no data)
	case 1:
		DecompressGradient4x4R (pInstance,dataUncompressedTilebitmap,dataUncompressedRGB,planeR);			return;
	case 2:
		DecompressGradient4x4G (pInstance,dataUncompressedTilebitmap,dataUncompressedRGB,planeG);			return;
	case 3:
		DecompressGradient4x4RG(pInstance,dataUncompressedTilebitmap,dataUncompressedRGB,planeR,planeG);	return;
	case 4:
		DecompressGradient4x4B (pInstance,dataUncompressedTilebitmap,dataUncompressedRGB,planeB);			return;
	case 5:
		DecompressGradient4x4RB(pInstance,dataUncompressedTilebitmap,dataUncompressedRGB,planeR,planeB);	return;
	case 6:
		DecompressGradient4x4GB(pInstance,dataUncompressedTilebitmap,dataUncompressedRGB,planeG,planeB);	return;
	default:
		// 7,this use case...
		break;
	}

	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	int idxX;
	int idxY		= 0;
	u64* tileBitmap	= (u64*)dataUncompressedTilebitmap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY4	= pInstance->strideRGBMap;
	int strideY32	= pInstance->strideRGBMap * 8; // 8 block of 4 pixel = skip 32.
	int strideTile	= (iw>>3)<<6;
	int strideUsedY = RDIV16(iw);

	// RGB Version Only.

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				// If nothing if upper 32 tile, early jump.
				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 16;
					idxY2  += strideY4<<2; // 4*4
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 8;
					idxY2  += strideY4<<1; // 2*4
				}

				for (; yt<y+32; yt+=4) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;		// 8 Pixel on screen.
					}

					int til4 = 1<<((yt & 4)>>1); // 0 or 2
					int offUsedPixY = ((yt >> 3)*strideUsedY);

					for (;xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							if ((hasRGB[DIV8(idxX2)] & v)==0) {
								hasRGB [DIV8(idxX2)]|= v;
								// Load LT
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+1;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RT
								pCornerRGB[3] = *dataUncompressedRGB++;
								pCornerRGB[4] = *dataUncompressedRGB++;
								pCornerRGB[5] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY4 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY4;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load LB
								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx++;
							v = 1<<MOD8(tmpIdx);
							if ((hasRGB[DIV8(tmpIdx)] & v)==0) {
								hasRGB [DIV8(tmpIdx)]|= v;
								// Load RB
								pCornerRGB[3] = *dataUncompressedRGB++;
								pCornerRGB[4] = *dataUncompressedRGB++;
								pCornerRGB[5] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Grad %i,%i [%i,%i,%i][%i,%i,%i]-[%i,%i,%i][%i,%i,%i]\n",
								xt,yt,
								rgbCorner[0][ 0],rgbCorner[0][ 1],rgbCorner[0][ 2],
								rgbCorner[0][ 3],rgbCorner[0][ 4],rgbCorner[0][ 5],
								rgbCorner[1][ 0],rgbCorner[1][ 1],rgbCorner[1][ 2],
								rgbCorner[1][ 3],rgbCorner[1][ 4],rgbCorner[1][ 5]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)) + (xt & 4) + ((yt & 4)<<3); // 4x4 tile inside 8x8 tile => (xt & 4) + ((yt & 4)<<3)

							int idxUsedPix = (xt >> 4) + offUsedPixY;
							pixelUsed[idxUsedPix] |= til4<<(((xt & 0x4)>>2)|((xt & 0x8)>>1)); // Shift 0/2 for Y Axis, then 0/1 for X Axis.

//							for (int tileY=0; tileY <16; tileY+=8) {
								u8* tileRL = &planeR[idxTilePlane];
								u8* tileGL = &planeG[idxTilePlane];
								u8* tileBL = &planeB[idxTilePlane];
								for (int ty=0; ty < 4; ty++) {
									int nW = 4-ty;
									// Vertical Blend
									int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
									int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
									int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
									int rR = (rgbCorner[0][3] * nW) + (rgbCorner[1][3] * ty);
									int gR = (rgbCorner[0][4] * nW) + (rgbCorner[1][4] * ty);
									int bR = (rgbCorner[0][5] * nW) + (rgbCorner[1][5] * ty);

									// Horizontal blending...
									// 16 Color Line Left Tile
									tileRL[0] = rL>>2;	tileRL[1] = (rL*3 + rR)>>4; tileRL[2] = (rL + rR)>>3; tileRL[3] = (rL + rR*3)>>4;
									tileGL[0] = gL>>2;	tileGL[1] = (gL*3 + gR)>>4; tileGL[2] = (gL + gR)>>3; tileGL[3] = (gL + gR*3)>>4;
									tileBL[0] = bL>>2;	tileBL[1] = (bL*3 + bR)>>4; tileBL[2] = (bL + bR)>>3; tileBL[3] = (bL + bR*3)>>4;

									tileRL += 8;
									tileGL += 8;
									tileBL += 8;
								}
//								idxTilePlane += strideTile;
//							}
						}
						
						msk  >>= 1;
						idxX2++;
					}
					
					idxY2 += strideY4;
				}
			}
			
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}
}

void DecompressGradient4x4RG					(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeR
												,u8*			planeG) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasR		= pInstance->mapRGBMask;
	u8* hasG		= &pInstance->mapRGBMask[pInstance->sizeMapMask];

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)dataUncompressedTilebitmap;
	u8* pixelUsedR	= pInstance->tile4x4Mask;
	u8* pixelUsedG	= &pInstance->tile4x4Mask[pInstance->tile4x4MaskSize];
	int strideY4	= pInstance->strideRGBMap;
	int strideY32	= pInstance->strideRGBMap * 8; // 8 block of 4 pixel = skip 32.
	int strideTile	= (iw>>3)<<6;
	int strideUsedY = RDIV16(iw);

	// RG Version.

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				// If nothing if upper 32 tile, early jump.
				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 16;
					idxY2  += strideY4<<2; // 4*4
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 8;
					idxY2  += strideY4<<1; // 2*4
				}

				for (; yt<y+32; yt+=4) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;		// 8 Pixel on screen.
					}

					int til4 = 1<<((yt & 4)>>1); // 0 or 2
					int offUsedPixY = ((yt >> 3)*strideUsedY);
					
					for (;xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							DBG_CODED_DEF;
							if ((hasR[DIV8(idxX2)] & v)==0) {
								hasR [DIV8(idxX2)]|= v;
								DBG_CODED(0x0001);
								// Load LT
								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}
							if ((hasG[DIV8(idxX2)] & v)==0) {
								hasG [DIV8(idxX2)]|= v;
								DBG_CODED(0x0002);
								// Load LT
//								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+1;
							v = 1<<MOD8(tmpIdx);
							if ((hasR[DIV8(tmpIdx)] & v)==0) {
								hasR [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0010);
								// Load RT
								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}
							if ((hasG[DIV8(tmpIdx)] & v)==0) {
								hasG [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0020);
								// Load RT
//								pCornerRGB[3] = *dataUncompressedRGB++;
								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY4 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY4;
							v = 1<<MOD8(tmpIdx);
							if ((hasR[DIV8(tmpIdx)] & v)==0) {
								hasR [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0100);
								// Load LB
								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}
							if ((hasG[DIV8(tmpIdx)] & v)==0) {
								hasG [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0200);
								// Load LB
//								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx++;
							v = 1<<MOD8(tmpIdx);
							if ((hasR[DIV8(tmpIdx)] & v)==0) {
								hasR [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x1000);
								// Load RB
								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}
							if ((hasG[DIV8(tmpIdx)] & v)==0) {
								hasG [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x2000);
								// Load RB
//								pCornerRGB[3] = *dataUncompressedRGB++;
								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Coded %x Grad %i,%i [%i,%i][%i,%i]-[%i,%i][%i,%i]\n",
								coded,
								xt,yt,
								rgbCorner[0][ 0],rgbCorner[0][ 1],
								rgbCorner[0][ 3],rgbCorner[0][ 4],
								rgbCorner[1][ 0],rgbCorner[1][ 1],
								rgbCorner[1][ 3],rgbCorner[1][ 4]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)) + (xt & 4) + ((yt & 4)<<3); // 4x4 tile inside 8x8 tile => (xt & 4) + ((yt & 4)<<3)

							int val = til4<<(((xt & 0x4)>>2)|((xt & 0x8)>>1));
							int idxUsedPix = (xt >> 4) + offUsedPixY;
							pixelUsedR[idxUsedPix] |= val; // Shift 0/2 for Y Axis, then 0/1 for X Axis.
							pixelUsedG[idxUsedPix] |= val; // Shift 0/2 for Y Axis, then 0/1 for X Axis.

//							for (int tileY=0; tileY <16; tileY+=8) {
								u8* tileRL = &planeR[idxTilePlane];
								u8* tileGL = &planeG[idxTilePlane];
//								u8* tileBL = &planeB[idxTilePlane];
								for (int ty=0; ty < 4; ty++) {
									int nW = 4-ty;
									// Vertical Blend
									int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
									int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
//									int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
									int rR = (rgbCorner[0][3] * nW) + (rgbCorner[1][3] * ty);
									int gR = (rgbCorner[0][4] * nW) + (rgbCorner[1][4] * ty);
//									int bR = (rgbCorner[0][5] * nW) + (rgbCorner[1][5] * ty);

									// Horizontal blending...
									// 16 Color Line Left Tile
									tileRL[0] = rL>>2;	tileRL[1] = (rL*3 + rR)>>4; tileRL[2] = (rL + rR)>>3; tileRL[3] = (rL + rR*3)>>4;
									tileGL[0] = gL>>2;	tileGL[1] = (gL*3 + gR)>>4; tileGL[2] = (gL + gR)>>3; tileGL[3] = (gL + gR*3)>>4;
//									tileBL[0] = bL>>2;	tileBL[1] = (bL*3 + bR)>>4; tileBL[2] = (bL + bR)>>3; tileBL[3] = (bL + bR*3)>>4;

									tileRL += 8;
									tileGL += 8;
//									tileBL += 8;
								}
//								idxTilePlane += strideTile;
//							}
						}
						
						msk  >>= 1;
						idxX2++;
					}
					
					idxY2 += strideY4;
				}
			}
			
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}
}

void DecompressGradient4x4GB					(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeG
												,u8*			planeB) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasG		= &pInstance->mapRGBMask[pInstance->sizeMapMask   ];
	u8* hasB		= &pInstance->mapRGBMask[pInstance->sizeMapMask<<1];

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)dataUncompressedTilebitmap;
	u8* pixelUsedG	= &pInstance->tile4x4Mask[pInstance->tile4x4MaskSize];
	u8* pixelUsedB	= &pInstance->tile4x4Mask[pInstance->tile4x4MaskSize>>1];
	int strideY4	= pInstance->strideRGBMap;
	int strideY32	= pInstance->strideRGBMap * 8; // 8 block of 4 pixel = skip 32.
	int strideTile	= (iw>>3)<<6;
	int strideUsedY = RDIV16(iw);

	// RGB Version Only.

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				// If nothing if upper 32 tile, early jump.
				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 16;
					idxY2  += strideY4<<2; // 4*4
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 8;
					idxY2  += strideY4<<1; // 2*4
				}

				for (; yt<y+32; yt+=4) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;		// 8 Pixel on screen.
					}

					int til4 = 1<<((yt & 4)>>1); // 0 or 2
					int offUsedPixY = ((yt >> 3)*strideUsedY);

					for (;xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							DBG_CODED_DEF;
							if ((hasG[DIV8(idxX2)] & v)==0) {
								hasG [DIV8(idxX2)]|= v;
								DBG_CODED(0x0002);
								// Load LT
//								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}
							if ((hasB[DIV8(idxX2)] & v)==0) {
								hasB [DIV8(idxX2)]|= v;
								DBG_CODED(0x0004);
								// Load LT
//								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+1;
							v = 1<<MOD8(tmpIdx);
							if ((hasG[DIV8(tmpIdx)] & v)==0) {
								hasG [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0020);
								// Load RT
//								pCornerRGB[3] = *dataUncompressedRGB++;
								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}
							if ((hasB[DIV8(tmpIdx)] & v)==0) {
								hasB [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0040);
								// Load RT
//								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
								pCornerRGB[5] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY4 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY4;
							v = 1<<MOD8(tmpIdx);
							if ((hasG[DIV8(tmpIdx)] & v)==0) {
								hasG [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0200);
								// Load LB
//								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}
							if ((hasB[DIV8(tmpIdx)] & v)==0) {
								hasB [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0400);
								// Load LB
//								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx++;
							v = 1<<MOD8(tmpIdx);
							if ((hasG[DIV8(tmpIdx)] & v)==0) {
								hasG [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x2000);
								// Load RB
//								pCornerRGB[3] = *dataUncompressedRGB++;
								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}
							if ((hasB[DIV8(tmpIdx)] & v)==0) {
								hasB [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x4000);
								// Load RB
//								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
								pCornerRGB[5] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Coded %x Grad %i,%i [%i,%i][%i,%i]-[%i,%i][%i,%i]\n",
								coded,
								xt,yt,
								rgbCorner[0][ 1],rgbCorner[0][ 2],
								rgbCorner[0][ 4],rgbCorner[0][ 5],
								rgbCorner[1][ 1],rgbCorner[1][ 2],
								rgbCorner[1][ 4],rgbCorner[1][ 5]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)) + (xt & 4) + ((yt & 4)<<3); // 4x4 tile inside 8x8 tile => (xt & 4) + ((yt & 4)<<3)

							int val = til4<<(((xt & 0x4)>>2)|((xt & 0x8)>>1));
							int idxUsedPix = (xt >> 4) + offUsedPixY;
							pixelUsedG[idxUsedPix] |= val; // Shift 0/2 for Y Axis, then 0/1 for X Axis.
							pixelUsedB[idxUsedPix] |= val; // Shift 0/2 for Y Axis, then 0/1 for X Axis.

//							for (int tileY=0; tileY <16; tileY+=8) {
//								u8* tileRL = &planeR[idxTilePlane];
								u8* tileGL = &planeG[idxTilePlane];
								u8* tileBL = &planeB[idxTilePlane];
								for (int ty=0; ty < 4; ty++) {
									int nW = 4-ty;
									// Vertical Blend
//									int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
									int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
									int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
//									int rR = (rgbCorner[0][3] * nW) + (rgbCorner[1][3] * ty);
									int gR = (rgbCorner[0][4] * nW) + (rgbCorner[1][4] * ty);
									int bR = (rgbCorner[0][5] * nW) + (rgbCorner[1][5] * ty);

									// Horizontal blending...
									// 16 Color Line Left Tile
//									tileRL[0] = rL>>2;	tileRL[1] = (rL*3 + rR)>>4; tileRL[2] = (rL + rR)>>3; tileRL[3] = (rL + rR*3)>>4;
									tileGL[0] = gL>>2;	tileGL[1] = (gL*3 + gR)>>4; tileGL[2] = (gL + gR)>>3; tileGL[3] = (gL + gR*3)>>4;
									tileBL[0] = bL>>2;	tileBL[1] = (bL*3 + bR)>>4; tileBL[2] = (bL + bR)>>3; tileBL[3] = (bL + bR*3)>>4;

//									tileRL += 8;
									tileGL += 8;
									tileBL += 8;
								}
//								idxTilePlane += strideTile;
//							}
						}
						
						msk  >>= 1;
						idxX2++;
					}
					
					idxY2 += strideY4;
				}
			}
			
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}
}

void DecompressGradient4x4RB					(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeR
												,u8*			planeB) {

	//
	// NOTE : This function CAN BE OPTIMIZED because hasR and hasB are the same buffer content at this point as input (but not as OUTPUT !)
	//        But if ORDER of call change, that will be broken. So I keep the code more generic for now at the cost of performance for
	//		  this particular function.
	//

	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasR		= pInstance->mapRGBMask;
	u8* hasB		= &pInstance->mapRGBMask[pInstance->sizeMapMask<<1];

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	int idxX;
	int idxY		= 0;
	u64* tileBitmap	= (u64*)dataUncompressedTilebitmap;
	u8* pixelUsedR	= pInstance->tile4x4Mask;
	u8* pixelUsedB	= &pInstance->tile4x4Mask[pInstance->tile4x4MaskSize>>1];
	int strideY4	= pInstance->strideRGBMap;
	int strideY32	= pInstance->strideRGBMap * 8; // 8 block of 4 pixel = skip 32.
	int strideTile	= (iw>>3)<<6;
	int strideUsedY = RDIV16(iw);

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				// If nothing if upper 32 tile, early jump.
				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 16;
					idxY2  += strideY4<<2; // 4*4
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 8;
					idxY2  += strideY4<<1; // 2*4
				}

				for (; yt<y+32; yt+=4) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;		// 8 Pixel on screen.
					}

					int til4 = 1<<((yt & 4)>>1); // 0 or 2
					int offUsedPixY = ((yt >> 3)*strideUsedY);

					for (;xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							DBG_CODED_DEF;
							if ((hasR[DIV8(idxX2)] & v)==0) {
								hasR [DIV8(idxX2)]|= v;
								DBG_CODED(0x0001);
								// Load LT
								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}
							if ((hasB[DIV8(idxX2)] & v)==0) {
								hasB [DIV8(idxX2)]|= v;
								DBG_CODED(0x0004);
								// Load LT
//								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+1;
							v = 1<<MOD8(tmpIdx);
							if ((hasR[DIV8(tmpIdx)] & v)==0) {
								hasR [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0010);
								// Load RT
								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}
							if ((hasB[DIV8(tmpIdx)] & v)==0) {
								hasB [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0040);
								// Load RT
//								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
								pCornerRGB[5] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY4 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY4;
							v = 1<<MOD8(tmpIdx);
							if ((hasR[DIV8(tmpIdx)] & v)==0) {
								hasR [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0100);
								// Load LB
								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}
							if ((hasB[DIV8(tmpIdx)] & v)==0) {
								hasB [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0400);
								// Load LB
//								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx++;
							v = 1<<MOD8(tmpIdx);
							if ((hasR[DIV8(tmpIdx)] & v)==0) {
								hasR [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x1000);
								// Load RB
								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}
							if ((hasB[DIV8(tmpIdx)] & v)==0) {
								hasB [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x4000);
								// Load RB
//								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
								pCornerRGB[5] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Coded %x Grad %i,%i [%i,%i][%i,%i]-[%i,%i][%i,%i]\n",
								coded,
								xt,yt,
								rgbCorner[0][ 0],rgbCorner[0][ 2],
								rgbCorner[0][ 3],rgbCorner[0][ 5],
								rgbCorner[1][ 0],rgbCorner[1][ 2],
								rgbCorner[1][ 3],rgbCorner[1][ 5]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)) + (xt & 4) + ((yt & 4)<<3); // 4x4 tile inside 8x8 tile => (xt & 4) + ((yt & 4)<<3)
							int val = til4<<(((xt & 0x4)>>2)|((xt & 0x8)>>1));
							int idxUsedPix = (xt >> 4) + offUsedPixY;
							pixelUsedR[idxUsedPix] |= val; // Shift 0/2 for Y Axis, then 0/1 for X Axis.
							pixelUsedB[idxUsedPix] |= val; // Shift 0/2 for Y Axis, then 0/1 for X Axis.

							u8* tileRL = &planeR[idxTilePlane];
//							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];
							for (int ty=0; ty < 4; ty++) {
								int nW = 4-ty;
								// Vertical Blend
								int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
//								int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
								int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
								int rR = (rgbCorner[0][3] * nW) + (rgbCorner[1][3] * ty);
//								int gR = (rgbCorner[0][4] * nW) + (rgbCorner[1][4] * ty);
								int bR = (rgbCorner[0][5] * nW) + (rgbCorner[1][5] * ty);

								// Horizontal blending...
								// 16 Color Line Left Tile
								tileRL[0] = rL>>2;	tileRL[1] = (rL*3 + rR)>>4; tileRL[2] = (rL + rR)>>3; tileRL[3] = (rL + rR*3)>>4;
//								tileGL[0] = gL>>2;	tileGL[1] = (gL*3 + gR)>>4; tileGL[2] = (gL + gR)>>3; tileGL[3] = (gL + gR*3)>>4;
								tileBL[0] = bL>>2;	tileBL[1] = (bL*3 + bR)>>4; tileBL[2] = (bL + bR)>>3; tileBL[3] = (bL + bR*3)>>4;

								tileRL += 8;
//								tileGL += 8;
								tileBL += 8;
							}
						}
						
						msk  >>= 1;
						idxX2++;
					}
					
					idxY2 += strideY4;
				}
			}
			
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}
}

void DecompressGradient4x4R						(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeR) {

	//
	// NOTE : This function CAN BE OPTIMIZED because hasR and hasB are the same buffer content at this point as input (but not as OUTPUT !)
	//        But if ORDER of call change, that will be broken. So I keep the code more generic for now at the cost of performance for
	//		  this particular function.
	//

	u16 iw		= pInstance->width;
	u16 ih		= pInstance->height;
	u64 tileWord= 0;

	u8* mapRGB = pInstance->mapRGB;
	u8* hasR   = pInstance->mapRGBMask;
	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	u64* tileBitmap = (u64*)dataUncompressedTilebitmap;

	int idxY = 0;
	int idxX;
	int strideY4  = pInstance->strideRGBMap;
	int strideY32 = pInstance->strideRGBMap * 8; // 8 block of 4 pixel = skip 32.
	int strideTile = (iw>>3)<<6;

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				// If nothing if upper 32 tile, early jump.
				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 16;
					idxY2  += strideY4<<2; // 4*4
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 8;
					idxY2  += strideY4<<1; // 2*4
				}

				for (; yt<y+32; yt+=4) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;		// 8 Pixel on screen.
					}

					int til4 = 1<<((yt & 4)>>1); // 0 or 2

					for (;xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							DBG_CODED_DEF;
							if ((hasR[DIV8(idxX2)] & v)==0) {
								hasR [DIV8(idxX2)]|= v;
								DBG_CODED(0x0001);
								// Load LT
								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+1;
							v = 1<<MOD8(tmpIdx);
							if ((hasR[DIV8(tmpIdx)] & v)==0) {
								hasR [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0010);
								// Load RT
								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY4 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY4;
							v = 1<<MOD8(tmpIdx);
							if ((hasR[DIV8(tmpIdx)] & v)==0) {
								hasR [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0100);
								// Load LB
								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx++;
							v = 1<<MOD8(tmpIdx);
							if ((hasR[DIV8(tmpIdx)] & v)==0) {
								hasR [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x1000);
								// Load RB
								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Coded %x Grad %i,%i [%i][%i]-[%i][%i]\n",
								coded,
								xt,yt,
								rgbCorner[0][ 0],
								rgbCorner[0][ 3],
								rgbCorner[1][ 0],
								rgbCorner[1][ 3]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)) + (xt & 4) + ((yt & 4)<<3); // 4x4 tile inside 8x8 tile => (xt & 4) + ((yt & 4)<<3)

//							pixelUsedR[idxTilePlane>>1] |= til4<<(((xt & 0x4)>>2)|((xt & 0x8)>>1)); // Shift 0/2 for Y Axis, then 0/1 for X Axis.

							u8* tileRL = &planeR[idxTilePlane];
//							u8* tileGL = &planeG[idxTilePlane];
//							u8* tileBL = &planeB[idxTilePlane];
							for (int ty=0; ty < 4; ty++) {
								int nW = 4-ty;
								// Vertical Blend
								int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
//								int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
//								int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
								int rR = (rgbCorner[0][3] * nW) + (rgbCorner[1][3] * ty);
//								int gR = (rgbCorner[0][4] * nW) + (rgbCorner[1][4] * ty);
//								int bR = (rgbCorner[0][5] * nW) + (rgbCorner[1][5] * ty);

								// Horizontal blending...
								// 16 Color Line Left Tile
								tileRL[0] = rL>>2;	tileRL[1] = (rL*3 + rR)>>4; tileRL[2] = (rL + rR)>>3; tileRL[3] = (rL + rR*3)>>4;
//								tileGL[0] = gL>>2;	tileGL[1] = (gL*3 + gR)>>4; tileGL[2] = (gL + gR)>>3; tileGL[3] = (gL + gR*3)>>4;
//								tileBL[0] = bL>>2;	tileBL[1] = (bL*3 + bR)>>4; tileBL[2] = (bL + bR)>>3; tileBL[3] = (bL + bR*3)>>4;

								tileRL += 8;
//								tileGL += 8;
//								tileBL += 8;
							}
						}
						
						msk  >>= 1;
						idxX2++;
					}
					
					idxY2 += strideY4;
				}
			}
			
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}
}

void DecompressGradient4x4G						(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeG) {
	u16 iw		= pInstance->width;
	u16 ih		= pInstance->height;
	u64 tileWord= 0;

	u8* mapRGB = pInstance->mapRGB;
	u8* hasG   = &pInstance->mapRGBMask[pInstance->sizeMapMask   ];

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	u64* tileBitmap = (u64*)dataUncompressedTilebitmap;

	// RGB Version Only.

	int idxY = 0;
	int idxX;
	int strideY4  = pInstance->strideRGBMap;
	int strideY32 = pInstance->strideRGBMap * 8; // 8 block of 4 pixel = skip 32.
	int strideTile = (iw>>3)<<6;
	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				// If nothing if upper 32 tile, early jump.
				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 16;
					idxY2  += strideY4<<2; // 4*4
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 8;
					idxY2  += strideY4<<1; // 2*4
				}

				for (; yt<y+32; yt+=4) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;		// 8 Pixel on screen.
					}

//					int til4 = 1<<((yt & 4)>>1); // 0 or 2

					for (;xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							DBG_CODED_DEF;
							if ((hasG[DIV8(idxX2)] & v)==0) {
								hasG [DIV8(idxX2)]|= v;
								DBG_CODED(0x0002);
								// Load LT
//								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+1;
							v = 1<<MOD8(tmpIdx);
							if ((hasG[DIV8(tmpIdx)] & v)==0) {
								hasG [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0020);
								// Load RT
//								pCornerRGB[3] = *dataUncompressedRGB++;
								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY4 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY4;
							v = 1<<MOD8(tmpIdx);
							if ((hasG[DIV8(tmpIdx)] & v)==0) {
								hasG [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0200);
								// Load LB
//								pCornerRGB[0] = *dataUncompressedRGB++;
								pCornerRGB[1] = *dataUncompressedRGB++;
//								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx++;
							v = 1<<MOD8(tmpIdx);
							if ((hasG[DIV8(tmpIdx)] & v)==0) {
								hasG [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x2000);
								// Load RB
//								pCornerRGB[3] = *dataUncompressedRGB++;
								pCornerRGB[4] = *dataUncompressedRGB++;
//								pCornerRGB[5] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Coded %x Grad %i,%i [%i][%i]-[%i][%i]\n",
								coded,
								xt,yt,
								rgbCorner[0][ 1],
								rgbCorner[0][ 4],
								rgbCorner[1][ 1],
								rgbCorner[1][ 4]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)) + (xt & 4) + ((yt & 4)<<3); // 4x4 tile inside 8x8 tile => (xt & 4) + ((yt & 4)<<3)

//							pixelUsedG[idxTilePlane>>1] |= til4<<(((xt & 0x4)>>2)|((xt & 0x8)>>1)); // Shift 0/2 for Y Axis, then 0/1 for X Axis.

//							for (int tileY=0; tileY <16; tileY+=8) {
//								u8* tileRL = &planeR[idxTilePlane];
								u8* tileGL = &planeG[idxTilePlane];
//								u8* tileBL = &planeB[idxTilePlane];
								for (int ty=0; ty < 4; ty++) {
									int nW = 4-ty;
									// Vertical Blend
//									int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
									int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
//									int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
//									int rR = (rgbCorner[0][3] * nW) + (rgbCorner[1][3] * ty);
									int gR = (rgbCorner[0][4] * nW) + (rgbCorner[1][4] * ty);
//									int bR = (rgbCorner[0][5] * nW) + (rgbCorner[1][5] * ty);

									// Horizontal blending...
									// 16 Color Line Left Tile
//									tileRL[0] = rL>>2;	tileRL[1] = (rL*3 + rR)>>4; tileRL[2] = (rL + rR)>>3; tileRL[3] = (rL + rR*3)>>4;
									tileGL[0] = gL>>2;	tileGL[1] = (gL*3 + gR)>>4; tileGL[2] = (gL + gR)>>3; tileGL[3] = (gL + gR*3)>>4;
//									tileBL[0] = bL>>2;	tileBL[1] = (bL*3 + bR)>>4; tileBL[2] = (bL + bR)>>3; tileBL[3] = (bL + bR*3)>>4;

//									tileRL += 8;
									tileGL += 8;
//									tileBL += 8;
								}
//								idxTilePlane += strideTile;
//							}
						}
						
						msk  >>= 1;
						idxX2++;
					}
					
					idxY2 += strideY4;
				}
			}
			
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}
}

void DecompressGradient4x4B						(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeB) {
	u16 iw		= pInstance->width;
	u16 ih		= pInstance->height;
	u64 tileWord= 0;

	u8* mapRGB = pInstance->mapRGB;
	u8* hasB   = &pInstance->mapRGBMask[pInstance->sizeMapMask<<1];
	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	u64* tileBitmap = (u64*)dataUncompressedTilebitmap;

	// RGB Version Only.

	int idxY = 0;
	int idxX;
	int strideY4  = pInstance->strideRGBMap;
	int strideY32 = pInstance->strideRGBMap * 8; // 8 block of 4 pixel = skip 32.
	int strideTile = (iw>>3)<<6;
	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				int idxY2 = idxX;
				u16 yt    = y;

				// If nothing if upper 32 tile, early jump.
				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 16;
					idxY2  += strideY4<<2; // 4*4
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 8;
					idxY2  += strideY4<<1; // 2*4
				}

				for (; yt<y+32; yt+=4) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>=8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					u16 xt=x;
					int idxX2 = idxY2;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;		// 8 Pixel on screen.
					}

//					int til4 = 1<<((yt & 4)>>1); // 0 or 2

					for (;xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* pCornerRGB = &mapRGB[idxX2*3];
							// Extract 4 RGB Edges.
							u8* rgbCorner[2];

							rgbCorner[0] = pCornerRGB;

							int v = 1<<MOD8(idxX2);
							DBG_CODED_DEF;
							if ((hasB[DIV8(idxX2)] & v)==0) {
								hasB [DIV8(idxX2)]|= v;
								DBG_CODED(0x0004);
								// Load LT
//								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							int tmpIdx = idxX2+1;
							v = 1<<MOD8(tmpIdx);
							if ((hasB[DIV8(tmpIdx)] & v)==0) {
								hasB [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0040);
								// Load RT
//								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
								pCornerRGB[5] = *dataUncompressedRGB++;
							}

							pCornerRGB += strideY4 * 3;
							rgbCorner[1] = pCornerRGB;

							tmpIdx = idxX2 + strideY4;
							v = 1<<MOD8(tmpIdx);
							if ((hasB[DIV8(tmpIdx)] & v)==0) {
								hasB [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x0400);
								// Load LB
//								pCornerRGB[0] = *dataUncompressedRGB++;
//								pCornerRGB[1] = *dataUncompressedRGB++;
								pCornerRGB[2] = *dataUncompressedRGB++;
							}

							tmpIdx++;
							v = 1<<MOD8(tmpIdx);
							if ((hasB[DIV8(tmpIdx)] & v)==0) {
								hasB [DIV8(tmpIdx)]|= v;
								DBG_CODED(0x4000);
								// Load RB
//								pCornerRGB[3] = *dataUncompressedRGB++;
//								pCornerRGB[4] = *dataUncompressedRGB++;
								pCornerRGB[5] = *dataUncompressedRGB++;
							}

#ifdef DEBUG_LOG
							printf("Coded %x Grad %i,%i [%i][%i]-[%i][%i]\n",
								coded,
								xt,yt,
								rgbCorner[0][ 2],
								rgbCorner[0][ 5],
								rgbCorner[1][ 2],
								rgbCorner[1][ 5]
							);
#endif

							//
							// Decompress 2x 8x8 Left and Right tile in loop.
							//

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)) + (xt & 4) + ((yt & 4)<<3); // 4x4 tile inside 8x8 tile => (xt & 4) + ((yt & 4)<<3)

							// Normally not necessary : When we reach 1 Plane grad.
							// pixelUsedB[idxTilePlane>>1] |= til4<<(((xt & 0x4)>>2)|((xt & 0x8)>>1)); // Shift 0/2 for Y Axis, then 0/1 for X Axis.

//							u8* tileRL = &planeR[idxTilePlane];
//							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];
							for (int ty=0; ty < 4; ty++) {
								int nW = 4-ty;
								// Vertical Blend
//								int rL = (rgbCorner[0][0] * nW) + (rgbCorner[1][0] * ty);
//								int gL = (rgbCorner[0][1] * nW) + (rgbCorner[1][1] * ty);
								int bL = (rgbCorner[0][2] * nW) + (rgbCorner[1][2] * ty);
//								int rR = (rgbCorner[0][3] * nW) + (rgbCorner[1][3] * ty);
//								int gR = (rgbCorner[0][4] * nW) + (rgbCorner[1][4] * ty);
								int bR = (rgbCorner[0][5] * nW) + (rgbCorner[1][5] * ty);

								// Horizontal blending...
								// 16 Color Line Left Tile
//								tileRL[0] = rL>>2;	tileRL[1] = (rL*3 + rR)>>4; tileRL[2] = (rL + rR)>>3; tileRL[3] = (rL + rR*3)>>4;
//								tileGL[0] = gL>>2;	tileGL[1] = (gL*3 + gR)>>4; tileGL[2] = (gL + gR)>>3; tileGL[3] = (gL + gR*3)>>4;
								tileBL[0] = bL>>2;	tileBL[1] = (bL*3 + bR)>>4; tileBL[2] = (bL + bR)>>3; tileBL[3] = (bL + bR*3)>>4;

//								tileRL += 8;
//								tileGL += 8;
								tileBL += 8;
							}
						}
						
						msk  >>= 1;
						idxX2++;
					}
					
					idxY2 += strideY4;
				}
			}
			
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}
}
