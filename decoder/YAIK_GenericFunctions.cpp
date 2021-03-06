#include "YAIK_functions.h"

#ifdef YAIK_DEVEL

//---------------------------------------------------------------------------------------------
#define STBI_ONLY_PNG

#define STB_IMAGE_IMPLEMENTATION
#include "../external/stb_image/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb_image/stb_image_write.h"
//---------------------------------------------------------------------------------------------

void DumpColorMap888Swizzle(const char* name, u8* arrayR, u8* arrayG, u8* arrayB, BoundingBox box) {
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

			exportB[pixIdx*3]    = arrayR[n];
			exportB[pixIdx*3 +1] = arrayG[n]; 		
			exportB[pixIdx*3 +2] = arrayB[n];
		}
	}

	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(name, w, h, 3, exportB, w*3);
	
	delete[] exportB;
}

void DebugRGBAsPng(const char* dump, u8* buffer, int w, int h, int channel) {
#ifdef YAIK_DEVEL
	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(dump, w, h, channel, buffer, w * channel);
#endif
}

void Dump4x4TileMap(const char* name, u8* mapR, u8* mapG, u8* mapB, int w, int h) {
	u8* exportB = new u8[w*h*3];
	memset(exportB,0,w*h*3);

	int tile16X = (w + 3)>>2;
	int tile8Y  = (h + 1)>>1;

	for (int y=0; y<tile8Y; y++) {
		for (int x=0; x<tile16X; x++) {
			int tileInfo = x + (y*tile16X);
			u8 vR = mapR[tileInfo];
			u8 vG = mapG[tileInfo];
			u8 vB = mapB[tileInfo];

			int px = x*4;
			int py = y*2;

			// AB Left Side
			// CD
			int idx = ((px) + ((py)*w)) * 3;
			if (vR & 1) { exportB[idx  ] = 255; }
			if (vG & 1) { exportB[idx+1] = 255; }
			if (vB & 1) { exportB[idx+2] = 255; }

			idx = ((px+1) + ((py)*w)) * 3;
			if (vR & 2) { exportB[idx  ] = 255; }
			if (vG & 2) { exportB[idx+1] = 255; }
			if (vB & 2) { exportB[idx+2] = 255; }

			idx = ((px) + ((py+1)*w)) * 3;
			if (vR & 4) { exportB[idx  ] = 255; }
			if (vG & 4) { exportB[idx+1] = 255; }
			if (vB & 4) { exportB[idx+2] = 255; }

			idx = ((px+1) + ((py+1)*w)) * 3;
			if (vR & 8) { exportB[idx  ] = 255; }
			if (vG & 8) { exportB[idx+1] = 255; }
			if (vB & 8) { exportB[idx+2] = 255; }

			// AB Right Side
			// CD
			idx = ((px+2) + ((py)*w)) * 3;
			if (vR & 16) { exportB[idx  ] = 255; }
			if (vG & 16) { exportB[idx+1] = 255; }
			if (vB & 16) { exportB[idx+2] = 255; }

			idx = ((px+3) + ((py)*w)) * 3;
			if (vR & 32) { exportB[idx  ] = 255; }
			if (vG & 32) { exportB[idx+1] = 255; }
			if (vB & 32) { exportB[idx+2] = 255; }

			idx = ((px+2) + ((py+1)*w)) * 3;
			if (vR & 64) { exportB[idx  ] = 255; }
			if (vG & 64) { exportB[idx+1] = 255; }
			if (vB & 64) { exportB[idx+2] = 255; }

			idx = ((px+3) + ((py+1)*w)) * 3;
			if (vR & 128) { exportB[idx  ] = 255; }
			if (vG & 128) { exportB[idx+1] = 255; }
			if (vB & 128) { exportB[idx+2] = 255; }
		}
	}

	int err = stbi_write_png(name, w, h, 3, exportB, w*3);
	delete[] exportB;
}

#endif

// #define DEBUG_PALETTE

#ifdef DEBUG_PALETTE
	#include <stdio.h>
#endif

void PaletteFullRangeRemapping(u8* inOutData, u8 originalRange, int lengthMultipleOf3) {
	u8* parse = inOutData;
	int t;
	int invMul = originalRange ? ((255<<16) / originalRange) : (255<<16);
	while (parse < &inOutData[lengthMultipleOf3]) {
		t = *parse; *parse++ = (t * invMul) >> 16;
		t = *parse; *parse++ = (t * invMul) >> 16;
		t = *parse; *parse++ = (t * invMul) >> 16;
	}
}

bool PaletteDecompressor(u8* input, int inputSize, int inputBufferSize, u8* output, int outputSize, u8 colorCompression) {
	u8* codeBook     = NULL;
	u8* lastColor    = NULL;
	u8  codeBookSize = *input++;
	int pos          = 1;
	u8* lastRGB		 = &output[outputSize-3];
	u8* inputEnd     = &input[inputBufferSize];
	u8* parse		 = NULL;
	u8* write		 = NULL;

	int t = pos+(128*3);
	pos += codeBookSize*3;
	if (pos > inputSize)       { goto error; } // Valid stream.
	if (t   > inputBufferSize) { goto error; } // Security check for invalid code book indexes.

	// Avoid memcpy, map in place.
	codeBook = input;
	input += codeBookSize*3;

	// All work with intermediate color system for now.

	// first color. (6 Bit !)
	write = output;
	*write++ = *input++;
	*write++ = *input++;
	*write++ = *input++;
#ifdef DEBUG_PALETTE
	printf("Color 0(%i,%i,%i)\n",output[0],output[1],output[2]);
#endif

	int c;
	lastColor = output;

	while (write <= lastRGB) {
		if (input >= inputEnd) { goto error; }
		c = *input++;
		if (c & 0x80) {
			if (c & 0x40) {
				// [1][1][Distance Ref]
				int index = ((c & 0x3F)+2)*(-3);
				lastColor = &write[index];
				// Check read out of buffer memory (can read garbage from Code book, don't care)
				if (lastColor < output) { goto error; }
			} else {
				if (write > lastRGB) { goto error; }

				// Check if read does not go out of source.
				static const u8 bitCount[] = { 0,1,1,2,1,2,2,3 };
				if ((input+bitCount[c&7]) > inputEnd) { goto error; }

				switch ((c>>3) & 7) {
				case 0:
					// [1][0][000][RGBMsk]+1,2,3 Delta non code book from LAST color. => Mostly never used...
					write[0] = lastColor[0] + ((c & 1) ? *input++ : 0);
					write[1] = lastColor[1] + ((c & 2) ? *input++ : 0);
					write[2] = lastColor[2] + ((c & 4) ? *input++ : 0);
#ifdef DEBUG_PALETTE
					printf("Color %i(%i,%i,%i)\n",(write-output) / 3,write[0],write[1],write[2]);
#endif
					break;
				case 1:
					// [1][0][001][RGBMsk]+1,2,3 Byte follow depending on mask. Create color from LAST color. (component per component)
					write[0] = ((c & 1) ? *input++ : lastColor[0]);
					write[1] = ((c & 2) ? *input++ : lastColor[1]);
					write[2] = ((c & 4) ? *input++ : lastColor[2]);
#ifdef DEBUG_PALETTE
					printf("Color %i(%i,%i,%i)\n",(write-output) / 3,write[0],write[1],write[2]);
#endif
					break;
				default:
					// 2..7 : Future extensions.
					goto error;
				}
				lastColor = write;
				write   += 3;
			}
		} else {
			if (write > lastRGB) { goto error; }

			// [0][Code book index]
			// Code book.
			u8* code = &codeBook[(c&0x7F)*3];
			write[0] = lastColor[0] + code[0];
			write[1] = lastColor[1] + code[1];
			write[2] = lastColor[2] + code[2];

#ifdef DEBUG_PALETTE
			printf("Color %i(%i,%i,%i)\n",(write-output) / 3,write[0],write[1],write[2]);
#endif

			lastColor = write;
			write   += 3;
		}
	}

	// Pass 2
	// Fast Conversion back to 0..255 range from [0..ColorCompression]
	PaletteFullRangeRemapping(output,colorCompression,outputSize);

	return true;
error:
	return false;
}
