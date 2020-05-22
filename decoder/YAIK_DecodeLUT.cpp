#include "../include/YAIK_private.h"

#include <math.h>

// -------------------- TEMPORARY STUFF -----------------
//#ifdef CHECK_MEM
#include <Windows.h>
// Needed to _CrtCheckMemory ?!
#include <iostream>
#define CHECK_MEM		_CrtCheckMemory();
//#endif
// ------------------------------------------------------

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

	uint8_t LUTLinear3B[ 8];
	uint8_t LUTLinear4B[16];
	uint8_t LUTExp3B   [ 8];
	uint8_t LUTExp4B   [16];
	uint8_t LUTLog3B   [ 8];
	uint8_t LUTLog4B   [16];
	uint8_t LUTSlope3B [ 8];
	uint8_t LUTSlope4B [16];

	inline int	GetModeCount	() { return 6; }
	inline uint8_t* GetTable		(int mode, int& count, int& bitCount) {
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

#include <stdio.h>
void assert(bool cond) {
	if (!cond) {
		printf("assert\n");
		while (true) {};
	}
}

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
	if ((min_ < 0) || (max_ <= min_) || (max_ > 255)) {
		invalidTable = true;
		return;
	}

	assert(min_ >=    0);
	assert(max_ >  min_);
	assert(max_ <=  255);

	if (min_ > MAX_BASE_RANGE) { min_ = MAX_BASE_RANGE; }
	if (max_ > 255) { max_ = 255; }
	int diff = max_ - min_;
	if (diff < 16) {
		diff = 16;
		max_ = min_ + diff;
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

		float outLinear   = LinearNormV * DistNormF;
		LUTLinear4B[input] = (u8)(BN + outLinear);	// Table 8 Bit fixed point. 

		float outExp      = ExpNormV * DistNormF;
		LUTExp4B   [input] = (u8)(BN + outExp);		// Table 8 Bit fixed point. 

		float outLog	  = LogNormV * DistNormF;
		LUTLog4B   [input] = (u8)(BN + outLog);		// Table 8 Bit fixed point. 
	}

	// 3 Bit Table
	for (int input = 0; input < 8; input++) {
		float pos = input / 7.0f;

		float LinearNormV = pos;
		float ExpNormV = powf(pos, 1.4f);
		float LogNormV = 1.0f - powf((1.0f - pos), 1.4f);

		//		float 
		float outLinear = LinearNormV * DistNormF;
		LUTLinear3B	[input] = (u8)(BN + outLinear);	// Table 8 Bit fixed point. 

		float outExp = ExpNormV * DistNormF;
		LUTExp3B	[input] = (u8)(BN + outExp);	// Table 8 Bit fixed point. 

		float outLog = LogNormV * DistNormF;
		LUTLog3B	[input] = (u8)(BN + outLog);	// Table 8 Bit fixed point. 
	}

	invalidTable = false;
}


DynamicTile	fullTables[256*256];
uint8_t		fullTableMap4Bit[32768*16];
uint8_t		fullTableMap3Bit[32768*16];

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
			int idx = (minV*256) + maxV;
			DynamicTile& pT = fullTables[idx];
			pT.buildTable(minV, maxV);
			if (!pT.invalidTable) {
				for (int t = 0; t < 4; t++) {
					int code  = EncodeTileType(t,fullTables[idx].distance6Bit,fullTables[idx].base7Bit);
					u8* tbl = NULL;
					switch (t) {
					case 0: tbl = &pT.LUTLinear4B[0];  break;
					case 1: tbl = &pT.LUTLog4B[0];     break;
					case 2: tbl = &pT.LUTExp4B[0];     break;
					case 3: tbl = &pT.LUTSlope4B[0];   break;
					}

					u8* tbl3 = NULL;
					switch (t) {
					case 0: tbl3 = &pT.LUTLinear3B[0]; break;
					case 1: tbl3 = &pT.LUTLog3B[0];    break;
					case 2: tbl3 = &pT.LUTExp3B[0];    break;
					case 3: tbl3 = &pT.LUTSlope3B[0];  break;
					}
					if (code > 32768) {
						printf("ERROR");
						while (1) {};
					}
					memcpy(&fullTableMap4Bit[code*16],tbl,16);
					memcpy(&fullTableMap3Bit[code*16],tbl3,16);
				}
			}
		}
	}

}

unsigned char* GetLUTBase4Bit(int planeType) {
	return fullTableMap4Bit;
}

unsigned char* GetLUTBase3Bit(int planeType) {
	return fullTableMap3Bit;
}

void InitLUT() {
	CHECK_MEM;
	DynamicTileEncoderTable();
	CHECK_MEM;
}
