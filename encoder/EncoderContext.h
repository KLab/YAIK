#ifndef KLB_ENCODER_CTX
#define KLB_ENCODER_CTX

#include "framework.h"

// FILE, fopen,fclose, etc...
#include <stdio.h>

// ------------------------------------------------------------------------------------------
#include "Segments.h"


struct BoundingBox3D {
	s16				x0;
	s16				y0;
	s16				z0;
	s16				x1;
	s16				y1;
	s16				z1;
};

struct EncoderContext {
public:
	EncoderContext()
	:debug1		(NULL)
	,debug2		(NULL)
	,original	(NULL)
	,YCoCgImg	(NULL)
	,mipmapMask	(NULL)
	,workCo		(NULL)
	,workCg		(NULL)
	,workCoTile	(NULL)
	,workCgTile (NULL)
	,workCoTileCode	(NULL)
	,workCgTileCode (NULL)
	,workY		(NULL)
	,workYCode	(NULL)
	,smoothMap	(NULL)
	,smoothBitMap4 (NULL)
	,remainingPixels	(0)
	,outFile	(NULL)
	,corr3D_stream6Bit			(NULL)
	,corr3D_stream5Bit			(NULL)
	,corr3D_stream4Bit			(NULL)
	,corr3D_sizeT8_16Map		(NULL)
	,corr3D_sizeT16_8Map		(NULL)
	,corr3D_sizeT8_8Map			(NULL)
	,corr3D_sizeT8_4Map			(NULL)
	,corr3D_sizeT4_8Map			(NULL)
	,corr3D_sizeT4_4Map			(NULL)
	,corr3D_tileStreamTileType	(NULL)
	,corr3D_colorStream			(NULL)
	{

	}

	// Color block stream is swizzled.
	bool isSwizzling;

	// Favor bigger accurate color table.
	bool favorAccuracy;

	// Encode parameter.
	bool halfCoW;
	bool halfCoH;
	EDownSample downSampleCo;

	bool halfCgW;
	bool halfCgH;
	EDownSample downSampleCg;

	Image* debug1;
	Image* debug2;

	// Mipmap mask bounding box in pixel.
	int mipMapTileSize;
	int boundX0;
	int boundY0;
	int boundX1;
	int boundY1;
	int remainingPixels;

	bool LoadImagePNG		(const char* name);
	void SaveTo2RGB			(bool doConversion, const char* optionnalFileName = NULL);
	void SaveAsYCoCg		(const char* optionnalFileName = NULL);
	void Release			();

	enum PlaneMode {
		Mode_RG = 0,
		Mode_GB = 1,
		Mode_RB = 2
	};

protected:
	int    fileOutSize;
	Image* original;
	Image* YCoCgImg;

	Plane* mipmapMask;

	//
	Plane* workCo;
	Plane* workCg;
	Plane* workCoTile;
	Plane* workCgTile;
	Plane* workCoTileCode;
	Plane* workCgTileCode;

	Plane* workY;
	Plane* workYCode;

	Plane* smoothMap;
//	u8*    smoothRGBMap4;
	Image* mapSmoothTile;
	Image* mappedRGB;
	u8*    smoothBitMap4;

	FILE*  outFile;

	// ----------------------------------------
	void CheckMipmapMask	();
	void PrepareQuadSmooth	();
	void DumpTileRGB		();
	int  FittingQuadSmooth	(int rejectFactor, Plane* a, Plane* b, Plane* c,Image* testOutput, bool useYCoCg, int tileBitSizeX, int tileBitSizeY);

	// In order...
	void MipPrefilter		(bool active);
	void ProcessAlpha		(bool force8Bit);
	void SingleColorOut		(bool active, Image* output);
	void convRGB2YCoCg		(bool notUseRGBAsIs);
	void chromaReduction	();
	void SmoothMap			(Image* output);
	// Analyze gradient...
	void Interpolate		(Image* output, Plane* src, EInterpMode mode, bool isXDouble, bool isYDouble);

	u8*  corr3D_stream6Bit;
	u8*  corr3D_stream5Bit;
	u8*  corr3D_stream4Bit;
	u8*  corr3D_stream3Bit;
//	u8*  corr3D_stream2Bit;

	u8*  corr3D_sizeT8_16Map;
	u8*  corr3D_sizeT16_8Map;
	u8*  corr3D_sizeT8_8Map;
	u8*  corr3D_sizeT8_4Map;
	u8*  corr3D_sizeT4_8Map;
	u8*  corr3D_sizeT4_4Map;
	u16* corr3D_tileStreamTileType;
	u8*  corr3D_colorStream;
//	int stream2BitCnt;
	int stream3BitCnt;
	int stream4BitCnt;
	int stream5BitCnt;
	int stream6BitCnt;
	int streamColorCnt;
	int streamTypeCnt;

	void RegisterAndCreate3DLut();
	void RegisterAndCreate2DLut();
	void StartCorrelationSearch(bool is3D);
	void EndCorrelationSearch(bool is3D);
	void Correlation3DSearch(Image* input, Image* output, int tileSizeX, int tileSizeY);
	void Correlation2DSearch(PlaneMode planeMode,Image* input, Image* output, int tileSizeX, int tileSizeY);

	int	DynamicTileEncode	(bool mode3BitOnly, Plane* plane, Plane* dst, bool isCo, bool isCg, bool isHalfX, bool isHalfY);	// Return bitcount.
	int DynamicTileEncodeCoCg(Plane* source, Plane* Target, Plane* targetCode);

	void ResampleUpCoCg		(const char* optionalRawSave = NULL);

	void AnalyzeColorCount(Image* input, int tileSize);

	enum Mode {
		ENCODE_3BIT = 0,
		ENCODE_4BIT,
		ENCODE_5BIT,
		ENCODE_6BIT,
		//
//		ENCODE_2BIT, DISABLED => [Warning : file need 3 BIT and NOT 2 BIT ANYMORE to store info !!!!]
		SKIP_TOO_LOSSY,
	};

	struct EvalCtxBase {
		EvalCtxBase() {}
		~EvalCtxBase() {}

		int			 equCount;
		int   sampleCount;

		// Common 2D/3D
		s16	  xFactor6Bit[64];
		s16	  yFactor6Bit[64];
		float tFactor6Bit[64];

		s16	  xFactor5Bit[32];
		s16	  yFactor5Bit[32];
		float tFactor5Bit[32];

		s16	  xFactor4Bit[16];
		s16	  yFactor4Bit[16];
		float tFactor4Bit[16];

		s16	  xFactor3Bit[8];
		s16	  yFactor3Bit[8];
		float tFactor3Bit[8];

		// 3D Only but made code easier, does not take much space anyway...
		s16	  zFactor6Bit[64];
		s16	  zFactor5Bit[32];
		s16	  zFactor4Bit[16];
		s16	  zFactor3Bit[8];

		/*
		int	  xFactor2Bit[4];
		int	  yFactor2Bit[4];
		int	  zFactor2Bit[4]; // 3D Only
		float tFactor2Bit[4];
		*/

		float acceptScore;
		int   color[3];

		u8 value6Bit[8*8*2];
		u8 value5Bit[8*8*2];
		u8 value4Bit[8*8*2];
		u8 value3Bit[8*8*2];
//		u8 value2Bit[8*8*2];

		/*
		void Save		(FILE* stream);
		void Load		(FILE* stream);
		*/
	};

	struct EvalCtx2D : EvalCtxBase {
		void BuildTable2D(s16* xFactor, s16* yFactor, float* tFactor, float totalDistance, int elementCount);
		void BuildDistanceField2D();

		LinearEqu2D* equList2D;

		// Evaluation result.
		int   sumDistance2D[8];
		int   distSamples[64][8];
		int	  distanceField2D[64*64];

		void Set2D(float acceptanceScore, LinearEqu2D* list, int countList, int r, int g, int b) {
			equList2D= list;
			equCount = countList;
			color[0] = r;
			color[1] = g;
			color[2] = b;
			acceptScore = acceptanceScore;
			BuildDistanceField2D();
		}

		void Set2DPointCloud(float acceptanceScore, int* ptsXY, int ptsCount) {
			// TODO
		}

		void EvaluateStart2D() {
			for (int n=0; n < 8; n++) {
				sumDistance2D[n] = 0;
			}
			sampleCount = 0;
		}

		u8*	 BinarySave2D	(u8* stream, u8 pattern, Mode mode);

		void EvaluatePoint2D(int x, int y) {
			for (int n=0; n < 8; n++) {
				int idx;
				switch (n) {
				// Bit 0 : Flip X
				// Bit 1 : Flip Y
				// Bit 2 : Swap X/Y
				case 0: idx =     x  + (     y<<6); break;
				case 1: idx = (63-x) + (     y<<6); break;
				case 2: idx =     x  + ((63-y)<<6); break;
				case 3: idx = (63-x) + ((63-y)<<6); break;
				case 4: idx =     y  + (     x<<6); break;
				case 5: idx = (63-y) + (     x<<6); break;
				case 6: idx =     y  + ((63-x)<<6); break;
				case 7: idx = (63-y) + ((63-x)<<6); break;
				}
				int dist = distanceField2D[idx];
				distSamples[sampleCount][n] = dist;
				sumDistance2D[n] += dist;
			}
			sampleCount++;
		}

		int   position6Bit2D[64*64];
		int   position5Bit2D[64*64];
		int   position4Bit2D[64*64];
		int   position3Bit2D[64*64];

		int   GetValue6Bit2D(int x, int y) { return position6Bit2D[x + y*64]; }
		int   GetValue5Bit2D(int x, int y) { return position5Bit2D[x + y*64]; }
		int   GetValue4Bit2D(int x, int y) { return position4Bit2D[x + y*64]; }
		int   GetValue3Bit2D(int x, int y) { return position3Bit2D[x + y*64]; }

		int GetEvaluation2D(float& score, float& stdDeviation) {
			int res = -1;
			float minScore = 999999999.0f;
			for (int f=0; f < 8; f++) {
				float avg = sumDistance2D[f] / (float)(sampleCount*1024.0f);
				float stdDev = 0.0f;
				for (int n=0; n < sampleCount; n++) {
					float diffAvg = avg - (distSamples[n][f]/1024.0f);	
					stdDev += (diffAvg * diffAvg);
				}
				if (avg < minScore) {
					// Return only the best score...
					score = avg;
					minScore = score;
					stdDeviation = stdDev / sampleCount;
					res = f;
				}
			}
			return res;
		}
	};

	struct EvalCtx3D : EvalCtxBase {
		void BuildTable3D(s16* xFactor, s16* yFactor, s16* zFactor, float* tFactor, float totalDistance, int elementCount);
		void BuildDistanceField3D();

		LinearEqu3D* equList3D;

		//
		// 3D
		//
		int   sumDistance3D[48];
		int	  distanceField3D[64*64*64];

		void Set3D(float acceptanceScore, LinearEqu3D* list, int countList, int r, int g, int b) {
			equList3D= list;
			equCount = countList;
			color[0] = r;
			color[1] = g;
			color[2] = b;
			acceptScore = acceptanceScore;
			BuildDistanceField3D();
		}

		void Set3DPointCloud(float acceptanceScore, int* ptsXYZ, int ptsCount) {
			// TODO
		}

		void EvaluateStart3D() {
			for (int n=0; n < 6*8; n++) {
				sumDistance3D[n] = 0;
			}
			sampleCount = 0;
		}

		u8*	 BinarySave3D	(u8* stream, u8 pattern, Mode mode);

		void EvaluatePoint3D(int x, int y, int z) {
			for (int n=0; n < (8*6); n++) {
				int idx;
				int tmp;

				switch (n>>3) {
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

				switch (n & 0x7) {
				// Bit 0 : Flip X
				// Bit 1 : Flip Y
				// Bit 2 : Flip Z
				//
				case 0: idx =     x  + (     y<<6) + (z << 12); break;
				case 1: idx = (63-x) + (     y<<6) + (z << 12); break;
				case 2: idx =     x  + ((63-y)<<6) + (z << 12); break;
				case 3: idx = (63-x) + ((63-y)<<6) + (z << 12); break;
				case 4: idx =     x  + (     y<<6) + ((63-z) << 12); break;
				case 5: idx = (63-x) + (     y<<6) + ((63-z) << 12); break;
				case 6: idx =     x  + ((63-y)<<6) + ((63-z) << 12); break;
				case 7: idx = (63-x) + ((63-y)<<6) + ((63-z) << 12); break;
				}
				int dist = distanceField3D[idx];
				sumDistance3D[n] += dist;
			}
			sampleCount++;
		}

		int   position6Bit3D[64*64*64];
		int   position5Bit3D[64*64*64];
		int   position4Bit3D[64*64*64];
		int   position3Bit3D[64*64*64];
//		int   position2Bit3D[64*64*64];

		int   GetValue6Bit3D(int x, int y, int z) { return position6Bit3D[x + y*64 + (z<<12)]; }
		int   GetValue5Bit3D(int x, int y, int z) { return position5Bit3D[x + y*64 + (z<<12)]; }
		int   GetValue4Bit3D(int x, int y, int z) { return position4Bit3D[x + y*64 + (z<<12)]; }
		int   GetValue3Bit3D(int x, int y, int z) { return position3Bit3D[x + y*64 + (z<<12)]; }
//		int   GetValue2Bit3D(int x, int y, int z) { return position2Bit3D[x + y*64 + (z<<12)]; }

		int GetEvaluation3D(float& score) {
			int res = -1;
			float minScore = 999999999.0f;
			for (int f=0; f < 48; f++) {
				float avg = sumDistance3D[f] / (float)(sampleCount*1024.0f);
				if (avg < minScore) {
					// Return only the best score...
					score = avg;
					minScore = score;
					res = f;
				}
			}
			return res;
		}
	};

	EvalCtx3D	correlationPattern3D[256];
	EvalCtx2D	correlationPattern2D[2048];
	int			correlationPatternCount3D;
	int			correlationPatternCount2D;
	void Create2DCorrelationPatterns();


	Mode computeValues2D(int flipMode, int px,int py, float* mapX, float* mapY, int pixCnt, BoundingBox bb, EvalCtx2D& ev, int& minSumErrDiff);
	Mode computeValues3D(int tileSizeX, int tileSizeY, u8* mask, int flipMode, Image* input,int px,int py, BoundingBox3D bb, EvalCtx3D& ev, int& minSumErrDiff, int* tile6B, int* tile5B, int* tile4B, int* tile3B/*, int* tile2B*/);
	Mode computeValues2D(int planeMode, int tileSizeX, int tileSizeY, u8* mask, int flipMode, Image* input,int px,int py, BoundingBox   bb, EvalCtx2D& ev, int& minSumErrDiff, int* tile6B, int* tile5B, int* tile4B, int* tile3B/*, int* tile2B*/);

public:
	void convert(const char* outputFile);
};

#endif
