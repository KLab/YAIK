#include "EncoderContext.h"

// External library to manipulate image files.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb_image/stb_image.h"
#include "../external/stb_image/stb_image_write.h"

// External library to do compression.
#include "../external/zstd/zstd.h"

// memset, memcpy
#include <memory.h>

// abs
#include <math.h>

// assert()
#include <cassert>

// -----------------------------
// [TODO CLEAN LATER] : Functions in framework.h
// -----------------------------
unsigned int nearest2Pow(unsigned int v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

unsigned int log2ui(unsigned int v) {
	int r = 0;
	while (v >>= 1) // unroll for more speed...
	{
		r++;
	}
	return r;
}

void RGBtoYCoCg(int R, int G, int B, int& oY, int& oCo,int& oCg) {

	// -255..+255
	int Co = R - B;
	// 0..255 + (-127..+127) => -127..382
	int tmp = B + Co / 2;
	// -382..+128
	int Cg = G - tmp;
	// -191..+64 + -127..384 = -318..+448
	int Y = tmp + Cg / 2;

	oY  = Y;  // 0..255
	oCo = Co/2; // -255..+255 -> -127..+127
	oCg = Cg/2; // -255..+255 -> -127..+127 Encoder could keep quality.
}

void YCoCgtoRGB(int Y, int Co, int Cg, int& oR, int& oG,int& oB) {
	Co *= 2;
	Cg *= 2;

	int tmp = Y - Cg / 2;
	int G = Cg + tmp;
	int B = tmp - Co / 2;
	int R = B + Co;

	oR = R;
	oG = G;
	oB = B;
}


// -------------------------------------------------------------------------------------------------
//    Local Functions to help debug / implement EncoderContext
// -------------------------------------------------------------------------------------------------

struct QuadCtx {
	Plane* planeAlpha;
	Plane* mipRGBMask;
	Image* image;
	int maxMipLevel; // 0=No mipmap, 1=First Mipmap.

	int boundingL;
	int boundingR;
	int boundingT;
	int boundingB;
};

void debug1BitAsPng(const char* dump, u8* buffer, int w, int h) {
	unsigned char* data = new unsigned char[w * h * 3];

	for (int n = 0; n < w * h; n++) {
		int v = (buffer[n >> 3] & (1<<(n & 7))) ? 255 : 0;

		int idx = n * 3;
		data[idx] = v;
		data[idx + 1] = v;
		data[idx + 2] = v;
	}

	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(dump, w, h, 3, data, w * 3);

	delete[] data;
}

int make1BitStream(Plane* plane, BoundingBox& r, Plane* skipPlane, unsigned char*& opt) {
	int sizeByteMax = (((r.w * r.h) + 7) >> 3)+1;
	if (opt == NULL) {
		opt = new unsigned char[sizeByteMax];
		memset(opt, 0, sizeByteMax);
	}

	int sizeByte;
	int bitPos = 0;
	int bytePos = 0;
	unsigned char* pBS = opt;

	int* planeP = plane->GetPixels();
	int* checkP = skipPlane ? skipPlane->GetPixels() : NULL;

	int validPixelCount = 0;

	for (int y = r.y; y < (r.y + r.h); y++) {
		for (int x = r.x; x < (r.x + r.w); x++) {
			int idx = x + y * plane->GetWidth();
			// All pixel if no skip plane, or only the one with 1 in it.
			if ((checkP == NULL) || (checkP[idx])) {
				validPixelCount++;
				int src = planeP[idx];
				*pBS |= (src & 1) << bitPos;
				bitPos++;
				if (bitPos == 8) {
					bitPos = 0;
					pBS++;
					*pBS = 0;
				}
			}
		}
	}

	// (Cast because warning, diff could be 64 bit, but our file won't be bigger than 4 GB)
	sizeByte = (int)((pBS - opt) + ((bitPos == 0)?0:1)); // Last byte is not used if bitPos is zero.
	return sizeByte;
}

bool quadRecursion(QuadCtx& ctx, int depth, int x, int y) {
	depth--;

	int sq      = 1 << depth;
	int sqChild = sq >> 1;

	// Own Size is 4 sq.

	// 1.Do clipping against image
	// 2.If alpha = 0

	if (depth > 0) { // Never go lower than 2x2 block.
		bool a, b, c, d;
		int u = x + sqChild;
		int v = y + sqChild;

		bool allowDown = v < ctx.planeAlpha->GetWidth();
		a = quadRecursion(ctx, depth, x, y);

		if (allowDown) {
			b = quadRecursion(ctx, depth, x, v);
		} else {
			b = true;
		}

		if (u < ctx.planeAlpha->GetWidth()) {
			c = quadRecursion(ctx, depth, u, y);
			if (allowDown) {
				d = quadRecursion(ctx, depth, u, v);
			}else {
				d = true;
			}
		} else {
			c = true;
			d = true;
		}

		bool res = a && b && c && d;
		if (res) {
			if (depth > ctx.maxMipLevel) {
				int imgW = ctx.image->GetWidth();
				int imgH = ctx.image->GetHeight();

				BoundingBox r;
				r.x = x;
				r.y = y;

				int sqW = sq;
				int sqH = sq;

				if (x + sq > imgW) { sqW = imgW - x; }
				if (y + sq > imgH) { sqH = imgH - y; }

				// Fill the mask...
				r.w = sqW;
				r.h = sqH;
				ctx.mipRGBMask->Fill(r, 0);
			}
		} else {
			if (depth == (ctx.maxMipLevel+1)) {
				// When we do NOT WRITE, update bounding box of untouched things...
				if (ctx.boundingL > x) { ctx.boundingL = x; }
				if (ctx.boundingT > y) { ctx.boundingT = y; }
				if (ctx.boundingR < x + sq) { ctx.boundingR = x + sq; }
				if (ctx.boundingB < y + sq) { ctx.boundingB = y + sq; }
			}
		}
		return res;
	} else {
		// Read Pixel
		// Has Alpha  ?
		return (ctx.planeAlpha->GetPixels()[ctx.planeAlpha->GetIndex(x, y)] == 0);
	}
}

class TileHistogram {
public:
	TileHistogram() :outside(false) {
		memset(count, 0, sizeof(int) * 15 * 15);
	}
	bool outside;
	int count[15][15];
};

TileHistogram tiles[90 * 2][64 * 2];

Plane* computeGradientMap(Plane* planeDeltaX, Plane* planeDeltaY) {
	Plane* nP = new Plane(planeDeltaX->GetWidth(), planeDeltaY->GetHeight());

#define MAX(x,y) (((x) > (y)) ? (x) : (y))
	for (int y = 0; y < planeDeltaX->GetHeight(); y++) {
		for (int x = 0; x < planeDeltaX->GetWidth(); x++) {
			int idx = planeDeltaX->GetIndex(x, y);
			// int deltaX = 0;
			int deltaX = planeDeltaX->GetPixels()[idx];
			//			int deltaY = 0;
			int deltaY = planeDeltaY->GetPixels()[idx];
			// 0,-1,+1,-2,+2,-3,+3,+4 ?
			int absDelta = MAX(abs(deltaX), abs(deltaY));

			//			DebugInfo2->setPixel(x, y, absDelta * 16, absDelta * 16, absDelta * 16);
			if ((deltaX >= -7 && deltaX <= 7) && (deltaY >= -7 && deltaY <= 7)) {
				tiles[y >> 2][x >> 2].count[deltaX + 7][deltaY]++;
			}
			else {
				tiles[y >> 2][x >> 2].outside = true;
			}

			nP->GetPixels()[idx] = (absDelta <= 6);
		}
	}

	return nP;
}

Plane* computeGradientMap(Plane* source) {
	LeftRightOrder	lr(source, 1, 1);
	TopDownOrder	td(source, 1, 1);

	Plane* ddx = source->ApplyDiff(lr);
	Plane* ddy = source->ApplyDiff(td);

	Plane* out = new Plane(source->GetWidth(), source->GetHeight());
	for (int y = 0; y < source->GetHeight(); y++) {
		for (int x = 0; x < source->GetWidth(); x++) {
			int idx = out->GetIndex(x, y);
			int v = MAX(abs(ddx->GetPixels()[idx]), abs(ddy->GetPixels()[idx]));
			out->GetPixels()[idx] = v < 4 ? 255 : 0;
		}
	}

	delete ddx;
	delete ddy;

	return out;
}

struct Stats {
	int bitCount;
	float distError;
	int startMode;
	int mode;
};

	struct HistoEntry {
		u8 count;
		u8 index;
	};

struct TileInfo {
	u8 type;
	u8 base;
	u8 range;
	int values[64];
	u8* indexStream;
	u8 valueCount;
	int indexGlobal;
	bool useSigned;
};

class DynamicTile {
public:
	bool invalidTable;
	int minV;
	int maxV;
	int tblMin;
	int tblMax;

	int base7Bit;
	int distance6Bit;
	int BN;
	int DistNorm;

	void buildTable(int min_, int max_);

	int LUTLinear3B[ 8];
	int LUTLinear4B[16];
	int LUTExp3B   [ 8];
	int LUTExp4B   [16];
	int LUTLog3B   [ 8];
	int LUTLog4B   [16];
	int LUTSlope3B [ 8];
	int LUTSlope4B [16];

	inline int	GetModeCount	() { return 6; }
	inline int* GetTable		(int mode, int& count, int& bitCount) {
		switch (mode) {
		case 0:
			bitCount = 4;
			count    = 16;
			return LUTLinear4B;
		case 1:
			bitCount = 4;
			count	 = 16;
			return LUTExp4B;
		case 2:
			bitCount = 4;
			count    = 16;
			return LUTLog4B;
		case 3:
			bitCount = 3;
			count    = 8;
			return LUTLinear3B;
		case 4:
			bitCount = 3;
			count    = 8;
			return LUTExp3B;
		case 5:
			bitCount = 3;
			count    = 8;
			return LUTLog3B;
		default:
			bitCount = 0;
			count    = 0;
			return NULL;
		}
	}
};


// -------------------------------------------------------------------------
// Base Value for a given tile values, Multiple of 2 better...
#define MAX_BASE_RANGE		(224)
#define MIN_DIFF_RANGE		(32)
#define BIT_COUNT_BASE		(6)
#define BIT_COUNT_RANGE		(7)
// -------------------------------------------------------------------------
#define BASE_MAX_ENC		((1<<BIT_COUNT_BASE)-1)
#define RANGE_MAX_ENC		((1<<BIT_COUNT_RANGE)-1)

int MinRangeEncode(int range8Bit) {
	assert(range8Bit >= 0);
	assert(range8Bit <= 255);


	if (range8Bit > MAX_BASE_RANGE) { range8Bit = MAX_BASE_RANGE; }
	const int rounding = MAX_BASE_RANGE / 2;

	// Ex : ((8 bit) * 127 + rounding) / 224;
	return ((range8Bit * BASE_MAX_ENC)+rounding) / MAX_BASE_RANGE;
}

int MinRangeDecode(int range7Bit) {
	// Ex : (7 bit) * 224 / 127
	return	(range7Bit * MAX_BASE_RANGE) / BASE_MAX_ENC;
}

int DiffRangeEncode(int diff8Bit, int renormalizedBase) {
	// Can not encode using smaller difference than 16.
	if (diff8Bit < MIN_DIFF_RANGE) { diff8Bit = MIN_DIFF_RANGE; }

	// Distance Normalized = [(Distance * ((255-16)-BN)) / 63] + 16
	// Distance Normalized - 16 = [(Distance * ((255-16)-BN)) / 63]
	// (Distance Normalized - 16)*63 = (Distance * ((255-16)-BN))
	// Distance = (Distance Normalized - 16)*63/((255-16)-BN) = 
	int scale        = ((255 - MIN_DIFF_RANGE) - renormalizedBase);
	// Ceil rounding necessary : We need the widest same table possible for multiple diff in 8 bit range.
	//                           So our 6 bit range need to take the widest possible for multiple 8 bit cases as input.
	int rounding     = (scale - 1);
	int distance6Bit = (((diff8Bit - MIN_DIFF_RANGE) * RANGE_MAX_ENC)+ rounding) / scale;
	return distance6Bit;
}

int DiffRangeDecode(int diff6Bit, int renormalizedBase) {
	int scale        = ((255 - MIN_DIFF_RANGE) - renormalizedBase);
	return ((diff6Bit * scale) / RANGE_MAX_ENC) + MIN_DIFF_RANGE;
}

void DynamicTile::buildTable(int min_, int max_) {
	if ((min_ < 0) || (max_ < min_) || (max_ > 255)) {
		invalidTable = true;
		return;
	}

	assert(min_ >=    0);
	assert(max_ >= min_);
	assert(max_ <=  255);

	if (min_ > MAX_BASE_RANGE) { min_ = MAX_BASE_RANGE; }
	if (max_ > 255) { max_ = 255; }
	int diff = max_ - min_;
	if (diff < 16) {
		diff = 16;
	}

	// Encode base value to bit stream ID.
	base7Bit		= MinRangeEncode(min_);
	// Renormalized Base
	int BN          = MinRangeDecode(base7Bit);


	// Distance Normalized = [(Distance * ((255-16)-BN)) / 63] + 16
	distance6Bit	= DiffRangeEncode(diff, BN);
	int rangeDecode = DiffRangeDecode(distance6Bit, BN);
	minV			= min_;
	maxV			= max_;
	tblMin			= BN;
	tblMax			= BN + rangeDecode;

	float DistNormF = (float)rangeDecode;

	// TODO : Optimization : compute Log / Exp / Spline with a SINGLE BIT LUT, then SCALE DOWN.
	// TODO : May be optimize table to get LESS 

	// 4 Bit Table
	for (int input = 0; input < 16; input++) {
		float pos   = input / 15.0f;

		float LinearNormV = pos;
		float ExpNormV    = powf(pos, 1.4f);
		float LogNormV    = 1.0f - powf((1.0f - pos), 1.4f);
//		float 
		float outLinear   = LinearNormV * DistNormF;
		LUTLinear4B[input] = (int)(BN + outLinear); // Table 8 Bit fixed point. 

		float outExp      = ExpNormV * DistNormF;
		LUTExp4B   [input] = (int)(BN + outExp); // Table 8 Bit fixed point. 

		float outLog	  = LogNormV * DistNormF;
		LUTLog4B   [input] = (int)(BN + outLog); // Table 8 Bit fixed point. 
	}

	// 3 Bit Table
	for (int input = 0; input < 8; input++) {
		float pos = input / 7.0f;

		float LinearNormV = pos;
		float ExpNormV = powf(pos, 1.4f);
		float LogNormV = 1.0f - powf((1.0f - pos), 1.4f);

		//		float 
		float outLinear = LinearNormV * DistNormF;
		LUTLinear3B[input] = (int)(BN + outLinear); // Table 8 Bit fixed point. 

		float outExp = ExpNormV * DistNormF;
		LUTExp3B[input] = (int)(BN + outExp); // Table 8 Bit fixed point. 

		float outLog = LogNormV * DistNormF;
		LUTLog3B[input] = (int)(BN + outLog); // Table 8 Bit fixed point. 
	}

	invalidTable = false;
}

DynamicTile fullTables[255][255];
void DynamicTileEncoderTable() {
	/*
		Type of tile :

		Flat Tile :
		----------------
		8 Bit	4x4 ? 8x8 ?

		Gradient Tile :
		----------------
		Gradient Tile 8x8 (40%) = 22-25 bit per block.
		Gradient Tile 8x8 (60%) = 23    bit per block.
			2 Color            = 8 bit
			Color A/B position = 4 bit

		Dynamic Tile :
		----------------
		Range Coding : Header + 3/4 bit per pixel in tile.
			Bit Table (2 bit) : different distribution of number in range.
				0 : Linear
				1 : Exp
				2 : Log
				3 : Slope (less in middle)
				Table always ALLOW 0 and MAX. (1.0 x factor)

			Base (7 bit) (Renormalized between 0 and 192) : Step = 192/127
				BN = Renormalized Base = ((Base * 192)/127)

				Note : Allow 0, and allow 255 IN ALL CASES thanks to DISTANCE.

			Distance 6 (0..63) :
				Distance Normalized = [(Distance * ((255-16)-BN)) / 63] + 16

				Note : meaning less to have a range less than 16 for a block. (0..15 not encoded)
				Note : As the BN is high, there is no point in having a range end (BN + Distance Max) > 255.
				So we optimize the Distance Range and get more precision for the same bit count of range and bit stream.
				=> No wasted bit
	 */
	for (int minV = 0; minV < 256; minV++) {
		for (int maxV = 0; maxV < 256; maxV++) {
			fullTables[minV][maxV].buildTable(minV, maxV);
		}
	}
}

int GetTileDynamic_Y(TileInfo& outTile, Plane* src, Plane* validPixel, Plane* smooth, BoundingBox& rect, int min, int max, Stats& stats) {

	// No pixel used inside the tile at all.
	if (min == 99999999) {
		outTile.valueCount = 0;
		return -1;
	}

	bool isHalfX = validPixel->GetWidth()  != src->GetWidth ();
	bool isHalfY = validPixel->GetHeight() != src->GetHeight();
	int  shiftHalfX = isHalfX ? 1:0;
	int  shiftHalfY = isHalfY ? 1:0;

	bool useSigned = false;
	bool tableNotFound = true;

	// Use same table, shift things... We are using -127..+127 entries.
	if (min < 0) {
		min += 128;
		max += 128;
		useSigned = true;
	}
	DynamicTile& tbl = fullTables[min][max];

	int* srcp = src->GetPixels();
	int* validPixelP = validPixel->GetPixels();
	int* SMP         = smooth ? smooth->GetPixels() : NULL;

	int bestMode = -1;
	int bestbitCount;
	float bestErrorDist = 99999999.0f;

	int bTile[64];
	int bTileCode[64];
	int origTile[64];

	int w = validPixel->GetWidth();

	for (int n = 0; n < 64; n++) {
		origTile[n] = -999;
		outTile.values[n] = -999;
	}

	int bestTile[64];
	int bestTileCode[64];
	HistoEntry histogram[16];
	HistoEntry bestHisto[16];

	for (int n=0; n < 16; n++) {
		bestHisto[n].count = 0;
		bestHisto[n].index = n;
	}

	int lx = rect.x;
	int ly = rect.y;

	for (int mode = stats.startMode; mode < tbl.GetModeCount(); mode++) {
		int curve  = mode % 3;
		int mode3B = mode / 3; // 4 bit vs 3 bit.
		float errorDist = 0.0f;
		int count;
		int bitCount;
		int* LUT = tbl.GetTable(mode,count,bitCount);

		for (int n=0; n < 16; n++) {
			histogram[n].count = 0;
			histogram[n].index = n;
		}

		for (int n = 0; n < 64; n++) {
			bTile[n] = -999;
		}

		for (int y = 0; y < rect.h; y++) {
			for (int x = 0; x < rect.w; x++) {
				int sx = x + lx;
				int sy = y + ly;
				int idx = (sx<<shiftHalfX) + ((sy<<shiftHalfY) * w);

				// VALID FOR Y,Co,Cg Sampling
				bool valid;
				if (!isHalfX && !isHalfY) {
					// Full size.
					valid = validPixelP[idx] && (smooth==NULL || (!(SMP[idx])));
				} else {
					valid = validPixelP[idx];
					if (smooth) {
						bool hasGrad = SMP[idx]; 
						if (isHalfX && isHalfY) {
							// Quarter.
							int idxL = idx;
							bool a = validPixelP[idxL];
							bool b = validPixelP[idxL+1];
							bool c = validPixelP[idxL+w];
							bool d = validPixelP[idxL+w+1];

							// If any of the pixel is used and gradient not filling the Quarter area.
	//						valid = (!hasGrad) && (a|b|c|d);
							valid = (!hasGrad) && (a&b&c&d);
						} else {
							if (isHalfX) {
								int idxL = idx;
								bool a = validPixelP[idxL];
								bool b = validPixelP[idxL+1];
	//							valid = (!hasGrad) && (a|b);		<--- Too much high quality.
								valid = (!hasGrad) && (a&b);
							} else {
								int idxL = idx;
								bool a = validPixelP[idxL];
								bool b = validPixelP[idxL+w];
	//							valid = (!hasGrad) && (a|b);
								valid = (!hasGrad) && (a&b);
							}
						}
					}
				}


				if (valid) {
					int valueOriginal = srcp[sx+(sy*src->GetWidth())] + (useSigned ? 128 : 0);
					origTile[x + (y << 3)] = valueOriginal;
					int minDiff = 99999;
					int foundLUT;
					int valueFound;

					// TODO : Replace with binary search !
					for (int n = 0; n < count; n++) {
						int diffRead = abs(LUT[n] - valueOriginal);
						// Closest record in LUT.
						if (diffRead < minDiff) {
							minDiff    = diffRead;
							foundLUT   = n;
							valueFound = LUT[n];
						}
					}

					// Cumulated % error
					if (valueOriginal != 0) {
						errorDist += ((float)minDiff / (float)valueOriginal);
					} // else errorDist += 0. No problem.

					bTile    [x + (y << 3)] = valueFound;
					bTileCode[x + (y << 3)] = foundLUT;
					histogram[foundLUT].count++;
				}
			}
		}

		// printf("Purc total error : %f , Purc Avg %f Mode:%i\n", (errorDist * 100), ((errorDist * 100) / 64), mode);

		if (errorDist <= bestErrorDist) { // <= allow 3 bit mode to override...
			bestErrorDist = errorDist;
			bestMode      = mode;
			bestbitCount  = bitCount;
			memcpy(bestTile    , bTile    , 64 * sizeof(int));
			memcpy(bestTileCode, bTileCode, 64 * sizeof(int));
			memcpy(bestHisto, histogram, 16 * sizeof(HistoEntry));
			tableNotFound = false;
		}
	}

	// printf("[%i,%i] Best Purc total error : %f , Purc Avg %f Mode:%i Tbl[%i,%i](%i Bit)\n", rect.x, rect.y, (bestErrorDist * 100), ((bestErrorDist * 100) / 64), bestMode, tbl.base7Bit, tbl.distance6Bit, bestbitCount);

	outTile.valueCount = 0;

	if (!tableNotFound) {
		outTile.type	= bestMode;
		outTile.base	= tbl.base7Bit;
		outTile.range	= tbl.distance6Bit;
		outTile.useSigned	= useSigned;

		// ------------- Compressor ---------------
		// Level 
		int sumA = 0;
		int sumB = 0;



		// 1 bit XOR.
		/*
		for (int n =0; n < 8; n++) { sumA += bestHisto[n].count; }
		for (int n =0; n < 8; n++) { sumB += bestHisto[n+8].count; }

		if (sumA < sumB) {
			for (int n=0; n <8; n++) {
				HistoEntry tmp = bestHisto[n];
				bestHisto[n] = bestHisto[n+8];
				bestHisto[n+8] = tmp;
			}

			// Reverse.
			for (int n=0; n < 64; n++) {
				bestTileCode[n] ^= 8;
			}
		}
		*/
		// Replace
#if 0
		bubble_sort(bestHisto, 16);	// Perfect Soft

		// 1 Bit Swap + 10 bit = 11 bit info.
		// bubble_sort(&bestHisto[0], 4);	// Perfect soft top 2 block.
		// bubble_sort(&bestHisto[4], 4);
		for (int n=0; n < 64; n++) {
			int v = bestTileCode[n];
			for (int i=0; i < 16; i++) {
				if (bestTileCode[n] == bestHisto[i].index) {
					bestTileCode[n] = i;
					break;
				}
			}
		}
#endif
#if 0
		// HERE USE SUM FOR BLOCK
		for (int n =0; n < 8; n++) { sumA += bestHisto[n].count; }
		for (int n =0; n < 8; n++) { sumB += bestHisto[n+8].count; }

		if (sumA < sumB) {	// Swap 8
			for (int n=0; n <8; n++) {
				HistoEntry tmp = bestHisto[n];
				bestHisto[n] = bestHisto[n+8];
				bestHisto[n+8] = tmp;
			}

			// Reverse.
			for (int n=0; n < 64; n++) {
				bestTileCode[n] ^= 8;
			}

			int sumA = 0;
			int sumB = 0;
			for (int n =0; n < 4; n++) { sumA = maxfct(sumA,bestHisto[n].count);   }
			for (int n =0; n < 4; n++) { sumB = maxfct(sumB,bestHisto[n+4].count); }

			if (sumA < sumB) {	// Swap 4
				for (int n=0; n <4; n++) {
					HistoEntry tmp = bestHisto[n];
					bestHisto[n] = bestHisto[n+4];
					bestHisto[n+4] = tmp;
				}

				// Reverse in left side
				for (int n=0; n < 64; n++) {
					if (bestTileCode[n] < 8) {
						bestTileCode[n] ^= 4;
					}
				}

				int sumA = 0;
				int sumB = 0;
				for (int n =0; n < 2; n++) { sumA = maxfct(sumA,bestHisto[n].count);   }
				for (int n =0; n < 2; n++) { sumB = maxfct(sumB,bestHisto[n+2].count); }

				if (sumA < sumB) {
					for (int n=0; n <2; n++) {
						HistoEntry tmp = bestHisto[n];
						bestHisto[n] = bestHisto[n+2];
						bestHisto[n+2] = tmp;
					}

					// Reverse in left side
					for (int n=0; n < 64; n++) {
						if (bestTileCode[n] < 4) {
							bestTileCode[n] ^= 2;
						}
					}

					if (bestHisto[0].count < bestHisto[1].count) {
						HistoEntry tmp = bestHisto[0];
						bestHisto[0] = bestHisto[1];
						bestHisto[1] = tmp;

						for (int n=0; n < 64; n++) {
							if (bestTileCode[n] < 2) {
								bestTileCode[n] ^= 1;
							}
						}
					}
				} else {
					if (bestHisto[0].count < bestHisto[1].count) {
						HistoEntry tmp = bestHisto[0];
						bestHisto[0] = bestHisto[1];
						bestHisto[1] = tmp;

						for (int n=0; n < 64; n++) {
							if (bestTileCode[n] < 2) {
								bestTileCode[n] ^= 1;
							}
						}
					}
				}
			} else {
				int sumA = 0;
				int sumB = 0;
				for (int n =0; n < 2; n++) { sumA = maxfct(sumA,bestHisto[n].count);   }
				for (int n =0; n < 2; n++) { sumB = maxfct(sumB,bestHisto[n+2].count); }

				if (sumA < sumB) {
					for (int n=0; n <2; n++) {
						HistoEntry tmp = bestHisto[n];
						bestHisto[n] = bestHisto[n+2];
						bestHisto[n+2] = tmp;
					}

					// Reverse in left side
					for (int n=0; n < 64; n++) {
						if (bestTileCode[n] < 4) {
							bestTileCode[n] ^= 2;
						}
					}

					if (bestHisto[0].count < bestHisto[1].count) {
						HistoEntry tmp = bestHisto[0];
						bestHisto[0] = bestHisto[1];
						bestHisto[1] = tmp;

						for (int n=0; n < 64; n++) {
							if (bestTileCode[n] < 2) {
								bestTileCode[n] ^= 1;
							}
						}
					}
				}
			}
		} else {
			int sumA = 0;
			int sumB = 0;
			for (int n =0; n < 4; n++) { sumA = maxfct(sumA,bestHisto[n].count);   }
			for (int n =0; n < 4; n++) { sumB = maxfct(sumB,bestHisto[n+4].count); }

			if (sumA < sumB) {	// Swap 4
				for (int n=0; n <4; n++) {
					HistoEntry tmp = bestHisto[n];
					bestHisto[n] = bestHisto[n+4];
					bestHisto[n+4] = tmp;
				}

				// Reverse in left side
				for (int n=0; n < 64; n++) {
					if (bestTileCode[n] < 8) {
						bestTileCode[n] ^= 4;
					}
				}

				int sumA = 0;
				int sumB = 0;
				for (int n =0; n < 2; n++) { sumA = maxfct(sumA,bestHisto[n].count);   }
				for (int n =0; n < 2; n++) { sumB = maxfct(sumB,bestHisto[n+2].count); }

				if (sumA < sumB) {
					for (int n=0; n <2; n++) {
						HistoEntry tmp = bestHisto[n];
						bestHisto[n] = bestHisto[n+2];
						bestHisto[n+2] = tmp;
					}

					// Reverse in left side
					for (int n=0; n < 64; n++) {
						if (bestTileCode[n] < 4) {
							bestTileCode[n] ^= 2;
						}
					}

					if (bestHisto[0].count < bestHisto[1].count) {
						HistoEntry tmp = bestHisto[0];
						bestHisto[0] = bestHisto[1];
						bestHisto[1] = tmp;

						for (int n=0; n < 64; n++) {
							if (bestTileCode[n] < 2) {
								bestTileCode[n] ^= 1;
							}
						}
					}
				} else {
					if (bestHisto[0].count < bestHisto[1].count) {
						HistoEntry tmp = bestHisto[0];
						bestHisto[0] = bestHisto[1];
						bestHisto[1] = tmp;

						for (int n=0; n < 64; n++) {
							if (bestTileCode[n] < 2) {
								bestTileCode[n] ^= 1;
							}
						}
					}
				}
			} else {
				int sumA = 0;
				int sumB = 0;
				for (int n =0; n < 2; n++) { sumA = maxfct(sumA,bestHisto[n].count);   }
				for (int n =0; n < 2; n++) { sumB = maxfct(sumB,bestHisto[n+2].count); }

				if (sumA < sumB) {
					for (int n=0; n <2; n++) {
						HistoEntry tmp = bestHisto[n];
						bestHisto[n] = bestHisto[n+2];
						bestHisto[n+2] = tmp;
					}

					// Reverse in left side
					for (int n=0; n < 64; n++) {
						if (bestTileCode[n] < 4) {
							bestTileCode[n] ^= 2;
						}
					}

					if (bestHisto[0].count < bestHisto[1].count) {
						HistoEntry tmp = bestHisto[0];
						bestHisto[0] = bestHisto[1];
						bestHisto[1] = tmp;

						for (int n=0; n < 64; n++) {
							if (bestTileCode[n] < 2) {
								bestTileCode[n] ^= 1;
							}
						}
					}
				}
			}
		}
#endif
		// --------------------------------------------------------------



		for (int y = 0; y < rect.h; y++) {
			for (int x = 0; x < rect.w; x++) {
				int idx = (x + lx) + (y + ly) * src->GetWidth();

				int v = bestTile[x + (y << 3)];
				if (v != -999) {
					if (outTile.indexGlobal & 1) {
						outTile.indexStream[outTile.indexGlobal>>1] |= bestTileCode[x + (y << 3)]<<4;
					} else {
						outTile.indexStream[outTile.indexGlobal>>1] |= bestTileCode[x + (y << 3)];
					}
					outTile.indexGlobal++;
					outTile.values[x + (y<<3)] = v;
					outTile.valueCount++;
				}
			}
		}

		/*
		for (int n=0; n <= 15; n++) {
			if (n==15) {
				printf("%i\n",bestHisto[n].count);
			} else {
				printf("%i,",bestHisto[n].count);
			}
		}
		*/
	} else {
		printf("WHAT ?!");
		while (1) {
			// ...
		}
	}

	stats.bitCount	= bestbitCount;
	stats.distError = bestErrorDist;
	stats.mode		= bestMode;
	return bestMode;
}

int GetTileEncode_Y(TileInfo& outTile, Plane* src, Plane* validPixel, Plane* quarterSmooth, Image* debug, BoundingBox& rect, int& bitCountTile, Stats& result) {
	int min, max, count;
	src->GetMinMax_Y(rect, validPixel, quarterSmooth, &min, &max, &count);

	int res = GetTileDynamic_Y(outTile, src, validPixel, quarterSmooth, rect, min, max, result);
	bitCountTile = ((res >= 0) ? 15 : 0) + (result.bitCount * outTile.valueCount);
	return res;
}

// -------------------------------------------------------------------------------------------------
//    EncoderContext
// -------------------------------------------------------------------------------------------------


bool EncoderContext::LoadImagePNG(const char* fileName) {
	// Create all the necessary LUT for internal encoding.
	DynamicTileEncoderTable();

	original = Image::LoadPNG(fileName);
	return (original != NULL);
}

void EncoderContext::MipPrefilter(bool active) {
	BoundingBox fullSize; fullSize.x = 0; fullSize.y = 0; fullSize.w = original->GetWidth(); fullSize.h = original->GetHeight();
	mipmapMask = new Plane(original->GetWidth(), original->GetHeight());

	if (original->HasAlpha()) {
		int maxSize = MAX(original->GetWidth(), original->GetHeight());
		int mapLevelMax = log2ui(maxSize);
		QuadCtx ctx;

		ctx.image		= original;
		ctx.planeAlpha	= original->GetPlane(3);
		ctx.mipRGBMask	= mipmapMask;

		int* arrPix = ctx.mipRGBMask->GetPixels();
		for (int n = 0; n < original->GetWidth() * original->GetHeight(); n++) {
			arrPix[n] = 255;
		}

		ctx.maxMipLevel = 3;

		// Decoder does not suppport other format for now...
		assert(ctx.maxMipLevel >= 2 && ctx.maxMipLevel <= 5);

		ctx.boundingL	= 9999999;
		ctx.boundingT	= 9999999;
		ctx.boundingR	= -1;
		ctx.boundingB	= -1;

		quadRecursion(ctx, mapLevelMax+1, 0, 0);

		boundX0			= ctx.boundingL;
		boundX1			= ctx.boundingR;
		boundY0			= ctx.boundingT;
		boundY1			= ctx.boundingB;
		mipMapTileSize	= 1<<(ctx.maxMipLevel+1);

		// If we have a surface smaller than the full image...
		if ((boundX0 != 0) || (boundY0 != 0) || (boundX1 != original->GetWidth()) || (boundY1 != original->GetHeight())) {
			int tileSizeBit = ctx.maxMipLevel + 1;
			int tileSize = 1 << tileSizeBit;

			int tileBX0 = boundX0 >> tileSizeBit;
			int tileBX1 = boundX1 >> tileSizeBit;
			int tileBY0 = boundY0 >> tileSizeBit;
			int tileBY1 = boundY1 >> tileSizeBit;

			int tileWB = tileBX1 - tileBX0;
			int tileHB = tileBY1 - tileBY0;

			int sizeByte = ((tileWB * tileHB)+7) / 8;
			unsigned char* bitmap = new unsigned char[sizeByte];
			memset(bitmap, 0, sizeByte);
			
			// Jump tile by tile, can not use make1BitStream.
			// And use bounding box.
			int bitPos = 0;
			int* src = ctx.mipRGBMask->GetPixels();

			remainingPixels = 0;

			for (int y = 0; y < tileHB; y++) {
				for (int x = 0; x < tileWB; x++) {
					int v = src[((x + tileBX0) * tileSize) + (((y + tileBY0) * tileSize) * original->GetWidth())];
					if (v != 0) {
						// Stream store map : 1 bit per tile.
						bitmap[bitPos >> 3] |= (1 << (bitPos & 7));
						remainingPixels += (tileSize * tileSize);
					}
					bitPos++;
				}
			}

			ctx.mipRGBMask->SaveAsPNG("EncoderDebug\\MipmapMask.png");
			debug1BitAsPng("EncoderDebug\\MIPMAPENC.png", bitmap, tileWB, tileHB);

			/* NO COMPRESSION FOR NOW
			unsigned char* pZStdStream = new unsigned char[sizeByte * 2]; // Max in case...
			memset(pZStdStream, 0, sizeByte * 2);

			int lastSize = 999999999;

			int n = 12;
			int lastCompressorRate;
			for (; n <= 19; n++)
			{
				size_t result = ZSTD_compress(pZStdStream, sizeByte * 2, bitmap, sizeByte, n);
				if (ZSTD_isError(result)) {
					//
					printf("Error");
				} else {
					if (lastSize > result) {
						lastSize = result;
						lastCompressorRate = n;
					} else {
						// Size goes up again... use previous compression.
						break;
					}
				}
				// [TODO]
				// --------
				// [Write Header:Chunk ID 32 Bit, Block Length : 32 bit][64 bit info for future extensions][Write Max MipLevel+1][Write Bounding Box X,Y,W,H : 2x4 8 Byte]
				// ... Then ZSTD Stream ...
				// --------

				printf("Mipmap Info Header [%i byte]\n", result);
			}

			size_t result = ZSTD_compress(pZStdStream, sizeByte * 2, bitmap, sizeByte, lastCompressorRate);
			*/

			HeaderBase headerTag;
			headerTag.tag.tag8[0] = 'M';
			headerTag.tag.tag8[1] = 'I';
			headerTag.tag.tag8[2] = 'P';
			headerTag.tag.tag8[3] = 'M';
			int baseSize = (sizeof(MipmapHeader) + sizeByte);
			headerTag.length	  = ((baseSize + 3) >> 2) <<2;	// Round multiple of 4.
			u8 pad[3] = { 0,0,0 };
			int padding = headerTag.length - baseSize;


			MipmapHeader headerMip;
			headerMip;

			fwrite(&headerTag, sizeof(HeaderBase)	, 1, outFile);
			
			headerMip.mipmapLevel = (ctx.maxMipLevel + 1);
			assert(headerMip.mipmapLevel >= 3); // Decoder does not support.

			headerMip.version		= 1;
			headerMip.bbox.x		= tileBX0;
			headerMip.bbox.y		= tileBY0;
			headerMip.bbox.w		= tileWB;
			headerMip.bbox.h		= tileHB;

			fwrite(&headerMip, sizeof(MipmapHeader)	, 1, outFile);
			fwrite(bitmap, 1, sizeByte, outFile);
			if (padding) {
				fwrite(pad, 1, padding, outFile);
			}

			// delete[] pZStdStream;
			delete[] bitmap;
		} else {
			mipmapMask->Fill(fullSize, 255);
			remainingPixels = boundX1 * boundY1;
		}
#if 0
		// [DEBUG STUFF]
		// original->replacePlane(0, ctx.plane, false); // Alpha mask as RED

		Rect r;
		r.x = 0;
		r.y = 0;
		r.w = 1024;
		r.h = 1024;

		ctx.plane = new Plane(1024, 1024);
		original->replacePlane(3, ctx.plane, false);
		ctx.plane->Fill(r, 255);
		original->savePNG("DebugImageMipfill.png", NULL);
#endif
	} else {
		boundX0 = 0;
		boundY0 = 0;
		boundX1 = original->GetWidth();
		boundY1 = original->GetHeight();
		mipmapMask->Fill(fullSize, 255);
		remainingPixels = boundX1 * boundY1;
	}
}

void EncoderContext::ProcessAlpha(bool force8Bit) {
	// --------------------------------------------
	//
	//  Encoding default parameters.....
	//
	bool useInverse = true;
	//
	// --------------------------------------------


	int w = original->GetWidth();
	int h = original->GetHeight();

	// Alpha Section : 0 bit, 1 bit or 6 bit.
	if (original->HasAlpha()) {
		//
		// [Reduce 8 Bit -> 6 Bit first, extract if 1 bit alpha]
		//
		Plane* alpha = original->GetPlane(3);

		// Compute alpha bounding box...
		int bL = 9999999;
		int bT = 9999999;
		int bB = -1;
		int bR = -1;

		// Search sub smaller alpha map using mipmap RGB bounding box.
		for (int y = boundY0; y < boundY1; y++) {
			for (int x = boundX0; x < boundX1; x++) {
				int* p = alpha->GetPixels();
				int  v = p[x + y*w];
				if (v>>2) { // Not ZERO in 6 bit !!! Graphic people allow alpha like 1,2,3... Sigh...
					if (bL > x) { bL = x; }
					if (bT > y) { bT = y; }
					if (bR < x) { bR = x; }
					if (bB < y) { bB = y; }
				}
			}
		}

		bR++;
		bB++;

		// Bounding box rounded to 4 pixel block : FASTER DECODER !
		// Decoder per block unrolled, proper alignment per line in stream.
		// Works too as we ignore per tile based. (tile 8 pixel wide)
		bL = (bL >> 2) << 2;
		bR = ((bR + 3) >> 2) << 2;

		bool isAll1			= true;
		bool isAll0			= true;
		bool isAnalogAlpha	= false;
// #define DELTA_ENCODING

#ifndef DELTA_ENCODING
		const int BIT_SIZE = force8Bit ? 8 : 6;
#else
		int lastV = 0;
		const int BIT_SIZE = 8;
#endif // !DELTA_ENCODING

		// 6 bit stream.
		int streamW = bR - bL;
		int streamH = bB - bT;

		int bitSteamDecompSize = BIT_SIZE * streamW * streamH;
		int byteSizeDecomp     = (bitSteamDecompSize + 7) >> 3;

		signed char* pBitStream = new signed char[byteSizeDecomp];
		signed char* pBS = pBitStream;

		int state = 0;
		int index = 0;

		//
		// Encode Alpha using only pixel in Mipmask.
		//

		bool ignoreMipmapMaskOnlyPixels = force8Bit; // If 8 Bit, FULL MAP for performance decode.

		for (int y = bT; y < bB; y++) {
			for (int x = bL; x < bR; x++) {
				int* p = alpha->GetPixels();
				int idx = x + y * w;
				p += idx;
				unsigned char v = force8Bit ? *p : (*p)>>2;
				unsigned char v8=*p;

				if ((v8 != 255) && (v8 > 0)) {
					isAnalogAlpha	= true;
					isAll1			= false;
					isAll0			= false;
				} else {
					if (v8 == 0) {
						isAll1 = false;
					}
					if (v8 == 255) {
						isAll0 = false;
					}
				}

				// Only in Mipmask !
				if (ignoreMipmapMaskOnlyPixels || (mipmapMask->GetPixels()[idx] != 0)) {
#ifndef DELTA_ENCODING
					if (force8Bit) {
						// 8 Bit channel.
						*pBS++ = *p;
					} else {
						int streamV = useInverse ? (63-v) : v;
					// Could probably unroll...
						switch (state) {
							// ..xxxxxx
						case 0: *pBS = streamV; state = 1; break;
							// yyxxxxxx
							// ....yyyy
						case 1: *pBS |= (streamV << 6); pBS++; *pBS = (streamV >> 2); state = 2; break;
							// zzzzyyyy
							// ......zz
						case 2: *pBS |= (streamV << 4); pBS++; *pBS = (streamV >> 4); state = 3; break;
							// ttttttzz
						case 3: *pBS |= (streamV << 2); pBS++; state = 0; break;
						}
					}
#else
					* pBS++ = v - lastV;
					lastV = v;
#endif
				}
			}
		}

		// End stream to make we compress everything.
		if (state != 0) {
			pBS++;
		}

		bool isBinaryStream = (!isAnalogAlpha) && (!isAll0) && (!isAll1);
		if ((!isAnalogAlpha) && (!isAll0) && (!isAll1)) {
			BoundingBox r;

			// Aligned per 8 with 1 bit mask decoder.
			bL = (bL >> 3) << 3;
			bR = ((bR + 7) >> 3) << 3;

			r.x = bL;
			r.y = bT;
			r.w = bR - bL;
			r.h = bB - bT;
			unsigned char* pBitStreamU = (unsigned char*)pBitStream;
			byteSizeDecomp = make1BitStream(alpha, r, /*mipmapMask*/NULL, pBitStreamU);
		} else {
			byteSizeDecomp = (int)(pBS - pBitStream);
		}

		if (isAnalogAlpha || isBinaryStream) {
			unsigned char* pZStdStream = new unsigned char[byteSizeDecomp * 2]; // Max in case...
			memset(pZStdStream, 0, byteSizeDecomp * 2);

			int bestCompressionSize = 999999999;
			int bestCompression;

			for (int n = 5; n < 22; n++)
			{
				size_t result = ZSTD_compress(pZStdStream, byteSizeDecomp * 2, pBitStream, byteSizeDecomp, n);
				if (ZSTD_isError(result)) {
					//
					printf("Error");
				}

				// [TODO]
				// --------
				// [Write Header:Chunk ID 32 Bit, Block Length : 32 bit][64 bit info for future extensions][Write Bounding Box X,Y,W,H : 2x4 8 Byte]
				//	Bounding box = bL,bT,bR-bL,bB-bT
				// Write Flag about INVERSED STREAM OR NOT.
				// Write Flag about 6 or 1 bit.
				// ... Then ZSTD Stream ...
				// --------
				if (bestCompressionSize > result) {
					bestCompressionSize = (int)result;
					bestCompression     = n;
				} else {
					if (bestCompressionSize < result) {
						break;
					}
				}

				printf("[Alpha Layer Header (%i bytes) lvl = %i]\n", (int)result, n);
			}

			size_t result = ZSTD_compress(pZStdStream, byteSizeDecomp * 2, pBitStream, byteSizeDecomp, bestCompression);
			int iresult   = (int)result;

			HeaderBase headerTag;
			headerTag.tag.tag8[0] = 'A';
			headerTag.tag.tag8[1] = 'L';
			headerTag.tag.tag8[2] = 'P';
			headerTag.tag.tag8[3] = 'M';
			int baseSize = (sizeof(AlphaHeader) + iresult);
			headerTag.length = ((baseSize + 3) >> 2) << 2;	// Round multiple of 4.
			u8 pad[3] = { 0,0,0 };
			int padding = headerTag.length - baseSize;

			AlphaHeader headerAlph;

			fwrite(&headerTag, sizeof(HeaderBase), 1, outFile);

			headerAlph.version = 1;
			headerAlph.bbox.x = bL;
			headerAlph.bbox.y = bT;
			headerAlph.bbox.w = bR - bL;
			headerAlph.bbox.h = bB - bT;

			if (isAnalogAlpha) {
				// Analog
				if (force8Bit) {
					headerAlph.parameters = AlphaHeader::IS_8_BIT_FULL;
				} else {
					if (ignoreMipmapMaskOnlyPixels) {
						headerAlph.parameters = useInverse ? AlphaHeader::IS_6_BIT_FULL_INVERSE
							                               : AlphaHeader::IS_6_BIT_FULL;
					} else {
						headerAlph.parameters = useInverse ? AlphaHeader::IS_6_BIT_USEMIPMAPMASK_INVERSE
							                               : AlphaHeader::IS_6_BIT_USEMIPMAPMASK;
					}
				}
			} else {
				// Binary
				// [TODO : Decoder does not support it]
				// headerAlph.parameters = AlphaHeader::IS_1_BIT_USEMIPMAPMASK;
				headerAlph.parameters = AlphaHeader::IS_1_BIT_FULL;
			}

			headerAlph.streamSize = iresult;
			headerAlph.expectedDecompressionSize = byteSizeDecomp;

			fwrite(&headerAlph, sizeof(AlphaHeader), 1, outFile);
			fwrite(pZStdStream, 1, result, outFile);
			if (padding) {
				fwrite(pad, 1, padding, outFile);
			}


			delete[] pZStdStream;
		}

		delete[] pBitStream;
	} else {
		Plane* newPlane = new Plane(w, h);
		BoundingBox r;
		r.x = 0; r.y = 0; r.w = w; r.h = h;
		newPlane->Fill(r, 31);
		original->ReplacePlane(3, newPlane); // Previous is NULL.
	}
}

void dumpColorMap(u8* array, u8* palRGB, int colCount, BoundingBox box) {
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

			if (array[n]) {
				int idx = array[n]*3;
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
	int err = stbi_write_png("ColorOut.png", w, h, 3, exportB, w*3);
	
	delete[] exportB;
}

// Linear
int GetCoordinateLinear(int x, int y, int w, int mapSize) {
	int idx = x+y*w;
	if ((idx < 0) || (idx >= mapSize)) {
		printf("ERROR"); while (1) {};
	}
	return idx;
}

// Swizzling

int GetCoordinateSwizzle(int x, int y, int w, int mapSize) {
	int wU = w>>3;
	int xU = x>>3; int xL = x&7;
	int yU = y>>3; int yL = y&7;

	int res = xL | (yL<<3) | ((xU + yU*wU)<<6);
	if ((res >= mapSize) || (res < 0)) {
		printf("ERROR"); while (1) {};
	}
	return res;
}

struct usedSpan {
	u32 deltaStart;
	u32 length;
};

void verifyComp(u8* mapPixel, u8* paletteStream, usedSpan* usedSpan_, usedSpan* lastSpan,int w, int h) {
	u8* targetBuffer = new u8[w*h];
	memset(targetBuffer,0,w*h);

	int pos = 0;
	while (usedSpan_ != lastSpan) {
		pos += usedSpan_->deltaStart;
//		printf("Draw %i byte From %i\n",pos,usedSpan_->length);
		memcpy(&targetBuffer[pos],paletteStream,usedSpan_->length);
		paletteStream += usedSpan_->length;
		usedSpan_++;
	}

//	printf("----\n");

	if (memcmp(mapPixel, targetBuffer, w*h)!=0) {
		printf("ERROR"); while (1) {};
	}
}

void EncoderContext::SingleColorOut(bool active, Image* output) {
	if (!active) { return; }

	CheckMipmapMask();

	int* histogram4D = new int[256 * 256 * 256];

	// Real RGB single color out, independant of Alpha channel (not counted if alpha == 0)
	BoundingBox mipmapBbox;
	mipmapBbox.x = boundX0;
	mipmapBbox.y = boundY0;
	mipmapBbox.w = boundX1 - boundX0;
	mipmapBbox.h = boundY1 - boundY0;

	//
	// So this first color compression IS an RGB value that NEED to be STORED
	// as color BUT MAY NOT BE VISIBLE TO THE USER EXCEPT ON MIPMAPPING LOW !
	//

	u8 rgbc[257*3];
	rgbc[0] = 0xFF;
	rgbc[1] = 0;
	rgbc[2] = 0xFF;

	int mapSize = mipmapBbox.h * mipmapBbox.w;
	u8* mapPixel = new u8[ mapSize];
	memset(mapPixel, 0, mapSize);

	int ColorID = 1;

	int totalCompressed = 0;
	int prevValidPixelCount = 0;
	int prevStreamSize = 0;

	Plane* MaskProcessed = mipmapMask->Clone();

	int validPixelCount = 0;

tryAgainNextColor:
	original->Compute4DHistogram(histogram4D, mipmapBbox, MaskProcessed);

	int found = -1;
	int foundCount = -1;
	for (int n = 0; n < 256 * 256 * 256; n++) {
		if (histogram4D[n] > foundCount) {
			found = n;
			foundCount = histogram4D[n];
		}
	}

	// Now decide if we output a Single color map ?
	// Consider that if we stay in Dynamic pixel with 1/4 CoCg, we consume 1502 per 256 pixels = 5.86 bit / pixel.
	// That does not include the Gradient map so we should be closer to 7.

	// So if this map is cheaper at 1 pixel per bit, even with its overhead for 0 pixel (not in map).
	// Encode a Single color bitmap.
	int values[3];
	values[2] = (found)       & 0xFF;
	values[1] = (found >> 8)  & 0xFF;
	values[0] = (found >> 16) & 0xFF;

	rgbc[ColorID*3]     = values[0];
	rgbc[(ColorID*3)+1] = values[1];
	rgbc[(ColorID*3)+2] = values[2];

	Plane* colorMask = original->ComputeOperatorMask(values, Image::EQUAL_MSK, true, true);
	colorMask->FillOutside(mipmapBbox, 0);

	unsigned char* paletteStream = new u8[mipmapBbox.h * mipmapBbox.w];

	BoundingBox r; int pixelCount = colorMask->GetBoundingBoxNonZeros(r);

	// ALSO Skip pixel that we know are encoded in PREVIOUS MASKS !
	int byteSizeDecomp = 0; // make1BitStream(colorMask, r, mipmapMask, maskAs1Bit);
	unsigned char* pBS = paletteStream;


	struct emptySpan {
		int pos;
		int length;
	};

	emptySpan* pSpansCompressed;
	emptySpan* pSpansCompressedP;

	{
		Plane* skipPlane = MaskProcessed;
		Plane* plane     = colorMask;
		int sizeByteMax = r.w * r.h;

		int bitPos = 0;
		int bytePos = 0;

		int* planeP = plane->GetPixels();
		int* checkP = skipPlane->GetPixels();

		// Update Map of palette index with new mask...
		for (int y = r.y; y < (r.y + r.h); y++) {
			for (int x = r.x; x < (r.x + r.w); x++) {
				int idx = x + y * plane->GetWidth();
				int pixel = planeP[idx];
				if (pixel) {
					int widx = isSwizzling ? GetCoordinateSwizzle(x-mipmapBbox.x, y-mipmapBbox.y, mipmapBbox.w,mapSize)
						                   : GetCoordinateLinear (x-mipmapBbox.x, y-mipmapBbox.y, mipmapBbox.w,mapSize);
					validPixelCount++;

					// Register palette.
					mapPixel[widx] = ColorID;
				}
			}
		}

		// Compute Edge and stream...
		int start = 0;
		int end   = 0;
		int spanCount = 0;

		emptySpan* pSpans = new emptySpan[mipmapBbox.h * mipmapBbox.w]; // Worst case.
		emptySpan* pSpansAlloc = pSpans;

		int currPos = 0;
		int prevIDX = 0;
		pSpans->pos = 0;
		pSpans->length = 0;

		int emptySpace = 0;
		int myValidPCount = 0;
		for (currPos = 0; currPos < (mipmapBbox.h * mipmapBbox.w); currPos++) {
			u8 palIdx = mapPixel[currPos]; // Linear or Swizzle, no pb. Done BEFORE.
			if (palIdx) {
				if (!prevIDX) {
					pSpans->length = currPos - pSpans->pos;
					emptySpace += pSpans->length;
					pSpans++;
					pSpans->length = 0;
					pSpans->pos    = -1;
					prevIDX = palIdx;
				}
				myValidPCount++;
			} else {
				if (prevIDX) {
					prevIDX = 0;
					pSpans->pos    = currPos;
					pSpans->length = 0;
				}
			}
		}

		if (prevIDX == 0) {
			pSpans->length = currPos - pSpans->pos;
			emptySpace += pSpans->length;
			pSpans++;
		}

		pSpansCompressed = new emptySpan[mipmapBbox.h * mipmapBbox.w]; // Worst case.
		pSpansCompressedP = pSpansCompressed;
		emptySpan* parseSrc = pSpansAlloc;
		int totalSpan = 0;
		int compressedSpan = 0;
		while (parseSrc < pSpans) {
			if (parseSrc->length >= 16) {
				*pSpansCompressedP++ = *parseSrc;
				compressedSpan+=parseSrc->length;
			}
			totalSpan += parseSrc->length;
			parseSrc++;
		}

		delete[] pSpansAlloc;

		printf("%i Span -> Reduced to %i Span. Skip %i over %i (%f)\n",	(int)(pSpans - pSpansAlloc), 
																		(int)(pSpansCompressedP - pSpansCompressed), 
																		compressedSpan, 
																		totalSpan, 
																		(float)compressedSpan/totalSpan
																		);

#if 0
		/*
		for (int y = 0; y < mipmapBbox.h; y++) {
			Edges[y].left  = -1;
			Edges[y].right = -1;

			bool startWrite = false;
			int lastX = -1;
			for (int x = 0; x < mipmapBbox.w; x++) {
				u8 palIdx = mapPixel[x + y*mipmapBbox.w];
				if (palIdx) {
					validPixelCount++;
					if (Edges[y].left==-1) {
						Edges[y].left = x;
						startWrite = true;
					}
					lastX = x+1;
				}
				if (startWrite) {
					*pBS++ = palIdx;
				}
			}

			if (lastX >= 0) {
				// Rollback
				Edges[y].right = lastX;
				pBS -= mipmapBbox.w - lastX;
			}

			// Better compression, easier decode.
			if (Edges[y].left == -1) {
				Edges[y].left  = 0;
				Edges[y].right = 0;
			}
		}*/


		for (int y = 0; y < mipmapBbox.h; y++) {
			Edges[y].left  = -1;
			Edges[y].right = -1;

			bool startWrite = false;
			int lastX = -1;
			for (int x = 0; x < mipmapBbox.w; x++) {
				u8 palIdx = mapPixel[x + y*mipmapBbox.w];
				if (palIdx) {
					validPixelCount++;
					if (Edges[y].left==-1) {
						Edges[y].left = x;
						startWrite = true;
					}
					lastX = x+1;
				}
				if (startWrite) {
					*pBS++ = palIdx;
				}
			}

			if (lastX >= 0) {
				// Rollback
				Edges[y].right = lastX;
				pBS -= mipmapBbox.w - lastX;
			}

			// Better compression, easier decode.
			if (Edges[y].left == -1) {
				Edges[y].left  = 0;
				Edges[y].right = 0;
			}
		}
#endif
	}

	usedSpan* ArrayStart = new usedSpan[mipmapBbox.h * mipmapBbox.w];
	usedSpan* filledSpan = ArrayStart;

	emptySpan* pSpanUse = pSpansCompressed;
	int lastPostCopy = 0;
	int posCopy = 0;
	while (pSpanUse < pSpansCompressedP) {
		if (posCopy != pSpanUse->pos) {
			int sizeCopy = pSpanUse->pos - posCopy;
			memcpy(pBS,&mapPixel[posCopy],sizeCopy);
			pBS += sizeCopy;
			filledSpan->deltaStart	= posCopy - lastPostCopy;
			lastPostCopy = posCopy;
			filledSpan->length		= sizeCopy;
			filledSpan++;
			posCopy = pSpanUse->pos;
		} else {
			posCopy += pSpanUse->length;
			pSpanUse++;
		}
	}

	
	if (posCopy < mipmapBbox.h * mipmapBbox.w) {
		int sizeCopy = (mipmapBbox.h * mipmapBbox.w) - posCopy;
		memcpy(pBS,&mapPixel[posCopy],sizeCopy);
		pBS += sizeCopy;
		filledSpan->deltaStart	= posCopy - lastPostCopy;
		filledSpan->length		= sizeCopy;
		filledSpan++;
	}

	verifyComp(mapPixel, paletteStream, ArrayStart, filledSpan, mipmapBbox.w, mipmapBbox.h);

	byteSizeDecomp = (int)(pBS - paletteStream);

	// ----------------------------------------------------------------------
	unsigned char* pZStdStream = new unsigned char[byteSizeDecomp * 2]; // Max in case...
	memset(pZStdStream, 0, byteSizeDecomp * 2);
	size_t result;
	int lastResult = 999999999;
	int lastLevel  = 15;
	
	
		result = ZSTD_compress(pZStdStream, byteSizeDecomp * 2, paletteStream, byteSizeDecomp, 15);
		int iresult = (int)result;

		if (ZSTD_isError(result)) {
			//
			printf("Error");
		}
		/*
		if (lastResult > result) {
			lastResult = result;
			lastLevel  = n;
		} else {
			if (lastResult < result) {
				break;
			}
		}*/
		printf("ZSTD Color Map : %i [level %i]\n", (int)result, 15);

	int sizeBlk = ((int)(filledSpan-ArrayStart)) * sizeof(emptySpan);
	unsigned char* pZStdStream2 = new unsigned char[sizeBlk * 2]; // Max in case...
	int	result2 = (int)ZSTD_compress(pZStdStream2, sizeBlk * 2, ArrayStart, sizeBlk, 15);


//	result = ZSTD_compress(pZStdStream, byteSizeDecomp * 2, paletteStream, byteSizeDecomp, lastLevel);

	//
	// Metric is the following :
	// Number of pixel found * 7 bit average (using 4:2:0, average for 16x16 pixel block is : ((272*4)+(207+207))/256 bits. Not including gradient and other compressed mask but...)
	// (We ignore the fact that flat color are going into the gradient map and pixel probably compressed at 1/16 rate. Also cleaner with this method !
	//
	int flatColorCostWithoutMask = validPixelCount * 7;	// Bit cost per pixel.
	int flatColorCostWithMask    = (iresult * 8) + (ColorID*24) + (result2 * 8);		// Bit cost of compressed mask.
	int diffPixelCounter = validPixelCount - prevValidPixelCount;
	int diffSizeByte     = (iresult+result2+(ColorID*3)) - prevStreamSize;
	float bitPerPixAdd   = (diffSizeByte*8.0f) / diffPixelCounter;
	bool skipColor       = favorAccuracy ? false : (bitPerPixAdd > 7.0);

	delete[] pSpansCompressed;

	if ((ColorID < 85) && (flatColorCostWithoutMask >= flatColorCostWithMask) && (favorAccuracy || (diffPixelCounter > 150))) { // Decide or not if worth it post compression. Amount of pixel must be big enough.. else cost function gets more and more inaccurate.
//		totalCompressed += result;

		printf("[Single Color %i Header, Pixel Count : %i, StreamSize : %i : Bit per pixel %f (Relative Bit per pix %f)]\n", 
			ColorID, 
			validPixelCount, 
			(int)result,
			((result*8.0f)/validPixelCount), 
			bitPerPixAdd
		);

		// Gradually remove valid pixels...
		MaskProcessed->RemoveMask(colorMask);

		if (!skipColor) {
			// We update only the mask for the color we have really saved to the stream...
			mipmapMask->RemoveMask(colorMask);

			#if 1
					{ char buffer[2000]; sprintf(buffer, "EncoderDebug\\ColorMask%i.png", ColorID);
					colorMask->SaveAsPNG(buffer); }

					{ char buffer[2000]; sprintf(buffer, "EncoderDebug\\ColorMaskMipmap%i.png", ColorID);
					mipmapMask->SaveAsPNG(buffer); }
			#endif

		} else {
			validPixelCount = prevValidPixelCount;
			Plane* plane     = colorMask;
			int* planeP = plane->GetPixels();
			for (int y = r.y; y < (r.y + r.h); y++) {
				for (int x = r.x; x < (r.x + r.w); x++) {
					int idx = x + y * plane->GetWidth();
					int pixel = planeP[idx];
					if (pixel) {
						// Rollback the changes to the exported pixel list...
						if (isSwizzling) {
							mapPixel[GetCoordinateSwizzle(x-mipmapBbox.x, y-mipmapBbox.y, mipmapBbox.w,mapSize)] = 0;
						} else {
							mapPixel[GetCoordinateLinear(x-mipmapBbox.x, y-mipmapBbox.y, mipmapBbox.w,mapSize)] = 0;
						}
					}
				}
			}
		}

		delete colorMask;


		// -----------------------------------------------------------------
		//  Recursively build mask until cost is not worth it...
		//  Huge surface with single color will always benefit...
		// -----------------------------------------------------------------

		// Can not use more than 8 bit palette.
		if (ColorID < 256) {
			delete[] pZStdStream;
			delete[] pZStdStream2;
			if (!skipColor) {
				ColorID++;
				prevValidPixelCount = validPixelCount;
				prevStreamSize = (int)result+result2;
			}

			goto tryAgainNextColor;
		}
	} else {
		// [TODO] For now we save the last colors testes as a stream
		//        So we also need to remove the color for the mask, yeah that feature is a bit buggy... but don't have time
		//		  for details now...
		// Gradually remove valid pixels...
		MaskProcessed->RemoveMask(colorMask);
	}

	dumpColorMap(mapPixel, rgbc, ColorID, mipmapBbox);

	HeaderBase headerTag;
	headerTag.tag.tag8[0] = 'U'; // UNIQUE COLOR
	headerTag.tag.tag8[1] = 'C';
	headerTag.tag.tag8[2] = 'O';
	headerTag.tag.tag8[3] = 'L';
	int baseSize = (sizeof(UniqueColorHeader)+ result2 + (int)result + (ColorID*3)); // Skip color zero in file.
	headerTag.length = ((baseSize + 3) >> 2) << 2;	// Round multiple of 4.
	u8 pad[3] = { 0,0,0 };
	int padding = headerTag.length - baseSize;

	UniqueColorHeader headerUCol;
	headerUCol.colorCount = ColorID+1;
	headerUCol.bbox.x = mipmapBbox.x;
	headerUCol.bbox.y = mipmapBbox.y;
	headerUCol.bbox.w = mipmapBbox.w;
	headerUCol.bbox.h = mipmapBbox.h;
	headerUCol.version						= isSwizzling ? 2 : 1;
	headerUCol.expectedDecompressionSizeM	= byteSizeDecomp;
	headerUCol.streamSize					= (int)result;
	headerUCol.streamSizeE					= result2;
	headerUCol.expectedDecompressionSkippers= sizeBlk;

#if 0
	{
		usedSpan* st = ArrayStart;
		while (st < filledSpan) {
			printf("%i(%i)\n",st->deltaStart, st->length);
			st++;
		}
	}
#endif

	fwrite(&headerTag,  sizeof(HeaderBase), 1, outFile);
	fwrite(&headerUCol, sizeof(UniqueColorHeader), 1, outFile);

	// RGB Palette.
	fwrite(&rgbc[3], 1, 3*ColorID, outFile);

	// Stream Indexes...
	fwrite(pZStdStream, 1, result, outFile);


	// Edge Table
	fwrite(pZStdStream2, 1, result2, outFile);
	if (padding) {
		fwrite(pad, 1, padding, outFile);
	}

	BoundingBox ignoreData;
	remainingPixels = mipmapMask->GetBoundingBoxNonZeros(ignoreData);
	mipmapMask->SaveAsPNG("EncoderDebug\\MipmapMaskPostSingle.png");

	{
		Plane* Y  = output->GetPlane(0);
		Plane* Co = output->GetPlane(1);
		Plane* Cg = output->GetPlane(2);

		int* pY =  Y->GetPixels();
		int* pCo= Co->GetPixels();
		int* pCg= Cg->GetPixels();

		for (int y=0; y < mipmapBbox.h; y++) {
			for (int x=0; x < mipmapBbox.w; x++) {
				// Linear in source buffer.
				int n = x + y*mipmapBbox.w;

				int insideTileX = n&7;
				int insideTileY = (n>>3)&7;
				int tileID      = n>>6;
				int ty          = tileID / (mipmapBbox.w>>3);
				int tx			= tileID % (mipmapBbox.w>>3);

				// Swizzle in dst buffer
				int dstIdx = ((ty*8 + insideTileY)*(mipmapBbox.w)) + (tx*8 + insideTileX);

				int colIdx = mapPixel[n] * 3;
				if (colIdx!=0) {
					// Linear in output buffer.
					u8 r = rgbc[colIdx  ];
					u8 g = rgbc[colIdx+1];
					u8 b = rgbc[colIdx+2];

					int Yv,Cov,Cgv;
					RGBtoYCoCg(r,g,b,Yv,Cov,Cgv);

					pY [dstIdx] = Yv;
					pCo[dstIdx] = Cov;
					pCg[dstIdx] = Cgv;
				}
			}
		}
	}

	delete[] mapPixel;
	delete[] paletteStream;

	delete[] pZStdStream;
	delete[] pZStdStream2;

	// ----------------------------------------------------------------------
//	delete colorMask; <--- Crash.
	delete[] histogram4D;
	delete MaskProcessed;
}

void EncoderContext::Interpolate(Image* output, Plane* src, EInterpMode mode, bool isXDouble, bool isYDouble) {
	bool isOutside;

	switch (mode) {
	case EInterpMode::QUART_TL_REFERENCE_BILINEAR:
		if (isXDouble && isYDouble) {
			for (int y=0;y<output->GetHeight();y+=2) {
				for (int x=0;x<output->GetWidth();x+=2) {
					if (smoothMap->GetPixelValue(x,y,isOutside) == 0) {
						int v0 = src->GetPixelValue(x  ,y  ,isOutside);
						int v1 = src->GetPixelValue(x+2,y  ,isOutside);

						int v2 = src->GetPixelValue(x  ,y+2,isOutside);
						int v3 = src->GetPixelValue(x+2,y+2,isOutside);

						int vR,vB,vRB;
						vR  = (v0 + v1) / 2;
						vB  = (v0 + v2) / 2;
						vRB = (v0+v1+v2+v3) / 4;

						/* [Dont Touch pix here] */		src->SetPixel(x+1,y  ,vR );
						src->SetPixel(x  ,y+1,vB );		src->SetPixel(x+1,y+1,vRB);
					}
				}
			}
		} else if (isXDouble) {
			for (int y=0;y<output->GetHeight();y++) {
				for (int x=0;x<output->GetWidth();x+=2) {
					if (smoothMap->GetPixelValue(x,y,isOutside) == 0) {
						int v0 = src->GetPixelValue(x  ,y  ,isOutside);
						int v1 = src->GetPixelValue(x+2,y,isOutside);

						int vR;
						vR  = (v0 + v1) / 2;

						/* [Dont Touch pix here] */  src->SetPixel(x+1,y,vR );
					}
				}
			}
		} else if (isYDouble) {
			for (int y=0;y<output->GetHeight();y+=2) {
				for (int x=0;x<output->GetWidth();x++) {
					if (smoothMap->GetPixelValue(x,y,isOutside) == 0) {
						int v0 = src->GetPixelValue(x,y  ,isOutside);
						int v1 = src->GetPixelValue(x,y+2,isOutside);

						int vR;
						vR  = (v0 + v1) / 2;

						/* [Dont Touch pix here] */
						src->SetPixel(x  ,y+1,vR );
					}
				}
			}
		} else {
			// Do nothing...
		}
		break;
	case EInterpMode::QUART_BR_REFERENCE_BILINEAR:
		// Cg
		for (int y=0;y<output->GetHeight();y+=2) {
			for (int x=0;x<output->GetWidth();x+=2) {
				if (smoothMap->GetPixelValue(x,y,isOutside) == 0) {
					int v0 = src->GetPixelValue(x-2,y-2,isOutside);
					int v1 = src->GetPixelValue(x  ,y-2,isOutside);

					int v2 = src->GetPixelValue(x-2,y  ,isOutside);
					int v3 = src->GetPixelValue(x  ,y  ,isOutside);

					int vL,vU,vTL;
					vL  = (v0 + v1) / 2;
					vU  = (v1 + v3) / 2;
					vTL = (v0+v1+v2+v3) / 4;

					src->SetPixel(x  ,y  ,vTL);		src->SetPixel(x+1,y  ,vU );
				
					src->SetPixel(x  ,y+1,vL );		src->SetPixel(x+1,y+1,v3 );
				}
			}
		}
		break;
	}


	/*
	Plane* p0 = output->GetPlane(0);
	Plane* p2 = output->GetPlane(2);

	mipmapMask->SaveAsPNG("mask.png");
	smoothMap->SaveAsPNG("smooth.png");

	for (int y=0; y < output->GetHeight(); y+=4) {
		// First, grid aligned interpolation...
		for (int x=0; x < output->GetWidth()-4; x+= 4) {	// Right edge problem later...
			bool isOutside;
			int v1 = mipmapMask->GetPixelValue(x+1,y,isOutside);
			int v2 = mipmapMask->GetPixelValue(x+2,y,isOutside);
			int v3 = mipmapMask->GetPixelValue(x+3,y,isOutside);

			int b1 = smoothMap->GetPixelValue(x>>1,y>>1,isOutside);
			int b2 = smoothMap->GetPixelValue((x>>1)+1,y>>1,isOutside);
			int mode = ((!v1 && b1) ? 0x1 : 0x0) | ((!v2 && b1) ? 0x2 : 0x0) | ((!v3 && b2) ? 0x4 : 0x0);


			switch (mode) {
			case 0:	break; // No pixel to fill
			case 1: 
				p0->SetPixel(x+1,y, (p0->GetPixelValue(x,y,isOutside)+p0->GetPixelValue(x+2,y,isOutside))/2); 
				p1->SetPixel(x+1,y, (p1->GetPixelValue(x,y,isOutside)+p1->GetPixelValue(x+2,y,isOutside))/2); 
				p2->SetPixel(x+1,y, (p2->GetPixelValue(x,y,isOutside)+p2->GetPixelValue(x+2,y,isOutside))/2); 
				break;
			case 2:
				p0->SetPixel(x+2,y, (p0->GetPixelValue(x+1,y,isOutside)+p0->GetPixelValue(x+3,y,isOutside))/2); 
				p1->SetPixel(x+2,y, (p1->GetPixelValue(x+1,y,isOutside)+p1->GetPixelValue(x+3,y,isOutside))/2); 
				p2->SetPixel(x+2,y, (p2->GetPixelValue(x+1,y,isOutside)+p2->GetPixelValue(x+3,y,isOutside))/2); 
				break;
			case 3:
				// [0] 1..2 [3]
				{
					int l[3]; l[0] = p0->GetPixelValue(x,y,isOutside);   l[1] = p1->GetPixelValue(x,y,isOutside);   l[2] = p2->GetPixelValue(x,y,isOutside);
					int r[3]; r[0] = p0->GetPixelValue(x+3,y,isOutside); r[1] = p1->GetPixelValue(x+3,y,isOutside); r[2] = p2->GetPixelValue(x+3,y,isOutside);

					p0->SetPixel(x+1,y, ((l[0]*683) + (r[0]*341)) / 1024);
					p1->SetPixel(x+1,y, ((l[1]*683) + (r[1]*341)) / 1024);
					p2->SetPixel(x+1,y, ((l[2]*683) + (r[2]*341)) / 1024);

					p0->SetPixel(x+2,y, ((l[0]*341) + (r[0]*683)) / 1024);
					p1->SetPixel(x+2,y, ((l[1]*341) + (r[1]*683)) / 1024);
					p2->SetPixel(x+2,y, ((l[2]*341) + (r[2]*683)) / 1024);
				}
				break;
			case 4:
				p0->SetPixel(x+3,y, (p0->GetPixelValue(x+2,y,isOutside)+p0->GetPixelValue(x+4,y,isOutside))/2); 
				p1->SetPixel(x+3,y, (p1->GetPixelValue(x+2,y,isOutside)+p1->GetPixelValue(x+4,y,isOutside))/2); 
				p2->SetPixel(x+3,y, (p2->GetPixelValue(x+2,y,isOutside)+p2->GetPixelValue(x+4,y,isOutside))/2); 
				break;
			case 5:
				// Cas 1
				p0->SetPixel(x+1,y, (p0->GetPixelValue(x,y,isOutside)+p0->GetPixelValue(x+2,y,isOutside))/2); 
				p1->SetPixel(x+1,y, (p1->GetPixelValue(x,y,isOutside)+p1->GetPixelValue(x+2,y,isOutside))/2); 
				p2->SetPixel(x+1,y, (p2->GetPixelValue(x,y,isOutside)+p2->GetPixelValue(x+2,y,isOutside))/2); 

				// Case 4
				p0->SetPixel(x+3,y, (p0->GetPixelValue(x+2,y,isOutside)+p0->GetPixelValue(x+4,y,isOutside))/2); 
				p1->SetPixel(x+3,y, (p1->GetPixelValue(x+2,y,isOutside)+p1->GetPixelValue(x+4,y,isOutside))/2); 
				p2->SetPixel(x+3,y, (p2->GetPixelValue(x+2,y,isOutside)+p2->GetPixelValue(x+4,y,isOutside))/2); 
				break;
			case 6:
				// [1] 2..3 [4]
				{
					int l[3]; l[0] = p0->GetPixelValue(x+1,y,isOutside); l[1] = p1->GetPixelValue(x+1,y,isOutside); l[2] = p2->GetPixelValue(x+1,y,isOutside);
					int r[3]; r[0] = p0->GetPixelValue(x+4,y,isOutside); r[1] = p1->GetPixelValue(x+4,y,isOutside); r[2] = p2->GetPixelValue(x+4,y,isOutside);

					p0->SetPixel(x+2,y, ((l[0]*683) + (r[0]*341)) / 1024);
					p1->SetPixel(x+2,y, ((l[1]*683) + (r[1]*341)) / 1024);
					p2->SetPixel(x+2,y, ((l[2]*683) + (r[2]*341)) / 1024);

					p0->SetPixel(x+3,y, ((l[0]*341) + (r[0]*683)) / 1024);
					p1->SetPixel(x+3,y, ((l[1]*341) + (r[1]*683)) / 1024);
					p2->SetPixel(x+3,y, ((l[2]*341) + (r[2]*683)) / 1024);
				}
				break;
			case 7:
				{
					int l[3]; l[0] = p0->GetPixelValue(x  ,y,isOutside); l[1] = p1->GetPixelValue(x  ,y,isOutside); l[2] = p2->GetPixelValue(x  ,y,isOutside);
					int r[3]; r[0] = p0->GetPixelValue(x+4,y,isOutside); r[1] = p1->GetPixelValue(x+4,y,isOutside); r[2] = p2->GetPixelValue(x+4,y,isOutside);

					p0->SetPixel(x+1,y, ((l[0]*768) + (r[0]*256)) / 1024);
					p1->SetPixel(x+1,y, ((l[1]*768) + (r[1]*256)) / 1024);
					p2->SetPixel(x+1,y, ((l[2]*768) + (r[2]*256)) / 1024);

					p0->SetPixel(x+2,y, ((l[0]*512) + (r[0]*512)) / 1024);
					p1->SetPixel(x+2,y, ((l[1]*512) + (r[1]*512)) / 1024);
					p2->SetPixel(x+2,y, ((l[2]*512) + (r[2]*512)) / 1024);

					p0->SetPixel(x+3,y, ((l[0]*256) + (r[0]*768)) / 1024);
					p1->SetPixel(x+3,y, ((l[1]*256) + (r[1]*768)) / 1024);
					p2->SetPixel(x+3,y, ((l[2]*256) + (r[2]*768)) / 1024);
				}
				break;
			}
		}

		// Vertically now...
		for (int x=0; x < output->GetWidth()-4; x+= 4) {	// Right edge problem later...
			bool isOutside;
			int v1 = mipmapMask->GetPixelValue(x,y+1,isOutside);
			int v2 = mipmapMask->GetPixelValue(x,y+2,isOutside);
			int v3 = mipmapMask->GetPixelValue(x,y+3,isOutside);

			int b1 = quarterSmoothMap->GetPixelValue(x>>1,y>>1,isOutside);
			int b2 = quarterSmoothMap->GetPixelValue((x>>1),(y>>1)+1,isOutside);
			int mode = ((!v1 && b1) ? 0x1 : 0x0) | ((!v2 && b1) ? 0x2 : 0x0) | ((!v3 && b2) ? 0x4 : 0x0);


			switch (mode) {
			case 0:	break; // No pixel to fill
			case 1: 
				p0->SetPixel(x,y+1, (p0->GetPixelValue(x,y,isOutside)+p0->GetPixelValue(x,y+2,isOutside))/2); 
				p1->SetPixel(x,y+1, (p1->GetPixelValue(x,y,isOutside)+p1->GetPixelValue(x,y+2,isOutside))/2); 
				p2->SetPixel(x,y+1, (p2->GetPixelValue(x,y,isOutside)+p2->GetPixelValue(x,y+2,isOutside))/2); 
				break;
			case 2:
				p0->SetPixel(x,y+2, (p0->GetPixelValue(x,y+1,isOutside)+p0->GetPixelValue(x,y+3,isOutside))/2); 
				p1->SetPixel(x,y+2, (p1->GetPixelValue(x,y+1,isOutside)+p1->GetPixelValue(x,y+3,isOutside))/2); 
				p2->SetPixel(x,y+2, (p2->GetPixelValue(x,y+1,isOutside)+p2->GetPixelValue(x,y+3,isOutside))/2); 
				break;
			case 3:
				// [0] 1..2 [3]
				{
					int l[3]; l[0] = p0->GetPixelValue(x,y,isOutside);   l[1] = p1->GetPixelValue(x,y,isOutside);   l[2] = p2->GetPixelValue(x,y,isOutside);
					int r[3]; r[0] = p0->GetPixelValue(x,y+3,isOutside); r[1] = p1->GetPixelValue(x,y+3,isOutside); r[2] = p2->GetPixelValue(x,y+3,isOutside);

					p0->SetPixel(x,y+1, ((l[0]*683) + (r[0]*341)) / 1024);
					p1->SetPixel(x,y+1, ((l[1]*683) + (r[1]*341)) / 1024);
					p2->SetPixel(x,y+1, ((l[2]*683) + (r[2]*341)) / 1024);

					p0->SetPixel(x,y+2, ((l[0]*341) + (r[0]*683)) / 1024);
					p1->SetPixel(x,y+2, ((l[1]*341) + (r[1]*683)) / 1024);
					p2->SetPixel(x,y+2, ((l[2]*341) + (r[2]*683)) / 1024);
				}
				break;
			case 4:
				p0->SetPixel(x,y+3, (p0->GetPixelValue(x,y+2,isOutside)+p0->GetPixelValue(x,y+4,isOutside))/2); 
				p1->SetPixel(x,y+3, (p1->GetPixelValue(x,y+2,isOutside)+p1->GetPixelValue(x,y+4,isOutside))/2); 
				p2->SetPixel(x,y+3, (p2->GetPixelValue(x,y+2,isOutside)+p2->GetPixelValue(x,y+4,isOutside))/2); 
				break;
			case 5:
				// Cas 1
				p0->SetPixel(x,y+1, (p0->GetPixelValue(x,y,isOutside)+p0->GetPixelValue(x,y+2,isOutside))/2); 
				p1->SetPixel(x,y+1, (p1->GetPixelValue(x,y,isOutside)+p1->GetPixelValue(x,y+2,isOutside))/2); 
				p2->SetPixel(x,y+1, (p2->GetPixelValue(x,y,isOutside)+p2->GetPixelValue(x,y+2,isOutside))/2); 

				// Case 4
				p0->SetPixel(x,y+3, (p0->GetPixelValue(x,y+2,isOutside)+p0->GetPixelValue(x,y+4,isOutside))/2); 
				p1->SetPixel(x,y+3, (p1->GetPixelValue(x,y+2,isOutside)+p1->GetPixelValue(x,y+4,isOutside))/2); 
				p2->SetPixel(x,y+3, (p2->GetPixelValue(x,y+2,isOutside)+p2->GetPixelValue(x,y+4,isOutside))/2); 
				break;
			case 6:
				// [1] 2..3 [4]
				{
					int l[3]; l[0] = p0->GetPixelValue(x,y+1,isOutside); l[1] = p1->GetPixelValue(x,y+1,isOutside); l[2] = p2->GetPixelValue(x,y+1,isOutside);
					int r[3]; r[0] = p0->GetPixelValue(x,y+4,isOutside); r[1] = p1->GetPixelValue(x,y+4,isOutside); r[2] = p2->GetPixelValue(x,y+4,isOutside);

					p0->SetPixel(x,y+2, ((l[0]*683) + (r[0]*341)) / 1024);
					p1->SetPixel(x,y+2, ((l[1]*683) + (r[1]*341)) / 1024);
					p2->SetPixel(x,y+2, ((l[2]*683) + (r[2]*341)) / 1024);

					p0->SetPixel(x,y+3, ((l[0]*341) + (r[0]*683)) / 1024);
					p1->SetPixel(x,y+3, ((l[1]*341) + (r[1]*683)) / 1024);
					p2->SetPixel(x,y+3, ((l[2]*341) + (r[2]*683)) / 1024);
				}
				break;
			case 7:
				{
					int l[3]; l[0] = p0->GetPixelValue(x  ,y  ,isOutside); l[1] = p1->GetPixelValue(x,y  ,isOutside); l[2] = p2->GetPixelValue(x,y  ,isOutside);
					int r[3]; r[0] = p0->GetPixelValue(x  ,y+4,isOutside); r[1] = p1->GetPixelValue(x,y+4,isOutside); r[2] = p2->GetPixelValue(x,y+4,isOutside);

					p0->SetPixel(x,y+1, ((l[0]*768) + (r[0]*256)) / 1024);
					p1->SetPixel(x,y+1, ((l[1]*768) + (r[1]*256)) / 1024);
					p2->SetPixel(x,y+1, ((l[2]*768) + (r[2]*256)) / 1024);

					p0->SetPixel(x,y+2, ((l[0]*512) + (r[0]*512)) / 1024);
					p1->SetPixel(x,y+2, ((l[1]*512) + (r[1]*512)) / 1024);
					p2->SetPixel(x,y+2, ((l[2]*512) + (r[2]*512)) / 1024);

					p0->SetPixel(x,y+3, ((l[0]*256) + (r[0]*768)) / 1024);
					p1->SetPixel(x,y+3, ((l[1]*256) + (r[1]*768)) / 1024);
					p2->SetPixel(x,y+3, ((l[2]*256) + (r[2]*768)) / 1024);
				}
				break;
			}
		}

	}
	//this->mipmapMask
	*/
}

void EncoderContext::SmoothMap(Image* output) {
	CheckMipmapMask();

	//---------------------------
	// Smooth Map
	//---------------------------
	Image* pGradient = Image::CreateImage(YCoCgImg->GetWidth(), YCoCgImg->GetHeight(), 3, false);

	Plane* gradR = computeGradientMap(YCoCgImg->GetPlane(0));
	Plane* gradG = computeGradientMap(YCoCgImg->GetPlane(1));
	Plane* gradB = computeGradientMap(YCoCgImg->GetPlane(2));

	Plane* gradRG  = gradR->ComputeOperatorMask(gradG, Plane::EQUAL_MSK);
	Plane* gradRGB = gradRG->ComputeOperatorMask(gradB, Plane::EQUAL_MSK);

	delete gradR;
	delete gradG;
	delete gradB;
	delete gradRG;

	gradRGB->SaveAsPNG("EncoderDebug\\RawGradient.png");

	Plane* smoothMap = gradRGB->ComputeOperatorMask(mipmapMask, Plane::AND_OP);

	smoothMap->SaveAsPNG("EncoderDebug\\RawGradientPostMipmapClip.png");

	smoothMap->Erosion();
	// First remove useless pixels
	// Make 1/4 Resolution with special operator.
	smoothMap = smoothMap->ReduceQuarterLogicMax();

	smoothMap->SaveAsPNG("EncoderDebug\\Smooth.png");
	smoothMap->SaveAsPNG("EncoderDebug\\SmoothQuarter.png");

	BoundingBox bbSmoothMap;
	int pixelAmount = smoothMap->GetBoundingBoxNonZeros(bbSmoothMap);

	// But also that we use swizzle decoder, need to be tile aligned in image space.
	// 8 pixel alignement in smooth map space -> 16 pixel alignment in screen space.
	int endX = bbSmoothMap.w + bbSmoothMap.x;
	int endY = bbSmoothMap.h + bbSmoothMap.y;
		// Align 8 pixel left
		bbSmoothMap.x >>= 3; bbSmoothMap.x <<= 3;
		bbSmoothMap.w = (((endX+7)>>3)<<3) - bbSmoothMap.x;
		bbSmoothMap.y >>= 3; bbSmoothMap.y <<= 3;
		bbSmoothMap.h = (((endY+7)>>3)<<3) - bbSmoothMap.y;

	// TODO : 'Skip pixel that we know are encoded in PREVIOUS MASKS !
	unsigned char* maskAs1Bit = NULL;
	int surface = bbSmoothMap.w * bbSmoothMap.h;

	int byteSizeDecomp = surface ? make1BitStream(smoothMap, bbSmoothMap, NULL, maskAs1Bit) : 0;

	unsigned char* pZStdStream = new unsigned char[byteSizeDecomp * 2];
	int lastResult = 0;
	int lastDecomp = 21;
	size_t result;

	if (surface) {
		// ----------------------------------------------------------------------
		lastResult = 99999999;
		for (int n=21; n > 10; n--)
		{	memset(pZStdStream, 0, byteSizeDecomp * 2);
			result = ZSTD_compress(pZStdStream, byteSizeDecomp * 2, maskAs1Bit, byteSizeDecomp, n);
			if (ZSTD_isError(result)) {
				//
				printf("Error");
			}

			if (lastResult > result) {
				lastResult = (int)result;
				lastDecomp = n;
			} else {
				if (result > lastResult) {
					break;
				}
			}
		}

		memset(pZStdStream, 0, byteSizeDecomp * 2);
		result = ZSTD_compress(pZStdStream, byteSizeDecomp * 2, maskAs1Bit, byteSizeDecomp, lastDecomp);

		printf("Smooth Map : %i\n", (int)result);

	} else {
		printf("NO SMOOTH MAP");
		result = 0;
	}


	// ----------------------------------------------------------------------
	//
	// Build RGB stream of RGB pixel at 1/16 resolution only included
	// with smooth map at 1/4 resolution.
	//
	int maxSize = (3 * YCoCgImg->GetHeight() * YCoCgImg->GetWidth()) / 16;
	unsigned char* streamRGB = new unsigned char[maxSize];
	if (surface) {
		int startX = 0; // 0,1,2,3
		int startY = 0; // 0,1,2,3

		int currSize = 0;
		unsigned char* wRGB = streamRGB;

		for (int y = startY; y < YCoCgImg->GetHeight(); y += 4) {
			for (int x = startX; x < YCoCgImg->GetWidth(); x += 4) {
				int index = x + y * YCoCgImg->GetWidth();
				// Read RGB
				bool isOut;
				if (smoothMap->GetPixelValue(x, y, isOut) /*&& (mipmapMask->GetPixelValue(x,y,isOut)!=0)*/) {
					// Remove from future export... Important for Tile Encoding.
					mipmapMask->SetPixel(x,y,0);

					int yC  = YCoCgImg->GetPlane(0)->GetPixelValue(x, y, isOut);
					int co = YCoCgImg->GetPlane(1)->GetPixelValue(x, y, isOut);
					int cg = YCoCgImg->GetPlane(2)->GetPixelValue(x, y, isOut);
//					printf("x:%i,y:%i = [%i,%i,%i]\n",x,y,r,g,b);

					smoothMap->GetPixels()[x + (y* smoothMap->GetWidth())] = 128;
					// Valid pixel RGB.
					*wRGB++ = yC;
					*wRGB++ = co;
					*wRGB++ = cg;

					output->SetPixel(x,y,yC,co,cg);
				}
			}
		}

		smoothMap->SaveAsPNG("EncoderDebug\\GradientMap.png");
		maxSize = (int)(wRGB - streamRGB);
	} else {
		maxSize = 0;
	}

	unsigned char* pZStdStream2 = new unsigned char[maxSize * 2]; // Max in case...;
	size_t result2;

	if (surface) {
		lastResult = 99999999;
		lastDecomp = 21;
		for (int n = 21; n > 10; n--)
		{ 
			memset(pZStdStream2, 0, maxSize * 2);
			result2 = ZSTD_compress(pZStdStream2, maxSize * 2, streamRGB, maxSize, n);
			if (ZSTD_isError(result2)) {
				//
				printf("Error");
			}

			if (lastResult > result2) {
				lastResult = (int)result2;
				lastDecomp = n;
			} else {
				if (result2 > lastResult) {
					break;
				}
			}
		}
		printf("RGB Gradient Map : %i\n", (int)result2);
	} else {
		result2 = 0;
	}

	mipmapMask->RemoveMask(smoothMap->SampleUp(false,false,false));

	HeaderBase headerTag;
	headerTag.tag.tag8[0] = 'S'; // Smooth Map
	headerTag.tag.tag8[1] = 'M';
	headerTag.tag.tag8[2] = 'A';
	headerTag.tag.tag8[3] = 'P';
	int baseSize = (sizeof(HeaderSmoothMap)+ (int)result2 + (int)result); // Skip color zero in file.
	headerTag.length = ((baseSize + 3) >> 2) << 2;	// Round multiple of 4.
	u8 pad[3] = { 0,0,0 };
	int padding = headerTag.length - baseSize;

	HeaderSmoothMap headerSmap;
	headerSmap.version = 1;
	headerSmap.bbox.x = bbSmoothMap.x;
	headerSmap.bbox.y = bbSmoothMap.y;
	headerSmap.bbox.w = bbSmoothMap.w;
	headerSmap.bbox.h = bbSmoothMap.h;
	headerSmap.streamSize		= (int)result;
	headerSmap.rgbStreamSize	= (int)result2;
	headerSmap.expectedRGBStreamSize	= maxSize;
	headerSmap.grid				= (0<<4) | (0); // Grid [0,0] 

	fwrite(&headerTag,  sizeof(HeaderBase),			1, outFile);
	fwrite(&headerSmap, sizeof(HeaderSmoothMap),	1, outFile);

	fwrite(pZStdStream,  1, result,  outFile);
	fwrite(pZStdStream2, 1, result2, outFile);

	if (padding) {
		fwrite(pad, 1, padding, outFile);
	}

	delete[] pZStdStream;
	delete[] pZStdStream2;
	delete[] streamRGB;
}

void EncoderContext::convRGB2YCoCg(bool notUseRGBAsIs) {
	YCoCgImg = original->ConvertToRGB2YCoCg(notUseRGBAsIs);
}

void EncoderContext::chromaReduction() {
	if (halfCoW || halfCoH) {
		workCo = YCoCgImg->GetPlane(1)->SampleDown(halfCoW, halfCoH, downSampleCo);
	} else {
		workCo = YCoCgImg->GetPlane(1)->Clone();
	}

	if (halfCgW || halfCgH) {
		workCg = YCoCgImg->GetPlane(2)->SampleDown(halfCgW, halfCgH, downSampleCg);
	} else {
		workCg = YCoCgImg->GetPlane(2)->Clone();
	}
}

void EncoderContext::CheckMipmapMask() {
	if (mipmapMask == NULL) {
		mipmapMask = new Plane(original->GetWidth(), original->GetHeight());
		BoundingBox r = mipmapMask->GetRect();
		mipmapMask->Fill(r, 255);
		boundX0 = r.x;
		boundY0 = r.y;
		boundX1 = r.w;
		boundY1 = r.h;
	}
}

void EncoderContext::PrepareQuadSmooth() {
	int size = (original->GetWidth()/4) * (original->GetHeight()/4) * 3;
	smoothRGBMap4 = new u8[size];
	// [TODO : could optimize by scanning original colors, select most used color by default]
	memset(smoothRGBMap4,0,size);

	smoothBitMap4 = new u8[size];
	memset(smoothBitMap4,0,size);
}

int  EncoderContext::FittingQuadSmooth(int rejectFactor, Image* testOutput, int tileShift) {
	CheckMipmapMask();

	int TileDone = 0;
	int tileSize = 1<<tileShift;

	bool createMap = false;

	int weight4 [] = { 1024,768,512, 256,0 };
	int weight8 [] = { 1024,896,768, 640,512,384,256,128, 0 };
	int weight16[] = { 1024,960,896,832,768,704,640,576,512,448,384,320,256,192,128,64, 0 };

	if (!smoothMap) {
		createMap = true;
		smoothMap = new Plane(YCoCgImg->GetWidth(),YCoCgImg->GetHeight());
		BoundingBox bb = smoothMap->GetRect();
		smoothMap->Fill(bb,0);
	}

	// +1 +1 because of lower side corners.
	int streamW		= (YCoCgImg->GetWidth ()/tileSize)+1;
	int streamH		= (YCoCgImg->GetHeight()/tileSize)+1;

	int sizeStream  = streamW * streamH * 3;
	u8* rgbStream   = new u8[sizeStream];
	u8* wrRgbStream = rgbStream;
	memset(rgbStream, 0, sizeStream);

	int rTile[256];
	int gTile[256];
	int bTile[256];

	int pos = 0;
	int sizeBitmap = (YCoCgImg->GetWidth() >> tileShift) * (YCoCgImg->GetHeight() >> tileShift) >> 3;
	u8* pFillBitMap = new u8[sizeBitmap];
	memset(pFillBitMap,0, sizeBitmap);

	for (int y=0; y < YCoCgImg->GetHeight(); y += tileSize) {

		bool prevTileReject = true;

		for (int x=0; x < YCoCgImg->GetWidth(); x += tileSize) {
			int tileX = x>>tileShift;
			int tileY = y>>tileShift;

			bool isOutside;

			// From tile 8->4
			if (smoothMap->GetPixelValue(x,y,isOutside) != 0) {
				continue;
			}

			int rgbTL[3]; 
			int rgbTR[3];
			int rgbBL[3];
			int rgbBR[3];
			YCoCgImg->GetPixel(x         ,y         ,rgbTL,isOutside);
			YCoCgImg->GetPixel(x+tileSize,y         ,rgbTR,isOutside);
			YCoCgImg->GetPixel(x         ,y+tileSize,rgbBL,isOutside);
			YCoCgImg->GetPixel(x+tileSize,y+tileSize,rgbBR,isOutside);

			// Compute fit or not.
			int lF,rF;
			int tF,bF;
			bool rejectTile = false;

			for (int dy = 0; dy < tileSize; dy++) {
				if (tileSize == 4) {
					tF = weight4[dy];
				} else {
					if (tileSize == 8) {
						tF = weight8[dy];
					} else {
						tF = weight16[dy];
					}
				}
				bF = 1024-tF;

				for (int dx = 0; dx < tileSize; dx++) {
					int rgbCurr[3]; YCoCgImg->GetPixel(x+dx,y+dy,rgbCurr,isOutside);
					if (tileSize == 4) {
						lF = weight4[dx];
					} else {
						if (tileSize == 8) {
							lF = weight8[dx];
						} else {
							lF = weight16[dx];
						}
					}
					rF = 1024-lF;


					int blendT[3];
					int blendB[3];
					int blendC[3];
					for (int ch=0; ch<3;ch++) { 
						blendT[ch] = rgbTL[ch] * lF + rgbTR[ch] * rF; // *1024 scale
						blendB[ch] = rgbBL[ch] * lF + rgbBR[ch] * rF;

						blendC[ch] = (blendT[ch] * tF + blendB[ch] * bF) / (1024*1024);
					}

					int idxT = dx + dy*tileSize;
					rTile[idxT] = blendC[0];
					gTile[idxT] = blendC[1];
					bTile[idxT] = blendC[2];

					if ((abs(rgbCurr[0]-blendC[0])>rejectFactor) || (abs(rgbCurr[1]-blendC[1])>rejectFactor)  || (abs(rgbCurr[2]-blendC[2])>rejectFactor)) {
						rejectTile = true;
						break;
					}
				}
			}

			if (!rejectTile) {
				if (smoothMap->GetPixelValue(x,y,isOutside) == 0) {
					bool isOutside;
					pFillBitMap[pos>>3] |= smoothMap->GetPixelValue(x,y,isOutside) ? (1<<(pos&7)) : 0;

					for (int dy = 0; dy < tileSize; dy++) {
						for (int dx = 0; dx < tileSize; dx++) {
							smoothMap->SetPixel(x+dx,y+dy,255);
						}
					}

					TileDone++;

					int idx = (tileX + streamW*tileY) * 3;

					for (int dy = 0; dy < tileSize; dy++) {
						for (int dx = 0; dx < tileSize; dx++) {
							int idxT = dx + dy*tileSize;
							// Generate RGB interpolated for compare
							testOutput->SetPixel(x+dx,y+dy,rTile[idxT],gTile[idxT],bTile[idxT]);
						}
					}


					// Write Top Left
					int readMapRGB4Idx = ((x>>2) + ((y>>2) * (YCoCgImg->GetWidth()>>2)))*3;
					u8* pRGB = &smoothRGBMap4[readMapRGB4Idx];
					if ((pRGB[0] == 0) && (pRGB[1] == 0) && (pRGB[2] == 0)) {
						for (int ch=0;ch<3;ch++) {
							*wrRgbStream++ = rgbTL[ch];
						}
						pRGB[0] = rgbTL[0];
						pRGB[1] = rgbTL[1];
						pRGB[2] = rgbTL[2];
					}

					// Top Right
					pRGB = &smoothRGBMap4[readMapRGB4Idx + ((1<<(tileShift-2))*3)];
					if ((pRGB[0] == 0) && (pRGB[1] == 0) && (pRGB[2] == 0)) {
						for (int ch=0;ch<3;ch++) {
							*wrRgbStream++ = rgbTR[ch];
						}
						pRGB[0] = rgbTR[0];
						pRGB[1] = rgbTR[1];
						pRGB[2] = rgbTR[2];
					}

					// Bottom Left
					pRGB = &smoothRGBMap4[readMapRGB4Idx + ((1<<(tileShift-2))*3*(YCoCgImg->GetWidth()>>2))];
					if ((pRGB[0] == 0) && (pRGB[1] == 0) && (pRGB[2] == 0)) {
						for (int ch=0;ch<3;ch++) {
							*wrRgbStream++ = rgbBL[ch];
						}
						pRGB[0] = rgbBL[0];
						pRGB[1] = rgbBL[1];
						pRGB[2] = rgbBL[2];
					}

					// Bottom Right
					pRGB += (1<<(tileShift-2))*3;
					if ((pRGB[0] == 0) && (pRGB[1] == 0) && (pRGB[2] == 0)) {
						for (int ch=0;ch<3;ch++) {
							*wrRgbStream++ = rgbBR[ch];
						}
						pRGB[0] = rgbBR[0];
						pRGB[1] = rgbBR[1];
						pRGB[2] = rgbBR[2];
					}

					/*
					for (int ch=0;ch<3;ch++) {
						rgbStream[idx+ch]   = rgbTL[ch];
						rgbStream[idx+ch+3] = rgbTR[ch];
						rgbStream[idx+ch+(streamW*3) ] = rgbBL[ch];
						rgbStream[idx+ch+(streamW*3)+3] = rgbBR[ch];
					*/
				}
			}

			pos++;
			prevTileReject = rejectTile;
		}
	}

	{
		int sizeDec = streamW * streamH * 3;
		unsigned char* pZStdStream = new unsigned char[streamH * streamW * 3 * 2];
		int result = (int)ZSTD_compress(pZStdStream, sizeDec * 2, rgbStream, wrRgbStream - rgbStream, 18);
		fwrite(pZStdStream,1,result,outFile);
		delete[] pZStdStream;

		printf("RGB MAP %i SMOOTH : %i\n",tileSize,result);
	}

		{
			int sizeDec = sizeBitmap * 2;
			unsigned char* pZStdStream = new unsigned char[sizeDec];
			int result = (int)ZSTD_compress(pZStdStream, sizeDec, pFillBitMap, sizeBitmap, 18);
			fwrite(pZStdStream,1,result,outFile);
			delete[] pZStdStream;
			printf("BITMAP %i SMOOTH : %i\n",tileSize,result);
		}

		delete[] pFillBitMap;

	if (tileSize ==  4) { smoothMap->SaveAsPNG("NewSmoothMap4.png"); }
	if (tileSize ==  8) { smoothMap->SaveAsPNG("NewSmoothMap8.png"); }
	if (tileSize == 16) { smoothMap->SaveAsPNG("NewSmoothMap16.png"); }

	return TileDone;
}

int EncoderContext::DynamicTileEncode(bool mode3BitOnly, Plane* yPlane, Plane* dst, bool isCo, bool isCg, bool isHalfX, bool isHalfY) {

	CheckMipmapMask();

	int dw = yPlane->GetWidth () / 8;
	int dh = yPlane->GetHeight() / 8;

	TileInfo* mapTile = new TileInfo[dw*dh];

	int sizeMap = yPlane->GetWidth() * yPlane->GetHeight();
	unsigned char* tileCodeStream     = new unsigned char[sizeMap];
	unsigned char* tileCodeStream4Bit = new unsigned char[sizeMap];

	unsigned char* tileMap = new unsigned char[dw * dh];
	int usedTileMap = 0;

	Plane* debug = new Plane( yPlane->GetWidth (), yPlane->GetHeight ());

	LeftRightOrder HorizParserTile(yPlane,8, 8);
	BoundingBox constraint;

	constraint.x = (boundX0>>3)<<3;
	constraint.y = (boundY0>>3)<<3;
	int widthT8  = (((boundX1+7)>>3)<<3) - constraint.x;
	int heightT8 = (((boundY1+7)>>3)<<3) - constraint.y;
	constraint.w = widthT8;
	constraint.h = heightT8;

	if (isHalfX) {
		constraint.x >>= 1;
		constraint.w >>= 1;
	}

	if (isHalfY) {
		constraint.y >>= 1;
		constraint.h >>= 1;
	}

	HorizParserTile.constraint = constraint;

	HorizParserTile.Start();
	bool isSubMarker;
	int layerSize = 0;
	int tileSize = 0;
	
	// Force to use 4 bit modes...
	Stats stats;
	stats.startMode = mode3BitOnly ? 3 : 0;
	stats.mode = 0;
	stats.bitCount = 0;
	stats.distError = 0;

	int pixelEncoded = 0;

	u16* streamTileDef = new u16[dw*dh];
	u16* wrTileDef = streamTileDef;
	u8*  streamTileIdx = new u8[dw*dh*32];
	memset(streamTileIdx,0,dw*dh*32);

	TileInfo currentTileInfo;
	currentTileInfo.indexGlobal = 0;
	currentTileInfo.indexStream = streamTileIdx;

	while (HorizParserTile.HasNextBlock(isSubMarker)) {
		BoundingBox& r = HorizParserTile.GetCurrentBlock(false);
		GetTileEncode_Y(currentTileInfo, yPlane, mipmapMask, smoothMap,NULL, r, tileSize, stats);

//		printf("R[%i,%i]=%i (Total %i)\n",r.x,r.y,currentTileInfo.valueCount,currentTileInfo.indexGlobal);

		if (currentTileInfo.valueCount) {
			// Put tile in the map, when at least one pixel is present :-)
			// Later we can optimize that with a direct RGB value stream I guess... 6 Bit (Idx Pixel in tile) + 21 bit RGB ?
			*wrTileDef++ = EncodeTileType(currentTileInfo.type,currentTileInfo.range,currentTileInfo.base);
		}

//		if (debug) {
			// Correct math :
			// int offset = currentTileInfo.useSigned ? -128 : 0; ==> Decoder look at BASE and select correct LUT For negative ranges...

			// But for PNG debug make them brighter.
		int offset = (isCg | isCo) ? (currentTileInfo.useSigned ? -128 : 0)
				                   : 0 /*Y*/;

		if (!isHalfX && !isHalfY) {
			for (int y=r.y; y < (r.y+r.h); y++) {
				for (int x=r.x; x < (r.x+r.w); x++) {
					int v = currentTileInfo.values[(x-r.x) + ((y-r.y)<<3)];
					if (v != -999) {
						dst->SetPixel(x,y,v + offset);
						// NEVER DO THAT : mipmapMask->SetPixel(x,y,0);  We need VALUE=1 for interpolator.
					}
				}
			}
		} else if (isHalfX && isHalfY) {
			// 1/4 size
			for (int y=r.y; y < (r.y+r.h); y++) {
				for (int x=r.x; x < (r.x+r.w); x++) {
					int v = currentTileInfo.values[(x-r.x) + ((y-r.y)<<3)];
					if (v != -999) {
						// Straight Write, interpolation comes later.
						dst->SetPixel(x*2,y*2,v + offset);
						/*
						dst->SetPixel(x*2+1,y*2,v + offset);
						dst->SetPixel(x*2,y*2+1,v + offset);
						dst->SetPixel(x*2+1,y*2+1,v + offset);
						*/
						// NEVER DO THAT : mipmapMask->SetPixel(x,y,0);  We need VALUE=1 for interpolator.
					}
				}
			}
		} else {
			if (isHalfX) {
				// 1/2 size X Axis
				for (int y=r.y; y < (r.y+r.h); y++) {
					for (int x=r.x; x < (r.x+r.w); x++) {
						int v = currentTileInfo.values[(x-r.x) + ((y-r.y)<<3)];
						if (v != -999) {
							// Straight Write, interpolation comes later.
							dst->SetPixel(x*2,y,v + offset);
							// NEVER DO THAT : mipmapMask->SetPixel(x,y,0);  We need VALUE=1 for interpolator.
						}
					}
				}
			} else {
				// 1/2 size Y Axis
				for (int y=r.y; y < (r.y+r.h); y++) {
					for (int x=r.x; x < (r.x+r.w); x++) {
						int v = currentTileInfo.values[(x-r.x) + ((y-r.y)<<3)];
						if (v != -999) {
							// Straight Write, interpolation comes later.
							dst->SetPixel(x,y*2,v + offset);
							// NEVER DO THAT : mipmapMask->SetPixel(x,y,0);  We need VALUE=1 for interpolator.
						}
					}
				}
			}
		}
//		}
	}

	if (!isCo && !isCg) {
		debug->SaveAsPNG("TileMapY.png");
	} else {
		if (isCo) {
			debug->SaveAsPNG("TileMapCo.png");
		} else {
			debug->SaveAsPNG("TileMapCg.png");
		}
	}

	// [ Y TILE MAP STREAM ]
	unsigned char* pZStdStreamTileMap = new unsigned char[dw * dh * 3]; // Max in case...
	size_t result;
	{	memset(pZStdStreamTileMap, 0, dw * dh * 3);
		result = ZSTD_compress(pZStdStreamTileMap, dw * dh * 3, streamTileDef, (wrTileDef - streamTileDef) * sizeof(u16), 21);
		if (ZSTD_isError(result)) {
			printf("Error");
		}
	}

	if (currentTileInfo.indexGlobal & 1) {
		currentTileInfo.indexGlobal++; // CLOSE STREAM IF HALF BYTE.
	}

	// [ Y STREAM PIXELS... ]
	unsigned char* pZStdStreamYPixels = new unsigned char[dw*dh*64]; // Max in case...
	size_t result2;
	{	memset(pZStdStreamYPixels, 0, dw*dh*64);
		result2 = ZSTD_compress(pZStdStreamYPixels, dw*dh*64, streamTileIdx, currentTileInfo.indexGlobal>>1, 21);
		if (ZSTD_isError(result2)) {
			printf("Error");
		}
	}

	HeaderBase headerTag;
	headerTag.tag.tag8[0] = 'P'; // Color Plane 
	headerTag.tag.tag8[1] = 'L';
	headerTag.tag.tag8[2] = 'N';
	headerTag.tag.tag8[3] = 'T';
	int baseSize = (sizeof(PlaneTile)+ (int)result2 + (int)result); // Skip color zero in file.
	headerTag.length = ((baseSize + 3) >> 2) << 2;	// Round multiple of 4.
	u8 pad[3] = { 0,0,0 };
	int padding = headerTag.length - baseSize;

	PlaneTile planeTile;
	planeTile.version				= 1;
	planeTile.bbox					= constraint;
	planeTile.streamSizeTileMap		= (u32)result;
	planeTile.streamSizeTileStream	= (u32)result2;
	planeTile.expectedSizeTileStream= currentTileInfo.indexGlobal>>1;

#if 0
	BoundingBox r;
	r.x = constraint.x;
	r.y = constraint.y;
	r.w = constraint.w;
	r.h = constraint.h;
	int countPixel = this->mipmapMask->GetBoundingBoxNonZeros(r);
	printf("%i\n",countPixel);
	mipmapMask->SaveAsPNG("Stupid.png");
#endif

	planeTile.version = 1;

	int type;
	if (isCo) {
		type = 1;
	} else {
		if (isCg) {
			type = 2;
		} else {
			type = 0;
		}
	}
	planeTile.format				= (type << 2) | (isHalfX ? 1:0) | (isHalfY ? 2:0); // Y Plane, full resolution.

	fwrite(&headerTag,  sizeof(HeaderBase),			1, outFile);
	fwrite(&planeTile,  sizeof(PlaneTile),			1, outFile);

	fwrite(pZStdStreamTileMap,  1, result,  outFile);
	fwrite(pZStdStreamYPixels, 1, result2, outFile);

	if (padding) {
		fwrite(pad, 1, padding, outFile);
	}

	printf("Tile 8x8 Map Info : %i\n",(int)result);
	printf("Tile Pixel Stream size : %i\n",(int)result2);

	delete[] pZStdStreamTileMap;
	delete[] pZStdStreamYPixels;

	delete[] tileMap;
	delete[] tileCodeStream;
	delete[] tileCodeStream4Bit;
	delete[] mapTile;
	return layerSize;
}

void EncoderContext::ResampleUpCoCg(const char* optionalRawSave) {
	// Rollback to full size with bilinear filtering.
	/*
	Plane* CoBack = workCoTile->SampleUp(halfCoW, halfCoH, true); // TODO : last pixel
	Plane* CgBack = workCgTile->SampleUp(halfCgW, halfCgH, true);

	//	delete quarterCo;
	//	delete quarterCg;

	YCoCgImg->replacePlane(1, CoBack, true);
	YCoCgImg->replacePlane(2, CgBack, true);


	if (optionalRawSave) {
		bool signedF[4];
		signedF[0] = false;
		signedF[1] = true;
		signedF[2] = true;
		signedF[3] = false;

		char tmpBuff[2000];
		sprintf(tmpBuff, "YCoCgRaw_%s", optionalRawSave); YCoCgImg->savePNG(tmpBuff, signedF);
	}
	*/
}

void EncoderContext::SaveTo2RGB(bool doConversion, const char* optionnalFileName) {
	if (optionnalFileName) {
		Image* pngBack = YCoCgImg->ConvertToYCoCg2RGB(doConversion);

		bool signedF[4];
		signedF[0] = false;
		signedF[1] = false;
		signedF[2] = false;
		signedF[3] = false;
		pngBack->SavePNG(optionnalFileName, signedF);
	}
}

void EncoderContext::Release() {
	// TODO
}


struct EvalCtx {
	EvalCtx(float acceptanceScore, int* tblStorage, int r, int g, int b):tableStorage(tblStorage) {
		color[0] = r;
		color[1] = g;
		color[2] = b;
		acceptScore = acceptanceScore;
	}

	int*  tableStorage;
	int   sumDistance;
	int   sampleCount;
	int   distSamples[64];

	float acceptScore;
	int   color[3];

	void EvaluateStart() {
		sumDistance = 0;
		sampleCount = 0;
	}

	void EvaluatePoint(int x, int y) {
		int dist = tableStorage[x + (y<<6)];
		distSamples[sampleCount++] = dist;
		sumDistance += dist;
	}

	void GetEvaluation(float& score, float& stdDeviation) {
		float avg = sumDistance / (float)(sampleCount*1024.0f);
		float stdDev = 0.0f;
		for (int n=0; n < sampleCount; n++) {
			float diffAvg = avg - (distSamples[n]/1024.0f);	
			stdDev += (diffAvg * diffAvg);
		}
		score = avg;
		stdDeviation = stdDev / sampleCount;
	}
};

#include "QuadSpline.h"

void BuildTable(QuadSpline* list, int countList, int* target64x64) {
	for (int y=0; y < 64; y++) {
		for (int x=0; x < 64; x++) {
			float minDist = 999999999.0f;
			for (int c = 0; c < countList; c++) {
				int idxPt;
				float dist = list[c].ComputeDistance(x,y,idxPt);
				target64x64[(y<<6) + x] = (int)(dist*1024.0f);
			}
		}
	}

	printf("\n");

	for (int y=0; y < 64; y++) {
		for (int x=0; x < 64; x++) {
			printf("%06i,",target64x64[(y<<6) + x]);
		}
		printf("\n");
	}

}

void EncoderContext::convert(const char* outputFile) {
	FILE* outF = fopen(outputFile, "wb");

	if (outF) {
		outFile = outF;

		// ---------------------------------------------------
		// Write Header
		FileHeader header;
		header.width	= original->GetWidth();
		header.height	= original->GetHeight();
		header.tag.tag8[0] = 'Y';
		header.tag.tag8[1] = 'A';
		header.tag.tag8[2] = 'I';
		header.tag.tag8[3] = 'K';
		header.version	= 1;
		header.infoMask = original->HasAlpha() ? 1 : 0;
		fwrite(&header, sizeof(FileHeader), 1, outF);
		// ---------------------------------------------------

		bool doConversionRGB2YCoCg = true;

		convRGB2YCoCg(doConversionRGB2YCoCg);
		Image* imgRGB = YCoCgImg->ConvertToYCoCg2RGB(doConversionRGB2YCoCg);
		imgRGB->SavePNG("recoded.png",NULL);

		chromaReduction();

//			MipPrefilter(true);
//			ProcessAlpha(true);

		Image* output = Image::CreateImage(this->original->GetWidth(), this->original->GetHeight(),3,true);

		Plane* outY  = output->GetPlane(0);
		Plane* outCo = output->GetPlane(1);
		Plane* outCg = output->GetPlane(2);

//			SingleColorOut(true, output);	// Saves time to do next stuff...
//			mipmapMask->SaveAsPNG("PostSingleColorMask.png");

//			output->savePNG("outputEncoder.png",NULL);
//			output->convertToYCoCg2RGB()->savePNG("outputEncoderRGB.png",NULL);

		PrepareQuadSmooth();

		int rejectFactor = 3;
		FittingQuadSmooth(rejectFactor,output,4);

		output->ConvertToYCoCg2RGB(doConversionRGB2YCoCg)->SavePNG("outputEncoderRGB.png",NULL);

		FittingQuadSmooth(rejectFactor,output,3);

		output->ConvertToYCoCg2RGB(doConversionRGB2YCoCg)->SavePNG("outputEncoderRGB.png",NULL);

		FittingQuadSmooth(rejectFactor,output,2);

		output->ConvertToYCoCg2RGB(doConversionRGB2YCoCg)->SavePNG("outputEncoderRGB.png",NULL);

		output->SavePNG("outputTileScaled.png",NULL);
//			output->savePNG("outputEncoder.png",NULL);
//			output->convertToYCoCg2RGB()->savePNG("outputEncoderRGB.png",NULL);

		DynamicTileEncode(false,YCoCgImg->GetPlane(0), outY, false, false, false,false);
		DynamicTileEncode(false,workCo, outCo,true, false, halfCoW,halfCoH);
		DynamicTileEncode(true ,workCg, outCg,false, true, halfCgW,halfCgH);

		int bx = 0;
		int by = 0;
		int tsize = 8;
		bool isOutSide;
		BoundingBox bb;
		bool isIsFirst = true;

		int linearTable		[64*64];
		int linearOppTable	[64*64];
		int Spline1Table	[64*64];
		int tileCnt = 0;

		QuadSpline spl;
 		spl.Set(0.0,0.0,    32.0,32.0,    64.0,64.0);
		BuildTable(&spl,1,linearTable);


		spl.Set(64.0,0.0,    32.0,32.0,    0.0,64.0);
		BuildTable(&spl,1,linearOppTable);

		spl.Set(0.0,0.0,    75.0,48.0,    20.0,64.0);
		BuildTable(&spl,1,Spline1Table);

		EvalCtx linearEval   (40.0f, linearTable,255,0,0);
		EvalCtx linearOppEval(40.0f, linearOppTable,0,255,0);
		EvalCtx Spline1      (40.0f, Spline1Table,255,128,0);

		EvalCtx* evaluators[3];
		evaluators[0] = &linearEval;
		evaluators[1] = &linearOppEval;
		evaluators[2] = &Spline1;

		int      evaluatorsCount = 2;

		u8* renderBuffer = new u8[64*64*3];

		for (int py = 0; py < workCo->GetHeight(); py += tsize) {
			for (int px = 0; px < workCg->GetWidth(); px += tsize) {
				tileCnt++;
				// For all tiles.

				isIsFirst = true;

				for (int y=py; y < py+tsize; y++) {
					for (int x=px; x < px+tsize; x++) {
						int a = workCo->GetPixelValue(x,y,isOutSide);
						int b = workCg->GetPixelValue(x,y,isOutSide);
						if (isIsFirst) {
							bb.x = a;
							bb.w = a;

							bb.y = b;
							bb.h = b;
							isIsFirst = false;
						} else {
							if (a < bb.x) { bb.x = a; }
							if (a > bb.w) { bb.w = a; }
							if (b < bb.y) { bb.y = b; }
							if (b > bb.h) { bb.h = b; }
						}
						// printf("%i %i\n",a,b);
					}
				}

				// Export normalized values...
				int dX = bb.w - bb.x;
				int dY = bb.h - bb.y;


				float div = (float)(1<<20);

				if ((dX == 0) || (dY == 0)) {
					// Co or Cg plane is a SINGLE VALUE or BOTH.
					// -> Case not detected because variation in the Y plane using Planar Mode.
					printf("Planar also...\n");
				} else {

					int nX = (1<<20)/dX;
					int nY = (1<<20)/dY;
					// Now try to find in the CoCg plane :
					// - Can we fit on a line ?
					// - Can we fit on a spline ?
					// - Can we fit on a triangle ? (PCA like)

					// Normalization 1.0 fixed 22 bit.
					float line1Prox    = 0.0f;
					float line1OppProx = 0.0f;


					bool isOutside;
					if (smoothMap->GetPixelValue(px,py,isOutside)==0) {

						memset(renderBuffer,0,64*64*3);

						for (int e=0;e<evaluatorsCount; e++) {
							evaluators[e]->EvaluateStart();
						}

						for (int y=py; y < py+tsize; y++) {
							for (int x=px; x < px+tsize; x++) {
								int a = workCo->GetPixelValue(x,y,isOutSide);
								int b = workCg->GetPixelValue(x,y,isOutSide);
					
								// Origin.
								a -= bb.x;
								b -= bb.y;

								// Rescale to 1.0 fixed.
								a *= nX;
								b *= nY;

								float fa = a/div;
								float fb = b/div;

								int ia64 = ((int)(fa*63));
								int ib64 = ((int)(fb*63));

								// printf("%f,%f\n",fa,fb);
								for (int e=0;e<evaluatorsCount; e++) {
									evaluators[e]->EvaluatePoint(ia64,ib64);
								}


								int idx = (ia64 + (ib64*64)) * 3;
								renderBuffer[idx    ] = 255;
								renderBuffer[idx + 1] = 255;
								renderBuffer[idx + 2] = 255;

								/*
									TODO : Generate a 128x128 Plot
									Save as PNG [x,y_size.png] + Log printf score associated.
								*/


								line1Prox    += powf(1.0f+fabsf(fa-fb),3.0f);
								line1OppProx += powf(1.0f+fabsf((fa+fb)-1.0f),3.0f);	// The worst the outlier, the huge impact on scoring.
							}
						}

						char buffer[2000];
						sprintf(buffer,"tiles\\tileMap_%i_(%i-%i_%i-%i).png",tileCnt,bb.x,bb.w,bb.y,bb.h);

						bool isMarkedLinear = false;
						/*
						if (line1Prox < 81.0f) {
							for (int n=0; n < 64*64*3; n+=3) {
								renderBuffer[n+1] = 255;
							}
							isMarkedLinear = true;
						}
						if (line1OppProx < 81.0f) {
							for (int n=0; n < 64*64*3; n+=3) {
								renderBuffer[n] = 255;
							}
							isMarkedLinear = true;
						}
						*/
						for (int e=0;e<evaluatorsCount; e++) {
							float score;
							float stdDev;
							evaluators[e]->GetEvaluation(score, stdDev);
							if (evaluators[e]->acceptScore >= score) {
								for (int n=0; n < 64*64*3; n+=3) {
									if (evaluators[e]->color[0]) { renderBuffer[n  ] = evaluators[e]->color[0]; }
									if (evaluators[e]->color[1]) { renderBuffer[n+1] = evaluators[e]->color[1]; }
									if (evaluators[e]->color[2]) { renderBuffer[n+2] = evaluators[e]->color[2]; }
								}
								isMarkedLinear = true;
								break;
							}
							printf("%f, %f %s\n",score,stdDev,isMarkedLinear ? "Yes" : "");
						}

						int err = stbi_write_png(buffer, 64, 64, 3, renderBuffer, 64*3);
						printf("%i -> %f,%f\n",tileCnt,line1Prox,line1OppProx);

					} else {
						// printf("Skip tile %i\n",tileCnt);
					}
				}
			}
		}
		printf("\n");

		/*
			1. Input BSpline
				1.1 Generate 64x64 Error Map
				1.2 Generate 5 Bit LUT.

			2. Encoder routine
				Input Co,Cg value
				2.1 Transform into bound box value.
				2.2 Find closest LUT entry. (Euclidian distance)
		*/

		YCoCgImg->SaveAsYCoCg("TestCoCg.png");

		Interpolate(output,outCo, EInterpMode::QUART_TL_REFERENCE_BILINEAR, halfCoW, halfCoH);
		Interpolate(output,outCg, EInterpMode::QUART_TL_REFERENCE_BILINEAR, halfCgW, halfCgH);

	
		output->ConvertToYCoCg2RGB(doConversionRGB2YCoCg)->SavePNG("outputEncoderRGB.png",NULL);

		Tag endBlk;
		endBlk.tag32 = 0xDEADBEEF;
		fwrite(&endBlk, sizeof(Tag), 1, outF);
		fclose(outF);
	}
}