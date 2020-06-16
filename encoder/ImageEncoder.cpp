// ImageEncoder.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "EncoderContext.h"

int main(int argCnt, const char** args)
{
	const char* fileNames[] = {
		"Aqours_43101005.png",
		"Aqours_31107015_n_720p.png",
//		"Aqours_31107015_n_2250p.png",
//		"Aqours_41107010_2250p.png",
		"Aqours_41107010_720p.png",
//		"Aqours_41109006_n_2250p.png",
		"Aqours_41109006_n_720p.png",
//		"Aqours_41109006_r2550p.png",
		"Aqours_41109006_r_720p.png",
//		"Aqours_42102005_n_2550p.png",
		"Aqours_42102005_n_720p.png",
//		"Aqours_42103008_2250p.png",
		"Aqours_42103008_720p.png",
		"Aqours_43104009.png",
//		"Aqours_43104009_r_2250p.png",
		"Aqours_43104009_r_720p.png",
	};

	int fileCount = 1;
	const char* fileName; // =  args[1];
	/*
//	"Muse_33009023.png"		// Girl with pink dress winter.
//	"Aqours_43104009.png"	// Girl sit on water
	"Aqours_43101005.png"	// Girl with Star bg
//	"b_rankup_4300300.png"	// Arabian Nights.
	;*/

	EncoderStats    stats;

	EncoderContext* pCtx = new EncoderContext(); // Big 4 MB.

	pCtx->pStats		 = &stats;
	
	EncoderContext& ctx  = *pCtx;
	// ---- Setup ----
	ctx.halfCoW = true;
	ctx.halfCoH = false;
	ctx.downSampleCo = EDownSample::AVERAGE_BOX;

	ctx.halfCgW = true;
	ctx.halfCgH = false;
	ctx.downSampleCg = EDownSample::AVERAGE_BOX;

	ctx.isSwizzling  = true;
	ctx.favorAccuracy= false;
	// ---------------

	ctx.evaluateLUT   = false;// Allow to test in load folder with list of 3d LUT and check result one by one.
	ctx.evaluateLUT2D = false;// Allow to load 2D Mask for faster processing and skip 3D stuff.

	ctx.dumpImage   = false && (!ctx.evaluateLUT);

	for (int n=0; n < fileCount; n++) {
		fileName = fileNames[n];
		ctx.originalName = fileName; // TODO : Never a path... just single name.
		ctx.LoadImagePNG(fileName);

		char buffer[2000];
		sprintf(buffer,"%s.yaik",fileName);
		ctx.Convert(fileName, buffer, ctx.dumpImage);

		printf("=== %s complete. === \n",fileName);
		// Dump LUT result for 300k LUT
	}
	ctx.Release();

	delete pCtx;
}
