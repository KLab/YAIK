#include "../include/YAIK_private.h"

/*
	This is a basic framework to process 2D arrays of pixels.
	It is NOT build for performance, but :
	- Ease of use.
	- Maintainability.
	- Independance from bigger library.

	Plane Class : allows to work with a 2D arrays of values, basically just grey scale.
		Internally all value are s32 type. Not limited to 8 bit like RGB.
		
	Image Class : is a collection of 3 or 4 planes.
		Allow to gather Plane into a single collection.
	
 */

/* 	----------------------------------------------------------------------
	Base abstract class for parsing 'things in specific order'
	Could be pixels, tile, etc...
	It allows switching to different parsing policy without rewriting code.
	----------------------------------------------------------------------*/
class ParsingOrder {
public:
	BoundingBox constraint;

	ParsingOrder(int w_, int h_, int blockW_, int blockH_):blockW(blockW_),blockH(blockH_),w(w_),h(h_) {
		result.w = blockW_;
		result.h = blockH_;
		
		constraint.x=0;
		constraint.y=0;
		constraint.w=w_;
		constraint.h=h_;
	}

	virtual void	Start			() = 0;
	virtual void	ResetParsing	() = 0;
	virtual bool	HasNextBlock	(bool& isSubMarker) = 0;
	inline
	BoundingBox&	GetCurrentBlock	(bool removeBlockFromList) { return result; }
	virtual void	Stop			() {}
protected:
	int w;
	int h;
	int blockW;
	int blockH;
	BoundingBox result;
};

/*	TO IMPLEMENT IF NEEDED
	class ZigZagOrder : public ParsingOrder {};
	class MortonOrder : public ParsingOrder {};	*/

/* 	----------------------------------------------------------------------
	Linear int buffer used as 2D Buffer storing values.
	
	----------------------------------------------------------------------*/

enum EDownSample {
	NEAREST_TL,
	NEAREST_BR,
	AVERAGE_BOX,
	MAX_BOX,
	MIN_BOX
};

enum EInterpMode {
	QUART_TL_REFERENCE_BILINEAR,
	QUART_BR_REFERENCE_BILINEAR,
	QUART_NEAREST_UP,
};

class Plane {
public:
	Plane(int w, int h) :w(w), h(h) { pixels = new int[w * h]; }
	~Plane() { delete[] pixels; }

	inline	int		GetWidth 	() { return w; }
	inline	int		GetHeight	() { return h; }
	inline	int*	GetPixels	() { return pixels; }
	inline	int		GetIndex	(int x, int y) { return x + y * w; }
	inline  BoundingBox    GetRect ()  { BoundingBox r ; r.x = 0; r.y = 0; r.w = w; r.h = h; return r; }

	Plane*	SampleDown			(bool x, bool y, EDownSample mode);
	Plane*	SampleUp			(bool x, bool y, bool interpolate);
	Plane*	ApplyDiff			(ParsingOrder& dir);
	void	GetMinMax_Y			(BoundingBox& rect, Plane* validPixel, Plane* quarterSmooth, int* min, int* max, int* unique);
	void	Fill				(BoundingBox& rect, int value);
	void	FillOutside			(BoundingBox& rect, int value);
	Plane*	RangeReduction		(int bitCount, float* min, float* max); // ??? Return min,max ??? Convert given input ???. Want to provide dithering (rounding technique) too may be...
	Plane*	Clone				();
	void	Compute2DHistogram	(int* histogram);
	void	SaveAsPNG			(const char* fileName);
	void	RemoveMask			(Plane* toRemove);
	void	SetPixel			(int x, int y, int v) { pixels[x + y*w] = v; }

	int		GetBoundingBoxNonZeros(BoundingBox& result);

	enum MASK_OPERATOR {
		EQUAL_MSK,
		NEQ_MSK,
		LT_MASK,
		GT_MASK,
		LE_MASK,
		GE_MASK,
		AND_OP
	};

	Plane* ComputeOperatorMask	(int v, MASK_OPERATOR op);
	Plane* ComputeOperatorMask	(Plane* srcPlane, MASK_OPERATOR op);
	Plane* Erosion				();
	Plane* ReduceQuarterLogicMax();
	
	int    GetPixelValue		(int x, int y, bool& isOutside) {
		isOutside = false;
		if (x < 0 || x >= w) { isOutside = true; if (x>=w) { x=w-1; } if (x < 0) { x = 0;} }
		if (y < 0 || y >= h) { isOutside = true; if (x>=w) { y=h-1; } if (y < 0) { y = 0;} }
		return pixels[x + y * w];
	}

private:
	int*	pixels;
	int		w;
	int		h;
};

typedef Plane*	TPlane;

/* 	----------------------------------------------------------------------
	Image is a collection of Plane.
	3 or 4 most of the time.
	
	Example : R,G,B,A planes
	----------------------------------------------------------------------*/
class Image {
private:
	Plane* 	planes[4];	// 4 planes max anyway.
	int		planeCount;
	int		w;
	int		h;
private:
	Image() :planeCount(0) {
		planes[0] = NULL;
		planes[1] = NULL;
		planes[2] = NULL;
		planes[3] = NULL;
	}
public:
	~Image() {
		// Shared pointer are reset...
		planes[0] = NULL;
		planes[1] = NULL;
		planes[2] = NULL;
		planes[3] = NULL;
	}

	enum MASK_OPERATOR {
		EQUAL_MSK,
		NEQ_MSK,
		LT_MASK,
		GT_MASK,
		LE_MASK,
		GE_MASK,
	};

	void	Compute4DHistogram	(int* histogramRGB, const BoundingBox& boundingBox, TPlane mask);

	Plane*	ComputeOperatorMask	(int* values, MASK_OPERATOR op, bool allTrue, bool only3Plane);

	/* Return false if size are different from the image size. */
	bool			ReplacePlane(int index, TPlane newPlane) {
		bool success = false;
		TPlane oldPlane = planes[index];
		if (newPlane) {
			if (oldPlane) {
				if ((newPlane->GetWidth() == oldPlane->GetWidth()) && (newPlane->GetHeight() == oldPlane->GetHeight())) {
					planes[index] = newPlane;
					success = true;
				}
			} else {
				planes[index] = newPlane;
				success = true;
			}
		}
		return success;
	}

	inline int		GetWidth	() { return w; }
	inline int		GetHeight	() { return h; }
	
	inline void		GetPixel	(int x, int y, int* rgb, bool& isOutside) {
		isOutside = false;
		if (x < 0) { isOutside=true; x = 0;   }
		if (x > w) { isOutside=true; x = w-1; }
		if (y < 0) { isOutside=true; y = 0;   }
		if (y > h) { isOutside=true; y = h-1; }

		int index = x + y*w;
		rgb[0] = planes[0]->GetPixels()[index];
		rgb[1] = planes[1]->GetPixels()[index];
		rgb[2] = planes[2]->GetPixels()[index];
	}

	static Image*	CreateImage	(int w, int h, int channelCount, bool fill);
	static Image*	LoadPNG		(const char* filename);
	void 			SavePNG		(const char* fileName, bool* signed_);
	void			SaveAsYCoCg	(const char* optionnalFileName);

	void			SetPixel	(int x, int y, int r, int g, int b);

	Image*			ConvertToRGB2YCoCg	(bool doConversion);
	Image*			ConvertToYCoCg2RGB	(bool doConversion);

	inline TPlane	GetPlane	(int index) { return planes[index]; }

	bool			HasAlpha	() { return planeCount == 4; }
};

// Need plane, so implemented after Plane definition.
class LeftRightOrder : public ParsingOrder {
protected:
	int x;
	int y;
public:
	LeftRightOrder(TPlane& pln, int blockW, int blockH) :LeftRightOrder(pln->GetWidth(), pln->GetHeight(), blockW, blockH) {}

	LeftRightOrder(int w, int h, int blockW, int blockH) :ParsingOrder(w, h, blockW, blockH) {}

	virtual void Start() { x = constraint.x-blockW; y = constraint.y; }
	virtual void ResetParsing() { Start(); }
	virtual bool HasNextBlock(bool& isSubMarker) {
		bool valid = (y < (constraint.y + constraint.h));
		if (valid) {
			x += blockW;
			if (x >= (constraint.x + constraint.w)) {
				x = constraint.x;
				y += blockH;
				valid = (y < h);
			}
		}
		isSubMarker = valid & (x == constraint.x);
		result.x = x;
		result.y = y;
		result.w = (x + blockW > constraint.w) ? (x % blockW) : blockW;
		result.h = (y + blockH > constraint.h) ? (y % blockW) : blockH;
		return valid;
	}
};

class TopDownOrder : public ParsingOrder {
protected:
	int x;
	int y;
public:
	TopDownOrder(TPlane& pln, int blockW, int blockH)   :TopDownOrder(pln->GetWidth(), pln->GetHeight(), blockW, blockH) {}

	TopDownOrder(int w, int h, int blockW, int blockH) :ParsingOrder(w, h, blockW, blockH) {}

	virtual void Start() { x = 0; y = -blockH; }
	virtual void ResetParsing() { Start(); }
	virtual bool HasNextBlock(bool& isSubMarker) {
		bool valid = (x < w);
		if (valid) {
			y += blockH;
			if (y >= h) {
				y = 0;
				x += blockW;
				valid = (x < w);
			}
		}
		isSubMarker = valid & (y == 0);
		result.x = x;
		result.y = y;
		result.w = (x + blockW > w) ? (x % blockW) : blockW;
		result.h = (y + blockH > h) ? (y % blockW) : blockH;
		return valid;
	}
};

unsigned int	nearest2Pow	(unsigned int v);
unsigned int	log2ui		(unsigned int v);
void			RGBtoYCoCg	(int R, int  G, int  B, int& oY, int& oCo,int& oCg);
void			YCoCgtoRGB	(int Y, int Co, int Cg, int& oR, int&  oG,int&  oB);
