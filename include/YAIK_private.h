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

struct Size {
	u16				w;
	u16				h;
};

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
	void*			srcCheck;
	u32				srcLength;

	u8*				mipMapMask;		// Current Mask during decompression.
	BoundingBox		maskBBox;

	u8*				alphaChannel;			// 8 Bit Alpha Channel
	u8*				targetRGBA;
	u8*				planeY;
	u8*				planeCo;
	u8*				planeCg;
	u16				width;
	u16				height;
	bool			isRGBA;

	void			FillGradient	();
	void			ComputeCoCg		();
	void			ConvertYCoCg2RGB();
};

struct YAIK_Library {
	// Handle the number of decoding context...
	s32				totalSlotCount;
	s32				freeSlotCount;
	// List of slot available.
	YAIK_Instance*	freeStack[257];
	// Allocate slots.
	YAIK_Instance*	instances;

	YAIK_Instance*	AllocInstance	();
	void			FreeInstance	(YAIK_Instance* inst);
};

//--------------------------------------------------------------------------------------------------------------------
//   Internal File Data Structures
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

#define EncodeTileType(t,r,b)		(((t)<<13)|((r)<<7)|(b))

#endif // YAIK_PRIVATE_HEADER
