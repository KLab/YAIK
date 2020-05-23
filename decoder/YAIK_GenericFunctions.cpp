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
#endif
