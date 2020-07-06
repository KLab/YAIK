#ifndef YAIK_INTERNAL_FUNC
#define YAIK_INTERNAL_FUNC

#include "../include/YAIK_private.h"

// Optimize Switch-Case performance
#ifndef DEFAULT_UNREACHABLE
	#if defined(__clang__)
		#define DEFAULT_UNREACHABLE		default: __builtin_unreachable();
	#elif defined(__GNUC__) || defined(__GNUG__)
		#define DEFAULT_UNREACHABLE		default: __builtin_unreachable();
	#elif defined(_MSC_VER)
		#define DEFAULT_UNREACHABLE		default: __assume(0);
	#else
		#define DEFAULT_UNREACHABLE     default: break;
	#endif
#endif

// -----------------------------------------------------------------------------------------------------------------------------------------
//	 Generic Functions for all modules.
// -----------------------------------------------------------------------------------------------------------------------------------------
void SetErrorCode								(YAIK_ERROR_CODE	error);
u8*  AllocateMem								(YAIK_SMemAlloc*	allocCtx, size_t size);
void FreeMem									(YAIK_SMemAlloc*	allocCtx, void*  ptr );
// Decompress the input data, allocate buffer to store the result, set error code if failure (+ return NULL)
u8*	 DecompressData								(YAIK_Instance*		pCtx    , u8* dataIn , u32 size, u32 expectedSize);
// Boundingbox is inside the image.
bool CheckInBound2D								(YAIK_Instance*     pCtx    , BoundingBox& bbox);

void kassert									(bool cond);

#ifdef YAIK_DEVEL
void DumpColorMap888Swizzle						(const char* name, u8* arrayR, u8* arrayG, u8* arrayB, BoundingBox box);
void DebugRGBAsPng								(const char* dump, u8* buffer, int w, int h, int channel);
void Dump4x4TileMap								(const char* name, u8* mapR, u8* mapG, u8* mapB,int w, int h);
#endif

// -----------------------------------------------------------------------------------------------------------------------------------------
//   Default Allocators & Default Image writer function.
// -----------------------------------------------------------------------------------------------------------------------------------------
void*	internal_allocFunc						(void* customContext, size_t size		);
void	internal_freeFunc						(void* customContext, void*  address	);
void	internal_imageBuilderFunc				(struct YAIK_SDecodedImage* userInfo, struct YAIK_SCustomDataSource* sourceImageInternal );

// -----------------------------------------------------------------------------------------------------------------------------------------
//   Decompresson of Mipmap Info.
// -----------------------------------------------------------------------------------------------------------------------------------------
bool Decompress1BitTiled						(YAIK_Instance* pCtx, MipmapHeader* header, u8* data_uncompressed, u32 data_length);
bool CreateDefaultMipmap						(YAIK_Instance* pCtx, BoundingBox& bbox);

// -----------------------------------------------------------------------------------------------------------------------------------------
//   Decompression Function for Alpha.
// -----------------------------------------------------------------------------------------------------------------------------------------
bool Decompress1BitMaskAlign8NoMask				(YAIK_Instance* pCtx, BoundingBox& header_bbox, u8* data_uncompressed, u32 data_size);
bool Decompress6BitTo8BitAlphaNoMask			(YAIK_Instance* pCtx, AlphaHeader* header, bool useInverseValues, u8* data_uncompressed, u32 data_size);
bool Decompress6BitTo8BitAlphaUsingMipmapMask	(YAIK_Instance* pCtx, AlphaHeader* header, bool useInverseValues, u8* data_uncompressed, u32 data_size);
bool Decompress8BitTo8BitAlphaNoMask			(YAIK_Instance* pCtx, AlphaHeader* header, u8* data_uncompressed, u32 data_size);

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

// -----------------------------------------------------------------------------------------------------------------------------------------
//   3D LUT TILE
// -----------------------------------------------------------------------------------------------------------------------------------------

// Parameters to pass to the function and update from function.
struct TileParam {
	u8*  stream3Bit;
	u8*  stream4Bit;
	u8*  stream5Bit;
	u8*  stream6Bit;
	u16* tileStream;
	u8*  colorStream;
	u8*  currentMap;
};

void Tile3D_16x8(YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
void Tile3D_8x16(YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
void Tile3D_8x8 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
void Tile3D_8x4 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
void Tile3D_4x8 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
void Tile3D_4x4 (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);

/* 
   2 Component-only processing is deprecated for now. But keep in code base for future research/modification
// -----------------------------------------------------------------------------------------------------------------------------------------
//   2D LUT TILE
// -----------------------------------------------------------------------------------------------------------------------------------------

void Tile2D_8x8_RG (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
void Tile2D_4x4_RG (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
void Tile2D_8x8_GB (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
void Tile2D_4x4_GB (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
void Tile2D_8x8_RB (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
void Tile2D_4x4_RB (YAIK_Instance* pInstance, HeaderTile3D* pHeader, TileParam* param, u8** TBLLUT);
*/

// -----------------------------------------------------------------------------------------------------------------------------------------
//   1D PLANE DECOMPRESSION
// -----------------------------------------------------------------------------------------------------------------------------------------

void Decompress1D  (YAIK_Instance* pInstance, u8** pTypeDecomp, u8** pPixStreamDecomp, u8 planeID, Header1D* pHeader);

// -----------------------------------------------------------------------------------------------------------------------------------------
//   Generic Math Bit Macro to make code a bit clearer
// -----------------------------------------------------------------------------------------------------------------------------------------

void PaletteFullRangeRemapping(u8* inOutData, u8 originalRange, int lengthMultipleOf3);

// Round and divide by 2 to 64 range.
#define	RDIV2(a)		(((a)+ 1)>>1)
#define	RDIV4(a)		(((a)+ 3)>>2)
#define	RDIV8(a)		(((a)+ 7)>>3)
#define	RDIV16(a)		(((a)+15)>>4)
#define	RDIV32(a)		(((a)+31)>>5)
#define	RDIV64(a)		(((a)+63)>>6)

// Modulo
#define MOD2(a)			((a)& 1)
#define MOD4(a)			((a)& 3)
#define MOD8(a)			((a)& 7)
#define MOD16(a)		((a)& 15)
#define MOD32(a)		((a)& 31)

// Div
#define DIV2(a)			((a)>>1)
#define DIV4(a)			((a)>>2)
#define DIV8(a)			((a)>>3)
#define DIV16(a)		((a)>>4)
#define DIV32(a)		((a)>>5)
#define DIV64(a)		((a)>>6)
#define DIV128(a)		((a)>>7)
#define DIV256(a)		((a)>>8)

// Mul
#define MUL2(a)			((a)<<1)
#define MUL4(a)			((a)<<2)
#define MUL8(a)			((a)<<3)
#define MUL16(a)		((a)<<4)
#define MUL32(a)		((a)<<5)
#define MUL64(a)		((a)<<6)
#define MUL128(a)		((a)<<7)
#define MUL256(a)		((a)<<8)

#endif
