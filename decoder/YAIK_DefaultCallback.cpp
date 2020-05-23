#include "YAIK_functions.h"

// malloc/free
#include <stdlib.h>

// memcpy
#include <memory.h>

#ifdef YAIK_DEVEL
#include <stdio.h>
#endif

void*	internal_allocFunc	(void* customContext, size_t size		) {
#ifdef YAIK_DEVEL
	printf("ALLOC : %i\n",size);
#endif
	return malloc(size);
}

void	internal_freeFunc	(void* customContext, void*  address	) {
	free(address);
}

void	internal_imageBuilderFunc	(struct YAIK_SDecodedImage* userInfo, struct YAIK_SCustomDataSource* sourceImageInternal ) {
	int fullSizeW	= userInfo->width  >> 3;
	int fullSizeH	= userInfo->height >> 3;
	int remainderW	= userInfo->width   & 7;
	int remainderH	= userInfo->height  & 7;

	u8* pR = sourceImageInternal->planeR;
	u8* pG = sourceImageInternal->planeG;
	u8* pB = sourceImageInternal->planeB;
	u8* pA = sourceImageInternal->planeA;

	u8* dst_p [8];
	u8* dst_pL[8];
	u8* pAL   [8];

	for (int n=0; n < 8; n++) {
		dst_pL[n] = userInfo->outputImage + (userInfo->outputImageStride * n);
		pAL   [n] = pA + (sourceImageInternal->strideA * n);
	}

	for (int ty=0; ty < (fullSizeH<<3); ty += 8) {
		memcpy(dst_p,dst_pL,8*sizeof(u8*));

		if (pA) {
			for (int tx=0; tx < fullSizeW; tx++) {
				for (int n=0; n < 8; n++) {
					u8* dst = dst_p[n];
					// 1 Line of 8 pixels.
					u8* pAlpha = pAL[n];
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					dst_p[n] = dst;
					pAL  [n] = pAlpha;
				}
			}
		} else {
			for (int tx=0; tx < fullSizeW; tx++) {
				for (int n=0; n < 8; n++) {
					u8* dst = dst_p[n];
					// 1 Line of 8 pixels.
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					dst_p[n] = dst;
				}
			}
		}

		// Right clipped side.
		if (remainderW) {
			if (pA) {
				for (int m=0; m < remainderW; m++) {
					for (int n=0; n < 8; n++) {
						u8* dst = dst_p[n];
						// 1 Pixel wide
						int idx = (n<<3) + m;
						*dst++ = pR[idx]; *dst++ = pG[idx]; *dst++ = pB[idx]; *dst = *pAL[n]; pAL[n]++;
						dst_p[n] = dst;
					}
				}
			} else {
				for (int m=0; m < remainderW; m++) {
					for (int n=0; n < 8; n++) {
						u8* dst = dst_p[n];
						// 1 Pixel wide
						int idx = (n<<3) + m;
						*dst++ = pR[idx]; *dst++ = pG[idx]; *dst++ = pB[idx];
						dst_p[n] = dst;
					}
				}
			}

			pR += 64; pG += 64; pB += 64;
		}

		// No stride necessary with Alpha channel, should match the output size exactly.

		for (int n=0; n < 8; n++) {
			dst_pL[n] += (userInfo->outputImageStride<<3);
		}
	}

	// Bottom clipped side.
	if (remainderH) {
		if (pA) {
			for (int tx=0; tx < fullSizeW; tx++) {
				for (int n=0; n < remainderH; n++) {
					u8* dst = dst_p[n];
					u8* pAlpha = pAL[n];

					// 1 Line of 8 pixels.
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++; *dst = *pAlpha++;

					dst_p[n] = dst;
					pAL  [n] = pAlpha;
				}
				// Go to beginning of next tile...
				pR += (8-remainderH)<<3;
				pG += (8-remainderH)<<3;
				pB += (8-remainderH)<<3;
			}
		} else {
			for (int tx=0; tx < fullSizeW; tx++) {
				for (int n=0; n < remainderH; n++) {
					u8* dst = dst_p[n];
					// 1 Line of 8 pixels.
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					*dst++ = *pR++; *dst++ = *pG++; *dst++ = *pB++;
					dst_p[n] = dst;
				}
				// Go to beginning of next tile...
				pR += (8-remainderH)<<3;
				pG += (8-remainderH)<<3;
				pB += (8-remainderH)<<3;
			}
		}
	}

	// Last right-bottom corner.
	if (remainderW && remainderH) {
		for (int n=0; n < remainderH; n++) {
			u8* dst = dst_p[n];
			if (pA) {
				u8* pAlpha = pAL[n];
				for (int tx=0; tx < remainderW; tx++) {
					// 1 Line of 8 pixels.
					int idx = (n<<3)+tx;
					*dst++ = pR[idx]; *dst++ = pG[idx]; *dst++ = pB[idx]; *dst = *pAlpha++;
				}
			} else {
				for (int tx=0; tx < remainderW; tx++) {
					// 1 Line of 8 pixels.
					int idx = (n<<3)+tx;
					*dst++ = pR[idx]; *dst++ = pG[idx]; *dst++ = pB[idx];
				}
			}
			dst_p[n] = dst;
			// Go to beginning of next tile...
			pR += (8-remainderH)<<3;
			pG += (8-remainderH)<<3;
			pB += (8-remainderH)<<3;
		}
	}
}
