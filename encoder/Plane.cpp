#include "framework.h"

// memcpy
#include "memory.h"

// No implementation, only definition in the framework. Implementation is in main.
#include "../external/stb_image/stb_image.h"
#include "../external/stb_image/stb_image_write.h"

Plane* Plane::Clone() {
	Plane* pNew = new Plane(w, h);
	memcpy(pNew->pixels, pixels, w * h * sizeof(int));
	return pNew;
}

int	Plane::GetBoundingBoxNonZeros(BoundingBox& result) {
	int x0 = 999999999;
	int y0 = 999999999;
	int x1 = -1;
	int y1 = -1;

	int count = 0;

	int* pix = GetPixels();
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int idx = x + (y * w);
			if (pix[idx] != 0) {
				count++;
				if (x < x0) {
					x0 = x; 
				}
				if (y < y0) {
					y0 = y; 
				}
				if (x > x1) {
					x1 = x; 
				}
				if (y > y1) {
					y1 = y; 
				}
			}
		}
	}

	if (x0 == 999999999) {
		// Nothing found...
		result.x = 0;
		result.y = 0;
		result.w = 0;
		result.w = 0;
	} else {
		result.x = x0;
		result.y = y0;
		result.w = (x1+1) - x0;
		result.h = (y1+1) - y0;
	}

	return count;
}

Plane* Plane::ComputeOperatorMask(int v, Plane::MASK_OPERATOR op) {
	int max = w * h;
	Plane* out = new Plane(w, h);
	int* dst	= out->GetPixels();
	int* src	= GetPixels();
	int* srcE	= &src[max];

	switch (op) {
	case EQUAL_MSK: while (src < srcE) { *dst++ = ((*src++) == v) ? 255 : 0; } break;
	case NEQ_MSK:   while (src < srcE) { *dst++ = ((*src++) != v) ? 255 : 0; } break;
	case LT_MASK:   while (src < srcE) { *dst++ = ((*src++)  < v) ? 255 : 0; } break;
	case GT_MASK:   while (src < srcE) { *dst++ = ((*src++)  > v) ? 255 : 0; } break;
	case LE_MASK:	while (src < srcE) { *dst++ = ((*src++) <= v) ? 255 : 0; } break;
	case GE_MASK:	while (src < srcE) { *dst++ = ((*src++) >= v) ? 255 : 0; } break;
	}

	return out;
}

/*

	Warning : Flood fill is crappy, work for most case,
	but it is not safe and recursion can make the program crash...

	Unwise to keep anything like that in Production code.
	Ok for just some tests on images... (Bad perf too)

*/

int recurseFloodFill(Plane* plane, int x, int y) {
	bool isOutside;

	int v = plane->GetPixelValue(x, y, isOutside);
	if (isOutside || (v!=255)) { return 0; }

	// Mark pixel as invalid for next time.
	plane->GetPixels()[x + y * plane->GetWidth()] = 128;

	int count = 1+recurseFloodFill(plane, x+1, y);
	count    += recurseFloodFill(plane, x-1, y);
	count    += recurseFloodFill(plane, x, y+1);
	count    += recurseFloodFill(plane, x, y-1);

	return count;
}

void recurseFloodFill0(Plane* plane, int x, int y) {
	bool isOutside;

	int v = plane->GetPixelValue(x, y, isOutside);
	if (isOutside || (v == 0) || (v > 128)) { return; }

	// Mark pixel as invalid for next time.
	plane->GetPixels()[x + y * plane->GetWidth()] = 0;

	recurseFloodFill0(plane, x + 1, y);
	recurseFloodFill0(plane, x - 1, y);
	recurseFloodFill0(plane, x, y + 1);
	recurseFloodFill0(plane, x, y - 1);
}

Plane* Plane::Erosion() {
//	Plane* out = new Plane(w, h);

	// First Remove Single pixels.
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			bool isOut;
			int vU = GetPixelValue(x, y - 1, isOut);
			int vD = GetPixelValue(x, y + 1, isOut);
			int vC = GetPixelValue(x, y, isOut);
			int vL = GetPixelValue(x-1, y, isOut);
			int vR = GetPixelValue(x+1, y, isOut);

			// Remove single point
			if (vC && !vU && !vD && !vL && !vR) {
				pixels[x + y * w] = 0;
			}
		}
	}

	// Second Remove 2 pix long horiz
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			bool isOut;
			int vU = GetPixelValue(x, y - 1, isOut);
			int vD = GetPixelValue(x, y + 1, isOut);
			int vU2 = GetPixelValue(x+1, y - 1, isOut);
			int vD2 = GetPixelValue(x+1, y + 1, isOut);
			int vC = GetPixelValue(x, y, isOut);
			int vC2 = GetPixelValue(x + 1, y, isOut);
			int vL = GetPixelValue(x - 1, y, isOut);
			int vR = GetPixelValue(x + 2, y, isOut);

			// Remove single point
			if (vC && vC2 && !vU && !vD && !vL && !vR && !vU2 && !vD2) {
				pixels[x + y * w] = 0;
				pixels[x + 1 + (y * w)] = 0;
			}
		}
	}

	// Second Remove 2 pix long vert
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			bool isOut;
			int vU = GetPixelValue(x, y - 1, isOut);
			int vD = GetPixelValue(x, y + 2, isOut);
			int vC = GetPixelValue(x, y, isOut);
			int vC2 = GetPixelValue(x, y + 1, isOut);
			int vL = GetPixelValue(x - 1, y, isOut);
			int vR = GetPixelValue(x + 1, y, isOut);
			int vL2 = GetPixelValue(x - 1, y + 1, isOut);
			int vR2 = GetPixelValue(x + 1, y + 1, isOut);

			// Remove single point
			if (vC && vC2 && !vU && !vD && !vL && !vR && !vL2 && !vR2) {
				pixels[x + y * w] = 0;
				pixels[x + (y + 1) * w] = 0;
			}
		}
	}

	int surfaces = 0;
	int totalSmooth = 0;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int count = recurseFloodFill(this, x, y);
			if (count > 50) {
				totalSmooth += count;
				surfaces++;
			} else if (count >= 1) {
				recurseFloodFill0(this, x, y);
			}
		}
	}

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			// Roll back to 255.
			int idx = x + y * w;
			int v = pixels[idx];
			pixels[idx] = v ? 255 : 0;
		}
	}

	return this;
}

Plane* Plane::ReduceQuarterLogicMax() {
	Plane* out = new Plane(w / 2, h / 2);
	
	int* src = GetPixels();
	int* dst = out->GetPixels();

	for (int y = 0; y < h / 2; y++) {
		for (int x = 0; x < w / 2; x++) {
			int* pSrc = &src[x * 2 + ((y * 2) * w)];
			int  a = pSrc[0];
			int  b = pSrc[1];
			pSrc += w;
			int  c = pSrc[0];
			int  d = pSrc[1];

			dst[x + y * out->GetWidth()] = (a && b && c && d) ? 255 : 0;
		}
	}

	return out;
}

Plane* Plane::ComputeOperatorMask(Plane* srcPlane, Plane::MASK_OPERATOR op) {
	int max = w * h;
	Plane* out = new Plane(w, h);
	int* dst = out->GetPixels();
	int* src = GetPixels();
	int* srcE = &src[max];
	int* srcB = srcPlane->GetPixels();

	switch (op) {
	case EQUAL_MSK: while (src < srcE) { *dst++ = ((*src++) == (*srcB++)) ? 255 : 0; } break;
	case NEQ_MSK:   while (src < srcE) { *dst++ = ((*src++) != (*srcB++)) ? 255 : 0; } break;
	case LT_MASK:   while (src < srcE) { *dst++ = ((*src++)  < (*srcB++)) ? 255 : 0; } break;
	case GT_MASK:   while (src < srcE) { *dst++ = ((*src++)  > (*srcB++)) ? 255 : 0; } break;
	case LE_MASK:	while (src < srcE) { *dst++ = ((*src++) <= (*srcB++)) ? 255 : 0; } break;
	case GE_MASK:	while (src < srcE) { *dst++ = ((*src++) >= (*srcB++)) ? 255 : 0; } break;
	case AND_OP:    while (src < srcE) {
		int a = (*src++);
		int b = (*srcB++);
		*dst++ = (a && b) ? 255 : 0; 
	} break;
	}

	return out;
}

void Plane::SaveAsPNG(const char* fileName) {
	if (this == NULL) { return; }

	unsigned char* data = new unsigned char[w * h];

	int* pln = GetPixels();

	for (int n = 0; n < w * h; n++) {
		int r = pln[n];
		if (r < 0) { r = 0; }
		if (r > 255) { r = 255; }
		data[n] = r;
	}

	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(fileName, w, h, 1, data, w);

	delete[] data;
}

Plane* Plane::SampleDown(bool x_, bool y_, EDownSample mode) {
	Plane* newPlane = new Plane((x_ ? w / 2 : w), (y_ ? h / 2 : h));

	int stepX = x_ ? 2 : 1;
	int stepY = y_ ? 2 : 1;

	int strideDst = x_ ? (w / 2) : w;
	int shiftXDst = x_ ? 1 : 0;
	int shiftYDst = y_ ? 1 : 0;

	// Source space.
	int* pSrc = GetPixels();
	int* pDst = newPlane->GetPixels();
	for (int y = 0; y < h; y += stepY) {
		int idxY = y * w;
		int idxDstY = (y >> shiftYDst)* strideDst;

		for (int x = 0; x < w; x += stepX) {
			int idx = x + idxY;

			int A = pSrc[idx];
			int B = pSrc[idx + 1];
			int C = pSrc[idx + w];
			int D = pSrc[idx + 1 + w];

			int v = A; // No interpolation, nearest.
			switch (mode) {
			case EDownSample::AVERAGE_BOX:
				if (x_ && y_) {
					v = (A + B + C + D) / 4; // Box filter.
				} else {
					if (x_) {
						v = (A + B) / 2;
					} else if (y_) {
						v = (A + C) / 2;
					}
				}
				break;
			case EDownSample::NEAREST_TL:
				v = A;
				break;
			case EDownSample::NEAREST_BR:
				if (x_ && y_) {
					v = D;
				} else {
					if (x_) {
						v = C;
					} else if (y_) {
						v = B;
					}
				}
				break;
			case EDownSample::MAX_BOX:
				{
					int a = A > B ? A : B;
					int b = C > D ? C : D;

					if (x_ & y_) {
						v = a > b ? a : b;
					} else {
						if (x_) {
							v = A > C ? A : C;
						} else if (y_) {
							v = a;
						}
					}
				}
				break;
			case EDownSample::MIN_BOX:
				{
					int a = A < B ? A : B;
					int b = C < D ? C : D;

					if (x_ & y_) {
						v = a < b ? a : b;
					} else {
						if (x_) {
							v = A < C ? A : C;
						} else if (y_) {
							v = a;
						}
					}
				}
				break;
			}

			pDst[idxDstY + (x >> shiftXDst)] = v;
		}
	}

	return newPlane;
}

void Plane::Fill(BoundingBox& r, int v) {
	for (int y = r.y; y < r.y + r.h; y++) {
		for (int x = r.x; x < r.x + r.w; x++) {
			pixels[x + y * w] = v;
		}
	}
}

void Plane::RemoveMask(Plane* toRemove) {
	int* dstPixels = toRemove->pixels;

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int idx = x + y * w;
			if (dstPixels[idx]) {
				pixels[idx] = 0;
			}
		}
	}
}

void Plane::FillOutside(BoundingBox& rKeep, int v) {
	int outR = rKeep.x + rKeep.w;
	int outB = rKeep.y + rKeep.h;
	for (int y=0;y<h;y++) {
		for (int x=0;x < w;x++) {
			if (x < rKeep.x || y<rKeep.y || x>= outR || y>= outB) {
				pixels[x + y * w] = 0;
			}
		}
	}
}

Plane* Plane::SampleUp(bool x_, bool y_, bool interpolate) {
	Plane* newPlane = new Plane((x_ ? w * 2 : w), (y_ ? h * 2 : h));

	int stepX = 1;
	int stepY = 1;

	int strideDst = x_ ? (w * 2) : w;
	int shiftYDst = y_ ? 1 : 0;
	int shiftXDst = x_ ? 1 : 0;

	// Source space.
	int* pSrc = GetPixels();
	int* pDst = newPlane->GetPixels();
	for (int y = 0; y < h; y += stepY) {
		int idxY    = y * w;
		int idxDstY = (y << shiftYDst) * strideDst;

		for (int x = 0; x < w; x += stepX) {
			int idx = x + idxY;

			// Sampling in Src.
			int A = pSrc[idx];
			int B = pSrc[idx + 1];				// Todo Duplicate if out
			int C = pSrc[idx + w];		
			int D = pSrc[idx + 1 + w];

			int v = A; // No interpolation, nearest.
			if (interpolate) {
				pDst[idxDstY + (x << shiftXDst)] = A;
				if (x_ && y_) {
					pDst[idxDstY + (x << shiftXDst) + 1]			= (A + B) / 2;
					pDst[idxDstY + strideDst + (x << shiftXDst)]	= (A + C) / 2;
					pDst[idxDstY + strideDst + (x << shiftXDst) + 1]= (A+B+C+D) / 4;
				} else {
					if (x_) {
						pDst[idxDstY + (x << shiftXDst) + 1] = (A + B) / 2;
					} else if (y_) {
						pDst[idxDstY + strideDst + (x << shiftXDst)] = (A + C) / 2;
					}
				}
			} else {
				// Nearest...
				if (x_) {
					if (y_) {
						pDst[idxDstY + strideDst + (x << shiftXDst)] = A;
						pDst[idxDstY + strideDst + (x << shiftXDst) + 1] = A;
					}
					pDst[idxDstY + (x << shiftXDst)] = A;
					pDst[idxDstY + (x << shiftXDst) + 1] = A;
				} else {
					if (y_) {
						pDst[idxDstY +             (x << shiftXDst)] = A;
						pDst[idxDstY + strideDst + (x << shiftXDst)] = A;
					}
				}
			}
		}
	}

	return newPlane;
}

Plane* Plane::ApplyDiff(ParsingOrder& parsingOrder) {
	Plane* newPlane = new Plane(w, h);
	int* dst = newPlane->GetPixels();
	int* src = GetPixels();

	parsingOrder.ResetParsing();
	bool isSubMarker;
	int prev = 0;
	while (parsingOrder.HasNextBlock(isSubMarker)) {
		BoundingBox& r = parsingOrder.GetCurrentBlock(false);
		int index = r.x + (r.y * w);
		if (isSubMarker) {
			dst[index] = src[index];
		}
		else {
			dst[index] = src[index] - prev;
		}
		prev = src[index];
	}

	return newPlane;
}

void	Plane::GetMinMax_Y(BoundingBox& rect, Plane* validPixel, Plane* smooth, int* min, int* max, int* unique) {
	int mn = 99999999;
	int mx = -99999999;
	bool isHalfX = validPixel->GetWidth() != this->GetWidth();
	bool isHalfY = validPixel->GetHeight() != this->GetHeight();
	int shiftValidX = isHalfX ? 1 : 0;
	int shiftValidY = isHalfY ? 1 : 0;

	int count[64*64]; // TODO Max 64x64 rect.
	int regCount = 0;

	int* pSrc = GetPixels();

	int maxY = (rect.y + rect.h);
	if (maxY > h) { maxY = h; }

	int maxX = (rect.x + rect.w);
	if (maxX > w) { maxX = w; }

	int* validSrc   = validPixel->GetPixels();
	int* smoothSrc  = smooth ? smooth->GetPixels() : NULL;

//	printf("-------------\n");

	for (int y = rect.y; y < maxY; y++) {
		// Duplicate bottom lines...
		int idxY = (y<<shiftValidY) * w;
		for (int x = rect.x; x < maxX; x++) {
			int validIdx = (x<<shiftValidX) + idxY;

			// Pixel is VALID only when :
			// - Not gradient (map 1/4 scale).
			// - Inside Valid pixel map.

			// VALID ONLY FOR Y
			bool valid;
			if (!isHalfX && !isHalfY) {
				// Full size.
				valid = validSrc[validIdx] && ((smooth==NULL) || !(smoothSrc[validIdx]));
			} else {
				valid = validSrc[validIdx];
				if (smooth) {
					bool hasGrad = smoothSrc[validIdx]; 
					if (isHalfX && isHalfY) {
						// Quarter.
						int idxL = validIdx; 
						bool a = validSrc[idxL];
						bool b = validSrc[idxL+1];
						bool c = validSrc[idxL+(w)];
						bool d = validSrc[idxL+(w)+1];

						// If any of the pixel is used and gradient not filling the Quarter area.
						valid = (!hasGrad) && (a|b|c|d);
					} else {
						if (isHalfX) {
							int idxL = validIdx; 
							bool a = validSrc[idxL];
							bool b = validSrc[idxL+1];
							valid = (!hasGrad) && (a|b);
						} else {
							int idxL = validIdx; 
							bool a = validSrc[idxL];
							bool b = validSrc[idxL+w];
							valid = (!hasGrad) && (a|b);
						}
					}
				}
			}

			if (valid) {
				int V = pSrc[x + (y*w)];

//				printf("[%i] = %i\n",x+(y*w),V);

				if (V < mn) { mn = V; }
				if (V > mx) { mx = V; }

				int n = 0;
				for (; n < regCount; n++) {
					if (count[n] == V) {
						break;
					}
				}
				if (regCount == n) {
					count[regCount++] = V;
				}
			}
		}
	}

	if (regCount) {
		*min = mn;
		*max = mx;
	} else {
		*min = 0;
		*max = 0;
	}
	*unique = regCount;
}

