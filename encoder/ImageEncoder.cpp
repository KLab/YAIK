// ImageEncoder.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "EncoderContext.h"

#include "..\external\stb_image\stb_image.h"
#include "..\external\stb_image\stb_image_write.h"

bool PaletteCompressor(u8* input, int size, u8* output, u32* maxSize);
bool PaletteCompressorLUT(u8* input, int size, u8* output, u32* maxSize);

void TestCompress(u8* start, u8* end);

void copyPalette(u8* src, int size_, int mode) {
	u8* bufferDst = new u8[size_];
	u8* bufferDst2 = new u8[size_];

	if (mode & 1) {
		// Group color by min/max
		u8* grp1 = bufferDst;
		u8* grp2 = &bufferDst[size_>>1];
		for (int n=0; n < size_; n+=6) {
			*grp1++ = src[n+0];
			*grp1++ = src[n+1];
			*grp1++ = src[n+2];
			*grp2++ = src[n+3];
			*grp2++ = src[n+4];
			*grp2++ = src[n+5];
		}
	} else {
		memcpy(bufferDst,src,size_);
	}

	if (mode & 2) {
		// Seperate RGB stream.
		u8* grp1 = bufferDst2;
		u8* grp2 = &bufferDst2[size_ / 3];
		u8* grp3 = &bufferDst2[(2*size_) / 3];
		for (int n=0; n < size_; n+=3) {
			*grp1++= bufferDst[n+0];
			*grp2++= bufferDst[n+1];
			*grp3++= bufferDst[n+2];
		}
	} else {
		memcpy(bufferDst2,bufferDst,size_);
	}

	TestCompress(bufferDst, &bufferDst[size_]);

	delete[] bufferDst2;
	delete[] bufferDst;

}

void testCompressionPalette() {
	int ch,h;
	int w;
	u8* img = stbi_load("Aqours_43101005.png",&w,&h,&ch,3);

	int w4 = w/4;
	int h4 = h/4;

	u8* minImg = new u8[w4 * h4 * 3];
	u8* maxImg = new u8[w4 * h4 * 3];

	for (int y=0; y < h; y+=4) {
		for (int x=0; x < w; x+=4) {
			int minRGB[3] = {  999,  999,  999 };
			int maxRGB[3] = { -999, -999, -999 };

			for (int ty = 0; ty < 4; ty++) {
				for (int tx = 0; tx < 4; tx++) {
					int pIdx = ((x+tx) + ((y+ty)*w))*3;
					int r = img[pIdx+0];
					int g = img[pIdx+1];
					int b = img[pIdx+2];

					if (r < minRGB[0]) { minRGB[0] = r; }
					if (g < minRGB[1]) { minRGB[1] = g; }
					if (b < minRGB[2]) { minRGB[2] = b; }
					if (r > maxRGB[0]) { maxRGB[0] = r; }
					if (g > maxRGB[1]) { maxRGB[1] = g; }
					if (b > maxRGB[2]) { maxRGB[2] = b; }
				}
			}

			int pWI = ((x>>2) + ((y>>2)*w4))*3; 
			for (int n=0; n < 3; n++) {
				minImg[pWI + n] = minRGB[n];
				maxImg[pWI + n] = maxRGB[n];
			}
		}
	}

	stbi_write_png("minImage.png",w4,h4,3,minImg,w4*3);
	stbi_write_png("maxImage.png",w4,h4,3,maxImg,w4*3);
	
	/*
		1/ Create Code Book.
		2/ Sort Code Book by vector length. (MRU ?)
		3/ Allow to encode a color with a better code, even if it generate an error (+1/-1 bit over original)
		   Mark the color as not being a reference for other color
		4/ Allow to compute new color from other colors with 'jump' offset.
			-> Warning : when creating new color from old color, encoder must take care of error and compensate with proper vector code.

		00
		[0][  Code Book   ] <--- 0..127 Code book possible. (Depend only on REFERENCE color)
		C0
		[1][1][   Offset  ] Select another reference color (-1..-64 colors back max)
		80
		[1][0][000][RGBMsk]+1,2,3 Byte follow depending on mask. Create color from REFERENCE color. (component per component)
		[1][0][001][RGBMsk]+1,2,3 Delta non code book
        
		[1][0][1][5 bit code] Code book extension

	 */
	int palLength;
	u8* dataPalette = stbi_load("palette3d.png",&palLength,&h,&ch,3);
	u32 maxSize = palLength * 3;
	u8* bufferDst = new u8[maxSize];

	/*
	copyPalette(dataPalette, palLength*3,0);
	copyPalette(dataPalette, palLength*3,1);
	copyPalette(dataPalette, palLength*3,2);
	copyPalette(dataPalette, palLength*3,3);
	*/
	/*
	for (int n=0; n < palLength; n+=2) {
		int idx = n*3;
		int deltaR = dataPalette[idx+3] - dataPalette[idx+0]; 
		int deltaG = dataPalette[idx+4] - dataPalette[idx+1]; 
		int deltaB = dataPalette[idx+5] - dataPalette[idx+2];
		printf("D:%i,%i,%i\n",deltaR,deltaG,deltaB);
	}*/
	for (int factor = 255; factor >= 0; factor--) {
	//	int factor  = 127;
		int factor2 = factor;
		for (int n=0; n < palLength; n+=2) {
			int idx = n*3;
			bufferDst[idx+0] = (dataPalette[idx+0] * factor ) / 255; 
			bufferDst[idx+1] = (dataPalette[idx+1] * factor ) / 255; 
			bufferDst[idx+2] = (dataPalette[idx+2] * factor ) / 255; 
			bufferDst[idx+3] = (dataPalette[idx+3] * factor2) / 255; 
			bufferDst[idx+4] = (dataPalette[idx+4] * factor2) / 255; 
			bufferDst[idx+5] = (dataPalette[idx+5] * factor2) / 255; 
		}
		// printf("Factor %i\n",factor);
		TestCompress(bufferDst, &bufferDst[maxSize]);

	}

//	PaletteCompressorLUT(dataPalette,palLength*3,bufferDst,&maxSize);
	delete[] bufferDst;
}

int main(int argCnt, const char** args)
{
	int ret = 1;
	if (argCnt <= 1) {
		printf("Command : ImageEncoder.exe imagename.png\n");
		printf("  Converts and generates imagename.png.yaik as output.\n");
	} else {
		const char* fileName = args[1];

		EncoderContext* pCtx = new EncoderContext(); // Big 4 MB.
		EncoderContext& ctx  = *pCtx;

		// No Stats
	//	EncoderStats    stats;
	//	pCtx->pStats		 = &stats;

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

		ctx.originalName = fileName; // Never a path... just single name.
		bool loaded = ctx.LoadImagePNG(fileName);

		if (loaded) {
			char buffer[2000];
			sprintf(buffer,"%s.yaik",fileName);
			bool completed = ctx.Convert(fileName, buffer, ctx.dumpImage);

			if (completed) {
				ret = 0;
				printf("=== %s complete. === \n",fileName);
			}

			ctx.SetImageToEncode(NULL);
		}

		ctx.Release();
		delete pCtx;
	}

	return ret;
}
