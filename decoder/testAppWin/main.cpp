
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

int main()
{
	// INIT LIBRARY
	u32 amount = YAIK_GetLibraryMemoryAmount(8);
	if (amount != 0) {
		u8* memoryContext = new u8[amount];
		YAIK_LIB lib = YAIK_Init(memoryContext, 8);

		YAIK_SDecodedImage imageInfo;
		FILE* f = fopen("../../encoder/vc_prj/myTestFile.yaik", "rb");
		fseek(f, 0, SEEK_END);
		u32 fileLength = ftell(f);
		fseek(f, 0, SEEK_SET);

		u8* fileData = new u8[fileLength];
		fread(fileData, fileLength, 1, f);
		fclose(f);

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
		for (int n=0; n < 25; n++) 
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
		delete[] memoryContext;
	}
}