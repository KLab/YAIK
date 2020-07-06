
// TODO : Bring stb_image
// TODO : Bring ZStd
// TODO : Write 1 bit to 8 bit conversion.
//        Code to dump intermediate decoding result to 

double PCFreq = 0.0;
__int64 CounterStart = 0;

#include <Windows.h>
#include <stdio.h>
#include "../../include/YAIK_private.h"

#ifndef YAIK_DEVEL 
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#define STBI_ONLY_PNG
#include "../../external/stb_image/stb_image.h"
#include "../../external/stb_image/stb_image_write.h"

void StartCounter()
{
    LARGE_INTEGER li;
    if(!QueryPerformanceFrequency(&li))
    printf("QueryPerformanceFrequency failed!\n");

    PCFreq = double(li.QuadPart)/1000.0;

    QueryPerformanceCounter(&li);
    CounterStart = li.QuadPart;
}
double GetCounter()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return double(li.QuadPart-CounterStart)/PCFreq;
}

u8* LoadFile(const char* name, u32& size) {
	FILE* f = fopen(name, "rb");
	fseek(f, 0, SEEK_END);
	u32 fileLength = ftell(f);
	fseek(f, 0, SEEK_SET);

	u8* fileData = new u8[fileLength];
	fread(fileData, fileLength, 1, f);
	size = fileLength;
	fclose(f);
	return fileData;
}

int main()
{
	// INIT LIBRARY
	YAIK_LIB lib = YAIK_Init(8,NULL);
	if (lib) {
		u32 LUTSize3D;
		u32 LUTSize2D;
		u8* LUTData3D = LoadFile("../../encoder/vc_prj/LutFile.lut",LUTSize3D);
//		u8* LUTData2D = LoadFile("../../encoder/vc_prj/LutFile2D.lut",LUTSize2D);
		StartCounter();
		YAIK_AssignLUT(lib,LUTData3D,LUTSize3D);
//		YAIK_AssignLUT(lib,LUTData2D,LUTSize2D);

		printf("Assign LUT : Millisecond %f\n",(float)GetCounter());


		YAIK_SDecodedImage imageInfo;
		u32 fileLength;
		u8* fileData = LoadFile("../../encoder/vc_prj/Aqours_43101005.png.yaik",fileLength);
//		u8* pngFileData;
//		u32 pngFileLength;
		{
//			FILE* f = fopen("Aqours_43101005.png", "rb");
//			fseek(f, 0, SEEK_END);
//			pngFileLength = ftell(f);
//			fseek(f, 0, SEEK_SET);

//			pngFileData = new u8[pngFileLength];
//			fread(pngFileData, pngFileLength, 1, f);
//			fclose(f);
		}

		int n = 0;
		for (int n=0; n < 1; n++) 
		{
			printf("---%i----\n",n);

			if (YAIK_DecodeImagePre(lib, fileData, fileLength, &imageInfo)) {

				int imgSize = imageInfo.width * imageInfo.height * (imageInfo.hasAlpha ? 4 : 3);
				u8* destinationRGB = new u8[imgSize];
				memset(destinationRGB, 0x0, imgSize );

				imageInfo.outputImage			= destinationRGB;
				imageInfo.outputImageStride		= imageInfo.width * (imageInfo.hasAlpha ? 4 : 3);

				StartCounter();

				YAIK_DecodeImage(fileData, fileLength, &imageInfo);
				printf("Millisecond %f\n",(float)GetCounter());

//				StartCounter();
//				int x,y,c;
//				u8* buffer = stbi_load_from_memory(pngFileData,pngFileLength,&x,&y,&c,3);
//				printf("P %i %i %f\n",buffer[0],buffer[imageInfo.width * imageInfo.height * 3 - 1],(float)GetCounter());
//				int err = stbi_write_png("test.png", imageInfo.width, imageInfo.height, imageInfo.hasAlpha ? 4 : 3 , buffer, imageInfo.width * imageInfo.hasAlpha ? 4 : 3);

//				delete[] buffer;

				delete[] destinationRGB;
			}
		};

		// END LIFE CYCLE OF LIBRARY.
		YAIK_Release(lib);
	}
}
