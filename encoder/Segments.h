#ifndef KLB_SEGMENTS
#define KLB_SEGMENTS

#include <math.h>
#include <float.h>
//#include "glm/vec2.hpp"

//float sgnf(float v) {
//	return (v > 0.0f) ? 1.0f : ((v < 0.0f) ? -1.0f : 0.0f);
//}

/*
struct vec2 {
	float x;
	float y;
	vec2() :x(0), y(0) {}
	vec2(float c) : x(c),y(c) {}
	vec2(float x, float y) : x(x), y(y) {}

	vec2& operator=(const vec2& v) {
		x = v.x;
		y = v.y;
		return *this;
	}
	vec2& operator+=(vec2& v) {
		x += v.x;
		y += v.y;
		return *this;
	}
	vec2& operator-=(vec2& v) {
		x -= v.x;
		y -= v.y;
		return *this;
	}
	vec2 operator+(vec2& v) {
		return vec2(x + v.x, y + v.y);
	}
	vec2 operator-(vec2& v) {
		return vec2(x - v.x, y - v.y);
	}
	vec2 operator+(vec2 v) {
		return vec2(x + v.x, y + v.y);
	}
	vec2 operator+(float s) {
		return vec2(x + s, y + s);
	}
	vec2 operator-(float s) {
		return vec2(x - s, y - s);
	}
	vec2 operator*(float s) {
		return vec2(x * s, y * s);
	}
	vec2 operator/(float s) {
		return vec2(x / s, y / s);
	}
	vec2& operator+=(float s) {
		x += s;
		y += s;
		return *this;
	}
	vec2& operator-=(float s) {
		x -= s;
		y -= s;
		return *this;
	}
	vec2& operator*=(float s) {
		x *= s;
		y *= s;
		return *this;
	}
	vec2 operator*(vec2& v) {
		return vec2(v.x*x,v.y*y);
	}

	vec2 operator*(vec2 v) {
		return vec2(v.x*x,v.y*y);
	}

	vec2& operator/=(float s) {
		x /= s;
		y /= s;
		return *this;
	}

	static vec2  sign(vec2& v) {
		return vec2(sgnf(v.x),sgnf(v.y));
	}
	static float dot(vec2 v1, vec2 v2) {
		return v1.x * v2.x + v1.y * v2.y;
	}
	static float cross(vec2 v1, vec2 v2) {
		return (v1.x * v2.y) - (v1.y * v2.x);
	}
	static vec2 powv2(vec2& v, vec2& v2) {
		return vec2(pow(v.x,v2.x),pow(v.y,v2.y));
	}
	static vec2 abs(vec2& v) {
		return vec2(fabsf(v.x),fabsf(v.y));
	}
};
*/

/*
float sdBezier( glm::vec2 pos, glm::vec2 A, glm::vec2 B, glm::vec2 C );
float udBezier( glm::vec2 pos, glm::vec2 A, glm::vec2 B, glm::vec2 C );
*/

struct AbstractSegment {
	float pieceLength;

	float Length() {
		return pieceLength;
	}

	virtual float ComputeDistance(float x, float y, float& xClosest, float& yClosest, float& tLocal) = 0;
};

struct LinearEqu2D : public AbstractSegment {
	float x0; // Included
	float y0;
	float x1; // Excluded, except last segment.
	float y1; // Excluded

	float dx;
	float dy;
	void Set(float x0, float y0, float x1, float y1) {
		this->x0 = x0;
		this->y0 = y0;
		this->x1 = x1;
		this->y1 = y1;

		dx = x1-x0;
		dy = y1-y0;
		pieceLength = sqrtf(dx*dx + dy*dy);
	}

	float distancePartPosition;
	float distancePart;

	float ComputeDistance(float x, float y, float& xClosest, float& yClosest, float& tLocal) {
		float U;
		float xIntersection,yIntersection;
 
		U = ( ( ( x - x0 ) * ( dx ) ) +
			(   ( y - y0 ) * ( dy ) )
			// + ( ( Point->Z - LineStart->Z ) * ( LineEnd->Z - LineStart->Z ) )
			) / ( pieceLength * pieceLength );

		tLocal = U;

		if (U < 0.0f) { U = 0.0f; }
		if (U > 1.0f) { U = 1.0f; }

		/*
		if( U < 0.0f || U > 1.0f ) {
				// closest point does not fall within the line segment
		}
		*/

		xIntersection = x0 + U * dx;
		yIntersection = y0 + U * dy;
		// Intersection.Z = LineStart->Z + U * ( LineEnd->Z - LineStart->Z );

		LinearEqu2D equ;
		equ.Set(x,y,xIntersection,yIntersection);
		return equ.pieceLength;
	}
};

/*
struct QuadSpline : public AbstractSegment {
	void Set(float x0, float y0, float cx, float cy, float x1, float y1) {
		this->x0 = x0;
		this->y0 = y0;
		this->x1 = x1;
		this->y1 = y1;
		this->cx = cx;
		this->cy = cy;

		Ax = x1 - x0;
		Ay = y1 - y0;

		Bx = x0 - (2.0f*cx) + x1;
		By = y0 - (2.0f*cy) + y1;

		minX = fminf(x0,fminf(cx,x1));
		maxX = fmaxf(x0,fmaxf(cx,x1));
		minY = fminf(y0,fminf(cy,y1));
		maxY = fmaxf(y0,fmaxf(cy,y1));

		if ((minX == cx) || (maxX == cx)) {
			float u = -Ax / Bx; // u where GetTan(u).x == 0
			u = (1.0f-u)*(1.0f-u)*x0 + 2.0f*u*(1.0f-u)*cx + u*u*x1;
			if (minX == cx) {
				minX = u;
			} else {
				maxX = u;
			}
		}
		if ((minY == cy) || (maxX == cy)) {
			float u = -Ay / By; // u where GetTan(u).x == 0
			u = (1.0f-u)*(1.0f-u)*y0 + 2.0f*u*(1.0f-u)*cy + u*u*y1;
			if (minY == cy) {
				minY = u;
			} else {
				maxY = u;
			}
		}

		float rx,ry;
		int idx = 0;
		float step = 0.005f;

		for (float n=0.0f; n <= 1.0f; n += step) {
			Evaluate(n,rx,ry);
			curveX[idx] = rx;
			curveY[idx] = ry;
			idx++;
		}

	}

	// From closest, can identify distance.
	void Evaluate(float t, float&x , float&y) {
		float m0x = ((cx-x0)*t) + x0; 
		float m0y = ((cy-y0)*t) + y0; 
		float m1x = ((x1-cx)*t) + cx; 
		float m1y = ((y1-cy)*t) + cy; 
		x = (m1x - m0x)*t + m0x;
		y = (m1y - m0y)*t + m0y;
	}

	int findClosest(float xp, float yp) {
		// TODO : Optimize iteration by looking at Point
		float step = 0.005f;
		int idx = 0;
		float minSq = 9999999999.0f;
		int minT;
		for (int n=0; n < 200;n++) {
			float dx = curveX[n]-xp;
			float dy = curveY[n]-yp;
			float distSq = dx*dx + dy*dy;
			if (distSq < minSq) {
				minSq = distSq;
				minT  = n;
			}
		}
		return minT;
	}

	float ComputeDistance(float x, float y, float& closestX, float& closestY) {
		float posX = x0 - x;
		float posY = y0 - y;
		
		// Can be precomputed.
		// 3rd degree solver.
		float    a = Bx * Bx + By*By;
		float    b = 3.0f*(Ax*Bx+Ay*By);
		float    c = 2.0f*(Ax*Ax+Ay*Ay) + posX*Bx + posY*By;
		float	 d = posX*Ax + posY*Ay;
		float  sol[3];
		int    count = thirdDegreeEqu(a,b,c,d,sol[0],sol[1],sol[2]);

		float distMin = FLT_MAX;
		float fx = (float)x;
		float fy = (float)y;

		float d0 = getDist(fx, fy, x0, y0);
		float d2 = getDist(fx, fy, x1, y1);
		float tMin = -9999999.0f;
		bool hasTmin = false;
		// float norX,norY;
		float posMinX,posMinY;
		float t = -1000000.0;

		float dist;

		if (count != 0) {
			for (int i=1;i<=count;i++) {
				t = sol[i-1];

				if (t >= 0.0 && t <= 1.0) {
					getPos(t,posX,posY);
					float dist = getDist(fx, fy, posX, posY);
					if (dist < distMin) {
						// minimum found!
						tMin = t;
						distMin = dist;
						posMinX = posX;
						posMinY = posY;
						hasTmin = true;
					}
				}
			}

			if (hasTmin && (distMin < d0) && (distMin < d2)) {
				// the closest point is on the curve
//				norX =   Ay + tMin * By;
//				norY = -(Ax + tMin * Bx);
//				nor.normalize(1);
//				float orientedDist = distMin;
//				if (((x - posMinX) * norX + (y - posMinY) * norY) < 0) {
//					norX *= -1.0;
//					norY *= -1.0;
//					orientedDist *= -1.0;
//				}
				
//				nearest.t = tMin;
				closestX = posMinX;
				closestY = posMinY;
//				nearest.nor = nor;
//				nearest.dist = distMin;
//				nearest.orientedDist = orientedDist;
//				nearest.onCurve = true;
				float dx = (closestX-x);
				float dy = (closestY-y);
				return sqrt(dx*dx+dy*dy);
			}
		}

		// the closest point is one of the 2 end points
		if (d0 <= d2) 
		{
			distMin = d0;
			tMin = 0;
			posMinX = x0;
			posMinY = y0;	
		} else {
			distMin = d2;
			tMin = 1;
			posMinX = x1;
			posMinY = y1;
		}
//		nor.x = x - posMin.x;
//		nor.y = y - posMin.y;
//		nor.normalize(1);
//		nearest.t = tMin;
		closestX = posMinX;
		closestY = posMinY;
//		nearest.pos = posMin;
//		nearest.nor = nor;
//		nearest.orientedDist = nearest.dist = distMin;
//		nearest.onCurve = false;

		float dx = (closestX-x);
		float dy = (closestY-y);
		return sqrt(dx*dx+dy*dy);
	}

	float ComputeDistance(int x, int y, int& idx) {
		float shortest = 999999999.0f;
		float fx = (float)x;
		float fy = (float)y;
		for (int n=0; n < 201; n++) {
			float dx = curveX[n]-fx;
			float dy = curveY[n]-fy;
			float dst = dx*dx + dy*dy;
			if (dst < shortest) {
				idx = n;
				shortest = dst;
			}
		}
		return shortest;

#if 0
		glm::vec2 pos(x,y);
		glm::vec2 A(x0,y0);
		glm::vec2 B(cx,cy);
		glm::vec2 C(x1,x1);
		float dist = udBezier( pos, A, B, C );
		return dist;
#else
		float clx,cly;
		if (ComputeClosest(x,y,clx,cly)) {
			float dx = clx - x;
			float dy = cly - y;
			return sqrtf((dx*dx)+(dy*dy));
		} else {
			printf("ERROR\n");
			return -99999999.0f;
		}
#endif
	}

	float curveX[201];
	float curveY[201];
protected:
	float x0, y0;
	float cx, cy;
	float x1, y1;

	float getDist(float x0, float y0, float x1, float y1) {
		float dx = x0-x1;
		float dy = y0-y1;
		return sqrtf(dx*dx + dy*dy);
	}

	void getPos(float t, float& x, float &y)
	{
		float a = (1.0f - t) * (1.0f - t);
		float b = 2.0f * t * (1.0f - t);
		float c = t * t;
		x = a * x0 + b * cx + c * x1;
		y = a * y0 + b * cy + c * y1;
	}


	int thirdDegreeEqu( float a, float b, float c, float d, float& s1, float& s2, float& s3) {
		// a value we consider "small enough" to equal it to zero:
		// (this is used for double solutions in 2nd or 3d degree equation)
		static const float zeroMax = 0.0000001f;
		static const float PI      = 3.14159267f;

		if (fabs(a) > zeroMax)
		{
			// let's adopt form: x3 + ax2 + bx + d = 0
			float z = a; // multi-purpose util variable
			a = b / z;
			b = c / z;
			c = d / z;
			// we solve using Cardan formula: http://fr.wikipedia.org/wiki/M%C3%A9thode_de_Cardan
			float p  = b - a * a / 3.0f;
			float q  = a * (2.0f * a * a - 9.0f * b) / 27.0f + c;
			float p3 = p * p * p;
			float D  = q * q + 4.0f * p3 / 27.0f;
			float offset = -a / 3.0f;

			if (D > zeroMax) {
				// D positive
				z = sqrtf(D);
				float u = ( -q + z) / 2.0f;
				float v = ( -q - z) / 2.0f;
				u = (u >= 0.0f) ? powf(u, 1.0f / 3.0f) : -powf( -u, 1.0f / 3.0f);
				v = (v >= 0.0f) ? powf(v, 1.0f / 3.0f) : -powf( -v, 1.0f / 3.0f);
				s1 = u + v + offset;
				return 1;
			} else if (D < -zeroMax) {
				// D negative
				float u = 2.0f * sqrtf( -p / 3.0f);
				float v = acosf( -sqrtf( -27.0f / p3) * q / 2.0f) / 3.0f;
				
				s1 = u * cosf(v) + offset;
				s2 = u * cosf(v + 2.0f * PI / 3.0f) + offset;
				s3 = u * cosf(v + 4.0f * PI / 3.0f) + offset;
				return 3;
			} else {
				// D zero
				float u;
				if (q < 0.0f) { u = powf( -q / 2.0f, 1.0f / 3.0f); }
				else          { u = -powf( q / 2.0f, 1.0f / 3.0f); }
				s1 = 2.0f*u + offset;
				s2 = -u + offset;
				return 2;
			}
		} else {
			// a = 0, then actually a 2nd degree equation:
			// form : ax2 + bx + c = 0;
			a = b;
			b = c;
			c = d;
			if (fabs(a) <= zeroMax) {
				if (fabs(b) <= zeroMax) return 0;
				else {
					s1 = -c / b;
					return 1;
				}
			}
			float D = b*b - 4.0f*a*c;
			if (D <= - zeroMax) return 0;
			if (D > zeroMax) {
				// D positive
				D = sqrtf(D);
				s1 = ( -b - D) / (2.0f * a);
				s2 = ( -b + D) / (2.0f * a);
				return 2;
			} else if (D < - zeroMax) {
				// D negative
				return 0;
			} else {
				// D zero
				s1 = -b / (2.0f * a);
				return 1;
			}
		}
	}

private:
	float Ax, Ay;
	float Bx, By;
	float minX, maxX,minY,maxY;
};
*/

#endif
