#ifndef YAIK_INTERNAL_FUNC
#define YAIK_INTERNAL_FUNC

#include "../include/YAIK_private.h"

// -----------------------------------------------------------------------------------------------------------------------------------------
//   Decompression Function for Gradient filling.
// -----------------------------------------------------------------------------------------------------------------------------------------
void DecompressGradient16x16(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* pGreen, u8* pBlue, u8 planeBit);
void DecompressGradient8x16	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* pGreen, u8* pBlue, u8 planeBit);
void DecompressGradient16x8	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* pGreen, u8* pBlue, u8 planeBit);
void DecompressGradient8x8	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* pGreen, u8* pBlue, u8 planeBit);
void DecompressGradient8x4	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* pGreen, u8* pBlue, u8 planeBit);
void DecompressGradient4x8	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* pGreen, u8* pBlue, u8 planeBit);
void DecompressGradient4x4	(YAIK_Instance* pInstance, u8* dataUncompressedTilebitmap, u8* dataUncompressedRGB, u8* planeR, u8* pGreen, u8* pBlue, u8 planeBit);

#endif
