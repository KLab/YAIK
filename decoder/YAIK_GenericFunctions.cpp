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
	stbi_set_flip_vertically_on_load(false);
	int err = stbi_write_png(dump, w, h, channel, buffer, w * channel);
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
