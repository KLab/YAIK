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
	,corr3D_stream5Bit			(NULL)
	,corr3D_stream4Bit			(NULL)
	,corr3D_stream3Bit			(NULL)
	,corr3D_sizeT8Map			(NULL)
	,corr3D_sizeT4Map			(NULL)
	,corr3D_tileStreamTileType	(NULL)
	,corr3D_colorStream			(NULL)
	{}

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





	u8*  corr3D_stream5Bit;
	u8*  corr3D_stream4Bit;
	u8*  corr3D_stream3Bit;
	u8*  corr3D_sizeT8Map;
	u8*  corr3D_sizeT4Map;
	u16* corr3D_tileStreamTileType;
	u8*  corr3D_colorStream;
	int stream3BitCnt;
	int stream4BitCnt;
	int stream5BitCnt;
	int streamColorCnt;
	int streamTypeCnt;

	void RegisterAndCreate3DLut();
	void StartCorrelationSearch3D();
	void EndCorrelationSearch3D();
	void Correlation3DSearch(Image* input, Image* output, int tileSizeX, int tileSizeY);

	int	DynamicTileEncode	(bool mode3BitOnly, Plane* plane, Plane* dst, bool isCo, bool isCg, bool isHalfX, bool isHalfY);	// Return bitcount.
	int DynamicTileEncodeCoCg(Plane* source, Plane* Target, Plane* targetCode);

	void ResampleUpCoCg		(const char* optionalRawSave = NULL);

	void AnalyzeColorCount(Image* input, int tileSize);

	struct EvalCtx {
		EvalCtx() {}
		~EvalCtx() {}

		void Set2D(float acceptanceScore, LinearEqu2D* list, int countList, int r, int g, int b) {
			equList2D= list;
			equList3D= NULL;
			equCount = countList;
			color[0] = r;
			color[1] = g;
			color[2] = b;
			acceptScore = acceptanceScore;
			BuildDistanceField2D();
		}

		void Set3D(float acceptanceScore, LinearEqu3D* list, int countList, int r, int g, int b) {
			equList2D= NULL;
			equList3D= list;
			equCount = countList;
			color[0] = r;
			color[1] = g;
			color[2] = b;
			acceptScore = acceptanceScore;
			BuildDistanceField3D();
		}

		int   GetValue5Bit2D(int x, int y) { return position5Bit2D[x + y*64]; }
		int   GetValue4Bit2D(int x, int y) { return position4Bit2D[x + y*64]; }
		int   GetValue3Bit2D(int x, int y) { return position3Bit2D[x + y*64]; }

		int   GetValue5Bit3D(int x, int y, int z) { return position5Bit3D[x + y*64 + (z<<12)]; }
		int   GetValue4Bit3D(int x, int y, int z) { return position4Bit3D[x + y*64 + (z<<12)]; }
		int   GetValue3Bit3D(int x, int y, int z) { return position3Bit3D[x + y*64 + (z<<12)]; }

		LinearEqu2D* equList2D;
		LinearEqu3D* equList3D;
		int			 equCount;

		int   sampleCount;

		// Evaluation result.
		int   sumDistance2D[8];
		int   distSamples[64][8];
		int	  distanceField2D[64*64];

		int   position5Bit2D[64*64];
		int   position4Bit2D[64*64];
		int   position3Bit2D[64*64];

		//
		// 3D
		//
		int   sumDistance3D[48];
		int	  distanceField3D[64*64*64];

		int   position5Bit3D[64*64*64];
		int   position4Bit3D[64*64*64];
		int   position3Bit3D[64*64*64];

		// Common 2D/3D
		int	  xFactor5Bit[64];
		int	  yFactor5Bit[64];
		int	  zFactor5Bit[64]; // 3D Only
		float tFactor5Bit[64];

		int	  xFactor4Bit[32];
		int	  yFactor4Bit[32];
		int	  zFactor4Bit[32]; // 3D Only
		float tFactor4Bit[32];

		int	  xFactor3Bit[16];
		int	  yFactor3Bit[16];
		int	  zFactor3Bit[16]; // 3D Only
		float tFactor3Bit[16];

		float acceptScore;
		int   color[3];

		void EvaluateStart2D() {
			for (int n=0; n < 8; n++) {
				sumDistance2D[n] = 0;
			}
			sampleCount = 0;
		}

		void EvaluateStart3D() {
			for (int n=0; n < 6*8; n++) {
				sumDistance3D[n] = 0;
			}
			sampleCount = 0;
		}

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

		void EvaluatePoint3D(int x, int y, int z) {
			for (int n=0; n < (8*6); n++) {
				int idx;
				int tmp;
				int tmp2;

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

		u8 value5Bit[8*8*2];
		u8 value4Bit[8*8*2];
		u8 value3Bit[8*8*2];

	private:
		void BuildDistanceField2D();
		void BuildDistanceField3D();
	};

	EvalCtx	correlationPattern[256];
	int     correlationPatternCount;
	void Create2DCorrelationPatterns();

	enum Mode {
		ENCODE_5BIT,
		ENCODE_4BIT,
		ENCODE_3BIT,
		SKIP_TOO_LOSSY,
	};


	Mode computeValues2D(int flipMode, int px,int py, float* mapX, float* mapY, int pixCnt, BoundingBox bb, EvalCtx& ev, int& minSumErrDiff);
	Mode computeValues3D(int tileSizeX, int tileSizeY, u64* mask, int flipMode, Image* input,int px,int py, BoundingBox3D bb, EvalCtx& ev, int& minSumErrDiff, int* tile5B, int* tile4B, int* tile3B);


public:
	void convert(const char* outputFile);
};

#endif
