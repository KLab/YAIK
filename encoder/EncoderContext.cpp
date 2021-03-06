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


#define FACTOR		(128)

void kassert(bool cond) {
	if (!cond) {
		printf("ERR");
	}
}

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

	oY  = Y;    // 0..255
	oCo = Co/2; // -255..+255 -> -127..+127
	oCg = Cg/2; // -255..+255 -> -127..+127 Encoder could keep quality.
}

void RGBtoYCoCgPos(int R, int G, int B, int& oY, int& oCo,int& oCg) {

	// -255..+255
	int Co = R - B;
	// 0..255 + (-127..+127) => -127..382
	int tmp = B + Co / 2;
	// -382..+128
	int Cg = G - tmp;
	// -191..+64 + -127..384 = -318..+448
	int Y = tmp + Cg / 2;

	oY  = Y;    // 0..255
	oCo = (Co/2) + 127; // -255..+255 -> -127..+127 -> 0..254
	oCg = (Cg/2) + 127; // -255..+255 -> -127..+127 -> 0..254 Encoder could keep quality.
}

void YCoCgPostoRGB(int Y, int Co, int Cg, int& oR, int& oG,int& oB) {
	Y  -= 127;
	Co -= 127;
	Cg -= 127;

	Co *= 2;
	Cg *= 2;

	int tmp = Y - Cg / 2;
	int G = Cg + tmp;
	int B = tmp - Co / 2;
	int R = B + Co;

	oR = R * 2;
	oG = G * 2;
	oB = B * 2;
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


BoundingBox3D roundNBit(BoundingBox3D bb3, int sh) {
	const int offS = ((1<<sh)-1);
	// ----- Low Rounding.
	bb3.x0 <<= sh; bb3.x0 >>= sh;
	bb3.y0 <<= sh; bb3.y0 >>= sh;
	bb3.z0 <<= sh; bb3.z0 >>= sh;

	// ----- High Rounding
	bb3.x1 <<= sh; bb3.x1 >>= sh; bb3.x1 += offS;
	bb3.y1 <<= sh; bb3.y1 >>= sh; bb3.y1 += offS;
	bb3.z1 <<= sh; bb3.z1 >>= sh; bb3.z1 += offS;
	return bb3;
}

BoundingBox3D buildBBox3D(Image* p, bool useYCoCg,Image* pFilter, int px, int py, int tsizeX, int tsizeY, int& pixelInTile, u8* pMask, bool& allInside) {
	bool isOutSide;
	bool isIsFirst = true;
	BoundingBox3D bb3;

	int pixelCount = 0;

	// By default.
	allInside = true;

	for (int y=0; y < tsizeY; y++) {
		for (int x=0; x < tsizeX; x++) {
			// Skip scanning.
			int rgb[3];
			pFilter->GetPixel(x+px,y+py,rgb,isOutSide);

			if (isOutSide) {
				allInside = false;
				goto exitFunc;
			}

			if (rgb[0]==255 && rgb[1]==255 && rgb[2]==255) {
				pMask[x + y*tsizeX] = 255;
				continue; 
			}

			pMask[x + y*tsizeX] = pixelCount;
			pixelCount++;
			p->GetPixel(x+px,y+py,rgb,isOutSide);

			if (useYCoCg) {
				RGBtoYCoCgPos(rgb[0],rgb[1],rgb[2],rgb[0],rgb[1],rgb[2]);
			}

			if (isIsFirst) {
				bb3.x0 = rgb[0];
				bb3.x1 = rgb[0];

				bb3.y0 = rgb[1];
				bb3.y1 = rgb[1];

				bb3.z0 = rgb[2];
				bb3.z1 = rgb[2];

				isIsFirst = false;
			} else {
				if (rgb[0] < bb3.x0) { bb3.x0 = rgb[0]; }
				if (rgb[0] > bb3.x1) { bb3.x1 = rgb[0]; }

				if (rgb[1] < bb3.y0) { bb3.y0 = rgb[1]; }
				if (rgb[1] > bb3.y1) { bb3.y1 = rgb[1]; }

				if (rgb[2] < bb3.z0) { bb3.z0 = rgb[2]; }
				if (rgb[2] > bb3.z1) { bb3.z1 = rgb[2]; }
			}
		}
	}

exitFunc:
	if (isIsFirst) { // Not single pixel processed.
		bb3.x0 = -1; bb3.x1 = -1;
		bb3.y0 = -1; bb3.y1 = -1;
		bb3.z0 = -1; bb3.z1 = -1;
	}

	pixelInTile = pixelCount;

	return bb3;
}

BoundingBox buildBBox2D(EncoderContext::PlaneMode planeMode,Image* p, Image* pFilter, int px, int py, int tsizeX, int tsizeY, int& pixelInTile, u8* pMask) {
	bool isOutSide;
	bool isIsFirst = true;
	BoundingBox bb2;

	int pixelCount = 0;

	for (int y=0; y < tsizeY; y++) {
		for (int x=0; x < tsizeX; x++) {
			// Skip scanning.
			int rgb[3];
			int ab [2];
			pFilter->GetPixel(x+px,y+py,rgb,isOutSide);

			switch (planeMode) {
			case EncoderContext::Mode_RG:
				ab[0] = rgb[0];
				ab[1] = rgb[1];
				break;
			case EncoderContext::Mode_GB:
				ab[0] = rgb[1];
				ab[1] = rgb[2];
				break;
			case EncoderContext::Mode_RB:
				ab[0] = rgb[0];
				ab[1] = rgb[2];
				break;
			}
		
			pMask[x + y*tsizeX] = ab[0] | ab[1]; // If any is 255, then mask return 255.

			if ((ab[0]!=0) || (ab[1]!=0)) {
				continue; 
			}

			pixelCount++;
			p->GetPixel(x+px,y+py,rgb,isOutSide);

			switch (planeMode) {
			case EncoderContext::Mode_RG:
				ab[0] = rgb[0];
				ab[1] = rgb[1];
				break;
			case EncoderContext::Mode_GB:
				ab[0] = rgb[1];
				ab[1] = rgb[2];
				break;
			case EncoderContext::Mode_RB:
				ab[0] = rgb[0];
				ab[1] = rgb[2];
				break;
			}
		
			if (isIsFirst) {
				bb2.x = ab[0];
				bb2.y = ab[1];

				bb2.w = ab[0];
				bb2.h = ab[1];

				isIsFirst = false;
			} else {
				if (ab[0] < bb2.x) { bb2.x = ab[0]; }
				if (ab[0] > bb2.w) { bb2.w = ab[0]; }

				if (ab[1] < bb2.y) { bb2.y = ab[1]; }
				if (ab[1] > bb2.h) { bb2.h = ab[1]; }
			}
		}
	}

	if (isIsFirst) { // Not single pixel processed.
		bb2.x = -1; bb2.w = -1;
		bb2.y = -1; bb2.h = -1;
	}

	pixelInTile = pixelCount;

	return bb2;
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

bool EncoderContext::SetImageToEncode(Image* newImage) {
	if (original) {
		delete original;
	}
	original = newImage;
	return (original != NULL);
}

bool EncoderContext::LoadImagePNG(const char* fileName) {
	// Create all the necessary LUT for internal encoding.
	// OUT. DynamicTileEncoderTable();

	if (original) {
		delete original;
	}
	original = Image::LoadPNG(fileName);

	if (original && pStats) {
		FILE* fGlobStat = fopen("globalStats.bin","rb");
		if (fGlobStat) {
			fread(pStats, sizeof(EncoderStats), 1, fGlobStat);
			fclose(fGlobStat);
		}

		pStats->loc.Reset(original->GetWidth(),original->GetHeight());
	}

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
}

static const uint32_t morton256_x[256] =
{
0x00000000,
0x00000001, 0x00000008, 0x00000009, 0x00000040, 0x00000041, 0x00000048, 0x00000049, 0x00000200,
0x00000201, 0x00000208, 0x00000209, 0x00000240, 0x00000241, 0x00000248, 0x00000249, 0x00001000,
0x00001001, 0x00001008, 0x00001009, 0x00001040, 0x00001041, 0x00001048, 0x00001049, 0x00001200,
0x00001201, 0x00001208, 0x00001209, 0x00001240, 0x00001241, 0x00001248, 0x00001249, 0x00008000,
0x00008001, 0x00008008, 0x00008009, 0x00008040, 0x00008041, 0x00008048, 0x00008049, 0x00008200,
0x00008201, 0x00008208, 0x00008209, 0x00008240, 0x00008241, 0x00008248, 0x00008249, 0x00009000,
0x00009001, 0x00009008, 0x00009009, 0x00009040, 0x00009041, 0x00009048, 0x00009049, 0x00009200,
0x00009201, 0x00009208, 0x00009209, 0x00009240, 0x00009241, 0x00009248, 0x00009249, 0x00040000,
0x00040001, 0x00040008, 0x00040009, 0x00040040, 0x00040041, 0x00040048, 0x00040049, 0x00040200,
0x00040201, 0x00040208, 0x00040209, 0x00040240, 0x00040241, 0x00040248, 0x00040249, 0x00041000,
0x00041001, 0x00041008, 0x00041009, 0x00041040, 0x00041041, 0x00041048, 0x00041049, 0x00041200,
0x00041201, 0x00041208, 0x00041209, 0x00041240, 0x00041241, 0x00041248, 0x00041249, 0x00048000,
0x00048001, 0x00048008, 0x00048009, 0x00048040, 0x00048041, 0x00048048, 0x00048049, 0x00048200,
0x00048201, 0x00048208, 0x00048209, 0x00048240, 0x00048241, 0x00048248, 0x00048249, 0x00049000,
0x00049001, 0x00049008, 0x00049009, 0x00049040, 0x00049041, 0x00049048, 0x00049049, 0x00049200,
0x00049201, 0x00049208, 0x00049209, 0x00049240, 0x00049241, 0x00049248, 0x00049249, 0x00200000,
0x00200001, 0x00200008, 0x00200009, 0x00200040, 0x00200041, 0x00200048, 0x00200049, 0x00200200,
0x00200201, 0x00200208, 0x00200209, 0x00200240, 0x00200241, 0x00200248, 0x00200249, 0x00201000,
0x00201001, 0x00201008, 0x00201009, 0x00201040, 0x00201041, 0x00201048, 0x00201049, 0x00201200,
0x00201201, 0x00201208, 0x00201209, 0x00201240, 0x00201241, 0x00201248, 0x00201249, 0x00208000,
0x00208001, 0x00208008, 0x00208009, 0x00208040, 0x00208041, 0x00208048, 0x00208049, 0x00208200,
0x00208201, 0x00208208, 0x00208209, 0x00208240, 0x00208241, 0x00208248, 0x00208249, 0x00209000,
0x00209001, 0x00209008, 0x00209009, 0x00209040, 0x00209041, 0x00209048, 0x00209049, 0x00209200,
0x00209201, 0x00209208, 0x00209209, 0x00209240, 0x00209241, 0x00209248, 0x00209249, 0x00240000,
0x00240001, 0x00240008, 0x00240009, 0x00240040, 0x00240041, 0x00240048, 0x00240049, 0x00240200,
0x00240201, 0x00240208, 0x00240209, 0x00240240, 0x00240241, 0x00240248, 0x00240249, 0x00241000,
0x00241001, 0x00241008, 0x00241009, 0x00241040, 0x00241041, 0x00241048, 0x00241049, 0x00241200,
0x00241201, 0x00241208, 0x00241209, 0x00241240, 0x00241241, 0x00241248, 0x00241249, 0x00248000,
0x00248001, 0x00248008, 0x00248009, 0x00248040, 0x00248041, 0x00248048, 0x00248049, 0x00248200,
0x00248201, 0x00248208, 0x00248209, 0x00248240, 0x00248241, 0x00248248, 0x00248249, 0x00249000,
0x00249001, 0x00249008, 0x00249009, 0x00249040, 0x00249041, 0x00249048, 0x00249049, 0x00249200,
0x00249201, 0x00249208, 0x00249209, 0x00249240, 0x00249241, 0x00249248, 0x00249249
};

// pre-shifted table for Y coordinates (1 bit to the left)
static const uint32_t morton256_y[256] = {
0x00000000,
0x00000002, 0x00000010, 0x00000012, 0x00000080, 0x00000082, 0x00000090, 0x00000092, 0x00000400,
0x00000402, 0x00000410, 0x00000412, 0x00000480, 0x00000482, 0x00000490, 0x00000492, 0x00002000,
0x00002002, 0x00002010, 0x00002012, 0x00002080, 0x00002082, 0x00002090, 0x00002092, 0x00002400,
0x00002402, 0x00002410, 0x00002412, 0x00002480, 0x00002482, 0x00002490, 0x00002492, 0x00010000,
0x00010002, 0x00010010, 0x00010012, 0x00010080, 0x00010082, 0x00010090, 0x00010092, 0x00010400,
0x00010402, 0x00010410, 0x00010412, 0x00010480, 0x00010482, 0x00010490, 0x00010492, 0x00012000,
0x00012002, 0x00012010, 0x00012012, 0x00012080, 0x00012082, 0x00012090, 0x00012092, 0x00012400,
0x00012402, 0x00012410, 0x00012412, 0x00012480, 0x00012482, 0x00012490, 0x00012492, 0x00080000,
0x00080002, 0x00080010, 0x00080012, 0x00080080, 0x00080082, 0x00080090, 0x00080092, 0x00080400,
0x00080402, 0x00080410, 0x00080412, 0x00080480, 0x00080482, 0x00080490, 0x00080492, 0x00082000,
0x00082002, 0x00082010, 0x00082012, 0x00082080, 0x00082082, 0x00082090, 0x00082092, 0x00082400,
0x00082402, 0x00082410, 0x00082412, 0x00082480, 0x00082482, 0x00082490, 0x00082492, 0x00090000,
0x00090002, 0x00090010, 0x00090012, 0x00090080, 0x00090082, 0x00090090, 0x00090092, 0x00090400,
0x00090402, 0x00090410, 0x00090412, 0x00090480, 0x00090482, 0x00090490, 0x00090492, 0x00092000,
0x00092002, 0x00092010, 0x00092012, 0x00092080, 0x00092082, 0x00092090, 0x00092092, 0x00092400,
0x00092402, 0x00092410, 0x00092412, 0x00092480, 0x00092482, 0x00092490, 0x00092492, 0x00400000,
0x00400002, 0x00400010, 0x00400012, 0x00400080, 0x00400082, 0x00400090, 0x00400092, 0x00400400,
0x00400402, 0x00400410, 0x00400412, 0x00400480, 0x00400482, 0x00400490, 0x00400492, 0x00402000,
0x00402002, 0x00402010, 0x00402012, 0x00402080, 0x00402082, 0x00402090, 0x00402092, 0x00402400,
0x00402402, 0x00402410, 0x00402412, 0x00402480, 0x00402482, 0x00402490, 0x00402492, 0x00410000,
0x00410002, 0x00410010, 0x00410012, 0x00410080, 0x00410082, 0x00410090, 0x00410092, 0x00410400,
0x00410402, 0x00410410, 0x00410412, 0x00410480, 0x00410482, 0x00410490, 0x00410492, 0x00412000,
0x00412002, 0x00412010, 0x00412012, 0x00412080, 0x00412082, 0x00412090, 0x00412092, 0x00412400,
0x00412402, 0x00412410, 0x00412412, 0x00412480, 0x00412482, 0x00412490, 0x00412492, 0x00480000,
0x00480002, 0x00480010, 0x00480012, 0x00480080, 0x00480082, 0x00480090, 0x00480092, 0x00480400,
0x00480402, 0x00480410, 0x00480412, 0x00480480, 0x00480482, 0x00480490, 0x00480492, 0x00482000,
0x00482002, 0x00482010, 0x00482012, 0x00482080, 0x00482082, 0x00482090, 0x00482092, 0x00482400,
0x00482402, 0x00482410, 0x00482412, 0x00482480, 0x00482482, 0x00482490, 0x00482492, 0x00490000,
0x00490002, 0x00490010, 0x00490012, 0x00490080, 0x00490082, 0x00490090, 0x00490092, 0x00490400,
0x00490402, 0x00490410, 0x00490412, 0x00490480, 0x00490482, 0x00490490, 0x00490492, 0x00492000,
0x00492002, 0x00492010, 0x00492012, 0x00492080, 0x00492082, 0x00492090, 0x00492092, 0x00492400,
0x00492402, 0x00492410, 0x00492412, 0x00492480, 0x00492482, 0x00492490, 0x00492492
};

// Pre-shifted table for z (2 bits to the left)
static const uint32_t morton256_z[256] = {
0x00000000,
0x00000004, 0x00000020, 0x00000024, 0x00000100, 0x00000104, 0x00000120, 0x00000124, 0x00000800,
0x00000804, 0x00000820, 0x00000824, 0x00000900, 0x00000904, 0x00000920, 0x00000924, 0x00004000,
0x00004004, 0x00004020, 0x00004024, 0x00004100, 0x00004104, 0x00004120, 0x00004124, 0x00004800,
0x00004804, 0x00004820, 0x00004824, 0x00004900, 0x00004904, 0x00004920, 0x00004924, 0x00020000,
0x00020004, 0x00020020, 0x00020024, 0x00020100, 0x00020104, 0x00020120, 0x00020124, 0x00020800,
0x00020804, 0x00020820, 0x00020824, 0x00020900, 0x00020904, 0x00020920, 0x00020924, 0x00024000,
0x00024004, 0x00024020, 0x00024024, 0x00024100, 0x00024104, 0x00024120, 0x00024124, 0x00024800,
0x00024804, 0x00024820, 0x00024824, 0x00024900, 0x00024904, 0x00024920, 0x00024924, 0x00100000,
0x00100004, 0x00100020, 0x00100024, 0x00100100, 0x00100104, 0x00100120, 0x00100124, 0x00100800,
0x00100804, 0x00100820, 0x00100824, 0x00100900, 0x00100904, 0x00100920, 0x00100924, 0x00104000,
0x00104004, 0x00104020, 0x00104024, 0x00104100, 0x00104104, 0x00104120, 0x00104124, 0x00104800,
0x00104804, 0x00104820, 0x00104824, 0x00104900, 0x00104904, 0x00104920, 0x00104924, 0x00120000,
0x00120004, 0x00120020, 0x00120024, 0x00120100, 0x00120104, 0x00120120, 0x00120124, 0x00120800,
0x00120804, 0x00120820, 0x00120824, 0x00120900, 0x00120904, 0x00120920, 0x00120924, 0x00124000,
0x00124004, 0x00124020, 0x00124024, 0x00124100, 0x00124104, 0x00124120, 0x00124124, 0x00124800,
0x00124804, 0x00124820, 0x00124824, 0x00124900, 0x00124904, 0x00124920, 0x00124924, 0x00800000,
0x00800004, 0x00800020, 0x00800024, 0x00800100, 0x00800104, 0x00800120, 0x00800124, 0x00800800,
0x00800804, 0x00800820, 0x00800824, 0x00800900, 0x00800904, 0x00800920, 0x00800924, 0x00804000,
0x00804004, 0x00804020, 0x00804024, 0x00804100, 0x00804104, 0x00804120, 0x00804124, 0x00804800,
0x00804804, 0x00804820, 0x00804824, 0x00804900, 0x00804904, 0x00804920, 0x00804924, 0x00820000,
0x00820004, 0x00820020, 0x00820024, 0x00820100, 0x00820104, 0x00820120, 0x00820124, 0x00820800,
0x00820804, 0x00820820, 0x00820824, 0x00820900, 0x00820904, 0x00820920, 0x00820924, 0x00824000,
0x00824004, 0x00824020, 0x00824024, 0x00824100, 0x00824104, 0x00824120, 0x00824124, 0x00824800,
0x00824804, 0x00824820, 0x00824824, 0x00824900, 0x00824904, 0x00824920, 0x00824924, 0x00900000,
0x00900004, 0x00900020, 0x00900024, 0x00900100, 0x00900104, 0x00900120, 0x00900124, 0x00900800,
0x00900804, 0x00900820, 0x00900824, 0x00900900, 0x00900904, 0x00900920, 0x00900924, 0x00904000,
0x00904004, 0x00904020, 0x00904024, 0x00904100, 0x00904104, 0x00904120, 0x00904124, 0x00904800,
0x00904804, 0x00904820, 0x00904824, 0x00904900, 0x00904904, 0x00904920, 0x00904924, 0x00920000,
0x00920004, 0x00920020, 0x00920024, 0x00920100, 0x00920104, 0x00920120, 0x00920124, 0x00920800,
0x00920804, 0x00920820, 0x00920824, 0x00920900, 0x00920904, 0x00920920, 0x00920924, 0x00924000,
0x00924004, 0x00924020, 0x00924024, 0x00924100, 0x00924104, 0x00924120, 0x00924124, 0x00924800,
0x00924804, 0x00924820, 0x00924824, 0x00924900, 0x00924904, 0x00924920, 0x00924924
};

u32 encode(int r, int g, int b) {
	return morton256_x[r] | morton256_y[g] | morton256_z[b];
//	return (210 * r) + (720 * g) + (70 * b);
}

u32 encode2D(int a, int b) {
	return morton256_x[a] | morton256_y[b];
//	return (210 * r) + (720 * g) + (70 * b);
}

void sortPalette(u8* position, u8* palette, u8 count) {
		
	// 1. Start from lower corner.
	// Each time :
	// 2. Look at the direction of 'center of mass' of remaining point.
	// 3. Find closest point + in direction of center of mass. (direction score > distance score)
	// 4. Remove point from list, including point with angle >90 or <-90 deg from new point. (past point unused are removed too) 
		// Score point removal --> too big : palette is not a single curve but complex stuff... --> Stop.

	u32 colorMorton;
	u8* rPal = palette;

	for(int counter1=0;counter1<(count-1)*3;counter1+=3)
	{
		int minimum=counter1;
		for(int counter2=counter1+3;counter2<(count*3);counter2+=3)
		{
			u32 colorMortonMin = encode(rPal[minimum ],rPal[minimum +1],rPal[minimum +2]);
			u32 colorMortonA   = encode(rPal[counter2 ],rPal[counter2 +1],rPal[counter2 +2]);
			if (colorMortonMin>colorMortonA)
				minimum=counter2;
		}

		if(minimum!=counter1) {
			u8 temp_valueR=rPal[counter1  ];
			u8 temp_valueG=rPal[counter1+1];
			u8 temp_valueB=rPal[counter1+2];
			rPal[counter1  ]=rPal[minimum  ];
			rPal[counter1+1]=rPal[minimum+1];
			rPal[counter1+2]=rPal[minimum+2];
			rPal[minimum   ]=temp_valueR;
			rPal[minimum +1]=temp_valueG;
			rPal[minimum +2]=temp_valueB;
			int c1D3 = counter1 / 3;
			int  mD3 = minimum  / 3;
			u8 tmpPos      = position[c1D3];
			position[c1D3] = position[mD3];
			position[mD3]  = tmpPos;
		}
	}
}

void sortPalette2D(u8* position, u8* palette, u8 count) {
		
	// 1. Start from lower corner.
	// Each time :
	// 2. Look at the direction of 'center of mass' of remaining point.
	// 3. Find closest point + in direction of center of mass. (direction score > distance score)
	// 4. Remove point from list, including point with angle >90 or <-90 deg from new point. (past point unused are removed too) 
		// Score point removal --> too big : palette is not a single curve but complex stuff... --> Stop.

	u32 colorMorton;
	u8* rPal = palette;

	for(int counter1=0;counter1<(count-1)*2;counter1+=2)
	{
		int minimum=counter1;
		for(int counter2=counter1+2;counter2<(count*2);counter2+=2)
		{
			u32 colorMortonMin = encode2D(rPal[minimum ],rPal[minimum +1]);
			u32 colorMortonA   = encode2D(rPal[counter2 ],rPal[counter2 +1]);
			if (colorMortonMin>colorMortonA)
				minimum=counter2;
		}

		if(minimum!=counter1) {
			u8 temp_valueR=rPal[counter1  ];
			u8 temp_valueG=rPal[counter1+1];
			rPal[counter1  ]=rPal[minimum  ];
			rPal[counter1+1]=rPal[minimum+1];
			rPal[minimum   ]=temp_valueR;
			rPal[minimum +1]=temp_valueG;
			int c1D3 = counter1 / 3;
			int  mD3 = minimum  / 3;
			u8 tmpPos      = position[c1D3];
			position[c1D3] = position[mD3];
			position[mD3]  = tmpPos;
		}
	}
}

void EncoderContext::DumpTileRGB() {
	int tileSize = 8;

	u8 map[128*128*3];
	bool isOutside;

	int tileCnt = 0;
	for (int y=0; y < original->GetHeight(); y += tileSize) {
		for (int x=0; x < original->GetWidth(); x += tileSize) {
			// If tile is available...
			bool allowA = true;//mapSmoothTile->GetPlane(0)->GetPixelValue(x,y,isOutside) == 0;
			bool allowB = true;//mapSmoothTile->GetPlane(1)->GetPixelValue(x,y,isOutside) == 0;
			bool allowC = true;//mapSmoothTile->GetPlane(2)->GetPixelValue(x,y,isOutside) == 0;

			if (allowA && allowB && allowC) {
				// 1. Extract 64 Color palette.
				// 2. Sort
				// 3. Remap palette between 2 and 3(4?) color points.
				// 4. Find original -> Palette closest color (Not by RGB, but Luma-Chroma !)
				//    4.1 Generate 6 bit x 8x8 Tile indexes.
				//    4.2 Map into library of tiles.

				u8 pixelPalette[64*3];
				u8 position    [64];
				u8 inversePos  [64];
				u8 compressPos [64];
				int idx = 0;

				for (int n=0; n < 64; n++) { position[n]= n; } // Optimize with memcpy ?

/*
				for (int yp=0; yp < tileSize; yp++) {
					for (int xp=0; xp < tileSize; xp++) {
						int rgb[3];
						original->GetPixel(x+xp,y+yp,rgb,isOutside);
						pixelPalette[idx  ] = rgb[0];
						pixelPalette[idx+1] = rgb[1];
						pixelPalette[idx+2] = rgb[2];
						idx+=3;
					}
				}

				for (int n=0; n < 64; n++) {
					inversePos[position[n]] = n; 
				}
				*/
//				sortPalette(position,pixelPalette);

				u8 tileMask[128];
				int pixelInTile;
				bool allInside;
				
				BoundingBox3D bb3 = buildBBox3D(original,false,mapSmoothTile,x,y,tileSize,tileSize,pixelInTile,tileMask,allInside);

				if (pixelInTile == 0) { tileCnt++; continue; }

				// Export normalized values...
				int dX = bb3.x1 - bb3.x0;
				int dY = bb3.y1 - bb3.y0;
				int dZ = bb3.z1 - bb3.z0;

				int nX = dX ? ((1<<20)/dX) : 0;
				int nY = dY ? ((1<<20)/dY) : 0;
				int nZ = dZ ? ((1<<20)/dZ) : 0;
				float div = (float)(1<<20);

				char buffer[2000];
				sprintf(buffer,"tileXYZ\\tileMap_%i.xyz",tileCnt);
				FILE* fXYZ = fopen(buffer,"wb");

				fprintf(fXYZ,"0,0,0,255,255,255\n");
				fprintf(fXYZ,"255,0,0,255,255,255\n");
				fprintf(fXYZ,"0,255,0,255,255,255\n");
				fprintf(fXYZ,"255,255,0,255,255,255\n");
				fprintf(fXYZ,"0,0,255,255,255,255\n");
				fprintf(fXYZ,"255,0,255,255,255,255\n");
				fprintf(fXYZ,"0,255,255,255,255,255\n");
				fprintf(fXYZ,"255,255,255,255,255,255\n");
				memset(map, 0, 128*128*3);
				memcpy(&map[127*128*3],pixelPalette,64*3);
				for (int yp=0; yp < tileSize; yp++) {
					for (int xp=0; xp < tileSize; xp++) {
						int rgb[3];
						int idxM = (xp+(yp*tileSize));
						if (tileMask[idxM] != 0) { continue; }

						original->GetPixel(x+xp,y+yp,rgb,isOutside);
						
						// Convert6Bit(rgb);

						int r = rgb[0];
						int g = rgb[1];
						int b = rgb[2];

						// Origin.
						int lr = rgb[0] - bb3.x0;
						int lg = rgb[1] - bb3.y0;
						int lb = rgb[2] - bb3.z0;

						// Rescale to 1.0 fixed.
						lr *= nX;
						lg *= nY;
						lb *= nZ;

						float fr = lr/div;
						float fg = lg/div;
						float fb = lb/div;

						int ir64 = ((int)(fr*255));
						int ig64 = ((int)(fg*255));
						int ib64 = ((int)(fb*255));

						assert(ir64 >= 0 && ir64 <= 255);
						assert(ig64 >= 0 && ig64 <= 255);
						assert(ib64 >= 0 && ib64 <= 255);


						for (int yo=-1;yo < 2; yo++) {
							for (int xo=-1;xo < 2; xo++) {
								for (int zo=-1;zo < 2; zo++) {
									fprintf(fXYZ,"%i,%i,%i,%i,%i,%i\n",ir64+xo,ig64+yo,ib64+zo,rgb[0],rgb[1],rgb[2]);
								}
							}
						}

						int idx = ((r+b) + ((g+b)*128))*3;
						map[idx  ] = rgb[0];
						map[idx+1] = rgb[1];
						map[idx+2] = rgb[2];

						int posIdx = xp + yp*8;

						{
							int OrgPos = position[xp + yp*8];
							idx = ((xp*2) + (yp*2)*128)*3;
							map[idx  ] = OrgPos*4;
							map[idx+1] = OrgPos*4;
							map[idx+2] = OrgPos*4;
							map[idx+3] = OrgPos*4;
							map[idx+4] = OrgPos*4;
							map[idx+5] = OrgPos*4;

							map[idx  +384] = OrgPos*4;
							map[idx+1+384] = OrgPos*4;
							map[idx+2+384] = OrgPos*4;
							map[idx+3+384] = OrgPos*4;
							map[idx+4+384] = OrgPos*4;
							map[idx+5+384] = OrgPos*4;
						}

						int OrgPos = position[xp + yp*8];
						int ox = OrgPos & 0x7;
						int oy = OrgPos >> 3;
						idx = (((ox+9) *2) + (oy*2)*128)*3;
						u8* pL = &pixelPalette[posIdx*3];
						map[idx  ] = pL[0];
						map[idx+1] = pL[1];
						map[idx+2] = pL[2];
						map[idx+3] = pL[0];
						map[idx+4] = pL[1];
						map[idx+5] = pL[2];

						map[idx  +384] = pL[0];
						map[idx+1+384] = pL[1];
						map[idx+2+384] = pL[2];
						map[idx+3+384] = pL[0];
						map[idx+4+384] = pL[1];
						map[idx+5+384] = pL[2];
					}
				}
				fclose(fXYZ);

//				char buffer[2000];
//				sprintf(buffer,"tilesRGB\\tileMap_%i(%i-%i).png",tileCnt,x,y);
//				int err = stbi_write_png(buffer, 128, 128, 3, map, 128*3);
			}

			tileCnt++;
		}
	}
}

int Round6(int u8V) {
//	return u8V;
//	u8V++;
//	if (u8V > 255) { u8V = 255; }
	int res = u8V>>2;
	return (res<<2) | (res>>4);
}

int CompressF(int u8V, int rate) {
	// With proper rounding.
	return ((u8V * rate) + 127)/255;
}

int UncompressF(int u8V, int rate) {
	// Use fast mul inverse in decoder, so need to have same exact computation if we need to compare.
	int invMul       = rate ? (255<<16) / rate : (255<<16);
	return (u8V * invMul) >> 16;
}

int Round6P(int u8V) {
	u8V++;
	if (u8V > 255) { u8V = 255; }
	int res = u8V>>2;
	return (res<<2) | (res>>4);
}

struct col {
	int ref;
	int dr;
	int dg;
	int db;
};

col CodeRGB[100000];
int CodeCount = 0;

#include <stdio.h>
#include <stdlib.h>

int compCode (const void * elem1, const void * elem2) 
{
    col* f = ((col*)elem1);
    col* s = ((col*)elem2);
    if (f->ref < s->ref) return  1;
    if (f->ref > s->ref) return -1;
    return 0;
}

void registerCodeBook(int diffR, int diffG, int diffB) {
	// 1. Register new code
	// 2. Increment ref counter if already found.
	for (int n=0; n < CodeCount; n++) {
		if ((CodeRGB[n].dr == diffR) && (CodeRGB[n].dg == diffG) && (CodeRGB[n].db == diffB)) {
			CodeRGB[n].ref++;
			return;
		}
	}

	CodeRGB[CodeCount].dr = diffR;
	CodeRGB[CodeCount].dg = diffG;
	CodeRGB[CodeCount].db = diffB;
	CodeRGB[CodeCount].ref= 0;
	CodeCount++;
}

int  FindCodeBook(int diffR,int diffG,int diffB) {
	for (int n=0; n < 64; n++) {
		if ((CodeRGB[n].dr == diffR) && (CodeRGB[n].dg == diffG) && (CodeRGB[n].db == diffB)) {
			return n;
		}
	}
	return -1;
}

#define DEBUG_COMPRESSOR (0)

bool PaletteCompressor(u8* input, int size, u8* output, u32* maxSize) {
	bool error = false;
	int streamIdx = 0;
	int lmaxSize  = *maxSize;

	int entryCol  = size / 3;

	u8* decomp = new u8[size];
	u8* decompStm = decomp;

	/*  Palette compressed to 6:6:6 already
	for (int n=0; n < size; n++) {
		input[n] >>= 1;
	}
	*/

	// Special null code, better be at top.
	CodeCount = 0; // Reset table.
	registerCodeBook(0,0,0);

	#define WriteRAW(v)					if (streamIdx < lmaxSize) { output[streamIdx++] = (v); } else { error = true; goto exit; }
	#define SelectAnotherColor(colRef)	if (streamIdx < lmaxSize) { output[streamIdx++] = 0xC0 | ((colRef) & 0x3F); } else { error = true; goto exit; }
	#define WriteCodeBook(codeIndex)	if (streamIdx < lmaxSize) { output[streamIdx++] = (codeIndex) & 0x7F; } else { error = true; goto exit; }

	//
	// Phase 1 : Build Best Code Book. (limit to 128 entries)
	//           Include a search window for smallest code book already.
	//
	u8* pPix = input;
	for (int n=1; n < entryCol; n++) {
		u8* pix = &input[n*3];
		int prevStart = n-64; if (prevStart < 0) { prevStart = 0; }

		int distMin = 999999999;
		int bR,bG,bB; // Best result.
		for (int prev= prevStart; prev < n; prev++) {
			int idx = prev*3;
			int dR = pix[0]-pPix[idx];
			int dG = pix[1]-pPix[idx+1];
			int dB = pix[2]-pPix[idx+2];
			int ldist = dR*dR + dG*dG + dB*dB; // Positive distance.
			if (ldist < distMin) {
				distMin = ldist;
				bR = dR;
				bG = dG;
				bB = dB;
			}
		}

		registerCodeBook(bR,bG,bB);
	}
#if DEBUG_COMPRESSOR
	printf("-->Found %i Delta Code Book\n",CodeCount);
#endif

	// Keep code 0,0,0 at top anyway. (normally, all contents should have a 0,0,0.
    qsort (&CodeRGB[1], CodeCount-1, sizeof(col), compCode);
	
	int finalCodeCount = (CodeCount > 128) ? 128 : CodeCount;

	u8* prevCol = decompStm;

	// Code Book Header
	WriteRAW(finalCodeCount);

	for (int n=0; n < finalCodeCount; n++) {
		WriteRAW(CodeRGB[n].dr);
		WriteRAW(CodeRGB[n].dg);
		WriteRAW(CodeRGB[n].db);
	}

	// Write first color.
	WriteRAW(input[0]);
	WriteRAW(input[1]);
	WriteRAW(input[2]);

	*decompStm++ = input[0];
	*decompStm++ = input[1];
	*decompStm++ = input[2];

	//
	// Phase 2 : Encode the color, when the delta is not found in code book, use another strategy :
	//           Many encoding possibilities... let's see.
	//           


	for (int n=1; n < entryCol; n++) {
		u8* pix = &input[n*3];
		int prevStart = n-65; if (prevStart < 0) { prevStart = 0; }

		int distMin = 999999999;
		int bR,bG,bB; // Best result.
		bool done = false;

		int bestIndexCodeBook = 999;
		int bestDistance = 0;

#if DEBUG_COMPRESSOR
		printf("Color %i (%i,%i,%i) : ",n,pix[0],pix[1],pix[2]);
#endif

		for (int prev= (n-1); prev >= prevStart; prev--) {
			int idx = prev*3;
			int dR = pix[0]-pPix[idx+0];
			int dG = pix[1]-pPix[idx+1];
			int dB = pix[2]-pPix[idx+2];

			int index = FindCodeBook(dR,dG,dB);
			if (index >= 0) {
				if (prev == (n-1)) {
#if DEBUG_COMPRESSOR
					printf("=> Add codebook[%i](%i,%i,%i) to previous color.\n",index,dR,dG,dB);
#endif
					// Compare to previous color directly...
					WriteCodeBook(index);
					done = true;
					*decompStm++ = prevCol[0] + dR;
					*decompStm++ = prevCol[1] + dG;
					*decompStm++ = prevCol[2] + dB;
					prevCol = &decompStm[-3];
					break;
				} else {
					int distance = ((n-prev)-2);
					// [TODO] Question : Find the most compressable JUMP + CodeBook index... ? => RDO
					// For now : Choose between closest jump + whatever index.
					//                          best    jump + smallest index.

					// Closest jump + whatever index...
					// -1..
					if (distance < 64) {
						if (index < bestIndexCodeBook) {
							bestIndexCodeBook = index;
							bestDistance      = distance;
							done              = true;
						}
						// No break -> Continue to find best match...
					}
				}
			}
		}

		if (bestIndexCodeBook != 999) {
#if DEBUG_COMPRESSOR
			printf("=> Add codebook[%i](%i,%i,%i) to color[%i].\n",
				bestIndexCodeBook,
				CodeRGB[bestIndexCodeBook].dr,
				CodeRGB[bestIndexCodeBook].dg,
				CodeRGB[bestIndexCodeBook].db,
				n - (bestDistance+2)
			);
#endif
			SelectAnotherColor(bestDistance);
			prevCol = &decompStm[(bestDistance+2)*(-3)];

			WriteCodeBook(bestIndexCodeBook);
			done = true;
			*decompStm++ = prevCol[0] + CodeRGB[bestIndexCodeBook].dr;
			*decompStm++ = prevCol[1] + CodeRGB[bestIndexCodeBook].dg;
			*decompStm++ = prevCol[2] + CodeRGB[bestIndexCodeBook].db;
			prevCol = &decompStm[-3];
		}


		if (!done) {
			// TODO : Can use SelectAnotherColor and base as 'reference' instead of last color only !!!
			int dR = pix[0]-pix[-3];
			int dG = pix[1]-pix[-2];
			int dB = pix[2]-pix[-1];
			// Find a way to encode the color 
			// RDO issue again :
			// [1][1][000][RGBMsk]+1,2,3 Delta non code book from LAST color. => Mostly never used...
			// [1][1][001][RGBMsk]+1,2,3 Byte follow depending on mask. Create color from LAST color. (component per component)

			u8 mask = 0;

			if (dR) { mask |= 0x1; }
			if (dG) { mask |= 0x2; }
			if (dB) { mask |= 0x4; }

			// For now just check encodability.
			if ((dR >= -128) && (dR <= 127) && 
				(dG >= -128) && (dG <= 127) &&
				(dB >= -128) && (dB <= 127)) {

				// Can use relative stuff : increase probably of matching pattern I guess... ?
				WriteRAW(0x80 | mask); // 0x80/0x88 and 0x00 have the same result... (0x80 and 0x88 never encoded)

				if (dR) { WriteRAW(dR); }
				if (dG) { WriteRAW(dG); }
				if (dB) { WriteRAW(dB); }



				// TODO : Get some stats -> If always RGB, remove that mask stuff : faster decoder, better compression rate...
				//          Force all to be saved...
//				printf("maskA : %i\n",mask);
#if DEBUG_COMPRESSOR
				printf("=> Add Relative Vector(msk:%i)(%i,%i,%i) to prev color.\n",mask,dR,dG,dB);
#endif
				*decompStm++ = prevCol[0] + dR;
				*decompStm++ = prevCol[1] + dG;
				*decompStm++ = prevCol[2] + dB;
				prevCol = &decompStm[-3];

			} else {
				// If we go with 7 Bit mode, B encoding is not necessary anymore...

				// Absolute stuff.
				WriteRAW(0x88 | mask);
				*decompStm++ = (dR) ? pix[0] : prevCol[0];
				*decompStm++ = (dG) ? pix[1] : prevCol[1];
				*decompStm++ = (dB) ? pix[2] : prevCol[2];
				prevCol = &decompStm[-3];

				if (dR) { WriteRAW(pix[0]); }
				if (dG) { WriteRAW(pix[1]); }
				if (dB) { WriteRAW(pix[2]); }

//				printf("maskB : %i\n",mask);
#if DEBUG_COMPRESSOR
				printf("=> Set Absolute Vector(msk:%i)(%i,%i,%i) to prev color.\n",mask,pix[0],pix[1],pix[2]);
#endif
			}
		}

		if ((decompStm[-3] != pix[0]) || (decompStm[-2] != pix[1]) || (decompStm[-1] != pix[2])) {
			printf("ERROR");
		}
	}
	
	#undef WriteRAW
	#undef SelectAnotherColor
	#undef WriteCodeBook
exit:
	if (!error) {
		printf("-->Reduce stream from %i to %i by compressor. ",*maxSize,streamIdx);
	}

	// Override size for output.
	*maxSize = error ? 0 : streamIdx;

	delete[] decomp;
	return !error;
}

bool PaletteCompressorLUT(u8* input, int size, u8* output, u32* maxSize) {
	bool error = false;
	int streamIdx = 0;
	int lmaxSize  = *maxSize;

	int entryCol  = size / 3;

	// Reduce drastically.
	for (int n=0; n < size; n++) {
		input[n] >>= 2;
	}

	// Special null code, better be at top.
	CodeCount = 0; // Reset table.
	registerCodeBook(0,0,0);

	#define WriteRAW(v)					if (streamIdx < lmaxSize) { output[streamIdx++] = (v); } else { error = true; goto exit; }
	#define SelectAnotherColor(colRef)	if (streamIdx < lmaxSize) { output[streamIdx++] = 0xC0 | ((colRef) & 0x3F); } else { error = true; goto exit; }
	#define WriteCodeBook(codeIndex)	if (streamIdx < lmaxSize) { output[streamIdx++] = (codeIndex) & 0x7F; } else { error = true; goto exit; }

	//
	// Phase 1 : Build Best Code Book. (limit to 128 entries)
	//           Include a search window for smallest code book already.
	//
	u8* pPix = input;
	for (int n=1; n < entryCol; n++) {
		u8* pix = &input[n*3];
		int prevStart = n-64; if (prevStart < 0) { prevStart = 0; }

		int distMin = 999999999;
		int bR,bG,bB; // Best result.
		for (int prev= prevStart; prev < n; prev++) {
			int idx = prev*3;
			int dR = pix[0]-pPix[idx];
			int dG = pix[1]-pPix[idx+1];
			int dB = pix[2]-pPix[idx+2];
			int ldist = dR*dR + dG*dG + dB*dB; // Positive distance.
			if (ldist < distMin) {
				distMin = ldist;
				bR = dR;
				bG = dG;
				bB = dB;
			}
		}

		registerCodeBook(bR,bG,bB);
	}
	printf("-->Found %i Delta Code Book\n",CodeCount);

	// Keep code 0,0,0 at top anyway. (normally, all contents should have a 0,0,0.
    qsort (&CodeRGB[1], CodeCount-1, sizeof(col), compCode);
	
	int finalCodeCount = (CodeCount > 128) ? 128 : CodeCount;

	// Code Book Header
	WriteRAW(finalCodeCount);

	for (int n=0; n < finalCodeCount; n++) {
		WriteRAW(CodeRGB[n].dr);
		WriteRAW(CodeRGB[n].dg);
		WriteRAW(CodeRGB[n].db);
	}

	//
	// Phase 2 : Encode the color, when the delta is not found in code book, use another strategy :
	//           Many encoding possibilities... let's see.
	//           
	for (int n=1; n < entryCol; n++) {
		u8* pix = &input[n*3];
		int prevStart = n-65; if (prevStart < 0) { prevStart = 0; }

		int distMin = 999999999;
		int bR,bG,bB; // Best result.
		bool done = false;

		for (int prev= (n-1); prev >= prevStart; prev--) {
			int idx = prev*3;
			int dR = pix[0]-pPix[idx+0];
			int dG = pix[1]-pPix[idx+1];
			int dB = pix[2]-pPix[idx+2];

			int index = FindCodeBook(dR,dG,dB);
			if (index >= 0) {
				if (prev == (n-1)) {
					// Compare to previous color directly...
					WriteCodeBook(index); // 0..128

					/*
					// Delta close box.
					WriteRAW((pix[3]-pix[0])>>1);
					WriteRAW((pix[4]-pix[1])>>1);
					WriteRAW((pix[5]-pix[2])>>1);
					*/
					done = true;
					break;
				} else {
					int distance = ((n-prev)-2);
					// [TODO] Question : Find the most compressable JUMP + CodeBook index... ? => RDO
					// For now : Choose between closest jump + whatever index.
					//                          best    jump + smallest index.

					// Closest jump + whatever index...
					// -1..
					if (distance < 64) {
						if (index == 0) {
							SelectAnotherColor(distance); // 11.xx xxxx
						} else {
							SelectAnotherColor(distance); // 11.xx xxxx
							WriteCodeBook(index);
						}

						/*
						// Delta close box.
						WriteRAW((pix[3]-pix[0])>>1);
						WriteRAW((pix[4]-pix[1])>>1);
						WriteRAW((pix[5]-pix[2])>>1);
						*/
						done = true;
						break;
					}
				}
			}
		}

		if (!done) {
			int dR = pix[0]-pix[-3];
			int dG = pix[1]-pix[-2];
			int dB = pix[2]-pix[-1];
			// Find a way to encode the color 
			// RDO issue again :
			// [0][..............]
			// [1][0][000][RGBMsk]+1,2,3 Delta non code book from LAST color. => Mostly never used...
			// [1][0][001][RGBMsk]+1,2,3 Byte follow depending on mask. Create color from LAST color. (component per component)

			u8 mask = 0;

			if (dR) { mask |= 0x1; }
			if (dG) { mask |= 0x2; }
			if (dB) { mask |= 0x4; }

			// For now just check encodability.
			if ((dR >= -128) && (dR <= 127) && 
				(dG >= -128) && (dG <= 127) &&
				(dB >= -128) && (dB <= 127)) {

				// Can use relative stuff : increase probably of matching pattern I guess... ?
				WriteRAW(0x80 | mask);

				if (dR) { WriteRAW(dR); }
				if (dG) { WriteRAW(dG); }
				if (dB) { WriteRAW(dB); }

				// TODO : Get some stats -> If always RGB, remove that mask stuff : faster decoder, better compression rate...
				//          Force all to be saved...
//				printf("maskA : %i\n",mask);
			} else {
				// If we go with 7 Bit mode, B encoding is not necessary anymore...

				// Absolute stuff.
				WriteRAW(0x88 | mask);

				if (dR) { WriteRAW(pix[0]); }
				if (dG) { WriteRAW(pix[1]); }
				if (dB) { WriteRAW(pix[2]); }

//				printf("maskB : %i\n",mask);
			}

			// Delta close box.
			WriteRAW((pix[3]-pix[0])>>1);
			WriteRAW((pix[4]-pix[1])>>1);
			WriteRAW((pix[5]-pix[2])>>1);
		}
	}
	
	#undef WriteRAW
	#undef SelectAnotherColor
	#undef WriteCodeBook
exit:
	if (!error) {
		printf("-->Reduce stream from %i to %i by specific compressor.\n",*maxSize,streamIdx);
	}

	// Override size for output.
	*maxSize = error ? 0 : streamIdx;
	return !error;
}

unsigned char* EncoderContext::CompressStream(u8* data, int size, int* sizeOut) {
	int dstSize = size * 2;
	if (dstSize < 1000) { dstSize = 1000; }

	unsigned char* pZStd = new unsigned char[dstSize];
	size_t result = (int)ZSTD_compress(pZStd, dstSize, data,size, 18);
	
	if (ZSTD_isError(result)) {
		const char* nameR = ZSTD_getErrorName(result);
		printf("ZSTD Compress error : %s\n", nameR);
		return NULL;
	} else {
		fileOutSize += result;
		*sizeOut = result; // to 32 bit.
		return pZStd;
	}
}

int  EncoderContext::FittingQuadSmooth(int rejectFactor, Plane* srcA, Plane* srcB, Plane* srcC, Image* testOutput, bool useYCoCg, int tileShiftX, int tileShiftY) {
	CheckMipmapMask();

	int imgW = 0;
	int imgH = 0;
	int PlaneBit = (srcA ? 0x1 : 0x0)
		         | (srcB ? 0x2 : 0x0)
		         | (srcC ? 0x4 : 0x0)
				 ;

	if (srcA) {	imgW = srcA->GetWidth();
				imgH = srcA->GetHeight(); }
	if (srcB) {	imgW = srcB->GetWidth();
				imgH = srcB->GetHeight(); }
	if (srcC) {	imgW = srcC->GetWidth();
				imgH = srcC->GetHeight(); }

//	Image* img = useYCoCg ? this->YCoCgImg : this->original;

	int TileDone = 0;
	int tileSizeX = 1<<tileShiftX;
	int tileSizeY = 1<<tileShiftY;

	bool createMap = false;

	int weight4 [] = { 1024,768,512, 256,0 };
	int weight8 [] = { 1024,896,768, 640,512,384,256,128, 0 };
	int weight16[] = { 1024,960,896,832,768,704,640,576,512,448,384,320,256,192,128,64, 0 };

	if (!smoothMap) {
		createMap = true;
		smoothMap = new Plane(imgW,imgH);
		if (!evaluateLUT2D) {
			mapSmoothTile = Image::CreateImage(imgW,imgH,3,true);
			mapSmoothTile->Clear();
		}
		mappedRGB     = Image::CreateImage(imgW+1,imgH+1,3,true);
		BoundingBox bb = smoothMap->GetRect();
		smoothMap->Fill(bb,0);
	}

	// 16x16 : 16 bit table (64x64 pixel) -> Lookup the table by block of 4x4 tile. -> Allow 16/4 skip
	// 16x 8 : 32 bit table (64x64 pixel) -> Lookup the table by block of 4x8 tile. -> Allow 
	//  8x16 : 32 bit table (64x64 pixel) -> Lookup the table by block of 8x4 tile.
	//  8x 8 : 64 bit table (64x64 pixel) -> Lookup the table by block of 8x8 tile.
	//  8x 4 : 64 bit table (64x32 pixel) -> Lookup the table by block of 8x8 tile.
	//  4x 8 : 64 bit table (32x64 pixel) -> Lookup the table by block of 8x8 tile.
	//  4x 4 : 64 bit table (32x32 pixel) -> Lookup the table by block of 8x8 tile.

	u32 swizzleParseY;
	u32 swizzleParseX;
	u32 bitCount;

	HeaderGradientTile::getSwizzleSize(tileShiftX,tileShiftY,swizzleParseX,swizzleParseY, bitCount);

	//
	// BECAUSE OF SWIZZLE and WORD SIZE. We must make sure that the bitmap table is wider and aligned to swizzle block size.
	//
	// Word size : 1,2,4 bits.

	int xBBTileCount = ((imgW+(swizzleParseX-1)) / swizzleParseX);
	int yBBTileCount = ((imgH+(swizzleParseY-1)) / swizzleParseY);

	int strideY = (((imgW+swizzleParseX-1) / swizzleParseX) * (swizzleParseX / tileSizeX));

	int sizeBitmap  = (xBBTileCount * yBBTileCount * bitCount) >> 3; // No need for rounding, bitCount garantees multibyte alignment.
	u8* pFillBitMap = new u8[sizeBitmap];
	memset(pFillBitMap,0, sizeBitmap);

	// +1 +1 because of lower side corners.
	int streamW		= (imgW/tileSizeX)+1;
	int streamH		= (imgH/tileSizeY)+1;

	int sizeStream  = streamW * streamH * 3;
	u8* rgbStream   = new u8[sizeStream];
	u8* wrRgbStream = rgbStream;
//	memset(rgbStream, 0, sizeStream);

	int rTile[256];
	int gTile[256];
	int bTile[256];

	int rTile6[256];
	int gTile6[256];
	int bTile6[256];

//	int pos = 0;

	int minX = imgW; int maxX = 0;
	int minY = imgH; int maxY = 0;

	int posYS; 
	int posXS;
	int stepYS = bitCount * xBBTileCount;
	int stepXS = bitCount;						// 1 block.
	int stepY  = swizzleParseX / tileSizeX;		// Inside block vertically.
	//  stepX  = 1

	// Linear 2D order for big tile defined by swizzling.
	posYS = 0;
	for (int swizzley=0; swizzley < imgH; swizzley += swizzleParseY) {
		posXS = posYS;
		for (int swizzlex=0; swizzlex < imgW; swizzlex += swizzleParseX) {
			// Linear 2D order inside each big tile

			int posY = posXS;
			for (int y=swizzley; y < swizzley+swizzleParseY; y+= tileSizeY) {
				// Because of big tile parsing, we may lookup outside of image with sub-tiles. (Y-Axis)
				if (y >= imgH || ((y + tileSizeY) > imgH)) {
					break; 
				}

				int pos = posY;
				for (int x=swizzlex; x < swizzlex+swizzleParseX; x+= tileSizeX) {
					// Because of big tile parsing, we may lookup outside of image with sub-tiles. (X-Axis)
					// Check tile left and RIGHT pos to fit...
					if (x >= imgW || ((x + tileSizeX) > imgW)) {
						break; 
					}

					int tileX = x>>tileShiftX;
					int tileY = y>>tileShiftY;

					bool isOutside;

					int rgbTL[3]; 
					int rgbTR[3];
					int rgbBL[3];
					int rgbBR[3];

					int rgbTL6[3]; 
					int rgbTR6[3];
					int rgbBL6[3];
					int rgbBR6[3];

					for (int n=0; n < 3; n++) {
						Plane* p = NULL;
						switch (n) {
						case 0: p=srcA; break;
						case 1: p=srcB; break;
						case 2: p=srcC; break;
						}
						if (p) {
							rgbTL[n] = p->GetPixelValue(x,y,isOutside);
							rgbTR[n] = p->GetPixelValue(x+tileSizeX,y,isOutside);
							rgbBL[n] = p->GetPixelValue(x,y+tileSizeY,isOutside);
							rgbBR[n] = p->GetPixelValue(x+tileSizeX,y+tileSizeY,isOutside);
						} else {
							rgbTL[n] = 0;
							rgbTR[n] = 0;
							rgbBL[n] = 0;
							rgbBR[n] = 0;
						}

						rgbTL6[n] = Round6(rgbTL[n]);
						rgbTR6[n] = Round6(rgbTR[n]);
						rgbBL6[n] = Round6(rgbBL[n]);
						rgbBR6[n] = Round6(rgbBR[n]);
					}

					// If tile is available...
					bool allowA = srcA ? (mapSmoothTile->GetPlane(0)->GetPixelValue(x,y,isOutside) == 0) : true;
					bool allowB = srcB ? (mapSmoothTile->GetPlane(1)->GetPixelValue(x,y,isOutside) == 0) : true;
					bool allowC = srcC ? (mapSmoothTile->GetPlane(2)->GetPixelValue(x,y,isOutside) == 0) : true;

					if (allowA && allowB && allowC) {
						// Compute fit or not.
						int lF,rF;
						int tF,bF;
						bool rejectTile  = false;
						bool rejectTile6 = false;
						bool rejectTileO = false;
						bool rejectTile6O= false;
						bool rejectTile6OE= false;
						bool rejectTile6E= false;

						//---------------------------------------------------------
						//  Evaluate if RGB Gradient from topleft pixel corner of
						//  CurrentTile -> Next Tile X
						//      |
						//      V
						//  Next Tile Y -> Next Tile X,Y
						//---------------------------------------------------------

						for (int dy = 0; dy < tileSizeY; dy++) {
							if (tileSizeY == 4) {
								tF = weight4[dy];
							} else {
								if (tileSizeY == 8) {
									tF = weight8[dy];
								} else {
									tF = weight16[dy];
								}
							}
							bF = 1024-tF;

							for (int dx = 0; dx < tileSizeX; dx++) {
								int rgbCurr[3]; 
								rgbCurr[0] = srcA ? srcA->GetPixelValue(x+dx,y+dy,isOutside) : 0;
								rgbCurr[1] = srcB ? srcB->GetPixelValue(x+dx,y+dy,isOutside) : 0;
								rgbCurr[2] = srcC ? srcC->GetPixelValue(x+dx,y+dy,isOutside) : 0;

								if (isOutside) {
									rejectTile = true; // Bottom partly cut tile can not be encoded... sorry... not for now.
									goto nextTile;
								}

								if (tileSizeX == 4) {
									lF = weight4[dx];
								} else {
									if (tileSizeX == 8) {
										lF = weight8[dx];
									} else {
										lF = weight16[dx];
									}
								}
								rF = 1024-lF;


								int rounding = ((1<<19)-1);

								// 888 Color blend.
								int blendT[3];
								int blendB[3];
								int blendC[3];
								int blendCO[3];
								for (int ch=0; ch<3;ch++) { 
									blendT[ch] = rgbTL[ch] * lF + rgbTR[ch] * rF; // *1024 scale
									blendB[ch] = rgbBL[ch] * lF + rgbBR[ch] * rF;
									blendC[ch]  = ((blendT[ch] * tF + blendB[ch] * bF) + rounding) / (1024*1024);
									blendCO[ch] = ((blendT[ch] * tF + blendB[ch] * bF)) / (1024*1024);
								}

								// 666 Color version check : noticed that some tile did pass the test when using 666 color instead of 888 interpolation.
								int blendT6[3];
								int blendB6[3];
								int blendC6[3];
								int blendC6O[3];

								for (int ch=0; ch<3;ch++) {
									blendT6 [ch] =    rgbTL6[ch] * lF +  rgbTR6[ch] * rF; // *1024 scale
									blendB6 [ch] =    rgbBL6[ch] * lF +  rgbBR6[ch] * rF;
									blendC6 [ch] = ((blendT6[ch] * tF + blendB6[ch] * bF) + rounding) / (1024*1024);
									blendC6O[ch] = ((blendT6[ch] * tF + blendB6[ch] * bF)) / (1024*1024);
								}

								int blendT6Exp[3];
								int blendB6Exp[3];
								int blendC6Exp[3];
								int blendC6OExp[3];

								for (int ch=0; ch<3;ch++) {
									blendT6Exp [ch] = Round6P(rgbTL[ch]) * lF +  Round6P(rgbTR[ch]) * rF; // *1024 scale
									blendB6Exp [ch] = Round6P(rgbBL[ch]) * lF +  Round6P(rgbBR[ch]) * rF;
									blendC6Exp [ch] = ((blendT6Exp[ch] * tF + blendB6Exp[ch] * bF) + rounding) / (1024*1024);
									blendC6OExp[ch] = ((blendT6Exp[ch] * tF + blendB6Exp[ch] * bF)) / (1024*1024);
								}

								int idxT = dx + dy*tileSizeX;
								rTile[idxT] = blendC6Exp[0]; rTile6[idxT] = blendC6[0];
								gTile[idxT] = blendC6Exp[1]; gTile6[idxT] = blendC6[1];
								bTile[idxT] = blendC6Exp[2]; bTile6[idxT] = blendC6[2];

								if ((abs(rgbCurr[0]-blendC[0])>rejectFactor) || (abs(rgbCurr[1]-blendC[1])>rejectFactor)  || (abs(rgbCurr[2]-blendC[2])>rejectFactor)) {
									rejectTile = true;
								}

								if ((abs(rgbCurr[0]-blendC6[0])>rejectFactor) || (abs(rgbCurr[1]-blendC6[1])>rejectFactor)  || (abs(rgbCurr[2]-blendC6[2])>rejectFactor)) {
									rejectTile6 = true;
								}
								if ((abs(rgbCurr[0]-blendCO[0])>rejectFactor) || (abs(rgbCurr[1]-blendCO[1])>rejectFactor)  || (abs(rgbCurr[2]-blendCO[2])>rejectFactor)) {
									rejectTileO = true;
								}
								if ((abs(rgbCurr[0]-blendC6O[0])>rejectFactor) || (abs(rgbCurr[1]-blendC6O[1])>rejectFactor)  || (abs(rgbCurr[2]-blendC6O[2])>rejectFactor)) {
									rejectTile6O = true;
								}
								if ((abs(rgbCurr[0]-blendC6OExp[0])>rejectFactor) || (abs(rgbCurr[1]-blendC6OExp[1])>rejectFactor)  || (abs(rgbCurr[2]-blendC6OExp[2])>rejectFactor)) {
									rejectTile6OE = true;
								}
								if ((abs(rgbCurr[0]-blendC6Exp[0])>rejectFactor) || (abs(rgbCurr[1]-blendC6Exp[1])>rejectFactor)  || (abs(rgbCurr[2]-blendC6Exp[2])>rejectFactor)) {
									rejectTile6E = true;
								}
							}
						}

						// --------------------------------------------------------
						//   If Tile match.
						// --------------------------------------------------------
						if ((!rejectTile || !rejectTileO) || (!rejectTile6 || !rejectTile6O) || (!rejectTile6OE || !rejectTile6E)) {
							int EncodedA[3],EncodedB[3],EncodedC[3],EncodedD[3];
					
							for (int n=0; n < 3; n++) {
								Plane* p = mappedRGB->GetPlane(n);
								if ((n == 0 && (PlaneBit&1)) || (n==1 && (PlaneBit&2)) || (n==2 && (PlaneBit&4))) {
									EncodedA[n] = p->GetPixelValue				  (x         ,y           ,isOutside);
									if (!isOutside && !EncodedA[n]) {
										p->SetPixel(x ,y ,255); 
									}
									EncodedB[n] = p->GetPixelValue               (x+tileSizeX,y          ,isOutside);
									if (!isOutside && !EncodedB[n]) {
										p->SetPixel(x+tileSizeX,y,255); 
									}
									EncodedC[n] = p->GetPixelValue               (x         ,y+tileSizeY ,isOutside);
									if (!isOutside && !EncodedC[n]) {
										p->SetPixel(x,y+tileSizeY,255); 
									}
									EncodedD[n] = p->GetPixelValue               (x+tileSizeX,y+tileSizeY,isOutside);
									if (!isOutside && !EncodedD[n]) {
										p->SetPixel(x+tileSizeX,y+tileSizeY,255);
									}
								}
							}
			
							// Is this pixel have been processed ?
							bool isOutside;

							pFillBitMap[pos>>3] |= (1<<(pos&7));

							// Mark the tile for next passes (avoid compressing sub tile inside bigger tile from previous pass)
							for (int dy = 0; dy < tileSizeY; dy++) {
								for (int dx = 0; dx < tileSizeX; dx++) {
									smoothMap->SetPixel(x+dx,y+dy,255);
									if (srcA) { mapSmoothTile->GetPlane(0)->SetPixel(x+dx,y+dy,255); }
									if (srcB) { mapSmoothTile->GetPlane(1)->SetPixel(x+dx,y+dy,255); }
									if (srcC) { mapSmoothTile->GetPlane(2)->SetPixel(x+dx,y+dy,255); }
									mipmapMask->SetPixel(x+dx,y+dy,0); // Pixel removed.
								}
							}

							if (minX > x)			{ minX = x; }
							if (minY > y)			{ minY = y; }
							if (maxX < x+tileSizeX) { maxX = x + tileSizeX; }
							if (maxY < y+tileSizeY) { maxY = y + tileSizeY; }

							TileDone++;

							if (pStats) {
								int pixelCountTile = tileSizeX * tileSizeY;
								if (srcA && srcB && srcC) {
									if ((tileSizeX == 16) && (tileSizeY == 16)) {
										pStats->loc.pixelCountGradient16_16 += pixelCountTile;
									}
									if ((tileSizeX == 16) && (tileSizeY == 8)) {
										pStats->loc.pixelCountGradient16_8 += pixelCountTile;
									}
									if ((tileSizeX == 8) && (tileSizeY == 16)) {
										pStats->loc.pixelCountGradient8_16 += pixelCountTile;
									}
									if ((tileSizeX == 8) && (tileSizeY == 8)) {
										pStats->loc.pixelCountGradient8_8 += pixelCountTile;
									}
									if ((tileSizeX == 4) && (tileSizeY == 8)) {
										pStats->loc.pixelCountGradient4_8 += pixelCountTile;
									}
									if ((tileSizeX == 8) && (tileSizeY == 4)) {
										pStats->loc.pixelCountGradient8_4 += pixelCountTile;
									}
									if ((tileSizeX == 4) && (tileSizeY == 4)) {
										pStats->loc.pixelCountGradient4_4 += pixelCountTile;
									}

									pStats->loc.pixelCount3DGradientTotal += pixelCountTile;
								}
								else
								if ((srcA && srcB) || (srcC && srcB) || (srcA && srcC)) {
									pStats->loc.pixelCount2DGradientTotal += pixelCountTile;

									if ((tileSizeX == 4) && (tileSizeY == 4)) {
										if (srcA && srcB) {
											pStats->loc.pixelCountGradientRG_4_4 += pixelCountTile;
										}
										if (srcB && srcC) {
											pStats->loc.pixelCountGradientGB_4_4 += pixelCountTile;
										}
										if (srcA && srcC) {
											pStats->loc.pixelCountGradientRB_4_4 += pixelCountTile;
										}
									}

									pStats->loc.pixelCount2DGradientTotal	+= pixelCountTile;
								} else {
									// 1D Gradient : Do nothing for now...
									pStats->loc.pixelCount1DGradientTotal	+= pixelCountTile;
								}
							}

							for (int dy = 0; dy < tileSizeY; dy++) {
								for (int dx = 0; dx < tileSizeX; dx++) {
									int idxT = dx + dy*tileSizeX;
									// Generate RGB interpolated for compare
									if (srcA) { testOutput->GetPlane(0)->SetPixel(x+dx,y+dy,rTile[idxT]); }
									if (srcB) { testOutput->GetPlane(1)->SetPixel(x+dx,y+dy,gTile[idxT]); }
									if (srcC) { testOutput->GetPlane(2)->SetPixel(x+dx,y+dy,bTile[idxT]); }
								}
							}

							// Write Top Left
							bool orgTile = (!rejectTile || !rejectTileO);
							int* TL = rgbTL6; // orgTile ?  : rgbTL6;   // Allow RGB full width to blend, even if matching not good in 888 space (but ok in 666 space)
							int* TR = rgbTR6; // orgTile ?  : rgbTR6;
							int* BL = rgbBL6; // orgTile ?  : rgbBL6;
							int* BR = rgbBR6; // orgTile ?  : rgbBR6;

							int coded = 0;

							if (srcA && !EncodedA[0]) { *wrRgbStream++ = CompressF(TL[0],colorCompressionQuad); coded |= 0x0001; /*printf("TL\n");*/ }
							if (srcB && !EncodedA[1]) { *wrRgbStream++ = CompressF(TL[1],colorCompressionQuad); coded |= 0x0002; }
							if (srcC && !EncodedA[2]) { *wrRgbStream++ = CompressF(TL[2],colorCompressionQuad); coded |= 0x0004; }

							// Top Right
							if (srcA && !EncodedB[0]) { *wrRgbStream++ = CompressF(TR[0],colorCompressionQuad); coded |= 0x0010; /*printf("TR\n");*/ }
							if (srcB && !EncodedB[1]) { *wrRgbStream++ = CompressF(TR[1],colorCompressionQuad); coded |= 0x0020; }
							if (srcC && !EncodedB[2]) { *wrRgbStream++ = CompressF(TR[2],colorCompressionQuad); coded |= 0x0040; }

							// Bottom Left
							if (srcA && !EncodedC[0]) { *wrRgbStream++ = CompressF(BL[0],colorCompressionQuad); coded |= 0x0100; /*printf("BL\n");*/ }
							if (srcB && !EncodedC[1]) { *wrRgbStream++ = CompressF(BL[1],colorCompressionQuad); coded |= 0x0200; }
							if (srcC && !EncodedC[2]) { *wrRgbStream++ = CompressF(BL[2],colorCompressionQuad); coded |= 0x0400; }

							// Bottom Right
							if (srcA && !EncodedD[0]) { *wrRgbStream++ = CompressF(BR[0],colorCompressionQuad); coded |= 0x1000; /*printf("BR\n");*/ }
							if (srcB && !EncodedD[1]) { *wrRgbStream++ = CompressF(BR[1],colorCompressionQuad); coded |= 0x2000; }
							if (srcC && !EncodedD[2]) { *wrRgbStream++ = CompressF(BR[2],colorCompressionQuad); coded |= 0x4000; }
#if 0
							if (srcA && srcB && srcC) {
								// RGB
								printf("Grad %i,%i [%i,%i,%i][%i,%i,%i]-[%i,%i,%i][%i,%i,%i]\n",
									x,y,
									TL[0],TL[1],TL[2],
									TR[0],TR[1],TR[2],
									BL[0],BL[1],BL[2],
									BR[0],BR[1],BR[2]
								);
							} else {
								if (srcA) {
									if (srcB) {
										// RG_
										printf("Coded %x Grad %i,%i [%i,%i][%i,%i]-[%i,%i][%i,%i]\n",
											coded,
											x,y,
											TL[0],TL[1],
											TR[0],TR[1],
											BL[0],BL[1],
											BR[0],BR[1]
										);
									} else {
										if (srcC) {
											// R_B
											printf("Coded %x Grad %i,%i [%i,%i][%i,%i]-[%i,%i][%i,%i]\n",
												coded,
												x,y,
												TL[0],TL[2],
												TR[0],TR[2],
												BL[0],BL[2],
												BR[0],BR[2]
											);
										} else {
											// R__
											printf("Coded %x Grad %i,%i [%i][%i]-[%i][%i]\n",
												coded,
												x,y,
												TL[0],
												TR[0],
												BL[0],
												BR[0]
											);
										}
									}
								} else {
									if (srcB) {
										if (srcC) {
											// _GB
											printf("Coded %x Grad %i,%i [%i,%i][%i,%i]-[%i,%i][%i,%i]\n",
												coded,
												x,y,
												TL[1],TL[2],
												TR[1],TR[2],
												BL[1],BL[2],
												BR[1],BR[2]
											);
										} else {
											// _G_
											printf("Coded %x Grad %i,%i [%i][%i]-[%i][%i]\n",
												coded,
												x,y,
												TL[1],
												TR[1],
												BL[1],
												BR[1]
											);
										}
									} else {
										// ___
										// __B
										printf("Coded %x Grad %i,%i [%i][%i]-[%i][%i]\n",
											coded,
											x,y,
											TL[2],
											TR[2],
											BL[2],
											BR[2]
										);
									}
								}
							}
#endif
							printf("[%i,%i] -> %i\n",x,y,(wrRgbStream - rgbStream) / 3);

							/*
							printf("Tile Color TL : %i,%i,%i\n",TL[0],TL[1],TL[2]);
							printf("Tile Color TR : %i,%i,%i\n",TR[0],TR[1],TR[2]);
							printf("Tile Color BL : %i,%i,%i\n",BL[0],BL[1],BL[2]);
							printf("Tile Color BR : %i,%i,%i\n",BR[0],BR[1],BR[2]);
							*/
						} // End Tile Accepted as gradient tile.
					} // End Accept Tile for encoding test.
					
				nextTile:
					pos++;
				}
				posY += stepY;
			}
			posXS += stepXS;
		}
		posYS += stepYS;
	}

	int uncompressRGBSize = wrRgbStream - rgbStream;

	if (!evaluateLUT && (maxX > minX) && (maxY > minY) && (uncompressRGBSize > 0)) {
		HeaderGradientTile header;

		/*
		// Clip correctly the bitmap because of swizzling.
		if (tileSizeY < 8) {
			minY = ((minY     >> 3) << 3);		// Rounding 8 pixel Trunc.
			maxY = (((maxY+7) >> 3) << 3);		// Rounding 8 pixel Ceil.
		}

		// Align horizontally to speed up decoder.
		minX = (minX     >> 3) << 3;
		maxX = ((maxX+7) >> 3) << 3;
		*/

		// Setup bounding box.
		header.bbox.x	= minX;
		header.bbox.y	= minY;
		header.bbox.w	= maxX - minX;
		header.bbox.h	= maxY - minX;

		// Setup format.
		header.format	= tileShiftX | (tileShiftY<<3);
		header.plane	= PlaneBit;

		printf("Tile Count : %i, ",TileDone);

		int result; unsigned char* pZStdStreamBitmap = CompressStream(pFillBitMap,sizeBitmap, &result);
		printf("Gradient Map %ix%i : %i , ",tileSizeX, tileSizeY,result);
		header.streamBitmapSize = result;

		int sizeDec = streamW * streamH * 3;
		unsigned char* pZStdStreamRGB = new unsigned char[streamH * streamW * 3 * 2];
#if 0
		// Compress the RGB stream.
		pZStdStreamRGB = CompressStream(rgbStream, uncompressRGBSize, &result);
		header.streamRGBSizeUncompressed	= uncompressRGBSize;
#else
		u8* tmpBuffer = new u8[uncompressRGBSize*3];
		u32 sizeInOut = uncompressRGBSize*3;
		if (PaletteCompressor(rgbStream, uncompressRGBSize, tmpBuffer, &sizeInOut)) {
			u8* decompTest = new u8[uncompressRGBSize];
			PaletteDecompressor(tmpBuffer, sizeInOut, uncompressRGBSize, decompTest, uncompressRGBSize,colorCompressionQuad);
			
			bool errorPal = false;
			/*
			u8* originalMapTo8 = new u8[uncompressRGBSize];
			for (int i=0; i < uncompressRGBSize; i++) {
				originalMapTo8[i] = (rgbStream[i]<<2) | (rgbStream[i]>>4)????; // WRONG. Need func(rgbStream[i],colorCompressionQuad)
				if (originalMapTo8[i] != decompTest[i]) {
					printf("ERROR PALETTE %i\n",i);
					errorPal = true;
				}
			}
			*/

			if (errorPal) {
				printf("Palette decompression error\n");
				// while (1) {}
			}
			delete[] decompTest;

			pZStdStreamRGB = CompressStream(tmpBuffer, sizeInOut, &result);
		}
		header.streamRGBSizeCustomCompressor= sizeInOut;
		header.streamRGBSizeUncompressed	= uncompressRGBSize;
		header.colorCompression				= colorCompressionQuad;

		delete[] tmpBuffer;
#endif
		header.streamRGBSizeZStd			= result;

		fileOutSize += result;
		printf("RGB Gradient Tile %ix%i : %i->%i\n",tileSizeX, tileSizeY,uncompressRGBSize*3,result);

		HeaderBase headerTag;
		headerTag.tag.tag8[0] = 'G';
		headerTag.tag.tag8[1] = 'T';
		headerTag.tag.tag8[2] = 'I';
		headerTag.tag.tag8[3] = 'L';

//		stbi_write_bmp("palette6.bmp",uncompressRGBSize / 3,1,3,rgbStream);

		int baseSize = (sizeof(HeaderGradientTile) + header.streamBitmapSize + header.streamRGBSizeZStd);
		headerTag.length	  = ((baseSize + 3) >> 2) <<2;	// Round multiple of 4.

		if (pStats) {
			if (PlaneBit == 7) {
				pStats->loc.sizeBlock3DGradient += headerTag.length; 
			} else {
				if ((PlaneBit==3) || (PlaneBit==6) || (PlaneBit==5)) {
					pStats->loc.sizeBlock2DGradient	+= headerTag.length;
				} else {
					pStats->loc.sizeBlock1DGradient	+= headerTag.length;
				}
			}
		}

		u8 pad[3] = { 0,0,0 };
		int padding = headerTag.length - baseSize;

		MipmapHeader headerMip;
		headerMip;

		fwrite(&headerTag, sizeof(HeaderBase)	, 1, outFile);
		fwrite(&header,1,sizeof(HeaderGradientTile),outFile);
		fwrite(pZStdStreamBitmap,1,header.streamBitmapSize,outFile);
		fwrite(pZStdStreamRGB,1,header.streamRGBSizeZStd,outFile);
		if (padding) { fwrite(pad, 1, padding, outFile); }

		delete[] pZStdStreamRGB;
		delete[] pZStdStreamBitmap;


		if (dumpImage) {
			if (tileSizeX ==  4) { smoothMap->SaveAsPNG("NewSmoothMap4.png"); }
			if (tileSizeX ==  8) { smoothMap->SaveAsPNG("NewSmoothMap8.png"); }
			if (tileSizeX == 16) { smoothMap->SaveAsPNG("NewSmoothMap16.png"); }
		}
	}

	delete[] pFillBitMap;
	delete[] rgbStream;
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


/*
void BuildDistanceField(QuadSpline* list, int countList, int* target64x64) {
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
*/

void EncoderContext::Create2DCorrelationPatterns() {
}

void reduceArray(u8* ptsXYZ_in, u8* ptsXYZ_out, u8 countIn, u8 countOut, bool is2D) {

	int step = is2D ? 2 : 3;

	int avg[3];
	int cnt = 1;
	int prevCurr = -1;

	int idxD = 0;
	for (int src = 0; src < countIn; src++) {
		int idxS = src*step;
		int curr = (src * countOut) / countIn;
		if (curr != prevCurr) {
			if (prevCurr != -1) {
				ptsXYZ_out[idxD+0] = avg[0]/cnt;
				ptsXYZ_out[idxD+1] = avg[1]/cnt;
				if (!is2D) {
					ptsXYZ_out[idxD+2] = avg[2]/cnt;
				}
				idxD += step;
			}

			avg[0] = ptsXYZ_in[idxS+0]; cnt = 1;
			avg[1] = ptsXYZ_in[idxS+1]; 
			if (!is2D) {
				avg[2] = ptsXYZ_in[idxS+2];
			}
		} else {
			avg[0] += ptsXYZ_in[idxS+0]; cnt++;
			avg[1] += ptsXYZ_in[idxS+1]; 
			if (!is2D) {
				avg[2] += ptsXYZ_in[idxS+2];
			}
		}

		prevCurr = curr;		
	}

	if (idxD < (countOut*step)) {
		ptsXYZ_out[idxD+0] = avg[0]/cnt;
		ptsXYZ_out[idxD+1] = avg[1]/cnt;
		if (!is2D) {
			ptsXYZ_out[idxD+2] = avg[2]/cnt;
		}
		idxD+=step;
		if (idxD < (countOut*step)) {
			printf("ERROR");
		}
	}
}

void EncoderContext::EvalCtxBase::Clear() {
	memset(xFactor3Bit,0,sizeof(s16)*8);
	memset(yFactor3Bit,0,sizeof(s16)*8);
	memset(zFactor3Bit,0,sizeof(s16)*8);

	memset(xFactor4Bit,0,sizeof(s16)*16);
	memset(yFactor4Bit,0,sizeof(s16)*16);
	memset(zFactor4Bit,0,sizeof(s16)*16);

	memset(xFactor5Bit,0,sizeof(s16)*32);
	memset(yFactor5Bit,0,sizeof(s16)*32);
	memset(zFactor5Bit,0,sizeof(s16)*32);

	memset(xFactor6Bit,0,sizeof(s16)*64);
	memset(yFactor6Bit,0,sizeof(s16)*64);
	memset(zFactor6Bit,0,sizeof(s16)*64);
}

void EncoderContext::EvalCtx3D::Set3DPointCloud(float acceptanceScore, u8* ptsXYZ, u8 ptsCount) {
	acceptScore = acceptanceScore;

	Clear();

	// 1,2,4,8 (6,5,4,3 bits)
	for (int pts =0; pts < ptsCount; pts++) {
		int idxPts = pts * 3;
		xFactor6Bit[pts] = (ptsXYZ[idxPts  ] / 63.0f) * FACTOR;
		yFactor6Bit[pts] = (ptsXYZ[idxPts+1] / 63.0f) * FACTOR;
		zFactor6Bit[pts] = (ptsXYZ[idxPts+2] / 63.0f) * FACTOR;
	}

	for (int pts =0; pts < ptsCount; pts += 2) {
		int idxPts = pts * 3;
		xFactor5Bit[pts>>1] = (ptsXYZ[idxPts  ] / 63.0f) * FACTOR;
		yFactor5Bit[pts>>1] = (ptsXYZ[idxPts+1] / 63.0f) * FACTOR;
		zFactor5Bit[pts>>1] = (ptsXYZ[idxPts+2] / 63.0f) * FACTOR;
	}

	for (int pts =0; pts < ptsCount; pts += 4) {
		int idxPts = pts * 3;
		xFactor4Bit[pts>>2] = (ptsXYZ[idxPts  ] / 63.0f) * FACTOR;
		yFactor4Bit[pts>>2] = (ptsXYZ[idxPts+1] / 63.0f) * FACTOR;
		zFactor4Bit[pts>>2] = (ptsXYZ[idxPts+2] / 63.0f) * FACTOR;
	}

	for (int pts =0; pts < ptsCount; pts += 8) {
		int idxPts = pts * 3;
		xFactor3Bit[pts>>3] = (ptsXYZ[idxPts  ] / 63.0f) * FACTOR;
		yFactor3Bit[pts>>3] = (ptsXYZ[idxPts+1] / 63.0f) * FACTOR;
		zFactor3Bit[pts>>3] = (ptsXYZ[idxPts+2] / 63.0f) * FACTOR;
	}

	for (int step=0; step < 4; step++) {
		// Scan whole space.
		for (int z=0; z < 64; z++) {
			for (int y=0; y < 64; y++) {
				for (int x=0; x < 64; x++) {

					int minDist = 999999999;

					// Find closest point.
					for (int pts =0; pts < ptsCount; pts += (1<<step)) {
						int idxPts = pts * 3;
						int px = ptsXYZ[idxPts  ];
						int py = ptsXYZ[idxPts+1];
						int pz = ptsXYZ[idxPts+2];

						int dx = x-px;
						int dy = y-py;
						int dz = z-pz;

						int dist = (dx*dx) + (dy*dy) + (dz*dz);
						if (dist < minDist) {
							minDist = dist;
							int idx3d = x + (y<<6) + (z<<12);
							distanceField3D[idx3d] = dist;
							switch (step) {
							case 0: position6Bit3D[idx3d] = pts;    break;
							case 1: position5Bit3D[idx3d] = pts>>1; break;
							case 2: position4Bit3D[idx3d] = pts>>2; break;
							case 3: position3Bit3D[idx3d] = pts>>3; break;
							}
						}
					}
				}
			}
		}
	}
}

void EncoderContext::EvalCtx3D::BuildTable3D(s16* xFactor, s16* yFactor, s16* zFactor, float* tFactor, float totalDistance, int elementCount) {
	// LinearEqu3D* equList3D, int equCount, 
	float step = 1.0f/((float)elementCount-1.0f);
	float pos  = 0.0f;
	int   c    = 0;

	bool  notFound = false;
	float dPP;
	float dPart;
	float x0;
	float y0;
	float z0 = 0.0f;
	float x1;
	float y1;
	float z1 = 0.0f;

	for (int n=0; n < elementCount; n++) {

		if (notFound) {
			xFactor[n] = (x1 / 64.0f) * FACTOR;
			yFactor[n] = (y1 / 64.0f) * FACTOR;
			zFactor[n] = (z1 / 64.0f) * FACTOR;
			tFactor[n] = 1.0f;
		} else {
			LinearEqu3D* equ = &equList3D[c];
			dPP   = equ->distancePartPosition;
			dPart = equ->distancePart;
			x0=equ->x0;x1=equ->x1; y0=equ->y0;y1=equ->y1; z0=equ->z0;z1=equ->z1;

			float posLocal = ((pos - dPP) / dPart) + 0.00001f;

			xFactor[n] = ((x0 + ((x1-x0) * posLocal)) / 64.0f) * FACTOR;
			yFactor[n] = ((y0 + ((y1-y0) * posLocal)) / 64.0f) * FACTOR;
			zFactor[n] = ((z0 + ((z1-z0) * posLocal)) / 64.0f) * FACTOR;
			tFactor[n] = pos;
		}

		kassert((xFactor[n] >= 0) && (xFactor[n] <= FACTOR));
		kassert((yFactor[n] >= 0) && (yFactor[n] <= FACTOR));
		kassert((zFactor[n] >= 0) && (zFactor[n] <= FACTOR));

		pos += step;
		while (pos > (dPP + dPart)) {
			c++;
			if (c < equCount) {
				LinearEqu3D* equ = &equList3D[c];
				dPP   = equ->distancePartPosition;
				dPart = equ->distancePart;
				x0=equ->x0;x1=equ->x1; y0=equ->y0;y1=equ->y1; z0=equ->z0;z1=equ->z1;
			} else {
				// No more equations... End of curve.
				// Use last segment (equ not modified)
				// Force 
				if (n < (elementCount-1)) {
					notFound = true;
				}
				break;
			}
		}
	}
}

void EncoderContext::EvalCtx2D::BuildTable2D(s16* xFactor, s16* yFactor, float* tFactor, float totalDistance, int elementCount) {
	float step = 1.0f/((float)elementCount-1.0f);
	float pos  = 0.0f;
	int   c    = 0;

	bool  notFound = false;
	float dPP;
	float dPart;
	float x0;
	float y0;
	float z0 = 0.0f;
	float x1;
	float y1;
	float z1 = 0.0f;

	for (int n=0; n < elementCount; n++) {

		if (notFound) {
			xFactor[n] = (x1 / 64.0f) * FACTOR;
			yFactor[n] = (y1 / 64.0f) * FACTOR;
			tFactor[n] = 1.0f;
		} else {
			LinearEqu2D* equ = &equList2D[c];
			dPP   = equ->distancePartPosition;
			dPart = equ->distancePart;
			x0=equ->x0;x1=equ->x1; y0=equ->y0;y1=equ->y1;
			
			float posLocal = ((pos - dPP) / dPart) + 0.00001f;

			xFactor[n] = ((x0 + ((x1-x0) * posLocal)) / 64.0f) * FACTOR;
			yFactor[n] = ((y0 + ((y1-y0) * posLocal)) / 64.0f) * FACTOR;
			tFactor[n] = pos;
		}

		kassert((xFactor[n] >= 0) && (xFactor[n] <= FACTOR));
		kassert((yFactor[n] >= 0) && (yFactor[n] <= FACTOR));

		pos += step;
		while (pos > (dPP + dPart)) {
			c++;
			if (c < equCount) {
				LinearEqu2D* equ = &equList2D[c];
				dPP   = equ->distancePartPosition;
				dPart = equ->distancePart;
				x0=equ->x0;x1=equ->x1; y0=equ->y0;y1=equ->y1;
			} else {
				// No more equations... End of curve.
				// Use last segment (equ not modified)
				// Force 
				if (n < (elementCount-1)) {
					notFound = true;
				}
				break;
			}
		}
	}
}

void EncoderContext::EvalCtx3D::BuildDistanceField3D() {
	float totalDistance = 0.0f;

	// Compute total distance.
	for (int n=0; n < equCount; n++) {
		totalDistance += equList3D[n].pieceLength;
	}

	// Compute Segment part : % from start, % of total length.
	float currDistance = 0.0f;
	for (int n=0; n < equCount; n++) {
		LinearEqu3D& equ = equList3D[n];
		equ.distancePartPosition	= currDistance    / totalDistance;
		equ.distancePart			= equ.pieceLength / totalDistance;
		currDistance += equ.pieceLength;
	}

	BuildTable3D(xFactor6Bit, yFactor6Bit, zFactor6Bit, tFactor6Bit, totalDistance, 64);
	BuildTable3D(xFactor5Bit, yFactor5Bit, zFactor5Bit, tFactor5Bit, totalDistance, 32);
	BuildTable3D(xFactor4Bit, yFactor4Bit, zFactor4Bit, tFactor4Bit, totalDistance, 16);
	BuildTable3D(xFactor3Bit, yFactor3Bit, zFactor3Bit, tFactor3Bit, totalDistance,  8);

	// Compute distance field.
	float absPos = 0.0f;
	for (int z=0; z < 64; z++) {
		for (int y=0; y < 64; y++) {
			for (int x=0; x < 64; x++) {
				int idx = (y<<6) + x + (z<<12);

				float minDist = 999999999.0f;
				for (int c = 0; c < equCount; c++) {
					float ptx,pty,ptz;
					float positionT;
					float dist;
					LinearEqu3D& equ = equList3D[c];
					dist = equ.ComputeDistance3D((float)x,(float)y,(float)z,ptx,pty,ptz,positionT);
					if (minDist > dist) {
						minDist = dist;
						absPos  = equ.distancePartPosition + (positionT * equ.distancePart);
					}
				}

				minDist += 1.0f;

				// Make sure that square distance always grow : Best score is 64.
				distanceField3D[idx] = (int)(((minDist * minDist)-1.0f)*1024.0f);

				int idx5, idx4, idx3;

				float closestT6 = 999999.0f;
				float closestT5 = 999999.0f;
				float closestT4 = 999999.0f;
				float closestT3 = 999999.0f;
//				float closestT2 = 999999.0f;

				u32   closestT6Idx = -1;
				u32   closestT5Idx = -1;
				u32   closestT4Idx = -1;
				u32   closestT3Idx = -1;
//				u32   closestT2Idx = -1;

				for (int n=0; n < 64; n++) {
					float diff = fabs(absPos - tFactor6Bit[n]);
					if (diff < closestT6) {
						closestT6    = diff;
						closestT6Idx = n;
					}
				}

				for (int n=0; n < 32; n++) {
					float diff = fabs(absPos - tFactor5Bit[n]);
					if (diff < closestT5) {
						closestT5    = diff;
						closestT5Idx = n;
					}
				}

				for (int n=0; n < 16; n++) {
					float diff = fabs(absPos - tFactor4Bit[n]);
					if (diff < closestT4) {
						closestT4    = diff;
						closestT4Idx = n;
					}
				}

				for (int n=0; n < 8; n++) {
					float diff = fabs(absPos - tFactor3Bit[n]);
					if (diff < closestT3) {
						closestT3    = diff;
						closestT3Idx = n;
					}
				}
				/*
				for (int n=0; n < 4; n++) {
					float diff = fabs(absPos - tFactor2Bit[n]);
					if (diff < closestT2) {
						closestT2    = diff;
						closestT2Idx = n;
					}
				}
				*/

//				position2Bit3D[idx] = closestT2Idx;
//				kassert(closestT2Idx <= 3);
				position3Bit3D[idx] = closestT3Idx;
				position4Bit3D[idx] = closestT4Idx;
				position5Bit3D[idx] = closestT5Idx;
				position6Bit3D[idx] = closestT6Idx;

				kassert(closestT3Idx <= 7);
				kassert(closestT4Idx <= 15);
				kassert(closestT5Idx <= 31);
				kassert(closestT6Idx <= 63);

	//			printf("%i,",distanceField3D[(y<<6) + x]);
			}
//			printf("\n");
		}
//		printf("\n");
	}
}

void EncoderContext::EvalCtx2D::BuildDistanceField2D() {
	float totalDistance = 0.0f;

	// Compute total distance.
	for (int n=0; n < equCount; n++) {
		totalDistance += equList2D[n].pieceLength;
	}

	// Compute Segment part : % from start, % of total length.
	float currDistance = 0.0f;
	for (int n=0; n < equCount; n++) {
		LinearEqu2D& equ = equList2D[n];
		equ.distancePartPosition	= currDistance    / totalDistance;
		equ.distancePart			= equ.pieceLength / totalDistance;
		currDistance += equ.pieceLength;
	}

	BuildTable2D(xFactor6Bit, yFactor6Bit, tFactor6Bit, totalDistance, 64);
	BuildTable2D(xFactor5Bit, yFactor5Bit, tFactor5Bit, totalDistance, 32);
	BuildTable2D(xFactor4Bit, yFactor4Bit, tFactor4Bit, totalDistance, 16);
	BuildTable2D(xFactor3Bit, yFactor3Bit, tFactor3Bit, totalDistance,  8);

	// Compute distance field.
	float absPos = 0.0f;
	for (int y=0; y < 64; y++) {
		for (int x=0; x < 64; x++) {
			int idx = (y<<6) + x;

			float minDist = 999999999.0f;
			for (int c = 0; c < equCount; c++) {
				float ptx,pty,ptz;
				float positionT;
				float dist;
				LinearEqu2D& equ = equList2D[c];
				dist = equ.ComputeDistance2D((float)x,(float)y,ptx,pty,positionT);
				if (minDist > dist) {
					minDist = dist;
					absPos  = equ.distancePartPosition + (positionT * equ.distancePart);
				}
			}

			minDist += 1.0f;

			// Make sure that square distance always grow : Best score is 64.
			distanceField2D[idx] = (int)(((minDist * minDist)-1.0f)*1024.0f);

			int idx5, idx4, idx3;

			float closestT6 = 999999.0f;
			float closestT5 = 999999.0f;
			float closestT4 = 999999.0f;
			float closestT3 = 999999.0f;
//				float closestT2 = 999999.0f;

			u32   closestT6Idx = -1;
			u32   closestT5Idx = -1;
			u32   closestT4Idx = -1;
			u32   closestT3Idx = -1;
//				u32   closestT2Idx = -1;

			for (int n=0; n < 64; n++) {
				float diff = fabs(absPos - tFactor6Bit[n]);
				if (diff < closestT6) {
					closestT6    = diff;
					closestT6Idx = n;
				}
			}

			for (int n=0; n < 32; n++) {
				float diff = fabs(absPos - tFactor5Bit[n]);
				if (diff < closestT5) {
					closestT5    = diff;
					closestT5Idx = n;
				}
			}

			for (int n=0; n < 16; n++) {
				float diff = fabs(absPos - tFactor4Bit[n]);
				if (diff < closestT4) {
					closestT4    = diff;
					closestT4Idx = n;
				}
			}

			for (int n=0; n < 8; n++) {
				float diff = fabs(absPos - tFactor3Bit[n]);
				if (diff < closestT3) {
					closestT3    = diff;
					closestT3Idx = n;
				}
			}
			/*
			for (int n=0; n < 4; n++) {
				float diff = fabs(absPos - tFactor2Bit[n]);
				if (diff < closestT2) {
					closestT2    = diff;
					closestT2Idx = n;
				}
			}
			*/

//				position2Bit3D[idx] = closestT2Idx;
//				kassert(closestT2Idx <= 3);
			position3Bit2D[idx] = closestT3Idx;
			position4Bit2D[idx] = closestT4Idx;
			position5Bit2D[idx] = closestT5Idx;
			position6Bit2D[idx] = closestT6Idx;

			kassert(closestT3Idx <= 7);
			kassert(closestT4Idx <= 15);
			kassert(closestT5Idx <= 31);
			kassert(closestT6Idx <= 63);

//			printf("%i,",distanceField3D[(y<<6) + x]);
		}
//			printf("\n");
	}
//		printf("\n");
}

EncoderContext::Mode EncoderContext::computeValues2D(int mode, int px,int py, float* mapX, float* mapY, int pixCnt, BoundingBox bb, EvalCtx2D& ev, int& minDiff) {
	bool outP;
	int mx;
	int my;

	// Default result.
	EncoderContext::Mode res = SKIP_TOO_LOSSY;

	bool reject6Bit = false;
	bool reject5Bit = false;
	bool reject4Bit = false;

	int absErr6Bit  = 0;
	int absErr5Bit  = 0;
	int absErr4Bit  = 0;

	int streamIdx = 0;

	for (int y=0; y < 8; y++) {
		for (int x=0; x < 8; x++) {

			int idxPix = x + y*8;

			int CoV = workCo->GetPixelValue(px + x, py+y,outP);
			int CgV = workCg->GetPixelValue(px + x, py+y,outP);

			// Find coordinate in normalized box.
			// ---------------------------------------------
			// Normalize to
			float relCo = CoV - bb.x;
			float relCg = CgV - bb.y;
			relCo /= bb.w - bb.x;
			relCg /= bb.h - bb.y;

			relCo *= 63.0f;
			relCg *= 63.0f;

			// Handle mode.
			mx = (mode & 1) ? 63 - relCo : relCo;
			my = (mode & 2) ? 63 - relCg : relCg;

			if (mode & 4) {
				int tmp = mx;
				mx = my;
				my = tmp;
			}

			// Lookup Map
			int idx6Bit = ev.GetValue6Bit2D((int)mx,(int)my);
			int idx5Bit = ev.GetValue5Bit2D((int)mx,(int)my);
			int idx4Bit = ev.GetValue4Bit2D((int)mx,(int)my);

			// Find Decompressed values for mapping (Verification without decoder)
			// -------------------------------------------

			// TODO : LUT Based on Rotation mode in decoder.
			int xCoord6Bit = ev.xFactor6Bit[idx6Bit];
			int yCoord6Bit = ev.yFactor6Bit[idx6Bit];

			if (mode & 1) { xCoord6Bit = FACTOR - xCoord6Bit; }
			if (mode & 2) { yCoord6Bit = FACTOR - yCoord6Bit; }
			if (mode & 4) {
				int tmp = xCoord6Bit;
				xCoord6Bit = yCoord6Bit;
				yCoord6Bit = tmp;
			}

			int xCoord5Bit = ev.xFactor5Bit[idx5Bit];
			int yCoord5Bit = ev.yFactor5Bit[idx5Bit];
			if (mode & 1) { xCoord5Bit = FACTOR - xCoord5Bit; }
			if (mode & 2) { yCoord5Bit = FACTOR - yCoord5Bit; }
			if (mode & 4) {
				int tmp = xCoord5Bit;
				xCoord5Bit = yCoord5Bit;
				yCoord5Bit = tmp;
			}

			int xCoord4Bit = ev.xFactor4Bit[idx4Bit];
			int yCoord4Bit = ev.yFactor4Bit[idx4Bit];
			if (mode & 1) { xCoord4Bit = FACTOR - xCoord4Bit; }
			if (mode & 2) { yCoord4Bit = FACTOR - yCoord4Bit; }
			if (mode & 4) {
				int tmp = xCoord4Bit;
				xCoord4Bit = yCoord4Bit;
				yCoord4Bit = tmp;
			}

			int Co6Bit     = bb.x + (xCoord6Bit*(bb.w-bb.x))/FACTOR; 
			int Cg6Bit     = bb.y + (yCoord6Bit*(bb.h-bb.y))/FACTOR;

			int Co5Bit     = bb.x + (xCoord5Bit*(bb.w-bb.x))/FACTOR; 
			int Cg5Bit     = bb.y + (yCoord5Bit*(bb.h-bb.y))/FACTOR;

			int Co4Bit     = bb.x + (xCoord4Bit*(bb.w-bb.x))/FACTOR; 
			int Cg4Bit     = bb.y + (yCoord4Bit*(bb.h-bb.y))/FACTOR;

			int diff6BitCo	= abs(Co6Bit - CoV);
			int diff5BitCo	= abs(Co5Bit - CoV);
			int diff4BitCo	= abs(Co4Bit - CoV);

			int diff6BitCg	= abs(Cg6Bit - CgV);
			int diff5BitCg	= abs(Cg5Bit - CgV);
			int diff4BitCg	= abs(Cg4Bit - CgV);

			#define max(a,b) (((a) > (b)) ? (a) : (b))

			int lDiff6Bit	= max(diff6BitCo,diff6BitCg);
			int lDiff5Bit	= max(diff5BitCo,diff5BitCg);
			int lDiff4Bit	= max(diff4BitCo,diff4BitCg);

			#undef max

			absErr6Bit += lDiff6Bit;
			absErr5Bit += lDiff5Bit;
			absErr4Bit += lDiff4Bit;

			if (lDiff6Bit > 3) { reject6Bit = true; }
			if (lDiff5Bit > 3) { reject5Bit = true; }
			if (lDiff4Bit > 3) { reject4Bit = true; }

			ev.value6Bit[streamIdx] = idx6Bit;
			ev.value5Bit[streamIdx] = idx5Bit;
			ev.value4Bit[streamIdx] = idx4Bit;
			streamIdx++;
//			printf("Co[%i] -> %i, %i, %i   Cg[%i] -> %i, %i, %i\n",CoV,Co5Bit,Co4Bit,Co3Bit,CgV,Cg5Bit,Cg4Bit,Cg3Bit);
		}
	}

	
	if (!reject4Bit) { minDiff = absErr4Bit; return Mode::ENCODE_4BIT; }
	if (!reject5Bit) { minDiff = absErr5Bit; return Mode::ENCODE_5BIT; }
	minDiff = absErr6Bit;
	if (!reject6Bit) { return Mode::ENCODE_5BIT; }
	return Mode::SKIP_TOO_LOSSY;
}

void swap3D(int mode, int& x,int& y,int& z) {
	int tmp;
	//
	// [ !!!!! DO NOT MODIFY THIS CODE !!!!!]
	//   WITHOUT MODIFYING swap3D AND swap3DTable !!!!
	//   WITHOUT MODIFYING THE DECODER CLIENT !!!
	//  ==> May be should refactor and have some common code ?
	//
	switch (mode) {
	case 0: // Do nothing. 
		break;
	case 1: // X[ZY] 
		tmp = z;
		z   = y;
		y   = tmp;
		break;
	case 2: // [YX]Z
		tmp = x;
		x   = y;
		y   = tmp;
		break;
	case 3: // YZX
		tmp = x;
		x   = y;
		y   = z;
		z   = tmp;
		break;
	case 4: // ZXY
		tmp = y;
		y   = x;
		x   = z;
		z   = tmp;
		break;
	case 5: // ZYX
		tmp = x;
		x   = z;
		// Y Same.
		z   = tmp;
		break;
	}
}

void swap3DTable(int mode, int** ppx,int** ppy,int** ppz) {
	int* tmp;
	switch (mode) {
	case 0: // Do nothing. 
		break;
	case 1: // X[ZY] 
		tmp = *ppz;
		*ppz   = *ppy;
		*ppy   = tmp;
		break;
	case 2: // [YX]Z
		tmp = *ppx;
		*ppx= *ppy;
		*ppy= tmp;
		break;
	case 3: // YZX
		tmp = *ppx;
		*ppx= *ppy;
		*ppy= *ppz;
		*ppz= tmp;
		break;
	case 4: // ZXY
		tmp = *ppy;
		*ppy= *ppx;
		*ppx= *ppz;
		*ppz= tmp;
		break;
	case 5: // ZYX
		tmp = *ppx;
		*ppx= *ppz;
		// Y Same.
		*ppz= tmp;
		break;
	}
}

/*
void EncoderContext::EvalCtxBase::Save(FILE* stream) {
	fwrite(distanceField3D,1,sizeof(int)*64*64*64,stream);

	fwrite(position6Bit3D,1,sizeof(int)*64*64*64,stream);
	fwrite(position5Bit3D,1,sizeof(int)*64*64*64,stream);
	fwrite(position4Bit3D,1,sizeof(int)*64*64*64,stream);
	fwrite(position3Bit3D,1,sizeof(int)*64*64*64,stream);

	fwrite(xFactor6Bit,1,sizeof(  int)*64,stream);
	fwrite(yFactor6Bit,1,sizeof(  int)*64,stream);
	fwrite(zFactor6Bit,1,sizeof(  int)*64,stream);
	fwrite(tFactor6Bit,1,sizeof(float)*64,stream);

	fwrite(xFactor5Bit,1,sizeof(  int)*32,stream);
	fwrite(yFactor5Bit,1,sizeof(  int)*32,stream);
	fwrite(zFactor5Bit,1,sizeof(  int)*32,stream);
	fwrite(tFactor5Bit,1,sizeof(float)*32,stream);

	fwrite(xFactor4Bit,1,sizeof(  int)*16,stream);
	fwrite(yFactor4Bit,1,sizeof(  int)*16,stream);
	fwrite(zFactor4Bit,1,sizeof(  int)*16,stream);
	fwrite(tFactor4Bit,1,sizeof(float)*16,stream);

	fwrite(xFactor3Bit,1,sizeof(  int)*8,stream);
	fwrite(yFactor3Bit,1,sizeof(  int)*8,stream);
	fwrite(zFactor3Bit,1,sizeof(  int)*8,stream);
	fwrite(tFactor3Bit,1,sizeof(float)*8,stream);
}

void EncoderContext::EvalCtxBase::Load(FILE* stream) {
	fread(distanceField3D,1,sizeof(int)*64*64*64,stream);

	fread(position6Bit3D,1,sizeof(int)*64*64*64,stream);
	fread(position5Bit3D,1,sizeof(int)*64*64*64,stream);
	fread(position4Bit3D,1,sizeof(int)*64*64*64,stream);
	fread(position3Bit3D,1,sizeof(int)*64*64*64,stream);

	fread(xFactor6Bit,1,sizeof(int)*64,stream);
	fread(yFactor6Bit,1,sizeof(int)*64,stream);
	fread(zFactor6Bit,1,sizeof(int)*64,stream);
	fread(tFactor6Bit,1,sizeof(float)*64,stream);

	fread(xFactor5Bit,1,sizeof(int)*32,stream);
	fread(yFactor5Bit,1,sizeof(int)*32,stream);
	fread(zFactor5Bit,1,sizeof(int)*32,stream);
	fread(tFactor5Bit,1,sizeof(float)*32,stream);

	fread(xFactor4Bit,1,sizeof(int)*16,stream);
	fread(yFactor4Bit,1,sizeof(int)*16,stream);
	fread(zFactor4Bit,1,sizeof(int)*16,stream);
	fread(tFactor4Bit,1,sizeof(float)*16,stream);

	fread(xFactor3Bit,1,sizeof(int)*8,stream);
	fread(yFactor3Bit,1,sizeof(int)*8,stream);
	fread(zFactor3Bit,1,sizeof(int)*8,stream);
	fread(tFactor3Bit,1,sizeof(float)*8,stream);
}
*/

u8* EncoderContext::EvalCtx3D::BinarySave3D(u8* in, u8 pattern, EncoderContext::Mode modeBit) {
	int length;
	s16* xtbl=NULL; s16* ytbl=NULL; s16* ztbl=NULL;

	switch (modeBit) {
	case ENCODE_3BIT: length =  8; xtbl = xFactor3Bit; ytbl = yFactor3Bit; ztbl = zFactor3Bit; break;
	case ENCODE_4BIT: length = 16; xtbl = xFactor4Bit; ytbl = yFactor4Bit; ztbl = zFactor4Bit; break;
	case ENCODE_5BIT: length = 32; xtbl = xFactor5Bit; ytbl = yFactor5Bit; ztbl = zFactor5Bit; break;
	case ENCODE_6BIT: length = 64; xtbl = xFactor6Bit; ytbl = yFactor6Bit; ztbl = zFactor6Bit; break;
	}

	// Swap Axis.
	s16* uxtbl; s16* uytbl; s16* uztbl;

//	for (int n=0; n < 48; n++) {

		// Swap the Axis
		uxtbl = xtbl; uytbl = ytbl; uztbl = ztbl; 
//		swap3DTable(swapMode,&uxtbl,&uytbl,&uztbl);

		//
		// Trick : Save the factor with a resolution of 7 bit instead of 8 : we need 256 but does not fit in a byte.
		// Can NOT use 255 either ? (fails multiplications later)
		//

		for (int m=0; m < length; m++) {
			if (uxtbl[m] > 128) {
				printf("ERROR\n");
			}
			*in++ = uxtbl[m];
		}

		for (int m=0; m < length; m++) {
			if (uytbl[m] > 128) {
				printf("ERROR\n");
			}
			*in++ = uytbl[m];
		}

		for (int m=0; m < length; m++) {
			if (uztbl[m] > 128) {
				printf("ERROR\n");
			}
			*in++ = uztbl[m];
		}
//	}

	return in;
}

u8* EncoderContext::EvalCtx2D::BinarySave2D(u8* in, u8 pattern, EncoderContext::Mode modeBit) {
	int length;
	s16* xtbl=NULL; s16* ytbl=NULL;

	switch (modeBit) {
	case ENCODE_3BIT: length =  8; xtbl = xFactor3Bit; ytbl = yFactor3Bit; break;
	case ENCODE_4BIT: length = 16; xtbl = xFactor4Bit; ytbl = yFactor4Bit; break;
	case ENCODE_5BIT: length = 32; xtbl = xFactor5Bit; ytbl = yFactor5Bit; break;
	case ENCODE_6BIT: length = 64; xtbl = xFactor6Bit; ytbl = yFactor6Bit; break;
	}

	// Swap Axis.
	s16* uxtbl; s16* uytbl;

//	for (int n=0; n < 48; n++) {
		// Swap the Axis
		uxtbl = xtbl; uytbl = ytbl;

		//
		// Trick : Save the factor with a resolution of 7 bit instead of 8 : we need 256 but does not fit in a byte.
		// Can NOT use 255 either ? (fails multiplications later)
		//

		for (int m=0; m < length; m++) {
			if (uxtbl[m] > 128) {
				printf("ERROR\n");
			}
			*in++ = uxtbl[m];
		}

		for (int m=0; m < length; m++) {
			if (uytbl[m] > 128) {
				printf("ERROR\n");
			}
			*in++ = uytbl[m];
		}
//	}

	return in;
}

/*
	RG : 0
	GB : 1
	RB : 2
*/
EncoderContext::Mode EncoderContext::computeValues2D(int planeMode, int tileSizeX, int tileSizeY, u8* mask, int mode, Image* input, int px,int py, BoundingBox bb, EvalCtx2D& ev, int& minDiff, int* tile6B, int* tile5B, int* tile4B, int* tile3B/*, int* tile2B*/) {
	bool outP;
	int mx,my,mz;

	// Default result.
	EncoderContext::Mode res = SKIP_TOO_LOSSY;

	bool reject6Bit = false;
	bool reject5Bit = false;
	bool reject4Bit = false;
	bool reject3Bit = false;
	bool reject2Bit = false;

	int absErr6Bit  = 0;
	int absErr5Bit  = 0;
	int absErr4Bit  = 0;
	int absErr3Bit  = 0;
	int absErr2Bit  = 0;

	int dx = bb.w - bb.x;
	int dy = bb.h - bb.y;

	int streamIdx = 0;

	// If tile is 16 pixel wide,
	// we split it into 2 8x8 tile.
	// Makes decoder a lot more efficient.
	int tileXAxisCount = 1;
	if (tileSizeX > 8) {
		tileSizeX = 8;
		tileXAxisCount = 2;
	}

	for (int xa=0; xa < tileXAxisCount; xa++) {
		for (int y=0; y < tileSizeY; y++) {
			for (int x=0; x < tileSizeX; x++) {
				int idxPix = x+ (xa<<3)+ y*(tileSizeX<<(tileXAxisCount-1)); // Always a 16 pixel wide if using 2x 8x8 tile wide.

				if (mask[idxPix]) { continue; }

				int rgb[3]; input->GetPixel(px + x + (xa<<3), py + y,rgb, outP);

				// Find coordinate in normalized box.
				// ---------------------------------------------
				// Normalize to
				float relA;
				float relB;
				int offA;
				int offB;

				// TODO : Also Encoder must pass boundingBox with correct value in x and y (proper channel !!!)
				switch (planeMode) {
				case 0:
					offA = 0;
					offB = 1;
					break;
				case 1:
					offA = 1;
					offB = 2;
					break;
				case 2:
					offA = 0;
					offB = 2;
					break;
				}

				relA = rgb[offA] - bb.x;
				relB = rgb[offB] - bb.y;

				kassert(relA >= 0.0f);
				kassert(relB >= 0.0f);

				if (dx) { relA /= dx; }
				if (dy) { relB /= dy; }

				relA *= 63.0f;
				relB *= 63.0f;

				kassert(relA < 64.0f);
				kassert(relB < 64.0f);

				// Handle mode.
				mx = (mode & 1) ? 63 - relA : relA;
				my = (mode & 2) ? 63 - relB : relB;
				if (mode & 4) { // Swap X,Y
					int tmp = mx;
					mx = my;
					my = tmp;
				}

				// Lookup Map
				int idx6Bit = ev.GetValue6Bit2D((int)mx,(int)my);
				int idx5Bit = ev.GetValue5Bit2D((int)mx,(int)my);
				int idx4Bit = ev.GetValue4Bit2D((int)mx,(int)my);
				int idx3Bit = ev.GetValue4Bit2D((int)mx,(int)my);
				int idx2Bit = ev.GetValue4Bit2D((int)mx,(int)my);

				// Find Decompressed values for mapping (Verification without decoder)
				// -------------------------------------------

				// TODO : LUT Based on Rotation mode in decoder.
				int xCoord6Bit = ev.xFactor6Bit[idx6Bit];
				int yCoord6Bit = ev.yFactor6Bit[idx6Bit];

				if (mode & 1) { xCoord6Bit = FACTOR - xCoord6Bit; }
				if (mode & 2) { yCoord6Bit = FACTOR - yCoord6Bit; }
				if (mode & 4) { // Swap X,Y
					int tmp = xCoord6Bit;
					xCoord6Bit = yCoord6Bit;
					yCoord6Bit = tmp;
				}

				int xCoord5Bit = ev.xFactor5Bit[idx5Bit];
				int yCoord5Bit = ev.yFactor5Bit[idx5Bit];
				if (mode & 1) { xCoord5Bit = FACTOR - xCoord5Bit; }
				if (mode & 2) { yCoord5Bit = FACTOR - yCoord5Bit; }
				if (mode & 4) { // Swap X,Y
					int tmp = xCoord5Bit;
					xCoord5Bit = yCoord5Bit;
					yCoord5Bit = tmp;
				}

				int xCoord4Bit = ev.xFactor4Bit[idx4Bit];
				int yCoord4Bit = ev.yFactor4Bit[idx4Bit];
				if (mode & 1) { xCoord4Bit = FACTOR - xCoord4Bit; }
				if (mode & 2) { yCoord4Bit = FACTOR - yCoord4Bit; }
				if (mode & 4) { // Swap X,Y
					int tmp = xCoord4Bit;
					xCoord4Bit = yCoord4Bit;
					yCoord4Bit = tmp;
				}

				int xCoord3Bit = ev.xFactor3Bit[idx3Bit];
				int yCoord3Bit = ev.yFactor3Bit[idx3Bit];
				if (mode & 1) { xCoord3Bit = FACTOR - xCoord3Bit; }
				if (mode & 2) { yCoord3Bit = FACTOR - yCoord3Bit; }
				if (mode & 4) { // Swap X,Y
					int tmp = xCoord3Bit;
					xCoord3Bit = yCoord3Bit;
					yCoord3Bit = tmp;
				}

				int a6Bit     = bb.x + ((xCoord6Bit*(bb.w-bb.x))/FACTOR); 
				int b6Bit     = bb.y + ((yCoord6Bit*(bb.h-bb.y))/FACTOR);

				int a5Bit     = bb.x + ((xCoord5Bit*(bb.w-bb.x))/FACTOR); 
				int b5Bit     = bb.y + ((yCoord5Bit*(bb.h-bb.y))/FACTOR);

				int a4Bit     = bb.x + ((xCoord4Bit*(bb.w-bb.x))/FACTOR); 
				int b4Bit     = bb.y + ((yCoord4Bit*(bb.h-bb.y))/FACTOR);

				int a3Bit     = bb.x + ((xCoord3Bit*(bb.w-bb.x))/FACTOR); 
				int b3Bit     = bb.y + ((yCoord3Bit*(bb.h-bb.y))/FACTOR);

				/*
				int diff6BitR	= abs(r6Bit - rgb[0]);
				int diff5BitR	= abs(r5Bit - rgb[0]);
				int diff4BitR	= abs(r4Bit - rgb[0]);
				int diff3BitR	= abs(r3Bit - rgb[0]);
	//			int diff2BitR	= abs(r2Bit - rgb[0]);

				int diff6BitG	= abs(g6Bit - rgb[1]);
				int diff5BitG	= abs(g5Bit - rgb[1]);
				int diff4BitG	= abs(g4Bit - rgb[1]);
				int diff3BitG	= abs(g3Bit - rgb[1]);
	//			int diff2BitG	= abs(g2Bit - rgb[1]);

				int diff6BitB	= abs(b6Bit - rgb[2]);
				int diff5BitB	= abs(b5Bit - rgb[2]);
				int diff4BitB	= abs(b4Bit - rgb[2]);
				int diff3BitB	= abs(b3Bit - rgb[2]);
	//			int diff2BitB	= abs(b2Bit - rgb[2]);
				*/

				int diff6BitA;
				int diff5BitA;
				int diff4BitA;
				int diff3BitA;

				int diff6BitB;
				int diff5BitB;
				int diff4BitB;
				int diff3BitB;

				assert(idxPix < 128);
				int idxPixRGB = idxPix * 3;

				diff6BitA = abs(a6Bit - rgb[offA]);
				diff6BitB = abs(b6Bit - rgb[offB]);
				tile6B[idxPixRGB+offA] = a6Bit;
				tile6B[idxPixRGB+offB] = b6Bit;

				diff5BitA = abs(a5Bit - rgb[offA]);
				diff5BitB = abs(b5Bit - rgb[offB]);
				tile5B[idxPixRGB+offA] = a5Bit;
				tile5B[idxPixRGB+offB] = b5Bit;

				diff4BitA = abs(a4Bit - rgb[offA]);
				diff4BitB = abs(b4Bit - rgb[offB]);
				tile4B[idxPixRGB+offA] = a4Bit;
				tile4B[idxPixRGB+offB] = b4Bit;

				diff3BitA = abs(a3Bit - rgb[offA]);
				diff3BitB = abs(b3Bit - rgb[offB]);
				tile5B[idxPixRGB+offA] = a3Bit;
				tile5B[idxPixRGB+offB] = b3Bit;

				// Export encoder result.
				#define max(a,b) (((a) > (b)) ? (a) : (b))

				int lDiff6Bit	= max(diff6BitA,diff6BitB);
				int lDiff5Bit	= max(diff5BitA,diff5BitB);
				int lDiff4Bit	= max(diff4BitA,diff4BitB);
				int lDiff3Bit	= max(diff3BitA,diff3BitB);

				#undef max

				absErr6Bit += lDiff6Bit;
				absErr5Bit += lDiff5Bit;
				absErr4Bit += lDiff4Bit;
				absErr3Bit += lDiff3Bit;
	//			absErr2Bit += lDiff2Bit;

				//
				// Per pixel different is max > 4, after that, start to notice artifacts...
				//                        max > 5 or 6 can be tolerated AT the condition that we measure the COMPLETE DIFFERENCE OF THE TILE and avoid if too much change.
				// For now, allow 'some artifact' but limit is 5. NO MORE !!!!
				//
				// DIFF is over the range... Be sure you are on a 0..255 or 0..127 range.
				//
				if (lDiff6Bit > 6) { reject6Bit = true; }
				if (lDiff5Bit > 6) { reject5Bit = true; }
				if (lDiff4Bit > 6) { reject4Bit = true; }
				if (lDiff3Bit > 6) { reject3Bit = true; }
	//			if (lDiff2Bit > 7) { reject2Bit = true; }

				ev.value6Bit[streamIdx] = idx6Bit;
				ev.value5Bit[streamIdx] = idx5Bit;
				ev.value4Bit[streamIdx] = idx4Bit;
				ev.value3Bit[streamIdx] = idx3Bit;
	//			ev.value2Bit[streamIdx] = idx2Bit;
				streamIdx++;

	//			printf("Co[%i] -> %i, %i, %i   Cg[%i] -> %i, %i, %i\n",CoV,Co5Bit,Co4Bit,Co3Bit,CgV,Cg5Bit,Cg4Bit,Cg3Bit);
			}
		}
	}

//	if (!reject2Bit || ((absErr2Bit/64.0f) < 0.0f)) { minDiff = absErr2Bit; return Mode::ENCODE_2BIT; }
	if (!reject3Bit || ((absErr3Bit/64.0f) < 0.0f)) { minDiff = absErr3Bit; return Mode::ENCODE_3BIT; }
	if (!reject4Bit || ((absErr4Bit/64.0f) < 0.0f)) { minDiff = absErr4Bit; return Mode::ENCODE_4BIT; }
	if (!reject5Bit || ((absErr5Bit/64.0f) < 0.0f)) { minDiff = absErr5Bit; return Mode::ENCODE_5BIT; }
	minDiff = absErr6Bit;
	if (!reject6Bit || ((absErr6Bit/64.0f) < 0.0f)) { return Mode::ENCODE_6BIT; }
	return Mode::SKIP_TOO_LOSSY;
}

int histo3D[64*64*64];

EncoderContext::Mode EncoderContext::computeValues3D(int tileSizeX, int tileSizeY, u8* mask, int mode, Image* input, int px,int py, BoundingBox3D bb, EvalCtx3D& ev, int& minDiff, int* tile6B, int* tile5B, int* tile4B, int* tile3B/*, int* tile2B*/) {
	bool outP;
	int mx,my,mz;

	// Default result.
	EncoderContext::Mode res = SKIP_TOO_LOSSY;

	bool reject6Bit = false;
	bool reject5Bit = false;
	bool reject4Bit = false;
	bool reject3Bit = false;
	bool reject2Bit = false;

	int absErr6Bit  = 0;
	int absErr5Bit  = 0;
	int absErr4Bit  = 0;
	int absErr3Bit  = 0;
	int absErr2Bit  = 0;

	int dx = bb.x1 - bb.x0;
	int dy = bb.y1 - bb.y0;
	int dz = bb.z1 - bb.z0;

	int streamIdx = 0;

	// If tile is 16 pixel wide,
	// we split it into 2 8x8 tile.
	// Makes decoder a lot more efficient.
	int tileXAxisCount = 1;
	if (tileSizeX > 8) {
		tileSizeX = 8;
		tileXAxisCount = 2;
	}

	int lDiff6Bit;
	int lDiff5Bit;
	int lDiff4Bit;
	int lDiff3Bit;

	int wrongPixel6 = 0;
	int wrongPixel5 = 0;
	int wrongPixel4 = 0;
	int wrongPixel3 = 0;

	bool log = false;

	int reject = 0;

	for (int xa=0; xa < tileXAxisCount; xa++) {
		for (int y=0; y < tileSizeY; y++) {
			for (int x=0; x < tileSizeX; x++) {
				int idxPix = x+ (xa<<3)+ y*(tileSizeX<<(tileXAxisCount-1)); // Always a 16 pixel wide if using 2x 8x8 tile wide.


				if (mask[idxPix] == 255) { continue; }

				int rgb[3]; input->GetPixel(px + x + (xa<<3), py + y,rgb, outP);

				if (useYCoCg) {
					RGBtoYCoCgPos(rgb[0],rgb[1],rgb[2],rgb[0],rgb[1],rgb[2]);
				}

				// Find coordinate in normalized box.
				// ---------------------------------------------
				// Normalize to
				float relR = rgb[0] - bb.x0;
				float relG = rgb[1] - bb.y0;
				float relB = rgb[2] - bb.z0;

				kassert(relR >= 0.0f);
				kassert(relG >= 0.0f);
				kassert(relB >= 0.0f);

				if (dx) { relR /= dx; }
				if (dy) { relG /= dy; }
				if (dz) { relB /= dz; }

				relR *= 63.0f;
				relG *= 63.0f;
				relB *= 63.0f;

				kassert(relR < 64.0f);
				kassert(relG < 64.0f);
				kassert(relB < 64.0f);

				// Handle mode.
				mx = (mode & 1) ? 63 - relR : relR;
				my = (mode & 2) ? 63 - relG : relG;
				mz = (mode & 4) ? 63 - relB : relB;
				swap3D(mode>>3,mx,my,mz);

				// Lookup Map
				int idx6Bit = ev.GetValue6Bit3D((int)mx,(int)my,(int)mz);
				int idx5Bit = ev.GetValue5Bit3D((int)mx,(int)my,(int)mz);
				int idx4Bit = ev.GetValue4Bit3D((int)mx,(int)my,(int)mz);
				int idx3Bit = ev.GetValue3Bit3D((int)mx,(int)my,(int)mz);
//				int idx2Bit = ev.GetValue2Bit3D((int)mx,(int)my,(int)mz);

				// Find Decompressed values for mapping (Verification without decoder)
				// -------------------------------------------

				// TODO : LUT Based on Rotation mode in decoder.
				int xCoord6Bit = ev.xFactor6Bit[idx6Bit];
				int yCoord6Bit = ev.yFactor6Bit[idx6Bit];
				int zCoord6Bit = ev.zFactor6Bit[idx6Bit];

				if (mode & 1) { xCoord6Bit = FACTOR - xCoord6Bit; }
				if (mode & 2) { yCoord6Bit = FACTOR - yCoord6Bit; }
				if (mode & 4) { zCoord6Bit = FACTOR - zCoord6Bit; }
				swap3D(mode>>3,xCoord6Bit,yCoord6Bit,zCoord6Bit);

				int xCoord5Bit = ev.xFactor5Bit[idx5Bit];
				int yCoord5Bit = ev.yFactor5Bit[idx5Bit];
				int zCoord5Bit = ev.zFactor5Bit[idx5Bit];
				if (mode & 1) { xCoord5Bit = FACTOR - xCoord5Bit; }
				if (mode & 2) { yCoord5Bit = FACTOR - yCoord5Bit; }
				if (mode & 4) { zCoord5Bit = FACTOR - zCoord5Bit; }
				swap3D(mode>>3,xCoord5Bit,yCoord5Bit,zCoord5Bit);

				int xCoord4Bit = ev.xFactor4Bit[idx4Bit];
				int yCoord4Bit = ev.yFactor4Bit[idx4Bit];
				int zCoord4Bit = ev.zFactor4Bit[idx4Bit];
				if (mode & 1) { xCoord4Bit = FACTOR - xCoord4Bit; }
				if (mode & 2) { yCoord4Bit = FACTOR - yCoord4Bit; }
				if (mode & 4) { zCoord4Bit = FACTOR - zCoord4Bit; }
				swap3D(mode>>3,xCoord4Bit,yCoord4Bit,zCoord4Bit);

				int xCoord3Bit = ev.xFactor3Bit[idx3Bit];
				int yCoord3Bit = ev.yFactor3Bit[idx3Bit];
				int zCoord3Bit = ev.zFactor3Bit[idx3Bit];
				if (mode & 1) { xCoord3Bit = FACTOR - xCoord3Bit; }
				if (mode & 2) { yCoord3Bit = FACTOR - yCoord3Bit; }
				if (mode & 4) { zCoord3Bit = FACTOR - zCoord3Bit; }
				swap3D(mode>>3,xCoord3Bit,yCoord3Bit,zCoord3Bit);

				/*
				int xCoord2Bit = ev.xFactor2Bit[idx2Bit];
				int yCoord2Bit = ev.yFactor2Bit[idx2Bit];
				int zCoord2Bit = ev.zFactor2Bit[idx2Bit];
				if (mode & 1) { xCoord2Bit = 256 - xCoord2Bit; }
				if (mode & 2) { yCoord2Bit = 256 - yCoord2Bit; }
				if (mode & 4) { zCoord2Bit = 256 - zCoord2Bit; }
				swap3D(mode>>3,xCoord2Bit,yCoord2Bit,zCoord2Bit);
				*/

				int r6Bit     = bb.x0 + ((xCoord6Bit*(bb.x1-bb.x0))/FACTOR); 
				int g6Bit     = bb.y0 + ((yCoord6Bit*(bb.y1-bb.y0))/FACTOR);
				int b6Bit     = bb.z0 + ((zCoord6Bit*(bb.z1-bb.z0))/FACTOR);

				int r5Bit     = bb.x0 + ((xCoord5Bit*(bb.x1-bb.x0))/FACTOR);
				int g5Bit     = bb.y0 + ((yCoord5Bit*(bb.y1-bb.y0))/FACTOR);
				int b5Bit     = bb.z0 + ((zCoord5Bit*(bb.z1-bb.z0))/FACTOR);

				int r4Bit     = bb.x0 + ((xCoord4Bit*(bb.x1-bb.x0))/FACTOR); 
				int g4Bit     = bb.y0 + ((yCoord4Bit*(bb.y1-bb.y0))/FACTOR);
				int b4Bit     = bb.z0 + ((zCoord4Bit*(bb.z1-bb.z0))/FACTOR);

				int r3Bit     = bb.x0 + ((xCoord3Bit*(bb.x1-bb.x0))/FACTOR); 
				int g3Bit     = bb.y0 + ((yCoord3Bit*(bb.y1-bb.y0))/FACTOR);
				int b3Bit     = bb.z0 + ((zCoord3Bit*(bb.z1-bb.z0))/FACTOR);

				/*
				int r2Bit     = bb.x0 + (xCoord2Bit*(bb.x1-bb.x0))/FACTOR; 
				int g2Bit     = bb.y0 + (yCoord2Bit*(bb.y1-bb.y0))/FACTOR;
				int b2Bit     = bb.z0 + (zCoord2Bit*(bb.z1-bb.z0))/FACTOR;
				*/

				int diff6BitR	= abs(r6Bit - rgb[0]);
				int diff5BitR	= abs(r5Bit - rgb[0]);
				int diff4BitR	= abs(r4Bit - rgb[0]);
				int diff3BitR	= abs(r3Bit - rgb[0]);
	//			int diff2BitR	= abs(r2Bit - rgb[0]);

				int diff6BitG	= abs(g6Bit - rgb[1]);
				int diff5BitG	= abs(g5Bit - rgb[1]);
				int diff4BitG	= abs(g4Bit - rgb[1]);
				int diff3BitG	= abs(g3Bit - rgb[1]);
	//			int diff2BitG	= abs(g2Bit - rgb[1]);

				int diff6BitB	= abs(b6Bit - rgb[2]);
				int diff5BitB	= abs(b5Bit - rgb[2]);
				int diff4BitB	= abs(b4Bit - rgb[2]);
				int diff3BitB	= abs(b3Bit - rgb[2]);
	//			int diff2BitB	= abs(b2Bit - rgb[2]);

				if (log && ((x+(xa<<3))==11) && (y==0)) {
					printf("x:%i,y:%i -> R(%i)[%i,%i,%i,%i] G(%i)[%i,%i,%i,%i] B(%i)[%i,%i,%i,%i]\n",
						x,y,
						rgb[0],diff6BitR,diff5BitR,diff4BitR,diff3BitR,
						rgb[1],diff6BitG,diff5BitG,diff4BitG,diff3BitG,
						rgb[2],diff6BitB,diff5BitB,diff4BitB,diff3BitB
					);
				}

				assert(idxPix < 128);

				int idxPixRGB = idxPix * 3;
				// Export encoder result.
				tile6B[idxPixRGB  ] = r6Bit;
				tile6B[idxPixRGB+1] = g6Bit;
				tile6B[idxPixRGB+2] = b6Bit;

				tile5B[idxPixRGB  ] = r5Bit;
				tile5B[idxPixRGB+1] = g5Bit;
				tile5B[idxPixRGB+2] = b5Bit;

				tile4B[idxPixRGB  ] = r4Bit;
				tile4B[idxPixRGB+1] = g4Bit;
				tile4B[idxPixRGB+2] = b4Bit;

				tile3B[idxPixRGB  ] = r3Bit;
				tile3B[idxPixRGB+1] = g3Bit;
				tile3B[idxPixRGB+2] = b3Bit;
				/*
				tile2B[idxPixRGB  ] = r2Bit;
				tile2B[idxPixRGB+1] = g2Bit;
				tile2B[idxPixRGB+2] = b2Bit;
				*/
				#define max(a,b) (((a) > (b)) ? (a) : (b))

				int lDiff6Bit	= max(max(diff6BitR,diff6BitG),diff6BitB);
				int lDiff5Bit	= max(max(diff5BitR,diff5BitG),diff5BitB);
				int lDiff4Bit	= max(max(diff4BitR,diff4BitG),diff4BitB);
				int lDiff3Bit	= max(max(diff3BitR,diff3BitG),diff3BitB);
	//			int lDiff2Bit	= max(max(diff2BitR,diff2BitG),diff2BitB);

				#undef max

				absErr6Bit += lDiff6Bit;
				absErr5Bit += lDiff5Bit;
				absErr4Bit += lDiff4Bit;
				absErr3Bit += lDiff3Bit;
	//			absErr2Bit += lDiff2Bit;

				//
				// Per pixel different is max > 4, after that, start to notice artifacts...
				//                        max > 5 or 6 can be tolerated AT the condition that we measure the COMPLETE DIFFERENCE OF THE TILE and avoid if too much change.
				// For now, allow 'some artifact' but limit is 5. NO MORE !!!!
				//
				// DIFF is over the range... Be sure you are on a 0..255 or 0..127 range.
				//
				if (lDiff6Bit > 5) {
					wrongPixel6++;
					if (wrongPixel6 > 3) { reject |= 1<<0; }
				}
				if (lDiff5Bit > 5) {
					wrongPixel5++;
					if (wrongPixel5 > 3) { reject |= 1<<1; }
				}
				if (lDiff4Bit > 5) {
					wrongPixel4++;
					if (wrongPixel4 > 3) { reject |= 1<<2; }
				}
				if (lDiff3Bit > 5) {
					wrongPixel3++;
					if (wrongPixel3 > 3) { reject |= 1<<3; }
				}
	//			if (lDiff2Bit > 7) { reject2Bit = true; }

				ev.value6Bit[streamIdx] = idx6Bit;
				ev.value5Bit[streamIdx] = idx5Bit;
				ev.value4Bit[streamIdx] = idx4Bit;
				ev.value3Bit[streamIdx] = idx3Bit;
	//			ev.value2Bit[streamIdx] = idx2Bit;
				streamIdx++;

	//			printf("Co[%i] -> %i, %i, %i   Cg[%i] -> %i, %i, %i\n",CoV,Co5Bit,Co4Bit,Co3Bit,CgV,Cg5Bit,Cg4Bit,Cg3Bit);
			}

			if (reject == 0xF) { // All wrong and reject... bye bye.
				// early exit.
				goto exit;
			}
		}
	}

//	if (!reject2Bit || ((absErr2Bit/64.0f) < 0.0f)) { minDiff = absErr2Bit; return Mode::ENCODE_2BIT; }
	if ((reject & 1) == 0) { minDiff = absErr6Bit; res = Mode::ENCODE_6BIT; }
	if ((reject & 2) == 0) { minDiff = absErr5Bit; res = Mode::ENCODE_5BIT; }
	if ((reject & 4) == 0) { minDiff = absErr4Bit; res = Mode::ENCODE_4BIT; }
	if ((reject & 8) == 0) { minDiff = absErr3Bit; res = Mode::ENCODE_3BIT; }
	
	if (log) {
		printf("%s\n", (res==SKIP_TOO_LOSSY) ? "Reject" : "Accept");
	}
exit:
	return res;
}

void EncoderContext::AnalyzeColorCount(Image* input, int tileSize) {

	return ;

	int rgb[3];
	int rgbUnique[3*64];
	bool isOutSide;
	int tileCnt = 0;

	for (int py = 0; py < input->GetHeight(); py += tileSize) {
		for (int px = 0; px < input->GetWidth(); px += tileSize) {

			int unique = 0;

			for (int y=0; y < tileSize; y++) {
				for (int x=0; x < tileSize; x++) {
					input->GetPixel(x+px,y+py,rgb,isOutSide);
					rgb[0] >>= 2;
					rgb[1] >>= 2;
					rgb[2] >>= 2;

					bool found = false;

					for (int n=0; n < unique*3; n+=3) {
						if ((rgbUnique[n] == rgb[0]) && (rgbUnique[n+1] == rgb[1]) && (rgbUnique[n+2] == rgb[2])) {
							found = true;
						}
					}

					if (!found) {
						rgbUnique[unique*3  ] = rgb[0];
						rgbUnique[unique*3+1] = rgb[1];
						rgbUnique[unique*3+2] = rgb[2];
						unique++;
					}
				}
			}

//			printf("Tile %i => %i\n",tileCnt,unique);

			if (unique < 12) {
				if (unique <= 8) {
					if ((unique <= 4) && (unique > 1)) {
						// 4
						for (int y=0; y < tileSize; y++) {
							for (int x=0; x < tileSize; x++) {
								input->GetPlane(2)->SetPixel(x+px,y+py,0);
							}
						}
					} else {
						if (unique == 1) {
							// Ignore
						} else {
							// 8
							for (int y=0; y < tileSize; y++) {
								for (int x=0; x < tileSize; x++) {
									input->GetPlane(1)->SetPixel(x+px,y+py,0);
								}
							}
						}
					}
				} else {
					// 12 max
					for (int y=0; y < tileSize; y++) {
						for (int x=0; x < tileSize; x++) {
							input->GetPlane(0)->SetPixel(x+px,y+py,0);
						}
					}
				}
			} else {
				if (unique < 16) {
					for (int y=0; y < tileSize; y++) {
						for (int x=0; x < tileSize; x++) {
							input->GetPlane(0)->SetPixel(x+px,y+py,0);
							input->GetPlane(2)->SetPixel(x+px,y+py,0);
						}
					}
				}
			}

			tileCnt++;
		}
	}

	if (dumpImage) {
		original->SavePNG("PALETTE.png",NULL);
	}
}

u8* createOrderTable(int w, int h,int mode) {
	// Can not be more than 16x16.
	int size = w*h;
	u8* res = new u8[size];
	int x = 0;
	int y = 0;
	int a,b,c;
	int state = 0;
	;
	int dir = 1;
	switch (mode) {
	case 0:
		x=0;y=0;
		break;
	case 1:
		x=0;y=0;
		break;
	default:
		break;
	}

	int count = 0;
	while (count != size) {
		switch (mode) {
		case 0:
			switch (state) {
			case 0:
				if (x<w) {
					x++;
					state = 1;
				} else {
					if (y < h) {
						y++;
						state = 1;
					} else {
						// end ?
					}
				}
			}
			break;
		case 1:
			if (dir == 1) {
				x--;
				y++;
			} else {
				x++;
				y--;
			}
			break;
		default:
			break;
		}

		res[count] = (y<<4) | x;
		count++;
	}

	return NULL;
}

void EncoderContext::Correlation3DSearch(Image* input,Image* output, int tileShiftX, int tileShiftY) {
	BoundingBox3D bb3;
	bool isOutSide;
	int matchTile = 0;

	if ((tileShiftX > 4) || (tileShiftY > 4)) {
		assert(false); // Can not support yet.
	}

	if ((tileShiftX == 4) && (tileShiftY > 3)) {
		assert(false); // Can not support yet.
	}

	if ((tileShiftY == 4) && (tileShiftX > 3)) {
		assert(false); // Can not support yet.
	}

	int imgW = input->GetWidth();
	int imgH = input->GetHeight();

	// 16x16 : 16 bit table (64x64 pixel) -> Lookup the table by block of 4x4 tile. -> Allow 16/4 skip
	// 16x 8 : 32 bit table (64x64 pixel) -> Lookup the table by block of 4x8 tile. -> Allow 
	//  8x16 : 32 bit table (64x64 pixel) -> Lookup the table by block of 8x4 tile.
	//  8x 8 : 64 bit table (64x64 pixel) -> Lookup the table by block of 8x8 tile.
	//  8x 4 : 64 bit table (64x32 pixel) -> Lookup the table by block of 8x8 tile.
	//  4x 8 : 64 bit table (32x64 pixel) -> Lookup the table by block of 8x8 tile.
	//  4x 4 : 64 bit table (32x32 pixel) -> Lookup the table by block of 8x8 tile.

	u32 swizzleParseY;
	u32 swizzleParseX;
	u32 bitCount;

	HeaderGradientTile::getSwizzleSize(tileShiftX,tileShiftY,swizzleParseX,swizzleParseY, bitCount);

	int tileSizeX = 1<<tileShiftX;
	int tileSizeY = 1<<tileShiftY;

	printf("----- Tile Size %i,%i : ", tileSizeX, tileSizeY);

	int bitCountMap = HeaderGradientTile::getBitmapSwizzleSize(tileShiftX,tileShiftY,imgW,imgH);

	int xBBTileCount = ((imgW+(swizzleParseX-1)) / swizzleParseX);
	int posYS; 
	int posXS;
	int stepYS = bitCount * xBBTileCount;
	int stepXS = bitCount;						// 1 block.
	int stepY  = swizzleParseX / tileSizeX;		// Inside block vertically.
	//  stepX  = 1

	// Linear 2D order for big tile defined by swizzling.
	posYS = 0;
	for (int swizzley=0; swizzley < imgH; swizzley += swizzleParseY) {
		posXS = posYS;
		for (int swizzlex=0; swizzlex < imgW; swizzlex += swizzleParseX) {
			// Target size tile inside quad tile
			int posY = posXS;
			for (int y=swizzley; y < swizzley+swizzleParseY; y+= tileSizeY) {
				// Because of big tile parsing, we may lookup outside of image with sub-tiles. (Y-Axis)
				if (y >= imgH || ((y + tileSizeY) > imgH)) {
					break; 
				}

				int pos = posY;
				for (int x=swizzlex; x < swizzlex+swizzleParseX; x+= tileSizeX) {
					// Because of big tile parsing, we may lookup outside of image with sub-tiles. (X-Axis)
					if (x >= imgW || ((x + tileSizeX) > imgW)) {
						break; 
					}

					u8 maskTile[128]; // 0..127 : Index of pixel, 255 : skip pixel.
					int pixelsInTile;
					// For all tiles.
					bool pixelInside;
					bb3 = buildBBox3D(input,useYCoCg,mapSmoothTile,x,y,tileSizeX,tileSizeY,pixelsInTile,maskTile,pixelInside);

					// Export normalized values...
					int dX = bb3.x1 - bb3.x0;
					int dY = bb3.y1 - bb3.y0;
					int dZ = bb3.z1 - bb3.z0;

					bool accept = ((dX == 0) && ((dY != 0) && (dZ != 0)))	// X Flat Planar, YZ not.
								| ((dY == 0) && ((dX != 0) && (dZ != 0)))	// Y Flat Planar, XZ not.
								| ((dZ == 0) && ((dX != 0) && (dY != 0)))	// Z Flat Planar, XY not.
								| ((dX != 0) &&  (dY != 0) && (dZ != 0) )	// XYZ Non Flat Planar.
								;

					// 3D encoded in 3D -> OK
					// 2D encoded in 3D -> OK = Equivalent 1 Planar + 2 Correlated
					// 1D encoded in 3D -> NO

					// Can we check the current tile for encoding ???
					if (accept && pixelsInTile != 0 && pixelInside) {
						bool isOutside;
						bool isIsFirst = true;

						int nX = dX ? ((1<<20)/dX) : 0;
						int nY = dY ? ((1<<20)/dY) : 0;
						int nZ = dZ ? ((1<<20)/dZ) : 0;

						float div = (float)(1<<20);

						for (int e=0;e<correlationPatternCount3D; e++) {
							correlationPattern3D[e].EvaluateStart3D();
						}

						FILE* captureFile = NULL;
						u8 captureCount;
						u8 captureR[128];
						u8 captureG[128];
						u8 captureB[128];

						if (isCaptureMode3D && (pixelsInTile>=64) && (!evaluateLUT)) { // For now limit to full 8x8 tile only.
							captureCount = 0;
							char buffer[2000];
							sprintf(buffer,"tile3D\\tileMap_%s_%i-%i.lut",originalName,x,y);
							captureFile  = fopen(buffer,"wb");
						}

						// Per Pixel test...
						for (int ty=0; ty < tileSizeY; ty++) {
							for (int tx=0; tx < tileSizeX; tx++) {

								int idx = tx + (ty*tileSizeX);

								// Ignore pixel already processed.
								if (maskTile[idx] == 255) { continue; }

								int rgb[3];
								input->GetPixel(x+tx,y+ty,rgb,isOutSide);
								// Convert6Bit(rgb);


								if (useYCoCg) {
									RGBtoYCoCgPos(rgb[0],rgb[1],rgb[2],rgb[0],rgb[1],rgb[2]);
								}

								// Origin.
								int r = rgb[0] - bb3.x0;
								int g = rgb[1] - bb3.y0;
								int b = rgb[2] - bb3.z0;

								// Rescale to 1.0 fixed.
								r *= nX;
								g *= nY;
								b *= nZ;

								float fr = r/div;
								float fg = g/div;
								float fb = b/div;

								int ir64 = ((int)(fr*63));
								int ig64 = ((int)(fg*63));
								int ib64 = ((int)(fb*63));

								kassert(ir64 >= 0 && ir64 < 64);
								kassert(ig64 >= 0 && ig64 < 64);
								kassert(ib64 >= 0 && ib64 < 64);

								static bool first = true;
								if (first) {
									first = false;
									memset(histo3D,0,sizeof(int)*64*64*64);
								}

								// printf("%i,%i,%i\n",ir64,ig64,ib64);
								histo3D[(int)ir64 + (((int)ig64)<<6) + (((int)ib64)<<12)]++;	

								if (captureFile) {
									captureR[captureCount] = ir64;
									captureG[captureCount] = ig64;
									captureB[captureCount] = ib64;
									captureCount++;
								}

	//							printf("%i,%i,%i,%i,%i,%i\n",ir64,ig64,ib64,rgb[0],rgb[1],rgb[2]);

								for (int e=0;e<correlationPatternCount3D; e++) {
									correlationPattern3D[e].EvaluatePoint3D(ir64,ig64,ib64);
								}
							}
						}

						if (captureFile) {
							fwrite(&captureCount,sizeof(u8),1,captureFile);
							fwrite(captureR,sizeof(u8),captureCount,captureFile);
							fwrite(captureG,sizeof(u8),captureCount,captureFile);
							fwrite(captureB,sizeof(u8),captureCount,captureFile);
							fclose(captureFile);
						}

						EvalCtx3D* evMin = &correlationPattern3D[0];
						float minScore = 999999999.0f;
						float fStdDev;
						bool found = false;
						int foundE = -1;
						int foundM48 = -1;
						int diffSum = 99999999999;
						int bestSize = 0;
						EncoderContext::Mode bitMode;

						int rgbOut6[128*3];
						int rgbOut5[128*3];
						int rgbOut4[128*3];
						int rgbOut3[128*3];
						int rgbOut2[128*3];

						//
						// Find best result for the current tile...
						//
						for (int e=0;e<correlationPatternCount3D; e++) {
							float score;
							EvalCtx3D& ev = correlationPattern3D[e];
							int mode48 = ev.GetEvaluation3D(score);
							bool accept = false;
							int diffSumL = 0;

							// Compute Channel values AND return valid mode if ERROR < 3 in ALL Component for each SINGLE pixels. (no average)
							EncoderContext::Mode m = computeValues3D(tileSizeX,tileSizeY,maskTile, mode48,input,x,y,bb3,ev,diffSumL,rgbOut6,rgbOut5,rgbOut4,rgbOut3/*,rgbOut2*/);
							int fileCost = (4+3) + 32;
							int* src = NULL;
							int bit = 0;
							switch (m) {
							case EncoderContext::ENCODE_4BIT: bit = 4; src = rgbOut4; fileCost += (pixelsInTile*4);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 4 bit.
								break;
							case EncoderContext::ENCODE_5BIT: bit = 5; src = rgbOut5; fileCost += (pixelsInTile*5);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 5 bit.
								break;
							case EncoderContext::ENCODE_6BIT: bit = 6; src = rgbOut6; fileCost += (pixelsInTile*6);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 6 bit.
								break;
							case EncoderContext::ENCODE_3BIT: bit = 3; src = rgbOut3; fileCost += (pixelsInTile*3);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 6 bit.
								break;
//							case EncoderContext::ENCODE_2BIT: bit = 2; src = rgbOut2; fileCost += (pixelsInTile*3);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 6 bit.
//								break;
							}

							// printf("Score Tile[%i] = %f\n",tileCnt,score);

							// If result is better than last one -> Get it.
							if ((m != EncoderContext::SKIP_TOO_LOSSY) && diffSumL <= diffSum) {
								bitMode  = m;
								minScore = score;
								evMin    = &ev;
								found    = true;
								accept   = true;
								foundE   = e;
								foundM48 = mode48;
								diffSum  = diffSumL;
								bestSize = fileCost;
								if (evaluateLUT) {
									// Or FAST Encoder...
									break; // Just quit on first valid one, not BEST ONE.
								}
								/*
								// Render With decoded data
								for (int ty=0; ty < tileSizeY; ty++) {
									for (int tx=0; tx < tileSizeX; tx++) {
										int idx = (tx+(ty*tileSizeX));
										if (maskTile[idx]) { src+=3; continue; }

										output->SetPixel(tx+x,ty+y,src[0],src[1],src[2]);
										src += 3;
									}
								}*/

							}
						}

						//
						// There was at least one best score possible => Encode in binary stream...
						//
						if (found) {
							matchTile++;

							printf("Tile3D[%i,%i] PixelCnt:%i [%i Bit,Mode:%i,Pattern:%i] (RGB:[%i,%i,%i]->[%i,%i,%i])\n", 
								x,y,pixelsInTile,   
								bitMode+3,foundM48,foundE, 
									bb3.x0,bb3.y0,bb3.z0,
									bb3.x1,bb3.y1,bb3.z1
							);

							// Color 1
							int comprCol = 0;

							bb3 = roundNBit(bb3,comprCol);


							corr3D_colorStream[streamColorCnt++] = bb3.x0 >> comprCol;
							corr3D_colorStream[streamColorCnt++] = bb3.y0 >> comprCol;
							corr3D_colorStream[streamColorCnt++] = bb3.z0 >> comprCol;
							// Color 2
							corr3D_colorStream[streamColorCnt++] = bb3.x1 >> comprCol;
							corr3D_colorStream[streamColorCnt++] = bb3.y1 >> comprCol;
							corr3D_colorStream[streamColorCnt++] = bb3.z1 >> comprCol;

							// Bitmap stream...
							if (tileSizeX == 8) {
								switch (tileSizeY) {
								case  4: corr3D_sizeT8_4Map [pos>>3] |= 1<<(pos&7); break;
								case  8: corr3D_sizeT8_8Map [pos>>3] |= 1<<(pos&7); break;
								case 16: corr3D_sizeT8_16Map[pos>>3] |= 1<<(pos&7); break;
								}
							} else {
								if (tileSizeX == 16) {
									switch (tileSizeY) {
									case 8: corr3D_sizeT16_8Map[pos>>3] |= 1<<(pos&7); break;
									}
								} else { // 4
									switch (tileSizeY) {
									case 4: corr3D_sizeT4_4Map[pos>>3] |= 1<<(pos&7); break;
									case 8: corr3D_sizeT4_8Map[pos>>3] |= 1<<(pos&7); break;
									}
								}
							}

							corr3D_tileStreamTileType[streamTypeCnt++] = foundM48 | (bitMode<<(6+8)) | (foundE<<6); // Bit [0..5] : 48 3D Pattern, Bit [6..13] : 256 Pattern, Bit [14..15] : 3/4/5/6 Bit Tile.
							
							if (pStats && (testedLUT == foundE)) {
								pStats->loc.pixelCount3DLUTTotal += pixelsInTile;
								pStats->loc.histogram3D[foundE]++;
								if (tileSizeX == 16 && tileSizeY == 8) {
										pStats->loc.pixelCount3D_Lut_16_8 += pixelsInTile;
								}
								if (tileSizeX == 8 && tileSizeY == 16) {
									pStats->loc.pixelCount3D_Lut_8_16 += pixelsInTile;
								}
								if (tileSizeX == 8 && tileSizeY == 8) {
									pStats->loc.pixelCount3D_Lut_8_8 += pixelsInTile;
								}
								if (tileSizeX == 4 && tileSizeY == 8) {
									pStats->loc.pixelCount3D_Lut_4_8 += pixelsInTile;
								}
								if (tileSizeX == 8 && tileSizeY == 4) {
									pStats->loc.pixelCount3D_Lut_8_4 += pixelsInTile;
								}
								if (tileSizeX == 4 && tileSizeY == 4) {
									pStats->loc.pixelCount3D_Lut_4_4 += pixelsInTile;
								}

								switch (bitMode) {
								case EncoderContext::ENCODE_3BIT:
									pStats->loc.pixelCount3D_3Bit += pixelsInTile;
									break;
								case EncoderContext::ENCODE_4BIT:
									pStats->loc.pixelCount3D_4Bit += pixelsInTile;
									break;
								case EncoderContext::ENCODE_5BIT:
									pStats->loc.pixelCount3D_5Bit += pixelsInTile;
									break;
								case EncoderContext::ENCODE_6BIT:
									pStats->loc.pixelCount3D_6Bit += pixelsInTile;
									break;
								default:
									break;
								}

								pStats->loc.tile3DCount++;
							}
#if 1
							u8* bitSrc = NULL;
							s16* tblR;
							s16* tblG;
							s16* tblB;
							int  maxIDX = 0;

							switch (bitMode) {
//							case EncoderContext::ENCODE_2BIT:
//								memcpy(&corr3D_stream2Bit[stream2BitCnt],evMin->value2Bit,pixelsInTile);
//								stream2BitCnt += pixelsInTile;
								break;
							case EncoderContext::ENCODE_3BIT:
								bitSrc = evMin->value3Bit;
								tblR   = &evMin->xFactor3Bit[0];
								tblG   = &evMin->yFactor3Bit[0];
								tblB   = &evMin->zFactor3Bit[0];
								maxIDX = 7;
								memcpy(&corr3D_stream3Bit[stream3BitCnt],evMin->value3Bit,pixelsInTile);
								stream3BitCnt += pixelsInTile;
								break;
							case EncoderContext::ENCODE_4BIT:
								bitSrc = evMin->value4Bit;
								tblR   = &evMin->xFactor4Bit[0];
								tblG   = &evMin->yFactor4Bit[0];
								tblB   = &evMin->zFactor4Bit[0];
								maxIDX = 15;
								memcpy(&corr3D_stream4Bit[stream4BitCnt],evMin->value4Bit,pixelsInTile);
								stream4BitCnt += pixelsInTile;
								break;
							case EncoderContext::ENCODE_5BIT:
								bitSrc = evMin->value5Bit;
								tblR   = &evMin->xFactor5Bit[0];
								tblG   = &evMin->yFactor5Bit[0];
								tblB   = &evMin->zFactor5Bit[0];
								maxIDX = 31;
								memcpy(&corr3D_stream5Bit[stream5BitCnt],evMin->value5Bit,pixelsInTile);
								stream5BitCnt += pixelsInTile;
								break;
							case EncoderContext::ENCODE_6BIT:
								bitSrc = evMin->value6Bit;
								tblR   = &evMin->xFactor6Bit[0];
								tblG   = &evMin->yFactor6Bit[0];
								tblB   = &evMin->zFactor6Bit[0];
								maxIDX = 63;
								memcpy(&corr3D_stream6Bit[stream6BitCnt],evMin->value6Bit,pixelsInTile);
								stream6BitCnt += pixelsInTile;
								break;
							default:
								tblR = NULL;
								tblG = NULL;
								tblB = NULL;
							}

							// Render compressed block.
							int lSizeX = (tileSizeX > 8) ? 8 : tileSizeX;
							int countX = (tileSizeX > 8) ? 2 : 1;
							int vx = 0;
							int pixelId = 0;

							if (!evaluateLUT) {
								int dt[3];

								dt[0] = bb3.x1 - bb3.x0;
								dt[1] = bb3.y1 - bb3.y0;
								dt[2] = bb3.z1 - bb3.z0;

								//
								// 
								//
								int lutXT[64];
								int lutYT[64];
								int lutZT[64];
								for (int ssize = 0; ssize <= maxIDX; ssize++) {
									lutXT[ssize] = tblR[ssize];
									lutYT[ssize] = tblG[ssize];
									lutZT[ssize] = tblB[ssize];
								}

								int* lutX = &lutXT[0];
								int* lutY = &lutYT[0];
								int* lutZ = &lutZT[0];

								swap3DTable(foundM48>>3,&lutX,&lutY,&lutZ);

								// Convert same format as Decoder to debug.
								int tbl[64*3];
								for (int ssize = 0; ssize <= maxIDX; ssize++) {
									int rl   = (foundM48 & 1) ? (FACTOR - lutX[ssize]) : lutX[ssize];
									int gl   = (foundM48 & 2) ? (FACTOR - lutY[ssize]) : lutY[ssize];
									int bl   = (foundM48 & 4) ? (FACTOR - lutZ[ssize]) : lutZ[ssize];

									tbl[ssize*3    ] = rl;
									tbl[(ssize*3)+1] = gl;
									tbl[(ssize*3)+2] = bl;
									// printf("[%i] => %i,%i,%i\n",ssize,rl,gl,bl);
								}

								u8* src = bitSrc;

//								printf("Tile:%i [%i,%i,%i]->[%i,%i,%i]\n",corr3D_tileStreamTileType[streamTypeCnt-1],(int)bb3.x0,(int)bb3.y0,(int)bb3.z0,(int)bb3.x1,(int)bb3.y1,(int)bb3.z1);

								for (; vx < countX; vx++) {
									for (int ty=0; ty < tileSizeY; ty++) {
										for (int tx=0; tx < lSizeX; tx++) {
											int idx = (tx + (vx*8) +(ty*tileSizeX));
											if (maskTile[idx] == 255) { continue; }

											u8 idxTbl = (*src++) * 3;

											/*
											u8 idxR   = (foundM48 & 1) ? (maxIDX - idxTbl) : idxTbl;
											u8 idxG   = (foundM48 & 2) ? (maxIDX - idxTbl) : idxTbl;
											u8 idxB   = (foundM48 & 4) ? (maxIDX - idxTbl) : idxTbl;

											u8 r = bb3.x0 + ((dt[0]*tblR[idxR]) >> 7);
											u8 g = bb3.y0 + ((dt[1]*tblG[idxG]) >> 7);
											u8 b = bb3.z0 + ((dt[2]*tblB[idxB]) >> 7);
											*/

											int r = bb3.x0 + ((dt[0]*tbl[idxTbl  ]) / FACTOR);
											int g = bb3.y0 + ((dt[1]*tbl[idxTbl+1]) / FACTOR);
											int b = bb3.z0 + ((dt[2]*tbl[idxTbl+2]) / FACTOR);

											if (useYCoCg) {
												YCoCgPostoRGB(r,g,b,r,g,b);
											}

//											printf("%i,%i(%i) -> [%i] -> [%i,%i,%i]\n",x,y,pixelId,idxTbl,tbl[idxTbl  ],tbl[idxTbl+1],tbl[idxTbl+2]);
											pixelId++;

											output->SetPixel(tx+x+(vx*8),ty+y,r,g,b);
										}
									}
								}
							} else {
								if (testedLUT == foundE) {
									for (; vx < countX; vx++) {
										for (int ty=0; ty < tileSizeY; ty++) {
											for (int tx=0; tx < lSizeX; tx++) {
												int idx = (tx + (vx*8) +(ty*tileSizeX));
												if (maskTile[idx] == 255) { continue; }

												evaluateMap->SetPixel(tx+x+(vx*8),ty+y,255);
											}
										}
									}
								}
							}

//							printf("end\n");
#endif

							//
							// Remove Tile from usable tiles...
							//
							if (!evaluateLUT) {
								for (int ty=0; ty < tileSizeY; ty++) {
									for (int tx=0; tx < tileSizeX; tx++) {
										mapSmoothTile->SetPixel(x + tx, y + ty, 255,255,255);
									}
								}
							}
						} else {
							// printf("Skip tile %i\n",tileCnt);
						}


					} // End 'valid tile' for verification ?
					pos++;
				} // Tile X end
				posY += stepY;
			} // Tile Y end
			posXS += stepXS;
		} // Swizzle block X end
		posYS += stepYS;
	} // Swizzle block Y end
	printf(" MATCHED TILE : %i\n",matchTile);
}

void EncoderContext::Correlation2DSearch(PlaneMode planeMode, Image* input,Image* output, int tileShiftX, int tileShiftY) {
	BoundingBox bb2;
	bool isOutSide;
	int matchTile = 0;

	if ((tileShiftX > 4) || (tileShiftY > 4)) {
		assert(false); // Can not support yet.
	}

	if ((tileShiftX == 4) && (tileShiftY > 3)) {
		assert(false); // Can not support yet.
	}

	if ((tileShiftY == 4) && (tileShiftX > 3)) {
		assert(false); // Can not support yet.
	}

	int imgW = input->GetWidth();
	int imgH = input->GetHeight();

	// 16x16 : 16 bit table (64x64 pixel) -> Lookup the table by block of 4x4 tile. -> Allow 16/4 skip
	// 16x 8 : 32 bit table (64x64 pixel) -> Lookup the table by block of 4x8 tile. -> Allow 
	//  8x16 : 32 bit table (64x64 pixel) -> Lookup the table by block of 8x4 tile.
	//  8x 8 : 64 bit table (64x64 pixel) -> Lookup the table by block of 8x8 tile.
	//  8x 4 : 64 bit table (64x32 pixel) -> Lookup the table by block of 8x8 tile.
	//  4x 8 : 64 bit table (32x64 pixel) -> Lookup the table by block of 8x8 tile.
	//  4x 4 : 64 bit table (32x32 pixel) -> Lookup the table by block of 8x8 tile.

	u32 swizzleParseY;
	u32 swizzleParseX;
	u32 bitCount;

	HeaderGradientTile::getSwizzleSize(tileShiftX,tileShiftY,swizzleParseX,swizzleParseY, bitCount);

	int tileSizeX = 1<<tileShiftX;
	int tileSizeY = 1<<tileShiftY;

	printf("----- Tile Size %i,%i\n", tileSizeX, tileSizeY);

	int bitCountMap = HeaderGradientTile::getBitmapSwizzleSize(tileShiftX,tileShiftY,imgW,imgH);

	int xBBTileCount = ((imgW+(swizzleParseX-1)) / swizzleParseX);
	int posYS; 
	int posXS;
	int stepYS = bitCount * xBBTileCount;
	int stepXS = bitCount;						// 1 block.
	int stepY  = swizzleParseX / tileSizeX;		// Inside block vertically.
	//  stepX  = 1

	// Linear 2D order for big tile defined by swizzling.
	posYS = 0;
	for (int swizzley=0; swizzley < imgH; swizzley += swizzleParseY) {
		posXS = posYS;
		for (int swizzlex=0; swizzlex < imgW; swizzlex += swizzleParseX) {
			int posY = posXS;
			// Target size tile inside quad tile
			for (int y=swizzley; y < swizzley+swizzleParseY; y+= tileSizeY) {
				// Because of big tile parsing, we may lookup outside of image with sub-tiles. (Y-Axis)
				if (y >= imgH) {
					break; 
				}

				int pos = posY;
				for (int x=swizzlex; x < swizzlex+swizzleParseX; x+= tileSizeX) {
					// Because of big tile parsing, we may lookup outside of image with sub-tiles. (X-Axis)
					if (x >= imgW) {
						break; 
					}

					u8 maskTile[128];
					int pixelsInTile;
					// For all tiles.
					memset(maskTile,0,128);
					bb2 = buildBBox2D(planeMode,input,mapSmoothTile,x,y,tileSizeX,tileSizeY,pixelsInTile,maskTile);

					// Export normalized values...
					int dX = bb2.w - bb2.x;
					int dY = bb2.h - bb2.y;

					bool accept = ((dX != 0) && ((dY != 0)))	// X Flat Planar, Y not.
								;
					/*
					bool accept = ((dX == 0) && ((dY != 0)))	// X Flat Planar, Y not.
								| ((dY == 0) && ((dX != 0)))	// Y Flat Planar, X not.
								;
					*/
					// 3D encoded in 3D -> OK
					// 2D encoded in 3D -> OK = Equivalent 1 Planar + 2 Correlated
					// 1D encoded in 3D -> NO

					// Can we check the current tile for encoding ???
					if (accept && pixelsInTile != 0) {
						bool isOutside;
						bool isIsFirst = true;

						int nX = dX ? ((1<<20)/dX) : 0;
						int nY = dY ? ((1<<20)/dY) : 0;

						float div = (float)(1<<20);

						for (int e=0;e<correlationPatternCount2D; e++) {
							correlationPattern2D[e].EvaluateStart2D();
						}

						FILE* captureFile = NULL;
						u8 captureCount;
						u8 captureA[128];
						u8 captureB[128];

						u8 shape[64*64*3];

						char buffer[2000];
						static int tileCountID = 0;

						if (isCaptureMode2D && (pixelsInTile >= 32)) { // For now limit to full 8x8 tile only.
							captureCount = 0;
							sprintf(buffer,"tile2D\\tileMap%i.lut",tileCountID);
							captureFile  = fopen(buffer,"wb");
							memset(shape,0,3*64*64);
						}

						// Per Pixel test...
						for (int ty=0; ty < tileSizeY; ty++) {
							for (int tx=0; tx < tileSizeX; tx++) {

								int idx = tx + (ty*tileSizeX);

								// Ignore pixel already processed.
								if (maskTile[idx]) { continue; }

								int rgb[3];
								input->GetPixel(x+tx,y+ty,rgb,isOutSide);

								rgb[0] >>= 0;
								rgb[1] >>= 0;
								rgb[2] >>= 0;

								// Origin.
								int va;
								int vb;
								switch (planeMode) {
								case Mode_RG:
									va = rgb[0] - bb2.x;
									vb = rgb[1] - bb2.y;
									break;
								case Mode_GB:
									va = rgb[1] - bb2.x;
									vb = rgb[2] - bb2.y;
									break;
								case Mode_RB:
									va = rgb[0] - bb2.x;
									vb = rgb[2] - bb2.y;
									break;
								}

								// Rescale to 1.0 fixed.
								va *= nX;
								vb *= nY;

								float fa = va/div;
								float fb = vb/div;

								int ia64 = ((int)(fa*63));
								int ib64 = ((int)(fb*63));

								kassert(ia64 >= 0 && ia64 < 64);
								kassert(ib64 >= 0 && ib64 < 64);

								if (captureFile) {
									captureA[captureCount] = ia64;
									captureB[captureCount] = ib64;
									captureCount++;
								}

	//							printf("%i,%i,%i,%i,%i,%i\n",ir64,ig64,ib64,rgb[0],rgb[1],rgb[2]);

								for (int e=0;e<correlationPatternCount2D; e++) {
									correlationPattern2D[e].EvaluatePoint2D(ia64,ib64);
								}
							}
						}


						if (captureFile) {
							fwrite(&captureCount,sizeof(u8),1,captureFile);
							fwrite(captureA,sizeof(u8),captureCount,captureFile);
							fwrite(captureB,sizeof(u8),captureCount,captureFile);
							fclose(captureFile);
	
							char buffer[2000];
							sprintf(buffer,"tile2DImg\\tileMap%i.lut.png",tileCountID);

							for (int n=0; n < captureCount; n++) {
								int idx = (captureA[n] + (captureB[n]*64))*3;
								shape[idx  ] = 255;
								shape[idx+1] = 255;
								shape[idx+2] = 255;
							}

							stbi_write_png(buffer,64,64,3,shape,64*3);
							tileCountID++;
						}

						EvalCtx2D* evMin = &correlationPattern2D[0];
						float minScore = 999999999.0f;
						float fStdDev;
						bool found = false;
						int foundE = -1;
						int foundM8 = -1;
						int diffSum = 99999999999;
						int bestSize = 0;
						EncoderContext::Mode bitMode;

						int rgbOut6[128*3];
						int rgbOut5[128*3];
						int rgbOut4[128*3];
						int rgbOut3[128*3];
						int rgbOut2[128*3];

						//
						// Find best result for the current tile...
						//
						for (int e=0;e<correlationPatternCount2D; e++) {
							float score;
							float stdDev;
							EvalCtx2D& ev = correlationPattern2D[e];
							int mode8 = ev.GetEvaluation2D(score,stdDev);
							bool accept = false;
							int diffSumL = 0;

							// Compute Channel values AND return valid mode if ERROR < 3 in ALL Component for each SINGLE pixels. (no average)
							EncoderContext::Mode m = computeValues2D(planeMode,tileSizeX,tileSizeY,maskTile, mode8,input,x,y,bb2,ev,diffSumL,rgbOut6,rgbOut5,rgbOut4,rgbOut3/*,rgbOut2*/);
							int fileCost = (4+3) + 32;
							int* src = NULL;
							int bit = 0;
							switch (m) {
							case EncoderContext::ENCODE_4BIT: bit = 4; src = rgbOut4; fileCost += (pixelsInTile*4);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 4 bit.
								break;
							case EncoderContext::ENCODE_5BIT: bit = 5; src = rgbOut5; fileCost += (pixelsInTile*5);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 5 bit.
								break;
							case EncoderContext::ENCODE_6BIT: bit = 6; src = rgbOut6; fileCost += (pixelsInTile*6);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 6 bit.
								break;
							case EncoderContext::ENCODE_3BIT: bit = 3; src = rgbOut3; fileCost += (pixelsInTile*3);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 6 bit.
								break;
//							case EncoderContext::ENCODE_2BIT: bit = 2; src = rgbOut2; fileCost += (pixelsInTile*3);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 6 bit.
//								break;
							}

							// printf("Score Tile[%i] = %f\n",tileCnt,score);

							// If result is better than last one -> Get it.
							if ((m != EncoderContext::SKIP_TOO_LOSSY) && diffSumL <= diffSum) {
								bitMode  = m;
								minScore = score;
								evMin    = &ev;
								found    = true;
								accept   = true;
								foundE   = e;
								foundM8  = mode8;
								diffSum  = diffSumL;
								bestSize = fileCost;

								/*
								// Render With decoded data
								for (int ty=0; ty < tileSizeY; ty++) {
									for (int tx=0; tx < tileSizeX; tx++) {
										int idx = (tx+(ty*tileSizeX));
										if (maskTile[idx]) { src+=3; continue; }

										output->SetPixel(tx+x,ty+y,src[0],src[1],src[2]);
										src += 3;
									}
								}*/

							}
						}

						//
						// There was at least one best score possible => Encode in binary stream...
						//
						if (found) {
							matchTile++;
							// Color 1
							corr3D_colorStream[streamColorCnt++] = bb2.x;
							corr3D_colorStream[streamColorCnt++] = bb2.y;
							// Color 2
							corr3D_colorStream[streamColorCnt++] = bb2.w;
							corr3D_colorStream[streamColorCnt++] = bb2.h;

							// Bitmap stream...
							if (tileSizeX == 8) {
								switch (tileSizeY) {
								case  4: corr3D_sizeT8_4Map [pos>>3] |= 1<<(pos&7); break;
								case  8: corr3D_sizeT8_8Map [pos>>3] |= 1<<(pos&7); break;
								case 16: corr3D_sizeT8_16Map[pos>>3] |= 1<<(pos&7); break;
								}
							} else {
								if (tileSizeX == 16) {
									switch (tileSizeY) {
									case 8: corr3D_sizeT16_8Map[pos>>3] |= 1<<(pos&7); break;
									}
								} else { // 4
									switch (tileSizeY) {
									case 4: corr3D_sizeT4_4Map[pos>>3] |= 1<<(pos&7); break;
									case 8: corr3D_sizeT4_8Map[pos>>3] |= 1<<(pos&7); break;
									}
								}
							}

							corr3D_tileStreamTileType[streamTypeCnt++] = foundM8 | (bitMode<<(6+8)) | (foundE<<3); // Bit [0..2] : 8x 2D Pattern, Bit [3..13] : 2048 Patterns, Bit [14..15] : 3/4/5/6 Bit Tile.
							
							if (pStats && (testedLUT == foundE)) {
								pStats->loc.pixelCount2DLUTTotal += pixelsInTile;
								pStats->loc.histogram2D[foundE]++;
								if (tileSizeX == 8 && tileSizeY == 8) {
									pStats->loc.pixelCount2D_Lut_8_8 += pixelsInTile;
								}
								if (tileSizeX == 4 && tileSizeY == 4) {
									pStats->loc.pixelCount2D_Lut_4_4 += pixelsInTile;
								}

								switch (bitMode) {
								case EncoderContext::ENCODE_3BIT:
									pStats->loc.pixelCount2D_3Bit += pixelsInTile;
									break;
								case EncoderContext::ENCODE_4BIT:
									pStats->loc.pixelCount2D_4Bit += pixelsInTile;
									break;
								case EncoderContext::ENCODE_5BIT:
									pStats->loc.pixelCount2D_5Bit += pixelsInTile;
									break;
								case EncoderContext::ENCODE_6BIT:
									pStats->loc.pixelCount2D_6Bit += pixelsInTile;
									break;
								default:
									break;
								}

								pStats->loc.tile3DCount++;
							}
#if 1
//							printf("Tile:%i [%i,%i]->[%i,%i]\n",corr3D_tileStreamTileType[streamTypeCnt-1],(int)bb2.x,(int)bb2.y,(int)bb2.w,(int)bb2.h);
#endif

#if 1
							u8* bitSrc = NULL;
							s16* tblA;
							s16* tblB;
							int  maxIDX = 0;

							switch (bitMode) {
//							case EncoderContext::ENCODE_2BIT:
//								memcpy(&corr3D_stream2Bit[stream2BitCnt],evMin->value2Bit,pixelsInTile);
//								stream2BitCnt += pixelsInTile;
								break;
							case EncoderContext::ENCODE_3BIT:
								bitSrc = evMin->value3Bit;
								tblA   = &evMin->xFactor3Bit[0];
								tblB   = &evMin->yFactor3Bit[0];
								maxIDX = 7;
								memcpy(&corr3D_stream3Bit[stream3BitCnt],evMin->value3Bit,pixelsInTile);
								stream3BitCnt += pixelsInTile;
								break;
							case EncoderContext::ENCODE_4BIT:
								bitSrc = evMin->value4Bit;
								tblA   = &evMin->xFactor4Bit[0];
								tblB   = &evMin->yFactor4Bit[0];
								maxIDX = 15;
								memcpy(&corr3D_stream4Bit[stream4BitCnt],evMin->value4Bit,pixelsInTile);
								stream4BitCnt += pixelsInTile;
								break;
							case EncoderContext::ENCODE_5BIT:
								bitSrc = evMin->value5Bit;
								tblA   = &evMin->xFactor5Bit[0];
								tblB   = &evMin->yFactor5Bit[0];
								maxIDX = 31;
								memcpy(&corr3D_stream5Bit[stream5BitCnt],evMin->value5Bit,pixelsInTile);
								stream5BitCnt += pixelsInTile;
								break;
							case EncoderContext::ENCODE_6BIT:
								bitSrc = evMin->value6Bit;
								tblA   = &evMin->xFactor6Bit[0];
								tblB   = &evMin->yFactor6Bit[0];
								maxIDX = 63;
								memcpy(&corr3D_stream6Bit[stream6BitCnt],evMin->value6Bit,pixelsInTile);
								stream6BitCnt += pixelsInTile;
								break;
							default:
								tblA = NULL;
								tblB = NULL;
							}

							// Render compressed block.
							{
								int dt[2];
								dt[0] = bb2.w - bb2.x;
								dt[1] = bb2.h - bb2.y;

								//
								// 
								//
								int lutXT[64];
								int lutYT[64];
								for (int ssize = 0; ssize <= maxIDX; ssize++) {
									lutXT[ssize] = tblA[ssize];
									lutYT[ssize] = tblB[ssize];
								}

								int* lutX = &lutXT[0];
								int* lutY = &lutYT[0];

								// Convert same format as Decoder to debug.
								int tbl[64*2];
								for (int ssize = 0; ssize <= maxIDX; ssize++) {
									int al   = (foundM8 & 1) ? (FACTOR - lutX[ssize]) : lutX[ssize];
									int bl   = (foundM8 & 2) ? (FACTOR - lutY[ssize]) : lutY[ssize];
									if (foundM8 & 4) {
										int tmp = al;
										al = bl;
										bl = tmp;
									}

									tbl[ssize*2    ] = al;
									tbl[(ssize*2)+1] = bl;
									// printf("[%i] => %i,%i,%i\n",ssize,rl,gl,bl);
								}

								if (!evaluateLUT2D) {
									u8* src = bitSrc;

									int lSizeX = (tileSizeX > 8) ? 8 : tileSizeX;
									int countX = (tileSizeX > 8) ? 2 : 1;
									int vx = 0;
									int pixelId = 0;
									for (; vx < countX; vx++) {
										for (int ty=0; ty < tileSizeY; ty++) {
											for (int tx=0; tx < lSizeX; tx++) {
												int idx = (tx + (vx*8) +(ty*tileSizeX));
												if (maskTile[idx]) { continue; }

												u8 idxTbl = (*src++) * 2;

												/*
												u8 idxR   = (foundM48 & 1) ? (maxIDX - idxTbl) : idxTbl;
												u8 idxG   = (foundM48 & 2) ? (maxIDX - idxTbl) : idxTbl;
												u8 idxB   = (foundM48 & 4) ? (maxIDX - idxTbl) : idxTbl;

												u8 r = bb3.x0 + ((dt[0]*tblR[idxR]) >> 7);
												u8 g = bb3.y0 + ((dt[1]*tblG[idxG]) >> 7);
												u8 b = bb3.z0 + ((dt[2]*tblB[idxB]) >> 7);
												*/

												int a = bb2.x + ((dt[0]*tbl[idxTbl  ]) / FACTOR);
												int b = bb2.y + ((dt[1]*tbl[idxTbl+1]) / FACTOR);

//												printf("%i,%i(%i) -> [%i] -> [%i,%i]\n",x,y,pixelId,idxTbl,tbl[idxTbl  ],tbl[idxTbl+1]);
												pixelId++;

												int targetX = tx+x+(vx*8);
												int targetY = ty+y;

												switch (planeMode) {
												case Mode_RG:
													output->GetPlane(0)->SetPixel(targetX,targetY,a);
													output->GetPlane(1)->SetPixel(targetX,targetY,b);
													break;
												case Mode_GB:
													output->GetPlane(1)->SetPixel(targetX,targetY,a);
													output->GetPlane(2)->SetPixel(targetX,targetY,b);
													break;
												case Mode_RB:
													output->GetPlane(0)->SetPixel(targetX,targetY,a);
													output->GetPlane(2)->SetPixel(targetX,targetY,b);
													break;
												}
											}
										}
									}
								}
							}

//							printf("end\n");
#endif

							//
							// Remove Tile from usable tiles...
							//
							if (!evaluateLUT2D) {
								for (int ty=0; ty < tileSizeY; ty++) {
									for (int tx=0; tx < tileSizeX; tx++) {
										int targetX = x + tx;
										int targetY = y + ty;
									
										switch (planeMode) {
										case Mode_RG:
											mapSmoothTile->GetPlane(0)->SetPixel(targetX,targetY,255);
											mapSmoothTile->GetPlane(1)->SetPixel(targetX,targetY,255);
											break;
										case Mode_GB:
											mapSmoothTile->GetPlane(1)->SetPixel(targetX,targetY,255);
											mapSmoothTile->GetPlane(2)->SetPixel(targetX,targetY,255);
											break;
										case Mode_RB:
											mapSmoothTile->GetPlane(0)->SetPixel(targetX,targetY,255);
											mapSmoothTile->GetPlane(2)->SetPixel(targetX,targetY,255);
											break;
										}
									}
								}
							}
						} else {
							// printf("Skip tile %i\n",tileCnt);
						}


					} // End 'valid tile' for verification ?
					
					kassert(pos < bitCountMap);
					pos++;
				} // Tile X end
				posY += stepY;
			} // Tile Y end
			posXS += stepXS;
		} // Swizzle block X end
		posYS += stepYS;
	} // Swizzle block Y end
	printf("\n-----------------\nMATCHED TILE : %i\n-------------\n\n",matchTile);
}

int BitmapSwizzleMapSize(int TshiftX, int TshiftY, int imgW, int imgH) {
	u32 swizzleParseX,swizzleParseY,bitCount;
	HeaderGradientTile::getSwizzleSize(TshiftX,TshiftY,swizzleParseX,swizzleParseY, bitCount);
	return ((imgW+swizzleParseX-1) / swizzleParseX) * ((imgH+swizzleParseY-1) / swizzleParseY) * bitCount;
}

void EncoderContext::StartCorrelationSearch(bool is3D) {
	int wi   = original->GetWidth();
	int hi	= original->GetHeight();

	int size = original->GetWidth() * original->GetHeight();

//	stream2BitCnt = 0;
	stream3BitCnt = 0;
	stream4BitCnt = 0;
	stream5BitCnt = 0;
	stream6BitCnt = 0;
	streamColorCnt = 0;
	streamTypeCnt  = 0;

	int sizeT16_8Map	= BitmapSwizzleMapSize(4,3, wi,hi);
	int sizeT8_16Map	= BitmapSwizzleMapSize(3,4, wi,hi);
	int sizeT8_8Map		= BitmapSwizzleMapSize(3,3, wi,hi);
	int sizeT4_8Map		= BitmapSwizzleMapSize(2,3, wi,hi);
	int sizeT8_4Map		= BitmapSwizzleMapSize(3,2, wi,hi);
	int sizeT4_4Map		= BitmapSwizzleMapSize(2,2, wi,hi);

	int maxTileCount	= (wi/4) * (hi*4); // ALL 4x4 (smallest, full image)

	corr3D_stream6Bit = new u8[size];
	corr3D_stream5Bit = new u8[size];
	corr3D_stream4Bit = new u8[size];
	corr3D_stream3Bit = new u8[size];
//	corr3D_stream2Bit = new u8[size];

	corr3D_sizeT16_8Map  = new u8[sizeT16_8Map];
	corr3D_sizeT8_16Map  = new u8[sizeT8_16Map];
	corr3D_sizeT8_8Map   = new u8[sizeT8_8Map];
	corr3D_sizeT8_4Map   = new u8[sizeT8_4Map];
	corr3D_sizeT4_8Map   = new u8[sizeT4_8Map];
	corr3D_sizeT4_4Map   = new u8[sizeT4_4Map];

	memset(corr3D_sizeT16_8Map, 0, sizeT16_8Map);
	memset(corr3D_sizeT8_16Map, 0, sizeT8_16Map);
	memset(corr3D_sizeT8_8Map , 0, sizeT8_8Map);
	memset(corr3D_sizeT8_4Map , 0, sizeT8_4Map);
	memset(corr3D_sizeT4_8Map , 0, sizeT4_8Map);
	memset(corr3D_sizeT4_4Map , 0, sizeT4_4Map);

	// Unique for everybody... just one pass after another...
	corr3D_tileStreamTileType = new u16[maxTileCount];	// 8 BIT TYPE, 6 BIT PATTERN POSITION.
	corr3D_colorStream        = new u8[maxTileCount*2*3];		// RGB pair per tile.
}

#include "../external/zstd/zdict.h"

void EncoderContext::EndCorrelationSearch(bool is3D, u8 component) {
	int wi   = original->GetWidth();
	int hi	= original->GetHeight();

	int sizeT16_8Map	= BitmapSwizzleMapSize(4,3, wi,hi);
	int sizeT8_16Map	= BitmapSwizzleMapSize(3,4, wi,hi);
	int sizeT8_8Map		= BitmapSwizzleMapSize(3,3, wi,hi);
	int sizeT4_8Map		= BitmapSwizzleMapSize(2,3, wi,hi);
	int sizeT8_4Map		= BitmapSwizzleMapSize(3,2, wi,hi);
	int sizeT4_4Map		= BitmapSwizzleMapSize(2,2, wi,hi);

	HeaderTile3D header3D;

	size_t result;

	u8* ZStdT16_8Map	= is3D ? new u8[sizeT16_8Map * 2] : NULL;
	u8* ZStdT8_16Map	= is3D ? new u8[sizeT8_16Map * 2] : NULL;
	u8* ZStdT8_8Map		= new u8[sizeT8_8Map * 2];
	u8* ZStdT8_4Map		= is3D ? new u8[sizeT8_4Map * 2] : NULL;
	u8* ZStdT4_8Map		= is3D ? new u8[sizeT4_8Map * 2] : NULL;
	u8* ZStdT4_4Map		= new u8[sizeT4_4Map * 2];

	u8* ZStdTileStream	= new u8[streamTypeCnt*sizeof(u16) * 2 +100];
	u8* ZStdColorStream	= new u8[streamColorCnt * 2 + 100];

	// --------------------------------------------------------------------------------
	//   Bit Tile Maps.
	// --------------------------------------------------------------------------------

	header3D.component = component;

	if (is3D) {
		result = ZSTD_compress(ZStdT16_8Map, sizeT16_8Map * 2, corr3D_sizeT16_8Map,sizeT16_8Map, 18);
		printf("T16_8 Map : %i\n",(int)result);
		header3D.sizeT16_8MapCmp = result;
		header3D.sizeT16_8Map   = sizeT16_8Map;
	} else {
		header3D.sizeT16_8MapCmp = 0;
		header3D.sizeT16_8Map    = 0;
	}

	if (is3D) {
		result = ZSTD_compress(ZStdT8_16Map, sizeT8_16Map * 2, corr3D_sizeT8_16Map,sizeT8_16Map, 18);
		printf("T8_16 Map : %i\n",(int)result);
		header3D.sizeT8_16Map    = sizeT8_16Map;
		header3D.sizeT8_16MapCmp = result;
	} else {
		header3D.sizeT8_16Map    = 0;
		header3D.sizeT8_16MapCmp = 0;
	}

	result = ZSTD_compress(ZStdT8_8Map, sizeT8_8Map * 2, corr3D_sizeT8_8Map,sizeT8_8Map, 18);
	printf("T8_8 Map : %i\n",(int)result);
	header3D.sizeT8_8Map    = sizeT8_8Map;
	header3D.sizeT8_8MapCmp = result;

	if (is3D) {
		result = ZSTD_compress(ZStdT8_4Map, sizeT8_4Map * 2, corr3D_sizeT8_4Map,sizeT8_4Map, 18);
		printf("T8_4 Map : %i\n",(int)result);
		header3D.sizeT8_4Map    = sizeT8_4Map;
		header3D.sizeT8_4MapCmp = result;
	} else {
		header3D.sizeT8_4Map    = 0;
		header3D.sizeT8_4MapCmp = 0;
	}

	if (is3D) {
		result = ZSTD_compress(ZStdT4_8Map, sizeT4_8Map * 2, corr3D_sizeT4_8Map,sizeT4_8Map, 18);
		printf("T4_8 Map : %i\n",(int)result);
		header3D.sizeT4_8Map    = sizeT4_8Map;
		header3D.sizeT4_8MapCmp = result;
	} else {
		header3D.sizeT4_8Map    = 0;
		header3D.sizeT4_8MapCmp = 0;
	}

	result = ZSTD_compress(ZStdT4_4Map, sizeT4_4Map * 2, corr3D_sizeT4_4Map,sizeT4_4Map, 18);
	printf("T4_4 Map : %i\n",(int)result);
	header3D.sizeT4_4Map    = sizeT4_4Map;
	header3D.sizeT4_4MapCmp = result;

	// Stream : Tile Type first...
	/*
		for (int t=0; t < 20; t++) { printf("%i-",corr3D_tileStreamTileType[t]); }
		printf("\t\t");
	*/
	if (streamTypeCnt > 0) {
		result = ZSTD_compress(ZStdTileStream, streamTypeCnt*sizeof(u16)*2 +100, corr3D_tileStreamTileType ,streamTypeCnt*sizeof(u16), 18);
		header3D.comprTypeSize  = result;
	} else {
		header3D.comprTypeSize  = 0;
	}
	header3D.streamTypeCnt  = streamTypeCnt;	// Nb Tile
	printf("Stream Tile : %i\n",(int)result);

	if (streamColorCnt > 0) {

#if 0
		// DUMP COLOR STREAM FOR RESEARCH LOSSLESS COMPRESSION PALETTE.
		u8* minStream = new u8[streamColorCnt];
		u8* maxStream = &minStream[streamColorCnt>>1];
		
		for (int n=0; n < streamColorCnt; n += 6) {
			int idx = n>>1;
			minStream[idx+0] = corr3D_colorStream[n+0];
			minStream[idx+1] = corr3D_colorStream[n+1];
			minStream[idx+2] = corr3D_colorStream[n+2];

			maxStream[idx+0] = corr3D_colorStream[n+3];
			maxStream[idx+1] = corr3D_colorStream[n+4];
			maxStream[idx+2] = corr3D_colorStream[n+5];
		}

		stbi_write_png("palette3DMin.png",streamColorCnt/6,1,3,minStream,streamColorCnt/2);
		stbi_write_png("palette3DMax.png",streamColorCnt/6,1,3,maxStream,streamColorCnt/2);

		delete [] minStream;
		delete [] maxStream;
#endif

		/*
		s16* diffStream = new s16[streamColorCnt*2];
		s16* wStream    = diffStream;
		for (int n=3; n < streamColorCnt; n += 3) {
			int dR = minStream[n]-minStream[n-3];
			int dG = minStream[n+1]-minStream[n-2];
			int dB = minStream[n+2]-minStream[n-1];
			*wStream++ = dR;
			*wStream++ = dG;
			*wStream++ = dB;
			// printf("%i,%i,%i\n",dR,dG,dB);
		}
		result = ZSTD_compress(ZStdColorStream, streamColorCnt * 2, diffStream ,streamColorCnt*2, 18);
		*/

		// Color Range compression (posterization)
		for (int n=0; n < streamColorCnt; n++) {
			corr3D_colorStream[n] = CompressF(corr3D_colorStream[n],colorCompressionLUT3D);
		}
		
		result = ZSTD_compress(ZStdColorStream, streamColorCnt * 2 + 100, corr3D_colorStream ,streamColorCnt, 18);
		header3D.comprColorSize = result;
		header3D.compressionRateColor= colorCompressionLUT3D;

	} else {
		header3D.comprColorSize = 0;
	}
	header3D.streamColorCnt = streamColorCnt;	// Nb Tile x 6
	printf("Stream Color : %i\n",(int)result);

	// --------------------------------------------------------------------------------
	//   Pixel Index Stream...
	// --------------------------------------------------------------------------------

	// TRICK : Multiply ALL the index by 3 to avoid computation at runtime
	//         Index directly point to interleaved entry in the decoder. ! NOICE !
	// We loose less than a few byte in compression, but it is worth the runtime savings.
	//

	// for (int n=0; n<stream2BitCnt; n++) { corr3D_stream2Bit[n] *= 3; }
	int multiplier = is3D ? 3 : 2;

	for (int n=0; n<stream3BitCnt; n++) { corr3D_stream3Bit[n] *= multiplier; }
	for (int n=0; n<stream4BitCnt; n++) { corr3D_stream4Bit[n] *= multiplier; }
	for (int n=0; n<stream5BitCnt; n++) { corr3D_stream5Bit[n] *= multiplier; }
	for (int n=0; n<stream6BitCnt; n++) { corr3D_stream6Bit[n] *= multiplier; }

	/*
	// Stream : 2 Bit
	result = ZSTD_compress(pZStdStream, sizeZStd, corr3D_stream2Bit,stream2BitCnt, 18);
	fileOutSize += result;
	printf("Stream 2Bit : %i ",(int)result);
	*/
	u8* ZStdStream6Bit	= new u8[stream6BitCnt * 2 +100];
	u8* ZStdStream5Bit	= new u8[stream5BitCnt * 2 +100];
	u8* ZStdStream4Bit	= new u8[stream4BitCnt * 2 +100];
	u8* ZStdStream3Bit	= new u8[stream3BitCnt * 2 +100];

	// Stream : 3 Bit
	if (stream3BitCnt) {
		result = ZSTD_compress(ZStdStream3Bit, stream3BitCnt * 2 +100, corr3D_stream3Bit,stream3BitCnt, 18);
		header3D.compr3BitSize  = result;
	} else {
		header3D.compr3BitSize  = 0;
	}
	header3D.stream3BitCnt  = stream3BitCnt;
	printf("Stream 3Bit : %i\n",(int)result);

	// Stream : 4 Bit
	if (stream4BitCnt) {
		result = ZSTD_compress(ZStdStream4Bit, stream4BitCnt * 2 +100, corr3D_stream4Bit,stream4BitCnt, 18);
		header3D.compr4BitSize  = result;
	} else {
		header3D.compr4BitSize  = 0;
	}
	header3D.stream4BitCnt  = stream4BitCnt;
	printf("Stream 4Bit : %i\n",(int)result);

	// Stream : 5 Bit
	if (stream5BitCnt) {
		result = ZSTD_compress(ZStdStream5Bit, stream5BitCnt * 2 +100, corr3D_stream5Bit,stream5BitCnt, 18);
		header3D.compr5BitSize  = result;
	} else {
		header3D.compr5BitSize  = 0;
	}
	header3D.stream5BitCnt  = stream5BitCnt;
	printf("Stream 5Bit : %i\n",(int)result);

	// Stream : 6 Bit
	if (stream6BitCnt) {
		result = ZSTD_compress(ZStdStream6Bit, stream6BitCnt * 2 +100, corr3D_stream6Bit,stream6BitCnt, 18);
		header3D.compr6BitSize  = result;
	} else {
		header3D.compr6BitSize  = 0;
	}
	header3D.stream6BitCnt  = stream6BitCnt;
	printf("Stream 6Bit : %i\n",(int)result);

	// Stream : Color, Tile Map
	// Stream : 3,4,5,6 Bit
	// Last Stream : Map in order 16x8,8x16,8x8,4x8,8x4,4x4 (SAME ORDER AS ENCODER)

#if 0
	u8* dictStore = new u8[1000000]; // 1 MB
	size_t sampleSizes[1];
	sampleSizes[0] = stream3BitCnt;
	size_t resultDico = ZDICT_trainFromBuffer(dictStore,1000000,corr3D_stream3Bit,sampleSizes,1);
#endif

	printf("File Size at this stage : %i\n", fileOutSize);

	HeaderBase headerTag;
	headerTag.tag.tag8[0] = is3D ? '3' : '2';
	headerTag.tag.tag8[1] = 'D';
	headerTag.tag.tag8[2] = 'T';
	headerTag.tag.tag8[3] = 'L';

	int baseSize = (sizeof(HeaderTile3D)	+ header3D.compr3BitSize + header3D.compr4BitSize + header3D.compr5BitSize + header3D.compr6BitSize

												+ header3D.comprColorSize + header3D.comprTypeSize
		
												+ header3D.sizeT16_8MapCmp + header3D.sizeT8_16MapCmp
												+ header3D.sizeT8_8MapCmp + header3D.sizeT8_4MapCmp
												+ header3D.sizeT4_8MapCmp + header3D.sizeT4_4MapCmp);

	headerTag.length	  = ((baseSize + 3) >> 2) <<2;	// Round multiple of 4.
	fileOutSize += headerTag.length;

	u8 pad[3] = { 0,0,0 };
	int padding = headerTag.length - baseSize;

	// TODO move to write
	pStats->loc.sizeBlock3DLUT += headerTag.length;

	fwrite(&headerTag, 1,sizeof(HeaderBase)  ,outFile);
	fwrite(&header3D , 1,sizeof(HeaderTile3D),outFile);

	// Write all Bit Stream
	fwrite(ZStdStream3Bit,1,header3D.compr3BitSize,outFile);
	fwrite(ZStdStream4Bit,1,header3D.compr4BitSize,outFile);
	fwrite(ZStdStream5Bit,1,header3D.compr5BitSize,outFile);
	fwrite(ZStdStream6Bit,1,header3D.compr6BitSize,outFile);

	fwrite(ZStdTileStream,1,header3D.comprTypeSize,outFile);
	fwrite(ZStdColorStream,1,header3D.comprColorSize,outFile);

	if (is3D) {
		fwrite(ZStdT16_8Map,1,header3D.sizeT16_8MapCmp,outFile);
		fwrite(ZStdT8_16Map,1,header3D.sizeT8_16MapCmp,outFile);
	}
	fwrite(ZStdT8_8Map ,1,header3D.sizeT8_8MapCmp ,outFile);
	if (is3D) {
		fwrite(ZStdT8_4Map ,1,header3D.sizeT8_4MapCmp ,outFile);
		fwrite(ZStdT4_8Map ,1,header3D.sizeT4_8MapCmp ,outFile);
	}
	fwrite(ZStdT4_4Map ,1,header3D.sizeT4_4MapCmp ,outFile);

	if (padding) { fwrite(pad, 1, padding, outFile); }

	delete[] ZStdStream6Bit;
	delete[] ZStdStream5Bit;
	delete[] ZStdStream4Bit;
	delete[] ZStdStream3Bit;
	delete[] ZStdTileStream;
	delete[] ZStdColorStream;

	delete[] ZStdT16_8Map;
	delete[] ZStdT8_16Map;
	delete[] ZStdT8_8Map;
	delete[] ZStdT8_4Map;
	delete[] ZStdT4_8Map;
	delete[] ZStdT4_4Map;

	deleteAllocated3DParts();
}

void EncoderContext::deleteAllocated3DParts() {
	delete [] corr3D_stream6Bit;
	delete [] corr3D_stream5Bit;
	delete [] corr3D_stream4Bit;
	delete [] corr3D_stream3Bit;
//	delete [] corr3D_stream2Bit;

	delete [] corr3D_sizeT16_8Map;
	delete [] corr3D_sizeT8_16Map;
	delete [] corr3D_sizeT8_8Map;
	delete [] corr3D_sizeT8_4Map;
	delete [] corr3D_sizeT4_8Map;
	delete [] corr3D_sizeT4_4Map;

	delete [] corr3D_tileStreamTileType;
	delete [] corr3D_colorStream;
}

void EncoderContext::RegisterAndCreate3DLut() {
	
	// Already loaded...
	if (correlationPatternCount3D != 0) { return; }

	/*
	LinearEqu3D diag3D;
	diag3D.Set(0.0f,0.0f,0.0f,64.0f,64.0f,64.0f);
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, &diag3D, 1, 0,255,0 );

	LinearEqu3D hoppe[22];
	hoppe[0].Set(0,0,0,   0,1,0  );
	hoppe[1].Set(0,1,0,   0,12,13); 
	hoppe[2].Set(0,12,13, 0,15,14); 
	hoppe[3].Set(0,15,14, 0,18,17); 
	hoppe[4].Set(0,18,17, 0,30,29); 

	hoppe[5].Set(63,30,26, 63,41,40);
	hoppe[6].Set(63,41,40, 63,44,41);
	hoppe[7].Set(63,44,41, 63,44,43);
	hoppe[8].Set(63,44,43, 63,50,47);
	hoppe[9].Set(63,50,47, 63,51,50);
	hoppe[10].Set(63,51,50, 63,53,50);
	hoppe[11].Set(63,53,50, 63,57,53);
	hoppe[12].Set(63,57,53, 63,57,55);
	hoppe[13].Set(63,57,55, 63,57,56);
	hoppe[14].Set(63,57,56, 63,58,55);
	hoppe[15].Set(63,58,55, 63,58,56);
	hoppe[16].Set(63,58,56, 63,60,56);
	hoppe[17].Set(63,60,56, 63,60,58);
	hoppe[18].Set(63,60,58, 63,60,59);
	hoppe[19].Set(63,60,59, 63,61,59);
	hoppe[20].Set(63,61,59, 63,61,61);
	hoppe[21].Set(63,61,61, 63,62,62);
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, hoppe, 22, 0,255,0 );

	LinearEqu3D equerre[2];
	equerre[0].Set(0.0f,0.0f,0.0f,64.0f,64.0f, 0.0f);
	equerre[1].Set(64.0f,64.0f,0.0f,64.0f,64.0f,64.0f);
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, equerre, 2, 0,255,0 );

	LinearEqu3D Pattern2[2];
	Pattern2[0].Set(64.0f,0.0f,0.0f  , 5.0f, 45.0f, 5.0f);
	Pattern2[1].Set(5.0f, 45.0f, 5.0f,32.0f, 64.0f,64.0f);
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, Pattern2, 2, 0,255,0 );

	LinearEqu3D Pattern3[2];
	Pattern3[0].Set(64.0f,0.0f,0.0f  , 5.0f, 45.0f, 5.0f);
	Pattern3[1].Set(5.0f, 45.0f, 5.0f,32.0f,  0.0f,64.0f);
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, Pattern3, 2, 0,255,0 );

	LinearEqu3D Pattern4[3];
	Pattern4[0].Set(2.0f  ,2.0f, 2.0f,30.0f,30.0f,0.0f);
	Pattern4[1].Set(32.0f,32.0f,0.0f,32.0f,32.0f,62.0f);
	Pattern4[2].Set(32.0f, 32.0f, 64.0f,64.0f,64.0f,64.0f);
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, Pattern4, 3, 0,255,0 );

	LinearEqu3D Pattern5[2];
	{ float x=2.0f; float y=2.0f; float z=62.0f;
	Pattern5[0].Set(2.0f  ,2.0f, 2.0f,x,y,z);
	Pattern5[1].Set(x,y,z,62.0f,62.0f, 2.0f); }
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, Pattern5, 2, 0,255,0 );

	LinearEqu3D Pattern6[2];
	{
	float x=32.0f; float y=32.0f; float z=62.0f;
	Pattern6[0].Set(2.0f  ,2.0f, 2.0f,x,y,z);
	Pattern6[1].Set(x,y,z,62.0f,62.0f, 2.0f); }
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, Pattern6, 2, 0,255,0 );

	LinearEqu3D Pattern7[2];
	{ float x=2.0f; float y=62.0f; float z=62.0f;
	Pattern7[0].Set(2.0f  ,2.0f, 2.0f,x,y,z);
	Pattern7[1].Set(x,y,z,62.0f,62.0f, 2.0f); }
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, Pattern7, 2, 0,255,0 );

	LinearEqu3D Pattern8[2];
	{ float x=32.0f; float y=2.0f; float z=62.0f;
	Pattern8[0].Set(2.0f  ,2.0f, 2.0f,x,y,z);
	Pattern8[1].Set(x,y,z,62.0f,62.0f, 2.0f); }
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, Pattern8, 2, 0,255,0 );

	LinearEqu3D Pattern9[2];
	{ float x=32.0f; float y=16.0f; float z=62.0f;
	Pattern9[0].Set(2.0f  ,2.0f, 2.0f,x,y,z);
	Pattern9[1].Set(x,y,z,62.0f,62.0f, 2.0f); }
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, Pattern9, 2, 0,255,0 );

	LinearEqu3D Pattern10[2];
	{ float x=0.0f; float y=28.0f; float z=2.0f;
	Pattern10[0].Set(2.0f  ,2.0f, 2.0f,x,y,z);
	Pattern10[1].Set(x,y,z,62.0f,62.0f,62.0f); }
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, Pattern10, 2, 0,255,0 );

	LinearEqu3D PatternY1[3];
	{ float x=32.0f; float y=16.0f; float z=62.0f;
	PatternY1[0].Set(58.0f,62.0f, 62.0f, 32.0f,32.0f,32.0f);
	PatternY1[1].Set(32.0f,32.0f,32.0f, 32.0f,0.0f,0.0f); 
	PatternY1[2].Set(32.0f,32.0f,32.0f, 0.0f,32.0f,0.0f); }
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, PatternY1, 3, 0,255,0 );

	LinearEqu3D PatternY2[3];
	{ float x=32.0f; float y=16.0f; float z=62.0f;
	PatternY2[0].Set(58.0f,62.0f, 62.0f, 32.0f,32.0f,32.0f);
	PatternY2[1].Set(32.0f,32.0f,32.0f, 32.0f,0.0f,0.0f); 
	PatternY2[2].Set(32.0f,32.0f,32.0f, 0.0f,0.0f,32.0f); }
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, PatternY2, 3, 0,255,0 );

	LinearEqu3D PatternMultiCol1[5];
	PatternMultiCol1[0].Set(63.0f,63.0f,63.0f,    33.0f,31.0f,27.0f); // Y Cross
	PatternMultiCol1[1].Set(33.0f, 31.0f, 27.0f,   0.0f, 0.0f, 9.5f); // Left
	PatternMultiCol1[2].Set(33.0f, 31.0f, 27.0f,  15.0f,12.0f, 0.0f); // Right
	PatternMultiCol1[3].Set( 0.0f, 0.0f, 9.5f,  15.0f,12.0f, 0.0f); // Connect Left-Right
	PatternMultiCol1[4].Set( 0.0f, 0.0f, 9.5f,  2.0f,  5.0f,34.0f); // Right->Special point.
	correlationPattern3D[correlationPatternCount3D++].Set3D(350.0f, PatternMultiCol1, 5, 0,255,0 );
*/
	Load3DPattern("Bank3D//tileMap_Aqours_31107015_n_2250p.png_352-1416.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_41107010_2250p.png_1000-112.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_41107010_2250p.png_1000-504.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_1520-1968.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_1520-1976.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_1520-800.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_1568-2104.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_280-888.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_352-840.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_448-2088.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_464-1632.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_608-2152.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_688-2184.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_768-2176.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_2250p.png_88-280.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_43104009_r_720p.png_384-280.lut");

	// Group 2
	Load3DPattern("Bank3D//tileMap_Aqours_31107015_n_2250p.png_1128-104.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_41107010_2250p.png_1304-2040.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_41107010_2250p.png_1384-2208.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_31107015_n_2250p.png_1136-64.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_41109006_n_2250p.png_2528-528.lut");
	Load3DPattern("Bank3D//tileMap_Aqours_41109006_r2550p.png_1296-752.lut");


	LUTHeader hd;
	hd.lutH[0] = 'L'; hd.lutH[1] = 'U'; hd.lutH[2] = 'L'; hd.lutH[3] = '0'; // Compressed or uncompressed.
	hd.version				= 0;
	hd.entryCount			= correlationPatternCount3D-1;
	hd.padding_extension[0] = 0;
	hd.padding_extension[0] = 1;

	// Single base pattern per save.
	u32 perPattern			= ((64+32+16+8)*3);
	u32 uncmpSize			= perPattern * correlationPatternCount3D;
	u8* stream				= new u8[uncmpSize];
	u8* streamFill			= stream;

	// 3 bit, 4 bit, 5 bit, 6 bit
	for (int n=0; n < 4; n++) {
		for (int m=0; m < correlationPatternCount3D;m++) {
			u8* next = correlationPattern3D[m].BinarySave3D(streamFill,0,(EncoderContext::Mode)n);
			streamFill = next;
		}
	}

	FILE* fLutO = fopen("LutFile.lut","wb");
	fwrite(&hd,sizeof(LUTHeader),1,fLutO);
	fwrite(stream,uncmpSize,1,fLutO);
	fclose(fLutO);

	delete[] stream;
}

void EncoderContext::Load3DPattern(const char* fileName) {
	// 
	FILE* f = fopen(fileName,"rb");
	if (f) {
		u8 r[256];
		u8 g[256];
		u8 b[256];

		u8 xyz[128*3];
		u8 pos[128];

		u8 count;
		fread(&count,1,1,f);
		fread(r,sizeof(u8),count,f);
		fread(g,sizeof(u8),count,f);
		fread(b,sizeof(u8),count,f);
		fclose(f);

		// Interleave linear arrays.
		for (int n=0; n < count; n++) {
			xyz[n*3  ] = r[n];
			xyz[n*3+1] = g[n];
			xyz[n*3+2] = b[n];
			pos[n]     = n;
		}


		// Now sort by morton interleaved code...
		sortPalette(pos,xyz,count);

		// Remove Duplicate.
		/*
		int write = 0;
		u8* ptrR  = &xyz[3];
		u8* ptrW  = &xyz[3];

		for (int n=1; n < count; n++) {
			//
			if (ptrR[0]==ptrR[-3] && ptrR[1]==ptrR[-2] && ptrR[2]==ptrR[-1]) {
				// Skip...
			} else {
				*ptrW++ = ptrR[0];
				*ptrW++ = ptrR[1];
				*ptrW++ = ptrR[2];
				write++;
			}
		}
		*/

		// Insert interpolated to fill if less than 64.

		// Remove if more than 64.

		// Now need to convert from n points to 64 as input.
		u8* arrayXYZ = xyz;
		u8 reduceXYZ[64*3];
		if (count > 64) {
			reduceArray(xyz, reduceXYZ, count, 64,false);
			arrayXYZ = reduceXYZ;
		}

		if (correlationPatternCount3D >= 64) {
			printf("ERROR : LUT 3D more than 64 entries.\n");
			exit(1);
		}

		correlationPattern3D[correlationPatternCount3D++].Set3DPointCloud(15.0f,arrayXYZ,count);

		/*
		for (int n=0; n < 64; n++) {
			xyz[n*3]   = (xyz[n*3  ]<<2) | (xyz[n*3  ]>>4); // 6->8 bit
			xyz[n*3+1] = (xyz[n*3+1]<<2) | (xyz[n*3+1]>>4); // 6->8 bit
			xyz[n*3+2] = (xyz[n*3+2]<<2) | (xyz[n*3+2]>>4); // 6->8 bit
		}

		char buffer[1000];
		sprintf(buffer,"%s.png",fileName);
		stbi_write_png(buffer, 8, 8, 3, xyz, 24);
		*/
	}


}

void EncoderContext::Load2DPattern(const char* fileName) {
	// 
	int strL = strlen(fileName);

	FILE* f = fopen(fileName,"rb");
	if (f) {
		u8 a[256];
		u8 b[256];

		u8 xy[128*2];
		u8 pos[128];

		u8 count;
		if (fileName[strL-1]=='g') {
			// PNG
			int x;
			int y;
			int ch;
			count = 0;
			unsigned char* src = stbi_load(fileName, &x, &y, &ch, 3);
			for (int py=0; py < y; py++) {
				for (int px=0; px < x; px++) {
					int idx = (px + (py*64)) * 3;
					u8 v = src[idx];
					if (count < 64 && v) {
						xy[count*2] = px;
						xy[count*2 +1] = py;
						pos[count] = count;
						count++;
					}
				}
			}

			printf("Load 2D LUT %s : count %i\n",fileName,count);
		} else {
			fread(&count,1,1,f);
			fread(a,sizeof(u8),count,f);
			fread(b,sizeof(u8),count,f);
			fclose(f);

			// Interleave linear arrays.
			for (int n=0; n < count; n++) {
				xy[n*2  ] = a[n];
				xy[n*2+1] = b[n];
				pos[n]     = n;
			}
		}

		// Now sort by morton interleaved code...
		sortPalette2D(pos,xy,count);

		// Remove Duplicate.
		/*
		int write = 0;
		u8* ptrR  = &xyz[3];
		u8* ptrW  = &xyz[3];

		for (int n=1; n < count; n++) {
			//
			if (ptrR[0]==ptrR[-3] && ptrR[1]==ptrR[-2] && ptrR[2]==ptrR[-1]) {
				// Skip...
			} else {
				*ptrW++ = ptrR[0];
				*ptrW++ = ptrR[1];
				*ptrW++ = ptrR[2];
				write++;
			}
		}
		*/

		// Insert interpolated to fill if less than 64.

		// Remove if more than 64.

		// Now need to convert from n points to 64 as input.

		u8* arrayXYZ = xy;
		u8 reduceXYZ[64*2];
		if (count > 64) {
			reduceArray(xy, reduceXYZ, count, 64,true);
			arrayXYZ = reduceXYZ;
		}
		
		correlationPattern2D[correlationPatternCount2D++].Set2DPointCloud(15.0f,arrayXYZ,count);

		/*
		for (int n=0; n < 64; n++) {
			arrayXYZ[n*2]   = (arrayXYZ[n*2  ]<<2) | (arrayXYZ[n*2  ]>>4); // 6->8 bit
			arrayXYZ[n*2+1] = (arrayXYZ[n*2+1]<<2) | (arrayXYZ[n*2+1]>>4); // 6->8 bit
		}

		char buffer[1000];
		sprintf(buffer,"%s.png",fileName);
		stbi_write_png(buffer, 8, 8, 3, xyz, 24);
		*/
	}
}


void EncoderContext::RegisterAndCreate2DLut() {
	correlationPatternCount2D = 0;

	Load2DPattern("Bank2D\\t10_tileMap13116.lut.png");
	Load2DPattern("Bank2D\\t11_tileMap2099.lut.png");
	Load2DPattern("Bank2D\\t12_tileMap2083.lut.png");
	Load2DPattern("Bank2D\\t13_tileMap29660.lut.png");
	Load2DPattern("Bank2D\\t14_tileMap30402.lut.png");
	Load2DPattern("Bank2D\\t15_tileMap10903.lut.png");
	Load2DPattern("Bank2D\\t16_tileMap16815.lut.png");
	Load2DPattern("Bank2D\\t17_tileMap18049.lut.png");
	Load2DPattern("Bank2D\\t18_tileMap20386.lut.png");
	Load2DPattern("Bank2D\\t19_tileMap29160.lut.png");
	Load2DPattern("Bank2D\\t1_tileMap17049.lut.png");
	Load2DPattern("Bank2D\\t20_tileMap30698.lut.png");
	Load2DPattern("Bank2D\\t21_tileMap12142.lut.png");
	Load2DPattern("Bank2D\\t22_tileMap1247.lut.png");
	Load2DPattern("Bank2D\\t23_tileMap14650.lut.png");
	Load2DPattern("Bank2D\\t2_tileMap13128.lut.png");
	Load2DPattern("Bank2D\\t3_tileMap32174.lut.png");
	Load2DPattern("Bank2D\\t4_tileMap13051.lut.png");
	Load2DPattern("Bank2D\\t5_tileMap22916.lut.png");
	Load2DPattern("Bank2D\\t6_tileMap29869.lut.png");
	Load2DPattern("Bank2D\\t7_tileMap10040.lut.png");
	Load2DPattern("Bank2D\\t8_tileMap10885.lut.png");
	Load2DPattern("Bank2D\\t9_tileMap12019.lut.png");
	Load2DPattern("Bank2D\\tileMap14143.lut.png");
	Load2DPattern("Bank2D\\tileMap16448.lut.png");
	Load2DPattern("Bank2D\\tileMap17195.lut.png");
	Load2DPattern("Bank2D\\tileMap18699.lut.png");
	Load2DPattern("Bank2D\\tileMap18784.lut.png");
	Load2DPattern("Bank2D\\tileMap18853.lut.png");
	Load2DPattern("Bank2D\\tileMap19957.lut.png");
	Load2DPattern("Bank2D\\tileMap22351.lut.png");
	Load2DPattern("Bank2D\\tileMap2359.lut.png");

	LUTHeader hd;
	hd.lutH[0] = 'L'; hd.lutH[1] = 'U'; hd.lutH[2] = '2'; hd.lutH[3] = '0'; // Compressed or uncompressed.
	hd.version				= 0;
	hd.entryCount			= correlationPatternCount2D-1;
	hd.padding_extension[0] = 0;
	hd.padding_extension[0] = 1;

	// Single base pattern per save.
	u32 perPattern			= ((64+32+16+8)*2);
	u32 uncmpSize			= perPattern * correlationPatternCount2D;
	u8* stream				= new u8[uncmpSize];
	u8* streamFill			= stream;

	// 3 bit, 4 bit, 5 bit, 6 bit
	for (int n=0; n < 4; n++) {
		for (int m=0; m < correlationPatternCount2D;m++) {
			u8* next = correlationPattern2D[m].BinarySave2D(streamFill,0,(EncoderContext::Mode)n);
			streamFill = next;
		}
	}

	FILE* fLutO = fopen("LutFile2D.lut","wb");
	fwrite(&hd,sizeof(LUTHeader),1,fLutO);
	fwrite(stream,uncmpSize,1,fLutO);
	fclose(fLutO);

	return;


	LinearEqu2D diag3D;
	/*
		X.......
		.X......
		..X.....
		...X....
		....X...
		.....X..
		......X.
		.......X
	*/
	diag3D.Set(0.0f,0.0f,64.0f,64.0f);
	correlationPattern2D[correlationPatternCount2D++].Set2D(350.0f, &diag3D, 1, 0,255,0 );

	LinearEqu2D fig[2];
//		fig[0].Set(0.0f,0.0f,48.0f,16.0f);
//		fig[1].Set(48.0f,16.0f,64.0f,64.0f);
	/*
		XXXX....
		....XXXX
		.......X
		......X.
		.....X..
		....X...
		....X...
		...X....
		*/
	fig[0].Set(0.0f,0.0f,64.0f,16.0f);
	fig[1].Set(64.0f,16.0f,40.0f,64.0f);
	correlationPattern2D[correlationPatternCount2D++].Set2D(1.0f, fig, 2 ,0 ,128,  0); // 35.0f

	LinearEqu2D fig2[2];
	/*
		....XXXX
		XXXX...X
		.......X
		.......X
		......X.
		......X.
		......X.
		......X.
		*/
	fig2[0].Set(0.0f,20.0f,64.0f,0.0f);
	fig2[1].Set(64.0f,0.0f,56.0f,64.0f);
	correlationPattern2D[correlationPatternCount2D++].Set2D(1.0f, fig2, 2 ,255 ,128,  0);

	LinearEqu2D fig3[2];
	/*
		XX......
		..XXX...
		.....XX.
		.......X
		.......X
		.......X
		.......X
		.......X
		*/
	fig3[0].Set(0.0f,0.0f,60.0f,32.0f);
	fig3[1].Set(60.0f,32.0f,64.0f,64.0f);
	correlationPattern2D[3].Set2D(12.0f, fig3, 2 ,0 ,0,  255);

	LinearEqu2D fig4[2];
	fig4[0].Set(0.0f,0.0f,64.0f,32.0f);
	fig4[1].Set(0.0f,0.0f,32.0f,64.0f);
	correlationPattern2D[correlationPatternCount2D++].Set2D(1.0f, fig4, 2 ,0 ,0,  128);

	LinearEqu2D splDiag2[2];
	/*
		X.......
		.X......
		..X.....
		...X....
		....X...
		.....X..
		......X.
		...XXXXX
		*/
 	splDiag2[0].Set(0.0f,0.0f,64.0f,64.0f);
 	splDiag2[1].Set(64.0f,64.0f,48.0f,64.0f);
	correlationPattern2D[correlationPatternCount2D++].Set2D(12.0f, splDiag2, 2 ,128,  0,  128); // 20.0f
	/*
	LinearEqu2D splOppDiag;
	splOppDiag.Set(64.0f,0.0f, 0.0f,64.0f);
	correlationPattern[1].Set(20.0f, &splOppDiag, 1 ,0, 128,  0);
	*/

	/*
	spl.Set(64.0,0.0,    32.0,32.0,    0.0,64.0);
	BuildDistanceField(&spl,1,linearOppTable);
	spl.Set(0.0,0.0,    75.0,48.0,    20.0,64.0);
	BuildDistanceField(&spl,1,Spline1Table);
	*/

	Load2DPattern("Lut2D_1.png");
	Load2DPattern("Lut2D_2.png");
	Load2DPattern("Lut2D_3.png");
	Load2DPattern("Lut2D_4.png");
	Load2DPattern("Lut2D_5.png");
	Load2DPattern("Lut2D_6.png");
	Load2DPattern("Lut2D_7.png");
	Load2DPattern("Lut2D_8.png");


	delete[] stream;
}

u8* tbl1D6Bit = new u8[100000];
u8* tbl1D5Bit = new u8[100000];
u8* tbl1D4Bit = new u8[100000];
u8* tbl1D3Bit = new u8[100000];
u8* tbl1D2Bit = new u8[100000];

int tbl1D2BitCnt = 0;
int tbl1D3BitCnt = 0;
int tbl1D4BitCnt = 0;
int tbl1D5BitCnt = 0;
int tbl1D6BitCnt = 0;

u8* streamType = new u8[100000];
u8* pType      = streamType;

u8* params     = new u8[100000];
u8* pParams    = params;

u16* LUTID     = new u16[100000];
u16* pLUTSs    = LUTID;

/*
	Natural :
	[Natural] (No format) (Tile 0 ?)
		[Base 6 Bit] (Store in Min/Max Stream)

	[Lut]     (Format 2/3/4/5 bit)
		[Min + Max]
		[Lut ID]
 */
u64 mask2Bit[10000];
u64 mask3Bit[10000];
u64 mask4Bit[10000];
u64 mask5Bit[10000];

int cntTbl2B = 0;
int cntTbl3B = 0;
int cntTbl4B = 0;
int cntTbl5B = 0;

int findMask(u64 searchMask, int format) {
	u64* pmaskTbl;
	int  countTbl;
	switch (format) {
	case 2:
		pmaskTbl = mask2Bit;
		countTbl = cntTbl2B;
		break;
	case 3:
		pmaskTbl = mask3Bit;
		countTbl = cntTbl3B;
		break;
	case 4:
		pmaskTbl = mask4Bit;
		countTbl = cntTbl4B;
		break;
	case 5:
		pmaskTbl = mask5Bit;
		countTbl = cntTbl5B;
		break;
	default:
		printf("ERROR\n");
		return 0;
	}

	for (int n=0; n < countTbl; n++) {
		if ((pmaskTbl[n] & searchMask) == searchMask) {
			return n;
		}
	}

	// New Entry...
	pmaskTbl[countTbl] = searchMask;
	switch (format) {
	case 2: cntTbl2B++; break;
	case 3: cntTbl3B++; break;
	case 4: cntTbl4B++; break;
	case 5: cntTbl5B++; break;
	}
	return countTbl;
}

// Returns position of the only set bit in 'n' 
int findPosition(u64 n) 
{
    unsigned i = 1, pos = 1; 
  
    // Iterate through bits of n till we find a set bit 
    // i&n will be non-zero only when 'i' and 'n' have a set bit 
    // at same position 
    while (!(i & n)) { 
        // Unset current bit and set the next bit in 'i' 
        i = i << 1; 
  
        // increment position 
        ++pos; 
    } 
  
    return pos; 
}

int GetBitCount(u64 v) {
	u64 c; // store the total here
	static const int S[] = {1, 2, 4, 8, 16, 32}; // Magic Binary Numbers
	static const u64 B[] = {0x5555555555555555ULL, 0x3333333333333333ULL, 0x0F0F0F0F0F0F0F0FULL, 0x00FF00FF00FF00FFULL, 0x0000FFFF0000FFFFULL, 0x00000000FFFFFFFFULL};

	c = v - ((v >> 1) & B[0]);
	c = ((c >> S[1]) & B[1]) + (c & B[1]);
	c = ((c >> S[2]) + c) & B[2];
	c = ((c >> S[3]) + c) & B[3];
	c = ((c >> S[4]) + c) & B[4];
	c = ((c >> S[5]) + c) & B[5];
	return c;
}

int GetBitIndex(u64 mask, int value, int format) {
	u64 maskClip = (1ULL<<(value+1))-1;
	u64 countBits = mask & maskClip;

	return GetBitCount(countBits);
}

void TestCompress(u8* start, u8* end) {
	int sizeMax = (end - start) * 3;
	u8* dstZ = new u8[sizeMax];
	u64 result = ZSTD_compress(dstZ, sizeMax, start, end-start, 18);
	printf(" %i -> %i\n",end-start, (int)result);
	delete[] dstZ;
}

int FindAndRemoveMostUsedColor(u8* histo_) {
	// Extract Min-Max, First and Second.
	int idxBestFirst = -1;
	int valBestFirst = -1;
	for (int n=0; n < 256; n++) {
		if (histo_[n] >= valBestFirst) { // >= is important !!! Take most RIGHT best value. Override OK !
			valBestFirst = histo_[n];
			idxBestFirst = n;
		}
	}
	
	// Will use Index-1..Index+1 as a unique single color.
	if (idxBestFirst ==   0) { idxBestFirst =   1; }
	if (idxBestFirst == 255) { idxBestFirst = 254; }

	// Remove the color from the histogram.
	histo_[idxBestFirst-1] = 0;
	histo_[idxBestFirst  ] = 0;
	histo_[idxBestFirst+1] = 0;

	return idxBestFirst;
}

void Model1(u8* histo_, int valueRange, int& outMin, int& outDelta) {
	//
	// V = Min + Range * Value (0..valueRange)
	//
	int minV8 = 99999;
	int maxV8 = -99999;
	for (int n=0; n < 256; n++) {
		int v = histo_[n];
		if (v) {
			if (minV8 > n)   { minV8 = n; }
			if (maxV8 < n)   { maxV8 = n; }
		}
	}

	if (minV8 != 99999) {
		int delta = maxV8-minV8;
		outMin   = minV8;
		outDelta = delta;
	} else {
		// Only TWO COLORS removed...
		outMin   = 0;
		outDelta = 0;
	}
}

int GetValueModel1(int value, int minCol, int delta, int rangeCompression) {
	int res;
	if (delta) {
		res = (((value - minCol) * rangeCompression)+((delta>>1)-1)) / delta;
	} else {
		res = 0;
	}
	return res;
}

int DecompModel1(int index, int minCol, int delta, int rangeCompression) {
	int v = minCol + ((index * delta)/rangeCompression);
	return v;
} 

u8* EncoderContext::DynamicTileCompressor(u8* stream, Plane* src, Plane* map, Plane* debug) {
	u8* startStream = stream;
	int w = src->GetWidth();
	int h = src->GetHeight();

	u8 histo256[256];
	u8 values[256];
	u8 offX[256];
	u8 offY[256];

	int totalPixel = 0;
	int total0 = 0;
	int total1 = 0;

	for (int y = 0; y < h; y+=8) {
		for (int x = 0; x < w; x+=8) {
			int pixelCount = 0;
			memset(histo256,0,sizeof(u8)*256);

			// 4x4 => 4 tiles
			bool isOut;
			int shape = 0xF;
			for (int y2=0; y2 < 8; y2 += 4) {
				// for (int x2=0; x2 < 8; x2 += 4) {
				bool hasLeft  = (map->GetPixelValue(x  ,y2+y,isOut) == 0); 
				if (hasLeft) {
					shape &= ~(1<<( 0 + ((y2&4)>>1) )); // Remove bit.
				}

				bool hasRight = (map->GetPixelValue(x+4,y2+y,isOut) == 0); 
				if (hasRight) {
					shape &= ~(1<<( 1 + ((y2&4)>>1) )); // Remove bit.
				}

				if (hasLeft | hasRight) {
					// Extract Histogram.
					int lengthX = (hasLeft && hasRight)        ? 8 : 4;
					int x2      = ((lengthX == 4) && hasRight) ? 4 : 0;
					for (int iy =0; iy < 4; iy++) {
						for (int ix =0; ix < lengthX; ix++) {

							int v = src->GetPixelValue(x+x2+ix,y+y2+iy,isOut);

							// Perform global color compression.
							v = CompressF(v, colorCompression1D);

							// Histogram.
							histo256[v]++;
							values[pixelCount] = v;
							offX[pixelCount]   = x2+ix;
							offY[pixelCount]   = y2+iy;

							pixelCount++;
						}
					}
				}
			}

			if (pixelCount > 0) {

				// Step 1 : find most used 0 and 1
				int unique = 0;
				for (int n=0; n < 256; n++) {
					if (histo256[n] != 0) {
						unique++;
					}
				}

				int colorIndex0 = FindAndRemoveMostUsedColor(histo256);
				// int colorIndex1 = FindAndRemoveMostUsedColor(histo256);


				int minCol;
				int delta;
				// Col = MinCol  + UBit*Range
				Model1(histo256, rangeCompression1D, minCol, delta);

				// LUT ID -> MIN + D Range      
				// Type 1:
				// Col = MinCol + POS_LUT[Bit]            (Min,LUTID)
				// Col = MinCol + POS_LUT[Bit]*Range      (Min,Range,LUTID) <-- Cheat with MinCol to give precise most used color for a given bit.
				// Type 2:
				// Col = BaseCol + SGN_LUT[Bit]           (Min,LUTID)
				// Col = BaseCol + SGB_LUT[Bit]*Range     (Min,Range,LUTID) <-- Good middle base color ??? May be inexistant value.
				// Type 3:
				// Col = BaseCol + SBit*Range             <-- SBit = 0 naturally encode most used color. Base Col is small and PRECISE.
				// Col = MinCol  + LUT[UBit]*Range
				// Col = BaseCol + LUT[SBit]*Range        

				for (int n=0; n < pixelCount; n++) {
					int v = values[n];
					if ((v >= colorIndex0-1) && (v <= colorIndex0+1)) {
						*stream++ = 0;
						// Visualize most used color for now.
						debug->SetPixel(x + offX[n],y + offY[n],colorIndex0);
//						printf("S:%i -> V:%i\n",stream[-1],colorIndex0);
					} else {
						int idx = GetValueModel1(v,minCol,delta,rangeCompression1D);
						*stream++ = 1 + idx;
						int vOut = DecompModel1(idx,minCol,delta,rangeCompression1D);
						debug->SetPixel(x + offX[n],y + offY[n],vOut);
//						printf("S:%i -> V:%i\n",stream[-1],vOut);
					}
				}

				*pType++ = colorIndex0; // Parameters = Index 0 value.
				*pType++ = minCol;
				*pType++ = delta;

				printf("Tile %i,%i => Data : %i (%x) PixCount, %i color0, %i Base, %i delta\n",x,y,pixelCount,shape,colorIndex0,minCol,delta);

				// LUT = Better color distribution for bit.
				// Note : UBit/SBit not force to a specific SIZE. Not 4 bit or 5 bit only... adapted to tile.

				//        -> Base + +-Range
				//		  -> Use pixel 0,0 of tile if available.
				//        -> Value 0/1 used for most used color. YES.
				// Range compr. per tile ?
				// 
			}
		}
	}

	return stream;
}

void EncoderContext::GenerateDynamicTileChunk(u8* stream, int sizeStream) {
	if (sizeStream > 0) {
		HeaderBase headerTag;
		headerTag.tag.tag8[0] = '1';
		headerTag.tag.tag8[1] = 'D';
		headerTag.tag.tag8[2] = 'T';
		headerTag.tag.tag8[3] = 'L';

		Header1D header1D;
		header1D.version			= 0;
		header1D.compressionColor	= colorCompression1D;
		header1D.compressionRange	= rangeCompression1D;

		// --- Pixel Data
		unsigned char* pZStdStream = new unsigned char[sizeStream*2];
		size_t result = (int)ZSTD_compress(pZStdStream, sizeStream*2, stream,sizeStream, 18);
		fileOutSize += result;

		header1D.streamPixelBit     = result;
		header1D.streamPixelUncmp   = sizeStream;

		// --- Tile Data
		unsigned char* pZStdType   = new unsigned char[sizeStream];
		int sizeTypeStreamUncmp    = pType-streamType;
		result = (int)ZSTD_compress(pZStdType, sizeStream, streamType,sizeTypeStreamUncmp, 18);
		fileOutSize += result;

		header1D.streamTypeCnt      = result;
		header1D.streamTypeUncmp    = sizeTypeStreamUncmp;

		int baseSize = sizeof(Header1D) 
			         + header1D.streamPixelBit
					 + header1D.streamTypeCnt
					 ;

		headerTag.length	  = ((baseSize + 3) >> 2) <<2;	// Round multiple of 4.

		u8 pad[3] = { 0,0,0 };
		int padding = headerTag.length - baseSize;

		fwrite(&headerTag, sizeof(HeaderBase)	, 1, outFile);
		fwrite(&header1D,  sizeof(Header1D  )   , 1, outFile);

		// 
		fwrite(pZStdType,1,header1D.streamTypeCnt,outFile);
		// 
		fwrite(pZStdStream,1,header1D.streamPixelBit,outFile);
		if (padding) { fwrite(pad, 1, padding, outFile); }

		delete[] pZStdStream;
		delete[] pZStdType;
	}
}

u8* DynamicTileAnalyze(u8* stream, Plane* src, Plane* map, Plane* debug) {
	u8* startStream = stream;
	int w = src->GetWidth();
	int h = src->GetHeight();

	u8 histo[64]; memset(histo,0,sizeof(u8)*64);
	u8 histo256[256];
	u8 values[256];

	int min = 99999;
	int max = -99999;
	int totalPixel = 0;
	int total0 = 0;
	int total1 = 0;

	for (int y = 0; y < h; y+=8) {
		for (int x = 0; x < w; x+=8) {

			int pixelCount = 0;
			memset(histo,0,sizeof(u8)*64);
			memset(histo256,0,sizeof(u8)*256);
			int minV8 = 99999;
			int maxV8 = -99999;
			int minV6 = 99999;
			int maxV6 = -99999;

			// 4x4 => 4 tiles
			bool isOut;
			for (int y2=0; y2 < 8; y2 += 4) {
				for (int x2=0; x2 < 8; x2 += 4) {

					if (map->GetPixelValue(x2+x,y2+y,isOut) == 0) {
						// Extract Histogram.
						for (int iy =0; iy < 4; iy++) {
							for (int ix =0; ix < 4; ix++) {

								int v = src->GetPixelValue(x+x2+ix,y+y2+iy,isOut);

								// Histogram
								int idx = (v+1) >> 2; // Reduce 6 bit,rounding correct.
								if (idx > 63) { idx = 63; } // Special 255 rounding.

								histo[idx]++;
								histo256[v]++;
								values[pixelCount] = idx;
								pixelCount++;

								if (minV8 > v)   { minV8 = v; }
								if (maxV8 < v)   { maxV8 = v; }
								if (minV6 > idx) { minV6 = idx; }
								if (maxV6 < idx) { maxV6 = idx; }
							}
						}
					}
				}
			}

			if (pixelCount > 0) {
				// Per 8x8 tile :
				int unique = 0;
				for (int n=0; n < 64; n++) {
//					printf("%x,",histo[n]);
					if (histo[n] != 0) {
						unique++;
					}
				}
				printf("\n");

				int delta  = (maxV6 - minV6)+1; // 
				int deltaV = (maxV6 - minV6); // 

				const char* type = "6 Bit";
				if (unique <= 33) {
					type = "5 Bit";
					if (unique <= 17) {
						type = "4 Bit";
						if (unique <= 9) {
							type = "3 Bit";
							if (unique <= 4) {
								type = "2 Bit";
							}
						}
					}
				}


				// Necessary bit for encoding...

				// [TODO : Possible trick to 'merge' close colors. (rounding etc..., depending on range, etc...)

				int format = 4; // 6 Bit.
				int range        = 64;
				if (unique <= 32) {
					format = 3; // 5 Bit.
					range = 32;
					if (unique <= 16) {
						format = 2; // 4 bit.
						range = 16;
						if (unique <= 8) {
							format = 1; // 3 bit.
							range = 8;
							if (unique <= 4) {
								format = 0; // 2 Bit.
								range = 4;
							}
						}
					}
				}

				int needCompress = delta > range;
//				printf(" (Pix:%i, Unique:%i , min:%i, max:%i (Delta:%i) %s %s\n",pixelCount,unique, minV6,maxV6,maxV6-minV6, type, needCompress ? "(Compress)" : "Natural" );

				// Fit into linear natural range...
				if (!needCompress) {
					*pType++	= format; // Natural [0..4]
					*pParams++	= minV6;

					switch (format) {
					case 0:
						for (int n=0; n<pixelCount; n++) { tbl1D2Bit[tbl1D2BitCnt++] = values[n] - minV6; }
						break;
					case 1:
						for (int n=0; n<pixelCount; n++) { tbl1D3Bit[tbl1D3BitCnt++] = values[n] - minV6; }
						break;
					case 2:
						for (int n=0; n<pixelCount; n++) { tbl1D4Bit[tbl1D4BitCnt++] = values[n] - minV6; }
						break;
					case 3:
						for (int n=0; n<pixelCount; n++) { tbl1D5Bit[tbl1D5BitCnt++] = values[n] - minV6; }
						break;
					case 4:
						for (int n=0; n<pixelCount; n++) { tbl1D6Bit[tbl1D6BitCnt++] = values[n] - minV6; }
						break;
					}
				} else {
					// 2..5 bit only -> 5..8, (6 bit always natural)
					*pType++	= format + 5; // 

					// Try to compress into LUT.
					u64 thisLutMask = 0;

					u8 normValues[64];

					for (int n=0; n < pixelCount; n++) {
						int idx      = ((values[n] - minV6) * 63) / deltaV; // Normalize Masks.
						normValues[n] = idx;
						thisLutMask |= (1ULL<<idx);
					}

					*pParams++	= minV6;
					*pParams++	= deltaV;

					if (unique <= 4) {
						// 2 Bit
						*pLUTSs++ = findMask(thisLutMask,2);
					} else {
						if (unique <= 8) {
							// 3 Bit
							*pLUTSs++ = findMask(thisLutMask,3);
						} else {
							if (unique <= 16) {
								// 4 Bit
								*pLUTSs++ = findMask(thisLutMask,4);
							} else {
								// 5 Bit
								*pLUTSs++ = findMask(thisLutMask,5);
							}
						}
					}
					
					switch (format) {
					case 0:
						for (int n=0; n<pixelCount; n++) { tbl1D2Bit[tbl1D2BitCnt++] = GetBitIndex(thisLutMask,normValues[n],2); }
						break;
					case 1:
						for (int n=0; n<pixelCount; n++) { tbl1D3Bit[tbl1D3BitCnt++] = GetBitIndex(thisLutMask,normValues[n],3); }
						break;
					case 2:
						for (int n=0; n<pixelCount; n++) { tbl1D4Bit[tbl1D4BitCnt++] = GetBitIndex(thisLutMask,normValues[n],4); }
						break;
					case 3:
						for (int n=0; n<pixelCount; n++) { tbl1D5Bit[tbl1D5BitCnt++] = GetBitIndex(thisLutMask,normValues[n],5); }
						break;
					default:
						printf("ERROR\n");
						break;
					}
				}

#if 0
				// Extract Min-Max, First and Second.
				int idxBestFirst = -1;
				int valBestFirst = -1;
				for (int n=0; n < 64; n++) {
					if (histo[n] >= valBestFirst) { // >= is important !!! Take most RIGHT best value. Override OK !
						valBestFirst = histo[n];
						idxBestFirst = n;
					}
				}

				int firstV = -1;
				int amount = -1;
				for (int sub=0; sub < 4; sub++) {
					int amnt = histo256[sub+(idxBestFirst*4)];
					if (amount < amnt) {
						amount = amnt;
						firstV = sub+(idxBestFirst*4);
					}
				}

				histo[idxBestFirst] = 0; // Reset histogram.

				// Extract Min-Max, First and Second.
				int idxBestSecnd = -1;
				int valBestSecnd = -1;
				for (int n=63; n >= 0; n--) {
					if (histo[n] >= valBestSecnd) { // >= is important !!! Take most LEFT best value. Override OK !
						valBestSecnd = histo[n];
						idxBestSecnd = n;
					}
				}

				histo[idxBestSecnd] = 0; // Reset histogram.

				//
				int minIdx = -1;
				int maxIdx = -1;
				for (int n=0; n < 64; n++) {
					if (histo[n] != 0) { // >= is important !!! Take most RIGHT best value. Override OK !
						if (minIdx < 0) {
							minIdx = n;
						}
						maxIdx = n;
					}
				}
						
				// outputValue = (idxBestFirst << 2) + 2;  (+2 ?) Compute some rounding ?

				// Extract 

			
						/* Quality loss too big.
						int idx2 = v>>3; // 5 bit.
						int v8 = (idx2<<3) | (idx2>>2);
						*/
				//		int idx2 = v>>2; // 6 bit. --> Add Dithering
				//		int v8 = (idx2<<2) | (idx2>>4);
				//		debug->SetPixel(x+x2+ix,y+y2+iy,v8);

				for (int y2=0; y2 < 8; y2 += 4) {
					for (int x2=0; x2 < 8; x2 += 4) {

						if (map->GetPixelValue(x2+x,y2+y,isOut) == 0) {
							// Extract Histogram.
							for (int iy =0; iy < 4; iy++) {
								for (int ix =0; ix < 4; ix++) {

									int v = src->GetPixelValue(x+x2+ix,y+y2+iy,isOut);

									if (!isOut) {
										// Histogram
										int idx = v >> 2; // Reduce 6 bit
										if (idx == idxBestFirst) {
											*stream++ = 0;
											total0++;
											debug->SetPixel(x2+x+ix,y2+y+iy,firstV);
										} else {
											if (idx == idxBestSecnd) {
												*stream++= 1;
												total1++;
												debug->SetPixel(x2+x+ix,y2+y+iy,idxBestSecnd<<2);

											} else {
												// Normalization between min and max over 6 bit. + 2
												// No LUT for now...
												int v = ((idx - minIdx)>>2) + 2;
												*stream++= v;
												debug->SetPixel(x2+x+ix,y2+y+iy,((idx<<2)+1));
											}
										}

										histo[idx]++;
										if (minV8 > v) { minV8 = v; }
										if (maxV8 < v) { maxV8 = v; }

										map->SetPixel(x2+x+ix,y2+y+iy,255);
									}
								}
							}
						}
					}
				}
#endif

				totalPixel += pixelCount;


#if 0
				printf("[%i,%i] (%i)\n",x,y,pixelCount);
				for (int n=0; n < 64; n++) {
					printf("%i,",histo[n]);
				}
				printf("\n");
#endif
			}
		}
	}

	TestCompress(tbl1D6Bit,&tbl1D6Bit[tbl1D6BitCnt]);
	TestCompress(tbl1D5Bit,&tbl1D5Bit[tbl1D5BitCnt]);
	TestCompress(tbl1D4Bit,&tbl1D4Bit[tbl1D4BitCnt]);
	TestCompress(tbl1D3Bit,&tbl1D3Bit[tbl1D3BitCnt]);
	TestCompress(tbl1D2Bit,&tbl1D2Bit[tbl1D2BitCnt]);
	TestCompress(streamType,pType);
	TestCompress(params,pParams);
	TestCompress((u8*)LUTID,(u8*)pLUTSs);

	printf("Plane : %i Pixel (0:%i, 1:%i)\n", totalPixel, total0, total1);
	return stream;
}

#include "dirent.h"
static int lutID		= 0;
DIR * currDir			= NULL;
struct dirent *entry	= NULL;

void EncoderContext::LoadLUT(int posLoading) {
	// Work on tile Y -> Load stats from previous picture.
	if (lutID == 0) {
		currDir = opendir("tile3D");
		if (currDir) {
			entry = readdir(currDir); // Give .
			entry = readdir(currDir); // ..
			entry = readdir(currDir); // 
		}
	}

	if (entry) {
		// Load Tile
		correlationPatternCount3D = posLoading;
		pStats->loc.Reset(original->GetWidth(),original->GetHeight());
		char buffer[1000];
		sprintf(buffer,"tile3D//%s",entry->d_name);
		Load3DPattern(buffer);
	}
}

void EncoderContext::EvalLutEnded() {
	if (pStats) {
		if (entry) {
			// Write the stats...
			FILE* fOut = fopen("logTile3D.txt","a+");
		
			fprintf(fOut,"%i,%i,%i,%i,%i,%i,%s\n",lutID,pStats->loc.tile3DCount,pStats->loc.pixelCount3D_3Bit,pStats->loc.pixelCount3D_4Bit,pStats->loc.pixelCount3D_5Bit,pStats->loc.pixelCount3D_6Bit,entry->d_name);
			// Go next...
			lutID++;
			fclose(fOut);
		}
	}
}

bool EncoderContext::evalLUTComplete() {
	// Ended all evals ?
	entry = readdir(currDir); // 
	if (!entry) {
		closedir(currDir);
		lutID = 0;
		return true;
	} else {
		return false;
	}
}

void EncoderContext::LoadLUT2D(int posLoading) {
	// Work on tile Y -> Load stats from previous picture.
	if (lutID == 0) {
		currDir = opendir("tile2D");
		if (currDir) {
			entry = readdir(currDir); // Give .
			entry = readdir(currDir); // ..
			entry = readdir(currDir); // 
		}
	}

	if (entry) {
		// Load Tile
		correlationPatternCount2D = posLoading;
		pStats->loc.Reset(original->GetWidth(),original->GetHeight());
		char buffer[1000];
		sprintf(buffer,"tile2D//%s",entry->d_name);
		Load2DPattern(buffer);
	}
}

void EncoderContext::EvalLutEnded2D() {
	if (pStats) {
		if (entry) {
			// Write the stats...
			FILE* fOut = fopen("logTile2D.txt","a+");
		
			fprintf(fOut,"%i,%i,%i,%i,%i,%i,%s\n",lutID,pStats->loc.tile3DCount,pStats->loc.pixelCount2D_3Bit,pStats->loc.pixelCount2D_4Bit,pStats->loc.pixelCount2D_5Bit,pStats->loc.pixelCount2D_6Bit,entry->d_name);
			// Go next...
			lutID++;
			fclose(fOut);
		}
	}
}

bool EncoderContext::Convert(const char* source, const char* outputFile, bool dump) {
	bool converted = false;

	FILE* outF = fopen(outputFile, "wb");

	if (outF) {
		outFile = outF;

		fileOutSize = 0;

		// Transfer flag if nessacessary for subroutines.
		this->dumpImage = dump;

		if (evaluateLUT2D) {
			char buffer[1000];
			sprintf(buffer,"mask2d_%s",source);
			mapSmoothTile = Image::LoadPNG(buffer);
		}

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
		if (dump) {
			imgRGB->SavePNG("recoded.png",NULL);
		}

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

		int rejectFactor = 3;
		bool useYCoCgForGradient = false; // false = use RGB. (Saved 1 KB in a sample test)

		PrepareQuadSmooth();

		if (!evaluateLUT2D) {

			// ---------------------------------------------------------------------------------------------------------------------------------------
			//
			// Full RGB Gradient : 16x16,16x8,8*16,8*8,8*4,4*8,4*4 pixels tiles....
			// Progressive decreasing gradation tile size reduce pressure on RGB pixel stream size.
			// And make final stream a bit smaller.
			//
			// ---------------------------------------------------------------------------------------------------------------------------------------
			// RGB 16x16 TILE
			FittingQuadSmooth(rejectFactor, original->GetPlane(0), original->GetPlane(1), original->GetPlane(2), output,useYCoCgForGradient,4,4); 
	//		output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
	//		mapSmoothTile->SavePNG("GradientMap.png",NULL); mappedRGB->SavePNG("RGBField.png",NULL);

			// ---------------------------------------------------------------------------------------------------------------------------------------
			// RGB 16x8 TILE
			FittingQuadSmooth(rejectFactor, original->GetPlane(0), original->GetPlane(1), original->GetPlane(2), output,useYCoCgForGradient,4,3); 
	//		output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
	//		mapSmoothTile->SavePNG("GradientMap.png",NULL); mappedRGB->SavePNG("RGBField.png",NULL);

			// ---------------------------------------------------------------------------------------------------------------------------------------
			// RGB 8x16 TILE
			FittingQuadSmooth(rejectFactor, original->GetPlane(0), original->GetPlane(1), original->GetPlane(2), output,useYCoCgForGradient,3,4); 
	//		output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
	//		mapSmoothTile->SavePNG("GradientMap.png",NULL); mappedRGB->SavePNG("RGBField.png",NULL);

			// ---------------------------------------------------------------------------------------------------------------------------------------
			// RGB 8x8 TILE
			FittingQuadSmooth(rejectFactor, original->GetPlane(0), original->GetPlane(1), original->GetPlane(2), output,useYCoCgForGradient,3,3); 
	//		output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
	//		mapSmoothTile->SavePNG("GradientMap.png",NULL); mappedRGB->SavePNG("RGBField.png",NULL);

			// ---------------------------------------------------------------------------------------------------------------------------------------
			// RGB 8x4 TILE
			FittingQuadSmooth(rejectFactor, original->GetPlane(0), original->GetPlane(1), original->GetPlane(2), output,useYCoCgForGradient,3,2); 
	//		output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
	//		mapSmoothTile->SavePNG("GradientMap.png",NULL); mappedRGB->SavePNG("RGBField.png",NULL);

			// ---------------------------------------------------------------------------------------------------------------------------------------
			// RGB 4x8 TILE
			FittingQuadSmooth(rejectFactor, original->GetPlane(0), original->GetPlane(1), original->GetPlane(2), output,useYCoCgForGradient,2,3); 
	//		output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
	//		mapSmoothTile->SavePNG("GradientMap.png",NULL); mappedRGB->SavePNG("RGBField.png",NULL);

			// ---------------------------------------------------------------------------------------------------------------------------------------
			// RGB 4x4 TILE
			FittingQuadSmooth(rejectFactor, original->GetPlane(0), original->GetPlane(1), original->GetPlane(2), output,useYCoCgForGradient,2,2); 
			if (dump) {
				output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
				mappedRGB->SavePNG("RGBFieldRGB.png",NULL);
			}
	//		mapSmoothTile->SavePNG("GradientMap.png",NULL); mappedRGB->SavePNG("RGBField.png",NULL);

			/* PROTOTYPE TEST : 2x2 tile (irrealist, as it uses too much RGB pixel stream... just for debug/test purpose)
			// ---------------------------------------------------------------------------------------------------------------------------------------
			FittingQuadSmooth(rejectFactor, original->GetPlane(0), original->GetPlane(1), original->GetPlane(2), output,useYCoCgForGradient,1,1); // RGB 2x2 TILE
			output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
			mapSmoothTile->SavePNG("GradientMap.png",NULL); mappedRGB->SavePNG("RGBField.png",NULL);
			*/

			// --------------------------------------------------------------
			// Find 3D Tile matching...
			// --------------------------------------------------------------

	#if 1
			// DumpTileRGB();
			AnalyzeColorCount(original,8);

			evaluateMap = NULL;

			RegisterAndCreate3DLut();
			testedLUT = correlationPatternCount3D;

		nextLutEval:
			if (evaluateLUT) {
				evaluateMap = new Plane(smoothMap->GetWidth(),smoothMap->GetHeight());
				evaluateMap->Clear();
			}

			char EvaluateBuffer[1000];
			if (evaluateLUT) {
				LoadLUT(testedLUT);
				// For each tile and bitmap we test...
				sprintf(EvaluateBuffer,"tile3DMap//%s_%s.png",outputFile,entry->d_name);
			} else {
				// RegisterAndCreate3DLut();
			}

			if (dump) {
				mapSmoothTile->SavePNG("GradientMap.png",NULL);
			}

			StartCorrelationSearch(true);

			// isCaptureMode3D = true;

	//		output->Clear();
			Correlation3DSearch(original, output,4,3); // Test in RGB Space.
			if (dump) {
				output->SavePNG("Post3DTile16_8.png",NULL);
			}

	//		output->Clear();
			Correlation3DSearch(original, output,3,4); // Test in RGB Space.
			if (dump) {
				output->SavePNG("Post3DTile8_16.png",NULL);
			}

	//		output->Clear();
			Correlation3DSearch(original, output,3,3); // Test in RGB Space.
			if (dump) {
				output->SavePNG("Post3DTile8_8.png",NULL);
			}
			isCaptureMode3D = false;


	//		output->Clear();
			Correlation3DSearch(original, output,3,2); // Test in RGB Space.
			if (dump) {
				output->SavePNG("Post3DTile8_4.png",NULL);
			}

	//		output->Clear();
			Correlation3DSearch(original, output,2,3); // Test in RGB Space.
			if (dump) {
				output->SavePNG("Post3DTile4_8.png",NULL);
			}

	//		output->Clear();
			Correlation3DSearch(original, output,2,2); // Test in RGB Space.
			if (dump) {
				output->SavePNG("Post3DTile4_4.png",NULL);
			}

		#if 0 // Tried using YCoCg with 3D Tile
			useYCoCg = true;

	//		output->Clear();
			Correlation3DSearch(original, output,3,2); // Test in RGB Space.
			if (dump) {
				output->SavePNG("Post3DTile8_4.png",NULL);
			}

	//		output->Clear();
			Correlation3DSearch(original, output,2,3); // Test in RGB Space.
			if (dump) {
				output->SavePNG("Post3DTile4_8.png",NULL);
			}

	//		output->Clear();
			Correlation3DSearch(original, output,2,2); // Test in RGB Space.
			if (dump) {
				output->SavePNG("Post3DTile4_4.png",NULL);
			}
	
			useYCoCg = false;
		#endif

			if (evaluateLUT) {
				EvalLutEnded();
				if (!evalLUTComplete()) {
					deleteAllocated3DParts();

					// evaluateMap->SaveAsPNG(EvaluateBuffer);

					delete evaluateMap;
					goto nextLutEval;
				}
				//
				deleteAllocated3DParts();
			} else {
				EndCorrelationSearch(true,7); // Header will be both 8 and 4 pixel tile stream...
			}
		//	Correlation3DSearch(YCoCgImg, output); // Test in YCoCb Space.
	#endif

	#if 0
			// [Dump 3D Histogram]
			FILE* logF = fopen("log3DHisto.txt","wb");
			int cnt = 0;
			for (int z=0; z < 64; z++) {
				for (int y=0; y < 64; y++) {
					for (int x=0; x < 64; x++) {
						int v = histo3D[x + (y<<6) + (z<<12)];
						if (v) {
							fprintf(logF,"%i,%i,%i,%i,%i\n",x,y,z,v);
							cnt++;
						}
					}
				}
			}
			fclose(logF);
	#endif
			//
			// Next Phase IN ORDER : 3D Correlation in RGB space. [RGB][RGB][Tile Type + Swap][Tile encoded 8x8]
			// For now support only Tile Type '0' -> Later add new type (now single LINE)
			//
		} else {
			CheckMipmapMask();
		}

		// ------------------------------------------------------
		//   Now TWO PLANE encoding...
		// ------------------------------------------------------

		// YCoCg -> Lower res works => BETTER. -> Can work on 1/2 or 1/4 CoCg for Co and Cg.
		dump = true;

		//
		// RB,RG,GB Gradient 4 pixels....
		//
		size_t result = 0;
if (0) {
		if (!evaluateLUT && !evaluateLUT2D) {
			FittingQuadSmooth(rejectFactor,
				original->GetPlane(0),
				NULL,
				original->GetPlane(2),
				output,useYCoCgForGradient,2,2);

			if (dump) {
				output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
				mapSmoothTile->SavePNG("RGB16+8+4+RBMap.png",NULL);
				mappedRGB->SavePNG("RGBFieldRB.png",NULL);
			}
		
			FittingQuadSmooth(rejectFactor,
				original->GetPlane(0),
				original->GetPlane(1),
				NULL,
				output,useYCoCgForGradient,2,2);

			if (dump) {
				output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
				mapSmoothTile->SavePNG("RGB16+8+4+RGMap.png",NULL);
				mappedRGB->SavePNG("RGBFieldRG.png",NULL);
			}

			FittingQuadSmooth(rejectFactor,
				NULL,
				original->GetPlane(1),
				original->GetPlane(2),
				output,useYCoCgForGradient,2,2);

			if (dump) {
				output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
				mapSmoothTile->SavePNG("RGB16+8+4+GBMap.png",NULL);
				mappedRGB->SavePNG("RGBFieldGB.png",NULL);
			}

			mapSmoothTile->SavePNG("Solve2D.png",NULL);
		}

		if (!evaluateLUT) {
#if 0
			evaluateMap = NULL;
			correlationPatternCount2D = 0;

//			RegisterAndCreate2DLut();
			testedLUT = correlationPatternCount2D;

		nextLut2DEval:
			if (evaluateLUT2D) {
				evaluateMap = new Plane(mapSmoothTile->GetWidth(),mapSmoothTile->GetHeight());
				evaluateMap->Clear();
			}

			char EvaluateBuffer[1000];
			if (evaluateLUT2D) {
				LoadLUT2D(testedLUT);
			} else {
				RegisterAndCreate2DLut();
			}

			StartCorrelationSearch(false); // 2D Mode RG
			isCaptureMode2D = false;

			// 2D Correlation in RG  space.
			Correlation2DSearch(Mode_RG,original, output,3,3);
			if (dump) {
				output->SavePNG("Post2DTileRG_8_8.png",NULL);
			}
			Correlation2DSearch(Mode_RG,original, output,2,2);
			if (dump) {
				output->SavePNG("Post2DTileRG_4_4.png",NULL);
			}
			if (evaluateLUT2D) {
				deleteAllocated3DParts();
			} else {
				EndCorrelationSearch(false,3); // Header will be both 8 and 4 pixel tile stream...
			}

			StartCorrelationSearch(false); // 2D Mode GB
			// 2D Correlation in GB  space.
			Correlation2DSearch(Mode_GB,original, output,3,3);
			if (dump) {
				output->SavePNG("Post2DTileGB_8_8.png",NULL);
			}
			Correlation2DSearch(Mode_GB,original, output,2,2);
			if (dump) {
				output->SavePNG("Post2DTileGB_4_4.png",NULL);
			}
			if (evaluateLUT2D) {
				deleteAllocated3DParts();
			} else {
				EndCorrelationSearch(false,6); // Header will be both 8 and 4 pixel tile stream...
			}

			StartCorrelationSearch(false); // 2D Mode RB
			// 2D Correlation in RB  space.
			Correlation2DSearch(Mode_RB,original, output,3,3);
			if (dump) {
				output->SavePNG("Post2DTileRB_8_8.png",NULL);
			}
			Correlation2DSearch(Mode_RB,original, output,2,2);
			if (dump) {
				output->SavePNG("Post2DTileRB_4_4.png",NULL);
			}
			isCaptureMode2D = false;

			if (evaluateLUT2D) {
				EvalLutEnded2D();
				if (!evalLUTComplete()) {
					deleteAllocated3DParts(); // Same memory block 2D/3D.

					// evaluateMap->SaveAsPNG(EvaluateBuffer);

					delete evaluateMap;
					goto nextLut2DEval;
				}
				//
				deleteAllocated3DParts(); // Same memory block 2D/3D.
			} else {
				EndCorrelationSearch(false,5); // Header will be both 8 and 4 pixel tile stream...
			}
#endif
		//	Correlation3DSearch(YCoCgImg, output); // Test in YCoCb Space.

			//
			// R,G,B Gradient 4 pixels...
			//

			// Seperate Y,Co,Cg plane.
			// TODO : Remove Tile from Original that were compressed.
			//
#if 0
			FittingQuadSmooth(rejectFactor,
				original->GetPlane(0),
				NULL,
				NULL,
				output,useYCoCgForGradient,2,2);

	//		output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
	//		mapSmoothTile->SavePNG("RGB16+8+4+Y Map.png",NULL);
	//		mappedRGB->SavePNG("RGBField.png",NULL);

			FittingQuadSmooth(rejectFactor,
				NULL,
				original->GetPlane(1),
				NULL,
				output,useYCoCgForGradient,2,2);

			if (dump) {
				output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
				mapSmoothTile->SavePNG("RGB16+8+4+G Map.png",NULL);
				mappedRGB->SavePNG("RGBField.png",NULL);
			}

			FittingQuadSmooth(rejectFactor,
				NULL,
				NULL,
				original->GetPlane(2),
				output,useYCoCgForGradient,2,2);
#endif

			if (dump) {
				output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB.png",NULL);
				mapSmoothTile->SavePNG("RGB16+8+4+B Map.png",NULL);
				mappedRGB->SavePNG("RGBField.png",NULL);
				mapSmoothTile->SavePNG("Map.png",NULL);
				output->ConvertToYCoCg2RGB(useYCoCgForGradient)->SavePNG("outputEncoderRGB_PostSmooth.png",NULL);
				mipmapMask->SaveAsPNG("PostQuadSmooth.png");
				output->SavePNG("outputTileScaled.png",NULL);
		//			output->savePNG("outputEncoder.png",NULL);
		//			output->convertToYCoCg2RGB()->savePNG("outputEncoderRGB.png",NULL);
			}

			char buffer[2000];
			sprintf(buffer,"output\\%s.png",outputFile);
			output->SavePNG(buffer,NULL);

} // End if (0) {

		} else {
			result = 0;
		}

		// Here we switch to another mode for 2 plane/1 plane
		u8* tmpStream = new u8[original->GetWidth() * original->GetHeight() * 3];
		u8* wrtStream = tmpStream;

		output->SavePNG("outputTileScaled.png",NULL);
		mapSmoothTile->SavePNG("Solve2D.png",NULL);

		wrtStream = DynamicTileCompressor(wrtStream, original->GetPlane(0),mapSmoothTile->GetPlane(0),output->GetPlane(0));
		if (dump) {
			output->SavePNG("RDyn.png",NULL);
		}

		wrtStream = DynamicTileCompressor(wrtStream, original->GetPlane(1),mapSmoothTile->GetPlane(1),output->GetPlane(1));
		if (dump) {
			output->SavePNG("GDyn.png",NULL);
		}
		wrtStream = DynamicTileCompressor(wrtStream, original->GetPlane(2),mapSmoothTile->GetPlane(2),output->GetPlane(2));
		if (dump) {
			output->SavePNG("BDyn.png",NULL);
		}

		GenerateDynamicTileChunk(tmpStream, wrtStream-tmpStream);

		delete[] tmpStream;

		if (pStats) {
			int totalGradients = pStats->loc.sizeBlock3DGradient + pStats->loc.sizeBlock2DGradient + pStats->loc.sizeBlock1DGradient;
			int totalLUT       = pStats->loc.sizeBlock2DLUT + pStats->loc.sizeBlock3DLUT;
			int total1DLUT     = result;
			pStats->loc.compressedFileTotal = total1DLUT + totalGradients + totalLUT;
			FILE* fGlobStat = fopen("globalStats.bin","wb");
			if (fGlobStat) {
				fwrite(pStats, sizeof(EncoderStats), 1, fGlobStat);
				fclose(fGlobStat);
			}

			char buffer[2000];
			sprintf(buffer,"%s.log.txt",outputFile);
			fGlobStat = fopen(buffer,"wb");
			if (fGlobStat) {
				fprintf(fGlobStat,"--------------------------\n");
				fprintf(fGlobStat,"totalPixelCount:%i\n"			,pStats->loc.totalPixelCount);
				fprintf(fGlobStat,"pixelCountGradient16_16:%i\n"	,pStats->loc.pixelCountGradient16_16);
				fprintf(fGlobStat,"pixelCountGradient16_8:%i\n"		,pStats->loc.pixelCountGradient16_8);
				fprintf(fGlobStat,"pixelCountGradient8_16:%i\n"		,pStats->loc.pixelCountGradient8_16);
				fprintf(fGlobStat,"pixelCountGradient8_8:%i\n"		,pStats->loc.pixelCountGradient8_8);
				fprintf(fGlobStat,"pixelCountGradient4_8:%i\n"		,pStats->loc.pixelCountGradient4_8);
				fprintf(fGlobStat,"pixelCountGradient8_4:%i\n"		,pStats->loc.pixelCountGradient8_4);
				fprintf(fGlobStat,"pixelCountGradient4_4:%i\n"		,pStats->loc.pixelCountGradient4_4);
				fprintf(fGlobStat,"pixelCount3DGradientTotal:%i\n"	,pStats->loc.pixelCount3DGradientTotal);
				fprintf(fGlobStat,"sizeBlock3DGradient:%i\n"		,pStats->loc.sizeBlock3DGradient);
				fprintf(fGlobStat,"pixelCount3D_Lut_16_8:%i\n"		,pStats->loc.pixelCount3D_Lut_16_8);
				fprintf(fGlobStat,"pixelCount3D_Lut_8_16:%i\n"		,pStats->loc.pixelCount3D_Lut_8_16);
				fprintf(fGlobStat,"pixelCount3D_Lut_8_8:%i\n"		,pStats->loc.pixelCount3D_Lut_8_8);
				fprintf(fGlobStat,"pixelCount3D_Lut_8_4:%i\n"		,pStats->loc.pixelCount3D_Lut_8_4);
				fprintf(fGlobStat,"pixelCount3D_Lut_4_8:%i\n"		,pStats->loc.pixelCount3D_Lut_4_8);
				fprintf(fGlobStat,"pixelCount3D_Lut_4_4:%i\n"		,pStats->loc.pixelCount3D_Lut_4_4);
				fprintf(fGlobStat,"pixelCount3DLUTTotal:%i\n"		,pStats->loc.pixelCount3DLUTTotal);
				fprintf(fGlobStat,"sizeBlock3DLUT:%i\n"				,pStats->loc.sizeBlock3DLUT);
				fprintf(fGlobStat,"pixelCountGradientRG_4_4:%i\n"	,pStats->loc.pixelCountGradientRG_4_4);
				fprintf(fGlobStat,"pixelCountGradientGB_4_4:%i\n"	,pStats->loc.pixelCountGradientGB_4_4);
				fprintf(fGlobStat,"pixelCountGradientRB_4_4:%i\n"	,pStats->loc.pixelCountGradientRB_4_4);
				fprintf(fGlobStat,"pixelCount2DGradientTotal:%i\n"	,pStats->loc.pixelCount2DGradientTotal);
				fprintf(fGlobStat,"sizeBlock2DGradient:%i\n"		,pStats->loc.sizeBlock2DGradient);
				/*
				fprintf(fGlobStat,"pixelCount2D_LutRG_8_8:%i\n"		,pStats->loc.pixelCount2D_LutRG_8_8);
				fprintf(fGlobStat,"pixelCount2D_LutRG_4_4:%i\n"		,pStats->loc.pixelCount2D_LutRG_4_4);
				fprintf(fGlobStat,"pixelCount2D_LutGB_8_8:%i\n"		,pStats->loc.pixelCount2D_LutGB_8_8);
				fprintf(fGlobStat,"pixelCount2D_LutGB_4_4:%i\n"		,pStats->loc.pixelCount2D_LutGB_4_4);
				fprintf(fGlobStat,"pixelCount2D_LutRB_8_8:%i\n"		,pStats->loc.pixelCount2D_LutRB_8_8);
				fprintf(fGlobStat,"pixelCount2D_LutRB_4_4:%i\n"		,pStats->loc.pixelCount2D_LutRB_4_4);
				*/
				fprintf(fGlobStat,"pixelCount2DLUTTotal:%i\n"		,pStats->loc.pixelCount2DLUTTotal);
				fprintf(fGlobStat,"sizeBlock2DLUT:%i\n"				,pStats->loc.sizeBlock2DLUT);
				fprintf(fGlobStat,"pixelCount1DGradientTotal:%i\n"	,pStats->loc.pixelCount1DGradientTotal);
				fprintf(fGlobStat,"sizeBlock1DGradient:%i\n"		,pStats->loc.sizeBlock1DGradient);
				fprintf(fGlobStat,"pixelCount1D:%i\n"				,pStats->loc.pixelCount1D);
				fprintf(fGlobStat,"compressedFileTotal:%i\n"		,pStats->loc.compressedFileTotal);

				fprintf(fGlobStat,"-----Histogram 3D ----------\n");
				for (int n=0; n < 256; n++) {
					if (n!=0) { fprintf(fGlobStat,","); }
					fprintf(fGlobStat,"%i",pStats->loc.histogram3D[n]);
				}
				fprintf(fGlobStat,"\n");
				fprintf(fGlobStat,"-----Histogram 2D ----------\n");
				for (int n=0; n < 2048; n++) {
					if (n!=0) { fprintf(fGlobStat,","); }
					fprintf(fGlobStat,"%i",pStats->loc.histogram2D[n]);
				}
				fprintf(fGlobStat,"\n");
				fclose(fGlobStat);
			}
		}

#if 0
		chromaReduction();

		DynamicTileEncode(false,YCoCgImg->GetPlane(0), outY, false, false, false,false);
		DynamicTileEncode(false,workCo, outCo,true, false, halfCoW,halfCoH);
		DynamicTileEncode(true ,workCg, outCg,false, true, halfCgW,halfCgH);
#endif
		// ---------------------------------------------------------------

#if 0

		int bx = 0;
		int by = 0;
		int tsize = 8;
		bool isOutSide;
		BoundingBox bb;
		bool isIsFirst = true;

		u8* renderBuffer = new u8[64*64*3];
		int tileCnt = 0;

		int totalFileSize = 0;
		int totalTileCoCgCorrelate = 0;

		if ((workCo->GetWidth()!=workCg->GetWidth()) || (workCo->GetWidth()!=workCg->GetWidth())) {
			printf("ERROR. => Can not correlate different size Cg and Co plane.\n");
			while (1) {}
		}
#endif
#if 0
		for (int py = 0; py < workCo->GetHeight(); py += tsize) {
			for (int px = 0; px < workCo->GetWidth(); px += tsize) {
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

					bool isOutside;

					float mapX[64];
					float mapY[64];

					if (smoothMap->GetPixelValue(px,py,isOutside)==0) {

						memset(renderBuffer,0,64*64*3);

						for (int e=0;e<correlationPatternCount; e++) {
							correlationPattern[e].EvaluateStart2D();
						}

						int pixCnt = 0;

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

								mapX[pixCnt] = fa;
								mapY[pixCnt] = fb;

								int ia64 = ((int)(fa*63));
								int ib64 = ((int)(fb*63));

								// printf("%f,%f\n",fa,fb);
								for (int e=0;e<correlationPatternCount; e++) {
									correlationPattern[e].EvaluatePoint2D(ia64,ib64);
								}


								int idx = (ia64 + (ib64*64)) * 3;
								renderBuffer[idx    ] = 255;
								renderBuffer[idx + 1] = 255;
								renderBuffer[idx + 2] = 255;
								pixCnt++;
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

						EvalCtx* evMin = &correlationPattern[0];
						float minScore = 999999999.0f;
						float fStdDev;
						bool found = false;
						int foundE = -1;
						int foundM = -1;
						int diffSum = 99999999999;
						int bestSize = 0;

						for (int e=0;e<correlationPatternCount; e++) {
							float score;
							float stdDev;
							EvalCtx& ev = correlationPattern[e];
							int mode = ev.GetEvaluation2D(score, stdDev);
							bool accept = false;
							int diffSumL;
							EncoderContext::Mode m = computeValues2D(mode, px,py,mapX,mapY,pixCnt,bb,ev,diffSumL);
							int fileCost = (4+3) + 32;
							switch (m) {
							case EncoderContext::ENCODE_3BIT: fileCost += (pixCnt*3);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 3 bit.
								break;
							case EncoderContext::ENCODE_4BIT: fileCost += (pixCnt*4);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 3 bit.
								break;
							case EncoderContext::ENCODE_5BIT: fileCost += (pixCnt*5);	// Type(4 bit), 3 bit (Mode), Bbox(32), Per pix : 3 bit.
								break;
							}

							if ((m != EncoderContext::SKIP_TOO_LOSSY) && diffSumL <= diffSum) {
//								if (minScore > score) {
									minScore = score;
									fStdDev	 = stdDev;
									evMin    = &ev;
									found    = true;
									accept   = true;
									foundE   = e;
									foundM   = mode;
									diffSum  = diffSumL;
									bestSize = fileCost;
//								}
							}
//							printf("[%i] Score %f @%i - ",tileCnt, score, e);
						}
//						printf("\n");

						if (found) {
//							printf("[%i] Mode %i Score %f,%f @%i %s\n",tileCnt,foundM, minScore,fStdDev,foundE, found ? "USE" : "");

							for (int n=0; n < 64*64*3; n+=3) {
								if (evMin->color[0]) { renderBuffer[n  ] = evMin->color[0]; }
								if (evMin->color[1]) { renderBuffer[n+1] = evMin->color[1]; }
								if (evMin->color[2]) { renderBuffer[n+2] = evMin->color[2]; }
							}
							totalFileSize += bestSize;
							totalTileCoCgCorrelate++;
						}

						int err = stbi_write_png(buffer, 64, 64, 3, renderBuffer, 64*3);
//						printf("Tile %i Written\n",tileCnt);
					} else {
						// printf("Skip tile %i\n",tileCnt);
					}
				}
			}
		}
		printf("\n");
#endif

#if 0
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
#endif
	
//		output->ConvertToYCoCg2RGB(doConversionRGB2YCoCg)->SavePNG("outputEncoderRGB.png",NULL);

		delete smoothMap;		smoothMap     = NULL;
		delete mapSmoothTile;	mapSmoothTile = NULL;
		delete mappedRGB;		mappedRGB     = NULL;
		delete mipmapMask;		mipmapMask    = NULL;

		Tag endBlk;
		endBlk.tag32 = 0xDEADBEEF;
		fwrite(&endBlk, sizeof(Tag), 1, outF);
		fclose(outF);
		converted = true;
	}

	return converted;
}
