//--------------------------------------------------------------------------------------------------------------------
//  [Internal header of YAIK Library]
//  Those definitions are shared between the encoder and decoder.
//	- Structures for file format and associated header.
//	- Decoder Internal Context.
//
//  Inside the library we are free to use C++ stuff.
//--------------------------------------------------------------------------------------------------------------------

#ifndef YAIK_PRIVATE_HEADER
#define YAIK_PRIVATE_HEADER

#include "YAIK.h"

struct BoundingBox {
	s16				x;
	s16				y;
	s16				w;
	s16				h;
};

//--------------------------------------------------------------------------------------------------------------------
//   Context Management
//--------------------------------------------------------------------------------------------------------------------

struct YAIK_Instance {
	YAIK_SMemAlloc	allocCtx;
	void*			decompCtx;
	void*			srcCheck;
	u32				srcLength;

	u8*				mipMapMask;		// Current Mask during decompression.
	u8*				mapRGB;
	u8*				mapRGBMask;
	u8*				tile4x4Mask;
	u32				sizeMapMask;
	u32				tile4x4MaskSize;
	BoundingBox		maskBBox;

	u8*				alphaChannel;			// 8 Bit Alpha Channel
	u8*				planeY;
	u8*				planeCo;
	u8*				planeCg;
	u8*				planeR;
	u8*				planeG;
	u8*				planeB;
	u16				width;
	u16				height;
	u16				tileWidth;
	u16				tileHeight;
	u16				strideRGBMap;
	bool			isRGBA;
	bool			singleRGB;
};

struct YAIK_Library {
	// Handle the number of decoding context...
	s32				totalSlotCount;
	s32				freeSlotCount;
	// List of slot available.
	YAIK_Instance*	freeStack[257]; // API Limit to u8 type max of multithreaded decode context.
	// Allocate slots.
	YAIK_Instance*	instances;
	u8*				LUT3D_BitFormat[4];
	YAIK_SMemAlloc	libraryAllocator;
	YAIK_Instance*	AllocInstance	();
	void			FreeInstance	(YAIK_Instance* inst);
};

//--------------------------------------------------------------------------------------------------------------------
//   Internal File Data Structures : LUT HEADER FILES
//--------------------------------------------------------------------------------------------------------------------

struct LUTHeader {
	u8 lutH[4];
	u8 version;
	u8 entryCount;		// Start from 1 = 1..256
	u8 padding_extension[2];
};

//--------------------------------------------------------------------------------------------------------------------
//   Internal File Data Structures : YAIK FILES
//--------------------------------------------------------------------------------------------------------------------

/*	Common header of each file section 
	Seen as a 32 bit code or 4 Byte letter code. */
union Tag {
	u32 			tag32;
	u8  			tag8[4];
};

//
// First Section of the file : Generation information.
// Always size multiple of 4 byte.
struct FileHeader {
	static const u32 BIT_ALPHA_CHANNEL = 1;
	
	Tag 			tag;

	u16 			version;
	u16 			width;
	u16 			height;
	u16 			infoMask;						// Bit 15 reserved for 32 bit extension or more...
};	

struct HeaderBase {									// [Always 4 byte aligned]
	Tag 			tag;
	u32 			length;
};

struct MipmapHeader {
	BoundingBox		bbox;
	u32 			streamSize;
	u8				version;						// Allow extension. 1 : non compressed.
	u8				mipmapLevel;
	// Then byte array...
};

struct AlphaHeader {
	enum ALPHA_PARAM {
		// 3 Bit for Alpha Type.
		IS_1_BIT_USEMIPMAPMASK			= 0,		// UNSUPPORTED YET
		IS_1_BIT_FULL					= 1,
		IS_6_BIT_USEMIPMAPMASK			= 2,
		IS_6_BIT_USEMIPMAPMASK_INVERSE	= 3,
		IS_6_BIT_FULL					= 4,
		IS_6_BIT_FULL_INVERSE			= 5,
		IS_8_BIT_FULL					= 6,
		// Free type : 1 bit.
	};
	
	BoundingBox		bbox;
	u32				streamSize;
	u32				expectedDecompressionSize;
	u8				version;

	u8				parameters;
	
	// Then byte array...
};

// Span information...
struct LineSpan {
	s32				startDelta;
	s32				length;
};

struct UniqueColorHeader {
	BoundingBox		bbox;
	u32				streamSize;						// Map
	u32				expectedDecompressionSizeM;		// Map
	u32				streamSizeE;					// Edge Table
	u32				expectedDecompressionSkippers;	// Size of Edge table.
	u8				version;						// 1: Linear, 2:Swizzle 8x8
	u8				colorCount;						// 1..255, if 0=256 colors.
};

// DEPRECATED
struct HeaderSmoothMap {
	BoundingBox		bbox;							// Quarter Size bitmap, 8 pixel aligned map.
	u32				streamSize;
	u32				rgbStreamSize;
	u32				expectedRGBStreamSize;
	u8				version;
	u8				grid;							// [Bit 0:3 : X Offset][Bit 4:7 Y Offset]
	// [Stream for bbQuarter]
	// [RGB Stream]
};
//---DEPRECATED

struct HeaderGradientTile {
	BoundingBox		bbox;
	u32				streamBitmapSize;				// Decompressed size estimated from bbox and type.
	u32				streamRGBSize;
	u32				streamRGBSizeUncompressed;
	u8				version;

	// Note : 
	// All format with tile < 8 pixel will be swizzled to localize 8x8 work.
	// 4x8 has no change.
	//
	// but 8x4 and 4x4 will have subtile in stream order and not just follow X->Y order.
	// [-1-]  [1][2]
	// [-2-]  [3][4]
	enum EFormat {
		TILE_16x16	= (4<<0) | (4<<3),
		TILE_16x8   = (4<<0) | (3<<3),
		TILE_8x16   = (3<<0) | (4<<3),
		TILE_8x8    = (3<<0) | (3<<3),
		TILE_8x4    = (3<<0) | (2<<3),
		TILE_4x8    = (2<<0) | (3<<3),
		TILE_4x4    = (2<<0) | (2<<3),
	}; // Note the format indicate in the X and Y Axis the number of bit to indicate the tile size.
	u8				format;

	// Bit 0 : Plane R/Y
	// Bit 1 : Plane G/Co
	// Bit 2 : Plane B/Cg
	u8				plane;

	// + [Compressed Stream Bitmap]
	// + [Compressed Stream RGB]


	//  Define the way of parsing the bitmap for gradient tile.
	//	- Give swizzling rule : necessary for encoder and decoder to MATCH.
	//	- Necessary for both to defined buffer allocation size.
	//	So the function is implemented here as common code between the projects.
	static void getSwizzleSize(int tileShiftX, int tileShiftY, u32& bigTileX, u32& bigTileY, u32& bitCount) {
		// Return default failure value.
		int swizzleParseX = 0;
		int swizzleParseY = 0;

		switch (tileShiftX) {
		case 4: // 16 Pixels
			switch (tileShiftY) {
			case 4:
				// 16 bit word.
				swizzleParseX = 64;
				swizzleParseY = 64;
				break;
			case 3:
				// 32 bit word.
				swizzleParseX = 64;
				swizzleParseY = 64;
				break;
			case 2:
				// Never happen here.
				break;
			}
			break;
		case 3: // 8 Pixels.
			switch (tileShiftY) {
			case 4:
				// 32 bit word.
				swizzleParseX = 64;
				swizzleParseY = 64;
				break;
			case 3:
				// 64 bit word.
				swizzleParseX = 64;
				swizzleParseY = 64;
				break;
			case 2:
				// 64 bit word.
				swizzleParseX = 64;
				swizzleParseY = 32;
				break;
			}
			break;
		case 2: // 2 Pixels.
			switch (tileShiftY) {
			case 4:
				// Never happen here.
				break;
			case 3:
				// 64 bit word.
				swizzleParseX = 32;
				swizzleParseY = 64;
				break;
			case 2:
				// 64 bit word.
				swizzleParseX = 32;
				swizzleParseY = 32;
				break;
			}
			break;
		}

		bigTileX = swizzleParseX;
		bigTileY = swizzleParseY;
		bitCount = (swizzleParseX >> tileShiftX) * (swizzleParseY >> tileShiftY);
	}

	static u32  getBitmapSwizzleSize(int TshiftX, int TshiftY, int imgW, int imgH) {
		u32 swizzleParseX,swizzleParseY,bitCount;
		HeaderGradientTile::getSwizzleSize(TshiftX,TshiftY,swizzleParseX,swizzleParseY, bitCount);
		if (swizzleParseX && swizzleParseY) {
			return ((imgW+swizzleParseX-1) / swizzleParseX) * ((imgH+swizzleParseY-1) / swizzleParseY) * bitCount;
		} else {
			return 0;
		}
	}

};

struct PlaneTile {
	BoundingBox		bbox;							// 8 Pixel Aligned.
	u32				streamSizeTileMap;
	u32				streamSizeTileStream;
	u32				expectedSizeTileStream;

	u8				version;
	u8				format;							// Bit 1	: QuarterXAxis (1=True, 0=False)
													// Bit 2	: QuarterYAxis (1=True, 0=False)
													// Bit 3..4 : Plane ID (0 = YPlane, 1 = Co, 2 = Cg)
};

struct HeaderTile3D {
	u32 streamColorCnt;
	u32 streamTypeCnt;
	u32 stream3BitCnt;
	u32 stream4BitCnt;
	u32 stream5BitCnt;
	u32 stream6BitCnt;
	u32 comprTypeSize;
	u32 comprColorSize;
	u32 compr3BitSize;
	u32 compr4BitSize;
	u32 compr5BitSize;
	u32 compr6BitSize;
	
	u16 sizeT16_8Map;
	u16 sizeT8_16Map;
	u16 sizeT8_8Map;
	u16 sizeT4_8Map;
	u16 sizeT8_4Map;
	u16 sizeT4_4Map;

	u16 sizeT16_8MapCmp;
	u16 sizeT8_16MapCmp;
	u16 sizeT8_8MapCmp;
	u16 sizeT4_8MapCmp;
	u16 sizeT8_4MapCmp;
	u16 sizeT4_4MapCmp;

	// + Stream 3 Bit
	// + Stream 4 Bit
	// + Stream 5 Bit
	// + Stream 6 Bit
	// + Tile  Stream
	// + Color Stream
	// + T16_8 Tile Map
	// + T8_16 Tile Map
	// + T8_8  Tile Map
	// + T8_4  Tile Map
	// + T4_8  Tile Map
	// + T4_4  Tile Map
};

#define EncodeTileType(t,r,b)		(((t)<<13)|((r)<<7)|(b))

#endif // YAIK_PRIVATE_HEADER
