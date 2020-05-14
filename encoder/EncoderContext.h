#ifndef KLB_ENCODER_CTX
#define KLB_ENCODER_CTX

#include "framework.h"

// FILE, fopen,fclose, etc...
#include <stdio.h>

// ------------------------------------------------------------------------------------------
#include "Segments.h"

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
	,smoothRGBMap4 (NULL)
	,remainingPixels	(0)
	,outFile	(NULL)
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
	u8*    smoothRGBMap4;
	u8*    smoothBitMap4;

	FILE*  outFile;

	// ----------------------------------------
	void CheckMipmapMask	();
	void PrepareQuadSmooth	();
	int  FittingQuadSmooth	(int rejectFactor, Image* testOutput, int tileBitSize);

	// In order...
	void MipPrefilter		(bool active);
	void ProcessAlpha		(bool force8Bit);
	void SingleColorOut		(bool active, Image* output);
	void convRGB2YCoCg		(bool notUseRGBAsIs);
	void chromaReduction	();
	void SmoothMap			(Image* output);
	// Analyze gradient...
	void Interpolate		(Image* output, Plane* src, EInterpMode mode, bool isXDouble, bool isYDouble);

	int	DynamicTileEncode	(bool mode3BitOnly, Plane* plane, Plane* dst, bool isCo, bool isCg, bool isHalfX, bool isHalfY);	// Return bitcount.
	int DynamicTileEncodeCoCg(Plane* source, Plane* Target, Plane* targetCode);

	void ResampleUpCoCg		(const char* optionalRawSave = NULL);


	struct EvalCtx {
		EvalCtx() {}
		~EvalCtx() {}

		void Set(float acceptanceScore, LinearEqu2D* list, int countList, int r, int g, int b) {
			equList  = list;
			equCount = countList;
			color[0] = r;
			color[1] = g;
			color[2] = b;
			acceptScore = acceptanceScore;
			BuildDistanceField();
		}

		int   GetValue5Bit(int x, int y) { return position5Bit[x + y*64]; }
		int   GetValue4Bit(int x, int y) { return position4Bit[x + y*64]; }
		int   GetValue3Bit(int x, int y) { return position3Bit[x + y*64]; }

		LinearEqu2D* equList;
		int			 equCount;

		// Evaluation result.
		int   sumDistance[8];
		int   sampleCount;
		int   distSamples[64][8];
		int	  distanceField[64*64];

		int   position5Bit[64*64];
		int   position4Bit[64*64];
		int   position3Bit[64*64];

		int	  xFactor5Bit[64];
		int	  yFactor5Bit[64];
		float tFactor5Bit[64];

		int	  xFactor4Bit[32];
		int	  yFactor4Bit[32];
		float tFactor4Bit[32];

		int	  xFactor3Bit[16];
		int	  yFactor3Bit[16];
		float tFactor3Bit[16];

		float acceptScore;
		int   color[3];

		void EvaluateStart() {
			for (int n=0; n < 8; n++) {
				sumDistance[n] = 0;
			}
			sampleCount = 0;
		}

		void EvaluatePoint(int x, int y) {
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
				int dist = distanceField[idx];
				distSamples[sampleCount][n] = dist;
				sumDistance[n] += dist;
			}
			sampleCount++;
		}

		int GetEvaluation(float& score, float& stdDeviation) {
			int res = -1;
			float minScore = 999999999.0f;
			for (int f=0; f < 8; f++) {
				float avg = sumDistance[f] / (float)(sampleCount*1024.0f);
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

	private:
		void BuildDistanceField(); 
	};

	EvalCtx	correlationPattern[256];
	int     correlationPatternCount;
	void Create2DCorrelationPatterns();

	void computeValues(int mode, int px,int py, float* mapX, float* mapY, int pixCnt, BoundingBox bb, EvalCtx& ev);

public:
	void convert(const char* outputFile);
};

#endif
