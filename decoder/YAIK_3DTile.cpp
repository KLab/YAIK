#include "YAIK_functions.h"

void Tile3D_16x8(YAIK_Instance* pInstance, HeaderTile3D* pHeader, Tile3DParam* param) {
}

void Tile3D_8x16(YAIK_Instance* pInstance, HeaderTile3D* pHeader, Tile3DParam* param) {
}

void Tile3D_8x8 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, Tile3DParam* param) {
/*
	u16 iw			= pInstance->width;
	u16 ih			= pInstance->height;
	u64 tileWord	= 0;
	u8* mapRGB		= pInstance->mapRGB;
	u8* hasRGB		= pInstance->mapRGBMask;

	// TODO : Add assert about little-endian vs big-endian : Swap bytes if needed. --> Check at library startup, set a flag accessible through a function : isBigEndian()
	// For now Intel and Arm devices mainly use the little-endian.

	u64* tileBitmap = (u64*)dataUncompressedTilebitmap;
	u8* pixelUsed	= pInstance->tile4x4Mask;
	int strideY8	= (pInstance->strideRGBMap * 2);
	int strideY64	= (pInstance->strideRGBMap * 16); // 16 block of 4 pixel = skip 64.
	int strideTile	= (iw>>3)<<6;

	u8* indexStream;

	for (u16 y=0; y<ih; y+=64) {
		for (u16 x=0; x<iw; x+=64) {
			u64 tWord = *tileBitmap++;

			// Early skip ! 64 tile if nothing.
			if (tWord) {
				u16 yt    = y;

				// If nothing if upper 32 tile, early jump.
				if ((tWord & 0xFFFFFFFF) == 0) {
					tWord >>= 32;
					yt     += 32;
				}

				if ((tWord & 0xFFFF) == 0) {
					tWord >>= 16;
					yt     += 16;
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
							continue; 
						}
					}

					u16 xt=x;
					if ((msk & 0xF) == 0) {
						msk  >>= 4;		// 4 Tile of 8 pixel
						xt    += 32;	// 32 Pixel on screen.
					}

					if ((msk & 0x3) == 0) {
						msk  >>= 2;		// 2 Tile of 8 pixel
						xt    += 16;	// 16 Pixel on screen.
					}

					for (;xt<x+64; xt+=8) {
						if ((xt >= iw) || (msk == 0)) {
							break; 
						} // We completed the decoding, trying to process OUTSIDE of the tile vertically, go next vertical...

						if (msk & 1) {
							u8* RGB = dataUncompressedRGB;
							dataUncompressedRGB += 6;

							int diff[3];
							diff[0] = RGB[3] - RGB[0];
							diff[1] = RGB[4] - RGB[1];
							diff[2] = RGB[5] - RGB[2];

							int idxTilePlane = (((yt>>3)*strideTile) + ((xt>>3)<<6));

							// [TODO : Select the stream 4/5/6 bit, Select LUT]
							u8* LUT;

//							for (int tileY=0; tileY <16; tileY+=8) {
								u8* tileRL = &planeR[idxTilePlane];
								u8* tileGL = &planeG[idxTilePlane];
								u8* tileBL = &planeB[idxTilePlane];

							u8 idx;
							u8* L;
							int n;
							block4Y:
								switch (patternQuad) {
								case 0:
									tileRL += 32; tileGL += 32; tileBL += 32;
									goto block4Y;
								case 1: {
									// -----------------------------------------------
									// Left 4x4 Tile
									// -----------------------------------------------
									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL   = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL   = RGB[2] + (diff[2]*(*L  ))>>8;

										// Next Line
										tileRL   += 5;
										tileGL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.

								} break;
								case 2: {
									// -----------------------------------------------
									// Right 4x4 Tile
									// -----------------------------------------------
									tileRL += 4;
									tileGL += 4;
									tileBL += 4;

									n = 4;
									do {
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL   = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL   = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL   = RGB[2] + (diff[2]*(*L  ))>>8;

										// Next Line
										tileRL   += 5;
										tileGL   += 5;
										tileBL   += 5;
									} while (--n); // Compare to zero, lower loop cost.

									// Return at X = 0 coordinate inside tile
									tileRL -= 4;
									tileGL -= 4;
									tileBL -= 4;
								} break;
								case 3: {
									// -----------------------------------------------
									// Both 4x4 Tile
									// -----------------------------------------------

									n = 4;
									do {
										// Left Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										// Right Block
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										L = &LUT[*indexStream++];
										*tileRL++ = RGB[0] + (diff[0]*(*L++))>>8;
										*tileGL++ = RGB[1] + (diff[1]*(*L++))>>8;
										*tileBL++ = RGB[2] + (diff[2]*(*L  ))>>8;
										// Next Line
									} while (--n); // Compare to zero, lower loop cost.
								} break;
								}

								patternQuad >>= 2;
								if (patternQuad) {
									goto block4Y;
								}

							exitBlock4Y:

								// TODO write back stream ptr.

//								idxTilePlane += strideTile;
//							}
						}
						
						msk  >>= 1;
					}
				}
			}
		}
	}
*/
}

void Tile3D_8x4 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, Tile3DParam* param) {
}

void Tile3D_4x8 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, Tile3DParam* param) {
}

void Tile3D_4x4 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, Tile3DParam* param) {
}
