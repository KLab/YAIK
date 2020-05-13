#include "framework.h"

// Memset, memcpy
#include <memory.h>

// No implementation, only definition in the framework. Implementation is in main.
#include "../external/stb_image/stb_image.h"
#include "../external/stb_image/stb_image_write.h"

/*static*/ Image* Image::CreateImage(int w, int h, int channelCount, bool fillImage) {
	Image* res = new Image();
	res->w = w;
	res->h = h;
	res->planeCount = channelCount;

	for (int ch = 0; ch < channelCount; ch++) {
		res->planes[ch] = new Plane(w, h);
		if (fillImage) { memset(res->planes[ch]->GetPixels(), 0, w * h * sizeof(int)); }
	}

	return res;
}

void Image::SaveAsYCoCg(const char* optionnalFileName) {
	if (optionnalFileName) {
		bool signedF[4];
		signedF[0] = false;
		signedF[1] = true;
		signedF[2] = true;
		signedF[3] = false;
		this->SavePNG(optionnalFileName, signedF);
	}
}

void Image::Compute4DHistogram(int* histogramRGB, const BoundingBox& boundingBox, Plane* mask_) {
	int* src[4] = { NULL,NULL,NULL,NULL };

	for (int n = 0; n < planeCount; n++) {
		src[n] = GetPlane(n)->GetPixels();
	}

	// Override alpha if 3 was alpha, by user given mask.
	if (mask_) {
		src[3] = mask_->GetPixels();
	}

	// 64 MB Table !
	memset(histogramRGB, 0, 256 * 256 * 256 * sizeof(int));

	int x1 = boundingBox.w + boundingBox.x;
	int y1 = boundingBox.h + boundingBox.y;

	for (int y = boundingBox.y; y < y1; y++) {
		for (int x = boundingBox.x; x < x1; x++) {
			int idx = x + (y * w);
			int targetIdx = (((src[0])[idx] & 0xFF) << 16) + (((src[1])[idx] & 0xFF) << 8) + (((src[2])[idx] & 0xFF));

			// Increment count, only when Mask authorize it.
			if ((src[3]==NULL) || (src[3])[idx]) {
				histogramRGB[targetIdx]++;
			}
		}
	}
}

Plane* Image::ComputeOperatorMask(int* values, Image::MASK_OPERATOR op, bool allTrue, bool only3Plane) {
	int max = w * h;
	Plane* out = new Plane(w, h);

	int* dst = out->GetPixels();
	int* src[4];
	
	for (int n = 0; n < planeCount; n++) {
		src[n] = GetPlane(n)->GetPixels();
	}

	// End loop with FIRST PLANE.
	int* srcE = &(src[0][max]);
	int pCnt = planeCount;
	if ((planeCount > 3) && only3Plane) { pCnt = 3; }

	int outV;

	if (allTrue) {
		switch (op) {
		case EQUAL_MSK: while (src[0] < srcE) {
							outV = 255;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) != values[n]) { outV = 0; }
							}
							*dst++ = outV;
						}
						break;
		case NEQ_MSK:   while (src[0] < srcE) {
							outV = 255;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) == values[n]) { outV = 0; }
							}
							*dst++ = outV;
						}
						break;
		case LT_MASK:   while (src[0] < srcE) {
							outV = 255;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) >= values[n]) { outV = 0; }
							}
							*dst++ = outV;
						}
						break;
		case GT_MASK:   while (src[0] < srcE) {
							outV = 255;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) <= values[n]) { outV = 0; }
							}
							*dst++ = outV;
						}
						break;
		case LE_MASK:   while (src[0] < srcE) {
							outV = 255;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) > values[n]) { outV = 0; }
							}
							*dst++ = outV;
						}
						break;
		case GE_MASK:   while (src[0] < srcE) {
							outV = 255;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) < values[n]) { outV = 0; }
							}
							*dst++ = outV;
						}
						break;
		}
	} else {
		switch (op) {
		case EQUAL_MSK: while (src[0] < srcE) {
							outV = 0;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) == values[n]) { outV = 255; }
							}
							*dst++ = outV;
						}
						break;
		case NEQ_MSK:   while (src[0] < srcE) {
							outV = 0;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) != values[n]) { outV = 255; }
							}
							*dst++ = outV;
						}
						break;
		case LT_MASK:   while (src[0] < srcE) {
							outV = 0;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) < values[n]) { outV = 255; }
							}
							*dst++ = outV;
						}
						break;
		case GT_MASK:   while (src[0] < srcE) {
							outV = 0;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) > values[n]) { outV = 255; }
							}
							*dst++ = outV;
						}
						break;
		case LE_MASK:   while (src[0] < srcE) {
							outV = 0;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) <= values[n]) { outV = 255; }
							}
							*dst++ = outV;
						}
						break;
		case GE_MASK:   while (src[0] < srcE) {
							outV = 0;
							for (int n = 0; n < pCnt; n++) {
								if ((*(src[n]++)) >= values[n]) { outV = 255; }
							}
							*dst++ = outV;
						}
						break;
		}
	}

	return out;
}

void Image::SetPixel(int x, int y, int r, int g, int b) {
	if (x < 0 || x >= w) { return; }
	if (y < 0 || y >= h) { return; }
	int idx = x + y * w;
	planes[0]->GetPixels()[idx] = r;
	planes[1]->GetPixels()[idx] = g;
	planes[2]->GetPixels()[idx] = b;
}

/*static*/ Image* Image::LoadPNG(const char* filename) {
	int w, h, n;
	stbi_set_flip_vertically_on_load(false);
	unsigned char* src = stbi_load(filename, &w, &h, &n, 0);
	Image* res = NULL;
	if (src) {
		res = Image::CreateImage(w,h,n,false);

		unsigned char* pSrc = src;
		int end = w * h;
		for (int p = 0; p < end; p++) {
			res->planes[0]->GetPixels()[p] = pSrc[0];
			res->planes[1]->GetPixels()[p] = pSrc[1];
			res->planes[2]->GetPixels()[p] = pSrc[2];
			if (n > 3) {
				res->planes[3]->GetPixels()[p] = pSrc[3];
			}
			pSrc += n;
		}
		res->planeCount = n;
		delete[] src;
	}
	return res;
}

void Image::SavePNG(const char* fileName, bool* signed_) {
	unsigned char* data = new unsigned char[w * h * 4];

	int idx = 0;
	int* pln[4];
	pln[0] = planes[0]->GetPixels();
	pln[1] = planes[1]->GetPixels();
	pln[2] = planes[2]->GetPixels();
	pln[3] = planes[3] ? planes[3]->GetPixels() : NULL;

	int offset[4];
	if (signed_) {
		for (int n = 0; n < 4; n++) {
			offset[n] = signed_[n] ? 128 : 0;
		}
	} else {
		offset[0] = 0;
		offset[1] = 0;
		offset[2] = 0;
		offset[3] = 0;
	}

	for (int n = 0; n < w * h; n++) {
		int r, g, b, a;

		r = (pln[0])[n] + offset[0];
		g = (pln[1])[n] + offset[1];
		b = (pln[2])[n] + offset[2];
		a = pln[3] ? (pln[3])[n]+offset[3] : 255;

		if (r < 0) { r = 0; }
		if (r > 255) { r = 255; }
		if (g < 0) { g = 0; }
		if (g > 255) { g = 255; }
		if (b < 0) { b = 0; }
		if (b > 255) { b = 255; }
		if (a < 0) { a = 0; }
		if (a > 255) { a = 255; }

		data[idx  ] = r;
		data[idx+1] = g;
		data[idx+2] = b;
		if (pln[3]) {
			data[idx+3] = a;
		}
		idx += planeCount;
	}

	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(fileName, w, h, planeCount, data, w * planeCount);

	delete[] data;
}

Image* Image::ConvertToRGB2YCoCg(bool doConversion) {
	if (doConversion) {
		Image* pNew = new Image();
		pNew->planeCount = planeCount;
		pNew->w = w; pNew->h = h;

		for (int n = 0; n < planeCount; n++) { pNew->planes[n] = new Plane(w, h); }

		int* pSrcR = planes[0]->GetPixels();
		int* pSrcG = planes[1]->GetPixels();
		int* pSrcB = planes[2]->GetPixels();
		int* pDstY = pNew->planes[0]->GetPixels();
		int* pDstCo = pNew->planes[1]->GetPixels();
		int* pDstCg = pNew->planes[2]->GetPixels();

		if (planeCount >= 4) {
			pNew->planes[3] = planes[3]->Clone();
		}

		for (int y = 0; y < h; y++) {
			int idxY = y * w;
			for (int x = 0; x < w; x++) {
				int idx = x + idxY;

				int R = pSrcR[idx];
				int G = pSrcG[idx];
				int B = pSrcB[idx];

				RGBtoYCoCg(R,G,B,pDstY[idx],pDstCo[idx],pDstCg[idx]);
			}
		}

		return pNew;
	} else {
		return this;
	}
}

Image* Image::ConvertToYCoCg2RGB(bool doConversion) {
	if (doConversion) {
		Image* pNew = new Image();
		pNew->planeCount = planeCount;
		pNew->w = w; pNew->h = h;

		for (int n = 0; n < planeCount; n++) { pNew->planes[n] = new Plane(w, h); }

		int* pSrcY = planes[0]->GetPixels();
		int* pSrcCo = planes[1]->GetPixels();
		int* pSrcCg = planes[2]->GetPixels();
		int* pDstR = pNew->planes[0]->GetPixels();
		int* pDstG = pNew->planes[1]->GetPixels();
		int* pDstB = pNew->planes[2]->GetPixels();

		if (planeCount >= 4) {
			pNew->planes[3] = planes[3]->Clone();
		}

		for (int y = 0; y < h; y++) {
			int idxY = y * w;
			for (int x = 0; x < w; x++) {
				int idx = x + idxY;

				YCoCgtoRGB(
					pSrcY[idx],pSrcCo[idx],pSrcCg[idx],
					pDstR[idx],pDstG[idx],pDstB[idx]
				);
			}
		}

		return pNew;
	} else {
		return this;
	}
}
