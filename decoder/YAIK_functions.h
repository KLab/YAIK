#ifndef YAIK_INTERNAL_FUNC
#define YAIK_INTERNAL_FUNC

#include "../include/YAIK_private.h"

// -----------------------------------------------------------------------------------------------------------------------------------------
//	 Generic Functions for all modules.
// -----------------------------------------------------------------------------------------------------------------------------------------
void SetErrorCode								(YAIK_ERROR_CODE	error);
u8*  AllocateMem								(YAIK_SMemAlloc*	allocCtx, size_t size);
void FreeMem									(YAIK_SMemAlloc*	allocCtx, void*  ptr );
u8*	 DecompressData								(YAIK_Instance*		pCtx    , u8* dataIn , u32 size, u32 expectedSize);

void kassert									(bool cond);

#ifdef YAIK_DEVEL
void DumpColorMap888Swizzle						(const char* name, u8* arrayR, u8* arrayG, u8* arrayB, BoundingBox box);
void DebugRGBAsPng								(const char* dump, u8* buffer, int w, int h, int channel);
#endif

// -----------------------------------------------------------------------------------------------------------------------------------------
//   LUT Loader / Creation
// -----------------------------------------------------------------------------------------------------------------------------------------
bool InitLUT									();
void ReleaseLUT									();

// -----------------------------------------------------------------------------------------------------------------------------------------
//   Default Allocators & Default Image writer function.
// -----------------------------------------------------------------------------------------------------------------------------------------
void*	internal_allocFunc						(void* customContext, size_t size		);
void	internal_freeFunc						(void* customContext, void*  address	);
void	internal_imageBuilderFunc				(struct YAIK_SDecodedImage* userInfo, struct YAIK_SCustomDataSource* sourceImageInternal );

// -----------------------------------------------------------------------------------------------------------------------------------------
//   Decompresson of Mipmap Info.
// -----------------------------------------------------------------------------------------------------------------------------------------
bool Decompress1BitTiled						(YAIK_Instance* pCtx, MipmapHeader* header, u8* data_uncompressed);
bool CreateDefaultMipmap						(YAIK_Instance* pCtx, BoundingBox& bbox);

// -----------------------------------------------------------------------------------------------------------------------------------------
//   Decompression Function for Alpha.
// -----------------------------------------------------------------------------------------------------------------------------------------
bool Decompress1BitMaskAlign8NoMask				(YAIK_Instance* pCtx, BoundingBox& header_bbox, u8* data_uncompressed);
bool Decompress6BitTo8BitAlphaNoMask			(YAIK_Instance* pCtx, AlphaHeader* header, bool useInverseValues, u8* data_uncompressed);
bool Decompress6BitTo8BitAlphaUsingMipmapMask	(YAIK_Instance* pCtx, AlphaHeader* header, bool useInverseValues, u8* data_uncompressed);
bool Decompress8BitTo8BitAlphaNoMask			(YAIK_Instance* pCtx, AlphaHeader* header, u8* data_uncompressed);

// -----------------------------------------------------------------------------------------------------------------------------------------
//   Decompression Function for Gradient filling.
// -----------------------------------------------------------------------------------------------------------------------------------------
void DecompressGradient16x16					(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeR, u8* pGreen, u8* pBlue
												,u8				planeBit);

void DecompressGradient8x16						(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeR, u8* pGreen, u8* pBlue
												,u8				planeBit);

void DecompressGradient16x8						(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeR, u8* pGreen, u8* pBlue
												,u8				planeBit);

void DecompressGradient8x8						(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeR, u8* pGreen, u8* pBlue
												,u8				planeBit);

void DecompressGradient8x4						(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeR, u8* pGreen, u8* pBlue
												,u8				planeBit);

void DecompressGradient4x8						(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeR, u8* pGreen, u8* pBlue
												,u8				planeBit);

void DecompressGradient4x4						(YAIK_Instance* pInstance
												,u8*			dataUncompressedTilebitmap
												,u8*			dataUncompressedRGB
												,u8*			planeR, u8* pGreen, u8* pBlue
												,u8				planeBit);
#endif
