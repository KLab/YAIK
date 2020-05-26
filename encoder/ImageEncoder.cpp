// ImageEncoder.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "EncoderContext.h"

int main(int argCnt, const char** args)
{
	const char* fileName = 
//	"Muse_33009023.png"		// Girl with pink dress winter.
//	"Aqours_43104009.png"	// Girl sit on water
	"Aqours_43101005.png"	// Girl with Star bg
//	"b_rankup_4300300.png"	// Arabian Nights.
	;

	EncoderContext* pCtx = new EncoderContext(); // Big 4 MB.

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

	ctx.LoadImagePNG(fileName);
	ctx.convert("myTestFile.yaik");
	ctx.Release();

	delete pCtx;
}
