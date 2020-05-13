#include "framework.h"

// FILE, fopen,fclose, etc...
#include <stdio.h>

// ------------------------------------------------------------------------------------------

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

public:
	void convert(const char* outputFile);
};
