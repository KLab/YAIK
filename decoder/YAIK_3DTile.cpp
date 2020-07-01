#include "YAIK_functions.h"

// #define DEBUG_TILE3D
// #define DEBUG_TILE1D

#ifdef DEBUG_TILE1D
	#include <stdio.h>
#endif

#ifdef DEBUG_TILE3D
	#include <stdio.h>
	#define DEBUG_PIXELCOUNTER		int dbg_pixelCount=0;
	#define DEBUG_PIX_ADD16			dbg_pixelCount+=16;
	#define DEBUG_PIX_ADD32			dbg_pixelCount+=32;
	#define DEF_PMC					int pmC = 0;
#else
	#define DEBUG_PIXELCOUNTER		;
	#define DEBUG_PIX_ADD16			;
	#define DEBUG_PIX_ADD32			;
	#define DEF_PMC					;
#endif

void deltaT3(u8* RGB, int* diff) {
	//
	// First round RGB 0,1,2 only.
	//
	/*
	// REDUCE 64 Range.
	int R = RGB[0]; R++;
	if (R>255) { R = 255; }
	R >>= 2;

	int G = RGB[1]; G++;
	if (G>255) { G = 255; }
	G >>= 2;

	int B = RGB[2]; B++;
	if (B>255) { B = 255; }
	B >>= 2;

	RGB[0] = (R<<2) | (R>>4);
	RGB[1] = (G<<2) | (G>>4);
	RGB[2] = (B<<2) | (B>>4);
	*/

	// REDUCE 128 RANGE.
	int R = RGB[0]; R++;
	if (R>255) { R = 255; }
	R >>= 1;

	int G = RGB[1]; G++;
	if (G>255) { G = 255; }
	G >>= 1;

	int B = RGB[2]; B++;
	if (B>255) { B = 255; }
	B >>= 1;

	RGB[0] = (R<<1) | (R>>6);
	RGB[1] = (G<<1) | (G>>6);
	RGB[2] = (B<<1) | (B>>6);

	// Round up 3,4,5
	diff[0] = RGB[3] - RGB[0]; diff[1] = RGB[4] - RGB[1]; diff[2] = RGB[5] - RGB[2];
}

void deltaT2(u8* RGB, int* diff) {
}


void Decompress1D  (YAIK_Instance* pInstance, u8** pTypeDecomp, u8** pPixStreamDecomp, u8 planeID, Header1D* pHeader) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

//	int idxX;
//	int idxY		= 0;
	u8* pixelUsed;
	u8* plane;
	u8* indexStream = *pPixStreamDecomp;
	u8* tileStream  = *pTypeDecomp;

	switch (planeID) {
	case 0:
		pixelUsed	= pInstance->tile4x4Mask;
		plane		= pInstance->planeR;
		break;
	case 1:
		pixelUsed	= &pInstance->tile4x4Mask[pInstance->tile4x4MaskSize];
		plane		= pInstance->planeG;
		break;
	case 2:
		pixelUsed	= &pInstance->tile4x4Mask[pInstance->tile4x4MaskSize<<1];
		plane		= pInstance->planeB;
		break;
	default:
		// Unsupported planeID.
		return;
	}

	int strideY8	= (pInstance->strideRGBMap * 2);  // 2 Block of 4 pixel = skip 8.
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	// RGB Version Only.
#ifdef DEBUG_TILE1D
	printf("--- Decoder 1D 8x8\n");
#endif

	int invRangeCompr = (1<<24) / pHeader->compressionRange; // 0.24 / 0.4 -> 0.20
	int tileCount = 0;

	for (u16 y=0; y<ih; y+=8) {
//		idxX = idxY;
		for (u16 x=0; x<iw; x+=8) {
			u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16
			int PixUsedIdx = (x>>4) + ((y>>3)*stride4x4Map);
			u8 patternQuad = pixelUsed[PixUsedIdx];

			patternQuad >>= ((x & 8) ? 4 : 0); // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
			patternQuad  &= 0xF;

			if (patternQuad != 0xF) {
				// Tile  Ready.
				u8 color0		= *tileStream++;
				u8 base         = *tileStream++;
				u8 delta        = *tileStream++;

				int delta2      = ((delta * invRangeCompr)>>8)+1;

				// LUT For Index Ready.
				u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16
				int idxTilePlane = (((y>>3)*strideTile) + ((x>>3)<<6));
				u8* tile = &plane[idxTilePlane];

				u8  L;
				int n;
				int cnt = 1;

				DEF_PMC
				DEBUG_PIXELCOUNTER

				tileCount++;
				u8 VP;
			block4Y:
				switch (patternQuad & 3) {
				case 3:
					tile += 32;
					break;
				case 2: {
					// -----------------------------------------------
					// Left 4x4 Tile
					// -----------------------------------------------
					n = 4;
					do {
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile   = VP;

						// Next Line
						tile   += 5;
					} while (--n); // Compare to zero, lower loop cost.
					DEBUG_PIX_ADD16;

				} break;
				case 1: {
					// -----------------------------------------------
					// Right 4x4 Tile
					// -----------------------------------------------
					tile += 4;
								
					n = 4;
					do {
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile   = VP;

						// Next Line
						tile   += 5;
					} while (--n); // Compare to zero, lower loop cost.
					DEBUG_PIX_ADD16;

					// Return at X = 0 coordinate inside tile
					tile -= 4;
				} break;
				case 0: {
					// -----------------------------------------------
					// Both 4x4 Tile
					// -----------------------------------------------

					n = 4;
					do {
						// Left Block
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;

						// Right Block
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;
						L = *indexStream++;
						VP= L ? (base + ((((L-1) * delta2))>>16)): color0;
						*tile++ = VP;

						// Next Line
					} while (--n); // Compare to zero, lower loop cost.
					DEBUG_PIX_ADD32;
				} break;
				DEFAULT_UNREACHABLE
				}

				if (cnt) {
					cnt = 0;
					patternQuad >>= 2;
					goto block4Y;
				}

#ifdef DEBUG_TILE1D
				int countPix;
				switch (patternQuad) {
				case  0: countPix = 4*16; break; // 0000
				case  1: countPix = 3*16; break; // 0001
				case  2: countPix = 3*16; break; // 0010
				case  3: countPix = 2*16; break; // 0011
				case  4: countPix = 3*16; break; // 0100
				case  5: countPix = 2*16; break; // 0101
				case  6: countPix = 2*16; break; // 0110
				case  7: countPix = 1*16; break; // 0111
				case  8: countPix = 3*16; break; // 1000
				case  9: countPix = 2*16; break; // 1001
				case 10: countPix = 2*16; break;  // 1010
				case 11: countPix = 1*16; break;  // 1011
				case 12: countPix = 2*16; break;  // 1100
				case 13: countPix = 1*16; break;  // 1101
				case 14: countPix = 1*16; break;  // 1110
				case 15: countPix = 0;    break; 
				}

				printf("Tile %i,%i => Data : %i (%x) PixCount, %i color0, %i Base, %i delta\n",x,y,countPix,patternQuad,color0,base,delta);
#endif


			}
		}
	}

	*pPixStreamDecomp = indexStream;
	*pTypeDecomp      = tileStream;
}

#define		COMPUTE_RGBDELTA	diff[0] = RGB[3] - RGB[0]; diff[1] = RGB[4] - RGB[1]; diff[2] = RGB[5] - RGB[2];
#define     COMPUTE_RGBDELTA2D  diff[0] = RGB[2] - RGB[0]; diff[1] = RGB[3] - RGB[1];
//#define		COMPUTE_RGBDELTA	deltaT3(RGB,diff);
//#define     COMPUTE_RGBDELTA2D  deltaT2(RGB,diff);

void Tile3D_16x8(YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u32 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u32* tileBitmap = (u32*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY8	= (pInstance->strideRGBMap * 2);
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

	u8*		LUT3Bit3D = TBLLUT[0];
	u8*		LUT4Bit3D = TBLLUT[1];
	u8*		LUT5Bit3D = TBLLUT[2];
	u8*		LUT6Bit3D = TBLLUT[3];

	u8*		planeR = pInstance->planeR;
	u8*		planeG = pInstance->planeG;
	u8*		planeB = pInstance->planeB;

#ifdef DEBUG_TILE3D
	printf("--- Decoder 16x8\n");
#endif

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

					for (; xt<x+64; xt+=16) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 6;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*3;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];

#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i,%i]->[%i,%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3],(int)RGB[4],(int)RGB[5]);
#endif

							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 6 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */

							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16
							int idxPixUsed = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[idxPixUsed];

							int diff[3];

							COMPUTE_RGBDELTA;

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6));

//							for (int tileY=0; tileY <16; tileY+=8) {
							u8* tileRL = &planeR[idxTilePlane];
							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							int cnt = 4;
							
							DEF_PMC
							DEBUG_PIXELCOUNTER

							block4Y:
								switch (patternQuad & 3) {
								case 3:
									tileRL += 32; tileGL += 32; tileBL += 32;
									patternQuad >>= 2;
									cnt--;
									
									if (cnt) {
										goto block4Y;
									} else {
										goto exitBlock4Y;
									}
								case 2: {
									// -----------------------------------------------
									// Left 4x4 Tile
									// -----------------------------------------------
									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

										// Next Line
										tileRL   += 5;
										tileGL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16
								} break;
								case 1: {
									// -----------------------------------------------
									// Right 4x4 Tile
									// -----------------------------------------------
									tileRL += 4;
									tileGL += 4;
									tileBL += 4;

									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

										// Next Line
										tileRL   += 5;
										tileGL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.

									// Return at X = 0 coordinate inside tile
									tileRL -= 4;
									tileGL -= 4;
									tileBL -= 4;
									DEBUG_PIX_ADD16
								} break;
								case 0: {
									// -----------------------------------------------
									// Both 4x4 Tile
									// -----------------------------------------------

									n = 4;
									do {
										// Left Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										// Right Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

										// Next Line
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD32;
								} break;
								DEFAULT_UNREACHABLE
								}

								cnt--;
								patternQuad >>= 2;
								if (cnt) {
									goto block4Y;
								}

							exitBlock4Y:
								pixelUsed[idxPixUsed] = 0xFF;
//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
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

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile3D_8x16(YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u32 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u32* tileBitmap = (u32*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY16	= (pInstance->strideRGBMap * 4);  // 4 Block of 4 pixel = skip 16.
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

	u8*		LUT3Bit3D = TBLLUT[0];
	u8*		LUT4Bit3D = TBLLUT[1];
	u8*		LUT5Bit3D = TBLLUT[2];
	u8*		LUT6Bit3D = TBLLUT[3];

	u8*		planeR = pInstance->planeR;
	u8*		planeG = pInstance->planeG;
	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 8x16\n");
#endif

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
					idxY2  += strideY16<<1; // 8*4
				}

				for (; yt<y+64; yt+=16) {
					if (yt >= ih) { break; } // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next horizontal...

					int msk = tWord & 0xFF;
					tWord >>= 8;

					int idxX2 = idxY2;
					u16 xt=x;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY16;
							continue; 
						}
					} else {
						if ((msk & 3) == 0) {
							msk  >>= 2;
							idxX2 += 4;
							xt    += 16;
						}
					}

					for (; xt<x+64; xt+=8) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 6;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*3;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];

#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i,%i]->[%i,%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3],(int)RGB[4],(int)RGB[5]);
#endif

							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 6 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								pixelUsed[PixUsedIdx] |= 0xF0;
							} else {
								pixelUsed[PixUsedIdx] |= 0x0F;
							}

							int diff[3];
							COMPUTE_RGBDELTA;

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6));

//							for (int tileY=0; tileY <16; tileY+=8) {
							u8* tileRL = &planeR[idxTilePlane];
							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							int cnt = 4;

							DEF_PMC
							DEBUG_PIXELCOUNTER

							block4Y:
								switch (patternQuad & 3) {
								case 3:
									tileRL += 32; tileGL += 32; tileBL += 32;
									break;
								case 2: {
									// -----------------------------------------------
									// Left 4x4 Tile
									// -----------------------------------------------
									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

										// Next Line
										tileRL   += 5;
										tileGL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16;

								} break;
								case 1: {
									// -----------------------------------------------
									// Right 4x4 Tile
									// -----------------------------------------------
									tileRL += 4;
									tileGL += 4;
									tileBL += 4;

									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

										// Next Line
										tileRL   += 5;
										tileGL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16;

									// Return at X = 0 coordinate inside tile
									tileRL -= 4;
									tileGL -= 4;
									tileBL -= 4;
								} break;
								case 0: {
									// -----------------------------------------------
									// Both 4x4 Tile
									// -----------------------------------------------

									n = 4;
									do {
										// Left Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										// Right Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										// Next Line
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD32;
								} break;
								DEFAULT_UNREACHABLE
								}

								switch (--cnt) {
								case 1: // Same as 3
								case 3: 
									patternQuad >>= 2;
									goto block4Y;
								case 2:
									{
										int stride = strideTile - 64;
										tileRL += stride; tileGL += stride; tileBL += stride;
									}
									PixUsedIdx+=stride4x4Map;
									patternQuad = pixelUsed[PixUsedIdx];
									if (xt & 8) {
										patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
										pixelUsed[PixUsedIdx] |= 0xF0;
									} else {
										pixelUsed[PixUsedIdx] |= 0x0F;
									}
									goto block4Y;
								case 0:
									break;
								DEFAULT_UNREACHABLE
								}

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY16;
				}
			}
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY64;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile3D_8x8 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY8	= (pInstance->strideRGBMap * 2);  // 2 Block of 4 pixel = skip 8.
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

	u8*		LUT3Bit3D = TBLLUT[0];
	u8*		LUT4Bit3D = TBLLUT[1];
	u8*		LUT5Bit3D = TBLLUT[2];
	u8*		LUT6Bit3D = TBLLUT[3];

	u8*		planeR = pInstance->planeR;
	u8*		planeG = pInstance->planeG;
	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 8x8\n");
#endif

	for (u16 y=0; y<ih; y+=64) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=64) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
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
					tWord >>= 8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY8;
							continue; 
						}
					}

					int idxX2 = idxY2;
					u16 xt=x;

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

					for (; xt<x+64; xt+=8) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 6;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*3;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];
							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 6 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i,%i]->[%i,%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3],(int)RGB[4],(int)RGB[5]);
#endif

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								pixelUsed[PixUsedIdx] |= 0xF0;
							} else {
								pixelUsed[PixUsedIdx] |= 0x0F;
							}

							int diff[3];
							COMPUTE_RGBDELTA;

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6));

//							for (int tileY=0; tileY <16; tileY+=8) {
							u8* tileRL = &planeR[idxTilePlane];
							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							int cnt = 1;

							DEF_PMC
							DEBUG_PIXELCOUNTER

							block4Y:
								switch (patternQuad & 3) {
								case 3:
									tileRL += 32; tileGL += 32; tileBL += 32;
									break;
								case 2: {
									// -----------------------------------------------
									// Left 4x4 Tile
									// -----------------------------------------------
									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

										// Next Line
										tileRL   += 5;
										tileGL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16;

								} break;
								case 1: {
									// -----------------------------------------------
									// Right 4x4 Tile
									// -----------------------------------------------
									tileRL += 4;
									tileGL += 4;
									tileBL += 4;

									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

										// Next Line
										tileRL   += 5;
										tileGL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16;

									// Return at X = 0 coordinate inside tile
									tileRL -= 4;
									tileGL -= 4;
									tileBL -= 4;
								} break;
								case 0: {
									// -----------------------------------------------
									// Both 4x4 Tile
									// -----------------------------------------------

									n = 4;
									do {
										// Left Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										// Right Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
										// Next Line
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD32;
								} break;
								DEFAULT_UNREACHABLE
								}

								if (cnt) {
									cnt = 0;
									patternQuad >>= 2;
									goto block4Y;
								}

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY8;
				}
			}
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY64;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile3D_8x4 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY4	= pInstance->strideRGBMap;  // 2 Block of 4 pixel = skip 8.
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

	u8*		LUT3Bit3D = TBLLUT[0];
	u8*		LUT4Bit3D = TBLLUT[1];
	u8*		LUT5Bit3D = TBLLUT[2];
	u8*		LUT6Bit3D = TBLLUT[3];

	u8*		planeR = pInstance->planeR;
	u8*		planeG = pInstance->planeG;
	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 8x4\n");
#endif

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=64) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
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
					tWord >>= 8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					int idxX2 = idxY2;
					u16 xt=x;

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

					for (; xt<x+64; xt+=8) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 6;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*3;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];

#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i,%i]->[%i,%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3],(int)RGB[4],(int)RGB[5]);
#endif

							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 6 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							int maskUse = 0x3;
							if (yt & 4) {
								patternQuad >>= 2;
								maskUse     <<= 2;
							}
							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								maskUse     <<= 4;
							}

							pixelUsed[PixUsedIdx] |= maskUse;

							int diff[3];
							COMPUTE_RGBDELTA;

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)) + ((yt & 4)<<3); // ((yt & 4)<<3) = Odd / Even Tile vertically (4x8)

//							for (int tileY=0; tileY <16; tileY+=8) {
							u8* tileRL = &planeR[idxTilePlane];
							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;

							DEF_PMC
							DEBUG_PIXELCOUNTER

							switch (patternQuad & 3) {
							case 3:
								break;
							case 2: {
								// -----------------------------------------------
								// Left 4x4 Tile
								// -----------------------------------------------
								n = 4;
								do {
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

									// Next Line
									tileRL   += 5;
									tileGL   += 5;
									tileBL   += 5;
								} while (--n); // Compare to zero, lower loop cost.
								DEBUG_PIX_ADD16;

							} break;
							case 1: {
								// -----------------------------------------------
								// Right 4x4 Tile
								// -----------------------------------------------
								tileRL += 4;
								tileGL += 4;
								tileBL += 4;

								n = 4;
								do {
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

									// Next Line
									tileRL   += 5;
									tileGL   += 5;
									tileBL   += 5;
								} while (--n); // Compare to zero, lower loop cost.
								DEBUG_PIX_ADD16;
							} break;
							case 0: {
								// -----------------------------------------------
								// Both 4x4 Tile
								// -----------------------------------------------

								n = 4;
								do {
									// Left Block
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									// Right Block
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									// Next Line
								} while (--n); // Compare to zero, lower loop cost.
								DEBUG_PIX_ADD32;
							} break;
							DEFAULT_UNREACHABLE
							}

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY4;
				}
			}
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY64;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile3D_4x8 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY8	= pInstance->strideRGBMap * 2;  // 2 Block of 4 pixel = skip 8.
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

	u8*		LUT3Bit3D = TBLLUT[0];
	u8*		LUT4Bit3D = TBLLUT[1];
	u8*		LUT5Bit3D = TBLLUT[2];
	u8*		LUT6Bit3D = TBLLUT[3];

	u8*		planeR = pInstance->planeR;
	u8*		planeG = pInstance->planeG;
	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 4x8\n");
#endif

	for (u16 y=0; y<ih; y+=64) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
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
					tWord >>= 8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY8;
							continue; 
						}
					}

					int idxX2 = idxY2;
					u16 xt=x;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;	// 8 Pixel on screen.
					}

					for (; xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 6;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*3;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];
#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i,%i]->[%i,%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3],(int)RGB[4],(int)RGB[5]);
#endif
							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 6 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)); // ((yt & 4)<<3) = Odd / Even Tile vertically (4x8)

							int maskUse = 0x5;
							if (xt & 4) {
								patternQuad >>= 1;
								maskUse     <<= 1;
								idxTilePlane += 4; // Shift target 4 pixel.
							}
							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								maskUse     <<= 4;
							}

							pixelUsed[PixUsedIdx] |= maskUse;

							int diff[3];
							COMPUTE_RGBDELTA;

//							for (int tileY=0; tileY <16; tileY+=8) {
							u8* tileRL = &planeR[idxTilePlane];
							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							int cnt = 1;
							DEF_PMC
							DEBUG_PIXELCOUNTER
nextVerticalBlock:
							if (patternQuad & 1) {
								// Empty tile
								tileRL += 32;
								tileGL += 32;
								tileBL += 32;
							} else {
								n = 4;
								do {
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

									// Next Line
									tileRL   += 5;
									tileGL   += 5;
									tileBL   += 5;
								} while (--n); // Compare to zero, lower loop cost.
								DEBUG_PIX_ADD16;
							}
							patternQuad >>= 2;

							if (cnt == 1) {
								cnt = 0;
								goto nextVerticalBlock;
							} // else end of 4x8 block...

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY8;
				}
			}
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY64;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile3D_4x4 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY4	= pInstance->strideRGBMap;  // 1 Block of 4 pixel = skip 4.
	int strideY32	= (pInstance->strideRGBMap * 8); // 8 block of 4 pixel = skip 32.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

	u8*		LUT3Bit3D = TBLLUT[0];
	u8*		LUT4Bit3D = TBLLUT[1];
	u8*		LUT5Bit3D = TBLLUT[2];
	u8*		LUT6Bit3D = TBLLUT[3];

	u8*		planeR = pInstance->planeR;
	u8*		planeG = pInstance->planeG;
	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 4x4\n");
#endif

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
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
					tWord >>= 8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					int idxX2 = idxY2;
					u16 xt=x;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;	// 8 Pixel on screen.
					}

					for (; xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 6;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*3;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];
#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i,%i]->[%i,%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3],(int)RGB[4],(int)RGB[5]);
#endif
							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 6 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)); // ((yt & 4)<<3) = Odd / Even Tile vertically (4x8)

							int maskUse = 0x1;
							if (yt & 4) {
								patternQuad >>= 2;
								maskUse     <<= 2;
								idxTilePlane += 32; // Shift target 4 pixel.
							}
							if (xt & 4) {
								patternQuad >>= 1;
								maskUse     <<= 1;
								idxTilePlane += 4; // Shift target 4 pixel.
							}
							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								maskUse     <<= 4;
							}

							pixelUsed[PixUsedIdx] |= maskUse;

							int diff[3];
							COMPUTE_RGBDELTA;

//							for (int tileY=0; tileY <16; tileY+=8) {
							u8* tileRL = &planeR[idxTilePlane];
							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							DEF_PMC
							DEBUG_PIXELCOUNTER

							if ((patternQuad & 1) == 0) {
								n = 4;
								do {
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif
									L = &LUT[*indexStream++];
									*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
									*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1],L[2]);
#endif

									// Next Line
									tileRL   += 5;
									tileGL   += 5;
									tileBL   += 5;
								} while (--n); // Compare to zero, lower loop cost.
								DEBUG_PIX_ADD16;
							}

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY4;
				}
			}
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile2D_8x8_RG (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY8	= (pInstance->strideRGBMap * 2);  // 2 Block of 4 pixel = skip 8.
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

	u8*		planeR = pInstance->planeR;
	u8*		planeG = pInstance->planeG;
//	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 8x8 RG\n");
#endif

	for (u16 y=0; y<ih; y+=64) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=64) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
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
					tWord >>= 8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY8;
							continue; 
						}
					}

					int idxX2 = idxY2;
					u16 xt=x;

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

					for (; xt<x+64; xt+=8) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 4;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*2;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];
#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i]->[%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3]);
#endif
							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 3 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								pixelUsed[PixUsedIdx] |= 0xF0;
							} else {
								pixelUsed[PixUsedIdx] |= 0x0F;
							}

							int diff[2];
							COMPUTE_RGBDELTA2D;

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6));

//							for (int tileY=0; tileY <16; tileY+=8) {
							u8* tileRL = &planeR[idxTilePlane];
							u8* tileGL = &planeG[idxTilePlane];
//							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							int cnt = 1;

							DEF_PMC
							DEBUG_PIXELCOUNTER

							block4Y:
								switch (patternQuad & 3) {
								case 3:
									tileRL += 32; tileGL += 32; /*tileBL += 32;*/
									break;
								case 2: {
									// -----------------------------------------------
									// Left 4x4 Tile
									// -----------------------------------------------
									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif

										// Next Line
										tileRL   += 5;
										tileGL   += 5;
//										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16;

								} break;
								case 1: {
									// -----------------------------------------------
									// Right 4x4 Tile
									// -----------------------------------------------
									tileRL += 4;
									tileGL += 4;
//									tileBL += 4;

									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif

										// Next Line
										tileRL   += 5;
										tileGL   += 5;
//										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16;

									// Return at X = 0 coordinate inside tile
									tileRL -= 4;
									tileGL -= 4;
//									tileBL -= 4;
								} break;
								case 0: {
									// -----------------------------------------------
									// Both 4x4 Tile
									// -----------------------------------------------

									n = 4;
									do {
										// Left Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										// Right Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//										*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										// Next Line
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD32;
								} break;
								DEFAULT_UNREACHABLE
								}

								if (cnt) {
									cnt = 0;
									patternQuad >>= 2;
									goto block4Y;
								}

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY8;
				}
			}
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY64;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile2D_4x4_RG (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY4	= pInstance->strideRGBMap;  // 1 Block of 4 pixel = skip 4.
	int strideY32	= (pInstance->strideRGBMap * 8); // 8 block of 4 pixel = skip 32.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

	u8*		planeR = pInstance->planeR;
	u8*		planeG = pInstance->planeG;
//	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 4x4 RG\n");
#endif

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
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
					tWord >>= 8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					int idxX2 = idxY2;
					u16 xt=x;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;	// 8 Pixel on screen.
					}

					for (; xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 4;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*2;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];
#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i]->[%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3]);
#endif
							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 6 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)); // ((yt & 4)<<3) = Odd / Even Tile vertically (4x8)

							int maskUse = 0x1;
							if (yt & 4) {
								patternQuad >>= 2;
								maskUse     <<= 2;
								idxTilePlane += 32; // Shift target 4 pixel.
							}
							if (xt & 4) {
								patternQuad >>= 1;
								maskUse     <<= 1;
								idxTilePlane += 4; // Shift target 4 pixel.
							}
							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								maskUse     <<= 4;
							}

							pixelUsed[PixUsedIdx] |= maskUse;

							int diff[2];
							COMPUTE_RGBDELTA2D;

//							for (int tileY=0; tileY <16; tileY+=8) {
							u8* tileRL = &planeR[idxTilePlane];
							u8* tileGL = &planeG[idxTilePlane];
//							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							DEF_PMC
							DEBUG_PIXELCOUNTER

							if ((patternQuad & 1) == 0) {
								n = 4;
								do {
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL++ = RGB[1] + ((diff[1]*L[1])>>7);
//									*tileBL++ = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
									L = &LUT[*indexStream++];
									*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
									*tileGL   = RGB[1] + ((diff[1]*L[1])>>7);
//									*tileBL   = RGB[2] + ((diff[2]*L[2])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif

									// Next Line
									tileRL   += 5;
									tileGL   += 5;
//									tileBL   += 5;
								} while (--n); // Compare to zero, lower loop cost.
								DEBUG_PIX_ADD16;
							}

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY4;
				}
			}
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile2D_8x8_GB (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY8	= (pInstance->strideRGBMap * 2);  // 2 Block of 4 pixel = skip 8.
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

//	u8*		planeR = pInstance->planeR;
	u8*		planeG = pInstance->planeG;
	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 8x8_GB\n");
#endif

	for (u16 y=0; y<ih; y+=64) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=64) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
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
					tWord >>= 8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY8;
							continue; 
						}
					}

					int idxX2 = idxY2;
					u16 xt=x;

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

					for (; xt<x+64; xt+=8) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 4;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*2;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];

#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i]->[%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3]);
#endif

							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 3 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								pixelUsed[PixUsedIdx] |= 0xF0;
							} else {
								pixelUsed[PixUsedIdx] |= 0x0F;
							}

							int diff[2];
							COMPUTE_RGBDELTA2D;

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6));

							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							int cnt = 1;

							DEF_PMC
							DEBUG_PIXELCOUNTER

							block4Y:
								switch (patternQuad & 3) {
								case 3:
									/*tileRL += 32;*/ tileGL += 32; tileBL += 32;
									break;
								case 2: {
									// -----------------------------------------------
									// Left 4x4 Tile
									// -----------------------------------------------
									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL   = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif

										// Next Line
										tileGL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16;

								} break;
								case 1: {
									// -----------------------------------------------
									// Right 4x4 Tile
									// -----------------------------------------------
									tileGL += 4;
									tileBL += 4;

									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL   = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif

										// Next Line
										tileGL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16;

									// Return at X = 0 coordinate inside tile
									tileGL -= 4;
									tileBL -= 4;
								} break;
								case 0: {
									// -----------------------------------------------
									// Both 4x4 Tile
									// -----------------------------------------------

									n = 4;
									do {
										// Left Block
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										// Right Block
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										// Next Line
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD32;
								} break;
								DEFAULT_UNREACHABLE
								}

								if (cnt) {
									cnt = 0;
									patternQuad >>= 2;
									goto block4Y;
								}

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY8;
				}
			}
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY64;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile2D_4x4_GB (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY4	= pInstance->strideRGBMap;  // 1 Block of 4 pixel = skip 4.
	int strideY32	= (pInstance->strideRGBMap * 8); // 8 block of 4 pixel = skip 32.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

//	u8*		planeR = pInstance->planeR;
	u8*		planeG = pInstance->planeG;
	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 4x4 GB\n");
#endif

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
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
					tWord >>= 8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					int idxX2 = idxY2;
					u16 xt=x;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;	// 8 Pixel on screen.
					}

					for (; xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 4;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*2;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];
#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i]->[%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3]);
#endif
							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 6 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)); // ((yt & 4)<<3) = Odd / Even Tile vertically (4x8)

							int maskUse = 0x1;
							if (yt & 4) {
								patternQuad >>= 2;
								maskUse     <<= 2;
								idxTilePlane += 32; // Shift target 4 pixel.
							}
							if (xt & 4) {
								patternQuad >>= 1;
								maskUse     <<= 1;
								idxTilePlane += 4; // Shift target 4 pixel.
							}
							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								maskUse     <<= 4;
							}

							pixelUsed[PixUsedIdx] |= maskUse;

							int diff[2];
							COMPUTE_RGBDELTA2D;

							u8* tileGL = &planeG[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							DEF_PMC
							DEBUG_PIXELCOUNTER

							if ((patternQuad & 1) == 0) {
								n = 4;
								do {
									L = &LUT[*indexStream++];
									*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
									L = &LUT[*indexStream++];
									*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
									L = &LUT[*indexStream++];
									*tileGL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
									L = &LUT[*indexStream++];
									*tileGL   = RGB[0] + ((diff[0]*L[0])>>7);
									*tileBL   = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif

									// Next Line
//									tileRL   += 5;
									tileGL   += 5;
									tileBL   += 5;
								} while (--n); // Compare to zero, lower loop cost.
								DEBUG_PIX_ADD16;
							}

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY4;
				}
			}
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile2D_8x8_RB (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY8	= (pInstance->strideRGBMap * 2);  // 2 Block of 4 pixel = skip 8.
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

	u8*		planeR = pInstance->planeR;
//	u8*		planeG = pInstance->planeG;
	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 8x8 RB\n");
#endif

	for (u16 y=0; y<ih; y+=64) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=64) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
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
					tWord >>= 8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY8;
							continue; 
						}
					}

					int idxX2 = idxY2;
					u16 xt=x;

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

					for (; xt<x+64; xt+=8) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 4;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*2;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];
#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i]->[%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3]);
#endif
							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 3 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								pixelUsed[PixUsedIdx] |= 0xF0;
							} else {
								pixelUsed[PixUsedIdx] |= 0x0F;
							}

							int diff[2];
							COMPUTE_RGBDELTA2D;

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6));

							u8* tileRL = &planeR[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							int cnt = 1;

							DEF_PMC
							DEBUG_PIXELCOUNTER

							block4Y:
								switch (patternQuad & 3) {
								case 3:
									tileRL += 32; /*tileGL += 32;*/ tileBL += 32;
									break;
								case 2: {
									// -----------------------------------------------
									// Left 4x4 Tile
									// -----------------------------------------------
									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL   = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif

										// Next Line
										tileRL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16;

								} break;
								case 1: {
									// -----------------------------------------------
									// Right 4x4 Tile
									// -----------------------------------------------
									tileRL += 4;
									tileBL += 4;

									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL   = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										// Next Line
										tileRL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD16;

									// Return at X = 0 coordinate inside tile
									tileRL -= 4;
									tileBL -= 4;
								} break;
								case 0: {
									// -----------------------------------------------
									// Both 4x4 Tile
									// -----------------------------------------------

									n = 4;
									do {
										// Left Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										// Right Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
										*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
										printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
										// Next Line
									} while (--n); // Compare to zero, lower loop cost.
									DEBUG_PIX_ADD32;
								} break;
								DEFAULT_UNREACHABLE
								}

								if (cnt) {
									cnt = 0;
									patternQuad >>= 2;
									goto block4Y;
								}

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY8;
				}
			}
			idxX += 16; // 64 pixel, 16 block of 4 pixel skip in screen space. (= 16 in RGB Map)
		}
		idxY += strideY64;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}

void Tile2D_4x4_RB (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT) {
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	int idxX;
	int idxY		= 0;
	u64* tileBitmap = (u64*)param->currentMap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY4	= pInstance->strideRGBMap;  // 1 Block of 4 pixel = skip 4.
	int strideY32	= (pInstance->strideRGBMap * 8); // 8 block of 4 pixel = skip 32.
	int strideTile	= (iw>>3)<<6;
	u8* dataStreamNBit[4];

	dataStreamNBit[0] = param->stream3Bit;
	dataStreamNBit[1] = param->stream4Bit;
	dataStreamNBit[2] = param->stream5Bit;
	dataStreamNBit[3] = param->stream6Bit;

	u8*		dataUncompressedRGB = param->colorStream;
	u16*	tileStream          = param->tileStream;

	u8*		planeR = pInstance->planeR;
//	u8*		planeG = pInstance->planeG;
	u8*		planeB = pInstance->planeB;

	// RGB Version Only.
#ifdef DEBUG_TILE3D
	printf("--- Decoder 4x4 RB\n");
#endif

	for (u16 y=0; y<ih; y+=32) {
		idxX = idxY;
		for (u16 x=0; x<iw; x+=32) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 16 tile if nothing.
			if (tWord) {
				u16 yt=y;
				int idxY2 = idxX;

				// Logarithmic shifter jump test...

				// If nothing if upper 16 pixel, early jump.
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
					tWord >>= 8;

					// Early skip, 4 tiles if nothing.
					if (msk == 0) {
						if (tWord == 0) { // Completed the tile early.
							break;
						} else {
							idxY2 += strideY4;
							continue; 
						}
					}

					int idxX2 = idxY2;
					u16 xt=x;

					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 4 pixel
						idxX2 += 4;		// 2 Tile of 2 colors
						xt    += 16;	// 16 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 4 pixel
						idxX2 += 2;		// 1 Tile of 2 colors
						xt    += 8;	// 8 Pixel on screen.
					}

					for (; xt<x+32; xt+=4) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							// Color Ready.
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 4;
							// Tile  Ready.
							u16 tile		= *tileStream++;
							u16 tileLutIDX  = (tile & 0x3FFF)*2;
							u8  format		= (tile >> 14) & 3;
							u8* indexStream = dataStreamNBit[format];
#ifdef DEBUG_TILE3D
							printf("Tile:%i [%i,%i]->[%i,%i]\n",(int)tile,(int)RGB[0],(int)RGB[1],(int)RGB[2],(int)RGB[3]);
#endif
							// LUT For Index Ready.
							u8* LUT			= &((TBLLUT[format])[tileLutIDX<<(3+format)]);
							/* Same as
							switch (format) {
							case 0: LUT = &LUT3Bit3D[tileLutIDX<<3]; break; // 6 Bit
							case 1: LUT = &LUT4Bit3D[tileLutIDX<<4]; break; // 4 Bit
							case 2: LUT = &LUT5Bit3D[tileLutIDX<<5]; break; // 5 Bit
							case 3: LUT = &LUT6Bit3D[tileLutIDX<<6]; break; // 6 Bit
							} */
							u32 stride4x4Map = iw >> 4; // TODO => Wrong, need to fix, does not work for image width != mod 16

							int PixUsedIdx = (xt>>4) + ((yt>>3)*stride4x4Map);
							u8 patternQuad = pixelUsed[PixUsedIdx];

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6)); // ((yt & 4)<<3) = Odd / Even Tile vertically (4x8)

							int maskUse = 0x1;
							if (yt & 4) {
								patternQuad >>= 2;
								maskUse     <<= 2;
								idxTilePlane += 32; // Shift target 4 pixel.
							}
							if (xt & 4) {
								patternQuad >>= 1;
								maskUse     <<= 1;
								idxTilePlane += 4; // Shift target 4 pixel.
							}
							if (xt & 8) {
								patternQuad >>= 4; // Shift of 4 if tile is 8 odd, else nothing if 16 aligned.
								maskUse     <<= 4;
							}

							pixelUsed[PixUsedIdx] |= maskUse;

							int diff[2];
							COMPUTE_RGBDELTA2D;

							u8* tileRL = &planeR[idxTilePlane];
							u8* tileBL = &planeB[idxTilePlane];

							u8* L;
							int n;
							DEF_PMC
							DEBUG_PIXELCOUNTER
nextVerticalBlock:
							if ((patternQuad & 1) == 0) {
								n = 4;
								do {
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
									L = &LUT[*indexStream++];
									*tileRL++ = RGB[0] + ((diff[0]*L[0])>>7);
									*tileBL++ = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif
									L = &LUT[*indexStream++];
									*tileRL   = RGB[0] + ((diff[0]*L[0])>>7);
									*tileBL   = RGB[1] + ((diff[1]*L[1])>>7);
#ifdef DEBUG_TILE3D
									printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",xt,yt,pmC++,indexStream[-1],L[0],L[1]);
#endif

									// Next Line
									tileRL   += 5;
//									tileGL   += 5;
									tileBL   += 5;
								} while (--n); // Compare to zero, lower loop cost.
								DEBUG_PIX_ADD16;
							}

//								idxTilePlane += strideTile;
//							}

							// Write back
							dataStreamNBit[format] = indexStream; 
						}
						msk >>= 1;
						idxX2 += 2;
					}
					
					idxY2 += strideY4;
				}
			}
			idxX += 8; // 32 pixel, 8 block of 4 pixel skip in screen space. (= 8 in RGB Map)
		}
		idxY += strideY32;
	}

	// Write back data for next pass...
	param->colorStream	= dataUncompressedRGB;
	param->tileStream	= tileStream;

	// Could be optimized by passing directly the array inside the param struct.
	param->stream3Bit	= dataStreamNBit[0];
	param->stream4Bit	= dataStreamNBit[1];
	param->stream5Bit	= dataStreamNBit[2];
	param->stream6Bit	= dataStreamNBit[3];
}
