/*
 *
 */
#include "mbed.h"
#include "KKSphere.h"
#include <math.h>

CKKSphere::CKKSphere(GlobalState* globalState, LCD_Handler_t *lcd) :
	TaskThread(this, osPriorityBelowNormal, 1024, NULL, "KKSphere"),
	m_GlobalState(globalState),
	m_State(State::Ready),
	m_Timer(10),
	m_clrLeft(0x00FF00),
	m_clrRight(0xFF0000),
	m_nPreset(PRESET_WAVE),
	m_FrameNo(0),
	m_Lcd(lcd)
{
	m_Rect.left = (lcd->_width - 256) / 2;
	m_Rect.top = (lcd->_height - 256) / 2;
	m_Rect.right = m_Rect.left + 256;
	m_Rect.bottom = m_Rect.top + 256;

	memset(&m_BMInf, 0, sizeof(m_BMInf));
	m_BMInf.bV4Size = sizeof(TBitmapHeader);
	m_BMInf.bV4Width = 256;
	m_BMInf.bV4Height = 256;
	m_BMInf.bV4Planes = 1;
	m_BMInf.bV4BitCount = 32;
	m_BMInf.bV4V4Compression = BI_BITFIELDS;
	m_BMInf.bV4SizeImage = 0;
	m_BMInf.bV4XPelsPerMeter = 0;
	m_BMInf.bV4YPelsPerMeter = 0;
	m_BMInf.bV4ClrUsed = 0;
	m_BMInf.bV4ClrImportant = 0;
	m_BMInf.bV4RedMask = 0x00FF0000;
	m_BMInf.bV4GreenMask = 0x0000FF00;
	m_BMInf.bV4BlueMask = 0x000000FF;
	m_BMInf.bV4AlphaMask = 0xFF000000;

	m_hDC[0]._width = 256; m_hDC[0]._height = 256; m_hDC[0]._buffer = (uint8_t *)m_pBitmapData[0];
	m_hDC[1]._width = 256; m_hDC[1]._height = 256; m_hDC[1]._buffer = (uint8_t *)m_pBitmapData[1];
	m_hDC[2]._width = 256; m_hDC[2]._height = 256; m_hDC[2]._buffer = (uint8_t *)m_pBitmapData[2];

	Mapping();
}

CKKSphere::~CKKSphere()
{
}

TColor HSV_RGB(double H, double S, double V)
{
	TColor A;

	switch ((int)H) {
	case 0:
		A = (int)(255 * V);
		A |= (int)(255 * (1 - (1 - H) * S) * V) << 8;
		A |= (int)(255 * (1 - S) * V) << 16;
		break;
	case 1:
		A = (int)(255 * V) << 8;
		A |= (int)(255 * (1 + (1 - H) * S) * V);
		A |= (int)(255 * (1 - S) * V) << 16;
		break;
	case 2:
		A = (int)(255 * V) << 8;
		A |= (int)(255 * (1 - (3 - H) * S) * V) << 16;
		A |= (int)(255 * (1 - S) * V);
		break;
	case 3:
		A = (int)(255 * V) << 16;
		A |= (int)(255 * (1 + (3 - H) * S) * V) << 8;
		A |= (int)(255 * (1 - S) * V);
		break;
	case 4:
		A = (int)(255 * V) << 16;
		A |= (int)(255 * (1 - (5 - H) * S) * V);
		A |= (int)(255 * (1 - S) * V) << 8;
		break;
	default: // case 5:
		A = (int)(255 * V);
		A |= (int)(255 * (1 + (5 - H) * S) * V) << 16;
		A |= (int)(255 * (1 - S) * V) << 8;
		break;
	}

	return A;
}

const static double pi = 3.1415926;
const static double pi2 = 2 * 3.1415926;

struct Vector {
	double x, y, z;
};

struct Matrix {
	Vector i, j, k;
};

inline Vector Add(const Vector &A, const Vector &B)
{
	Vector Result;

	Result.x = A.x + B.x;
	Result.y = A.y + B.y;
	Result.z = A.z + B.z;

	return Result;
}

inline Vector Sub(const Vector &A, const Vector &B)
{
	Vector Result;

	Result.x = A.x - B.x;
	Result.y = A.y - B.y;
	Result.z = A.z - B.z;

	return Result;
}

inline Vector Multiply(const Vector &A, double B)
{
	Vector Result;

	Result.x = A.x * B;
	Result.y = A.y * B;
	Result.z = A.z * B;

	return Result;
}

inline Vector Divide(const Vector &A, double B)
{
	Vector Result;

	Result.x = A.x / B;
	Result.y = A.y / B;
	Result.z = A.z / B;

	return Result;
}

inline Vector MltAdd(double A, const Vector &B, const Vector &C)
{
	Vector Result;

	Result.x = A * B.x + C.x;
	Result.y = A * B.y + C.y;
	Result.z = A * B.z + C.z;

	return Result;
}

inline double InnPro(const Vector &A, const Vector &B)
{
	return A.x * B.x + A.y * B.y + A.z * B.z;
}

inline double Norm(const Vector &A)
{
	return sqrt(InnPro(A, A));
}

inline Vector ForElement(const Vector &A)
{
	return Divide(A, Norm(A));
}

inline void PolerFrom(const Vector &Vct, Matrix Axl, double &x, double &y)
{
	y = InnPro(Axl.i, Vct);
	y = acos(y);
	x = InnPro(Axl.j, Vct) / sin(y);
	x = acos(x);

	if (InnPro(Axl.k, Vct) < 0)
		x = pi2 - x;
}

inline void Solve2(double a, double b, double c, double &x1, double &x2)
{
	double d;

	x1 = 0;
	x2 = 0;
	d = b * b - a * c;
	if (d > 0) {
		d = sqrt(d);
		x1 = (-b - d) / a;
		x2 = (-b + d) / a;
	}
	else if (d == 0) {
		if (a != 0) {
			x1 = -b / a;
		}
	}
}

void AlphaBlendingPixel(int Alpha, TARGB *Pos, TARGB &color)
{
	int ZAlpha = 255 - Alpha;
	Pos->Alpha = (ZAlpha * Pos->Alpha + Alpha * color.Alpha) >> 8;
	Pos->Red   = (ZAlpha * Pos->Red   + Alpha * color.Red  ) >> 8;
	Pos->Green = (ZAlpha * Pos->Green + Alpha * color.Green) >> 8;
	Pos->Blue  = (ZAlpha * Pos->Blue  + Alpha * color.Blue ) >> 8;
}

void CKKSphere::Mapping()
{
	int x, y, cx, cy, sx, sy;
	Vector Scn;
	double Dst1, Dst2, A, B, C, tx, ty, RR;
	Vector CrP, Nor, Nm1, Ct, iCt;
	Matrix Axl;
	double CA, SA, CB, SB;

	memset(m_Map, 0, sizeof(m_Map));

	cx = m_BMInf.bV4Width / 2;
	cy = m_BMInf.bV4Height / 2;

	Axl.i.x = 1;
	Axl.i.y = 0;
	Axl.i.z = 0;
	Axl.j.x = 0;
	Axl.j.y = 1;
	Axl.j.z = 0;
	Axl.k.x = 0;
	Axl.k.y = 0;
	Axl.k.z = 1;

	CA = cos(0);
	SA = sin(0);
	CB = cos(-0.5);
	SB = sin(-0.5);

	RR = 3.0 * 3.0;
	Nm1.x = 0;
	Nm1.y = 0;
	Nm1.z = 7;
	Ct.y = Nm1.z * CA - Nm1.y * SA;
	Ct.z = Ct.y * CB - Nm1.x * SB;
	Ct.x = Ct.y * SB + Nm1.x * CB;
	Ct.y = Nm1.z * SA + Nm1.y * CA;
	iCt.x = -Ct.x;
	iCt.y = -Ct.y;
	iCt.z = -Ct.z;
	C = InnPro(iCt, iCt) - RR;

	Nm1.z = 256;
	for (x = 0; x < m_BMInf.bV4Width; x++) {
		Nm1.x = (double)(x - cx);
		for (y = 0; y < m_BMInf.bV4Height; y++) {
			Nm1.y = (double)(cy - y);
			Scn.y = Nm1.z * CA - Nm1.y * SA;
			Scn.z = Scn.y * CB - Nm1.x * SB;
			Scn.x = Scn.y * SB + Nm1.x * CB;
			Scn.y = Nm1.z * SA + Nm1.y * CA;

			A = InnPro(Scn, Scn);
			B = InnPro(Scn, iCt);
			Solve2(A, B, C, Dst1, Dst2);
			if (Dst1 >= 10e-10) {
				CrP = Multiply(Scn, Dst1);
				Nor = ForElement(Sub(CrP, Ct));
				PolerFrom(Nor, Axl, tx, ty);

				sx = (int)(m_BMInf.bV4Width * tx / pi2);
				sy = m_BMInf.bV4Height - (int)(m_BMInf.bV4Height * ty / pi);
				m_Map[x][y][0] = (uint16_t)(sx + sy * m_BMInf.bV4Width);
			}
			if (Dst2 >= 10e-10) {
				CrP = Multiply(Scn, Dst2);
				Nor = ForElement(Sub(CrP, Ct));
				PolerFrom(Nor, Axl, tx, ty);

				sx = (int)(m_BMInf.bV4Width * tx / pi2);
				sy = m_BMInf.bV4Height - (int)(m_BMInf.bV4Height * ty / pi);
				m_Map[x][y][1] = (uint16_t)(sx + sy * m_BMInf.bV4Width);
			}
		}
	}
}

#define SAMPLE_DISTANCE 96
int CKKSphere::Render(TTimedLevel *pLevels, LCD_Handler_t &hdc, TRectangle *prc)
{
	const static int Shift[3][3] = {
		{4, 3, 4},
		{3, 2, 3},
		{4, 3, 4}
	};
	const static unsigned int Mask[3][3] = {
		{0x0F0F0F0F, 0x1F1F1F1F, 0x0F0F0F0F},
		{0x1F1F1F1F, 0x3F3F3F3F, 0x1F1F1F1F},
		{0x0F0F0F0F, 0x1F1F1F1F, 0x0F0F0F0F}
	};
	// Fill background with black
	int NextFrame = m_FrameNo ^ 1;
	int lx, ly, rx, ry, cx, cy;

	LCD_Handler_t *hPaintDC = &m_hDC[m_FrameNo];

	cx = m_BMInf.bV4Width / 2;
	cy = m_BMInf.bV4Height / 2;

	// draw using the current preset
	switch (m_nPreset) {
	case PRESET_FREQ1: {
		int i;
		double /*log2,*/ f;

		//log2 = log(2);
		for (i = 0; i < pLevels->bufferSize; i++) {
			f = (double)i / (double)pLevels->bufferSize;
			//f = log((double)(i)) / log2;
			f = f - (int)f;

			lx = (int)(255 * f);
			ly = (pLevels->frequency[0][i] >> 8);

			rx = (int)(255 * f);
			ry = m_BMInf.bV4Height - (pLevels->frequency[1][i] >> 8);

			if ((lx == rx) && (ly == ry)) {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * f, 1, 1));
			}
			else {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * f, 1, 1));
				DrawPixel(hPaintDC, rx, ry, HSV_RGB(6 * f, 1, 1));
			}
		}
		break;
	}
	case PRESET_FREQ1_MIX: {
		int i;
		double f, v;

		for (i = 0; i < pLevels->bufferSize; i++) {
			f = (double)(pLevels->bufferSize - 1) * (double)i / ((double)32 * (double)pLevels->bufferSize);
			v = (int)f;
			f = f - v;
			v = (47 - v) / 48;

			lx = (int)(255 * f);
			ly = (pLevels->frequency[0][i] >> 8);

			rx = (int)(255 * f);
			ry = m_BMInf.bV4Height - (pLevels->frequency[1][i] >> 8);

			if ((lx == rx) && (ly == ry)) {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * f, v, 1));
			}
			else {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * f, v, 1));
				DrawPixel(hPaintDC, rx, ry, HSV_RGB(6 * f, v, 1));
			}
		}
		break;
	}
	case PRESET_FREQ2: {
		int i;
		double f;

		for (i = 0; i < pLevels->bufferSize; i++) {
			f = (double)i / (double)pLevels->bufferSize;
			f = f - (int)f;

			lx = (int)(255 * f);
			ly = cy - (pLevels->frequency[0][i] >> 8) / 2;

			rx = (int)(255 * f);
			ry = cy + (pLevels->frequency[1][i] >> 8) / 2;

			if ((lx == rx) && (ly == ry)) {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * f, 1, 1));
			}
			else {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * f, 1, 1));
				DrawPixel(hPaintDC, rx, ry, HSV_RGB(6 * f, 1, 1));
			}
		}
		break;
	}
	case PRESET_FREQ2_MIX: {
		int i;
		double f, v;

		for (i = 0; i < pLevels->bufferSize; i++) {
			f = (double)(pLevels->bufferSize - 1) * (double)i / ((double)32 * (double)pLevels->bufferSize);
			v = (int)f;
			f = f - v;
			v = (47 - v) / 48;

			lx = (int)(255 * f);
			ly = cy - (pLevels->frequency[0][i] >> 8) / 2;

			rx = (int)(255 * f);
			ry = cy + (pLevels->frequency[1][i] >> 8) / 2;

			if ((lx == rx) && (ly == ry)) {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * f, v, 1));
			}
			else {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * f, v, 1));
				DrawPixel(hPaintDC, rx, ry, HSV_RGB(6 * f, v, 1));
			}
		}
		break;
	}
	case PRESET_WAVE: {
		int i;

		for (i = 0; i < (pLevels->bufferSize); i++) {
			lx = (int)((double)i / (double)(pLevels->bufferSize) * 255);
			ly = ((pLevels->waveform[0][i] >> 8) + 128);

			rx = (int)((double)i / (double)(pLevels->bufferSize) * 255);
			ry = ((pLevels->waveform[1][i] >> 8) + 128);

			if ((lx == rx) && (ly == ry)) {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * ly / 255.0, 1, 1));
			}
			else {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * ly / 255.0, 1, 1));
				DrawPixel(hPaintDC, rx, ry, HSV_RGB(6 * ry / 255.0, 1, 1));
			}
		}
		break;
	}
	case PRESET_WAVE_MIX: {
		int i, j;
		double f;

		for (j = 0; j < (pLevels->bufferSize); j++) {
			f = (double)(pLevels->bufferSize - 1) * (double)j / ((double)32 * (double)pLevels->bufferSize);
			i = (int)((double)pLevels->bufferSize * (f - (int)f));
			lx = (int)((double)j / (double)(pLevels->bufferSize) * 255);
			ly = ((pLevels->waveform[0][i] >> 8) + 128);

			rx = (int)((double)j / (double)(pLevels->bufferSize) * 255);
			ry = ((pLevels->waveform[1][i] >> 8) + 128);

			if ((lx == rx) && (ly == ry)) {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * ly / 255.0, 1, 1));
			}
			else {
				DrawPixel(hPaintDC, lx, ly, HSV_RGB(6 * ly / 255.0, 1, 1));
				DrawPixel(hPaintDC, rx, ry, HSV_RGB(6 * ry / 255.0, 1, 1));
			}
		}
		break;
	}
	case PRESET_ATTRACTOR1: {
		int i, la, lb, ra, rb;
		int lcr, rcr, lcg, rcg, lcb, rcb;
		double lr, rr, lt, rt;

		for (i = 0; i < (pLevels->bufferSize - SAMPLE_DISTANCE - 1); i++) {
			lx = (int)((pLevels->waveform[0][i                  ] >> 8) + 128) - cx;
			ly = (int)((pLevels->waveform[0][i + SAMPLE_DISTANCE] >> 8) + 128) - cy;
			lr = sqrt(lx * lx + ly * ly);
			lt = atan2(ly, lx);
			lx = (int)(lt / pi * cx + cx);
			//ly = m_BMInf.bV4Height - (int)(1.414 * lr);
			ly = cy - (int)(lr / 1.414);

			la = (int)(((pLevels->waveform[0][i +                   1] >> 8) + 128)
					 - ((pLevels->waveform[0][i                      ] >> 8) + 128));
			lb = (int)(((pLevels->waveform[0][i + SAMPLE_DISTANCE + 1] >> 8) + 128)
					 - ((pLevels->waveform[0][i + SAMPLE_DISTANCE    ] >> 8) + 128));

			rx = (int)((pLevels->waveform[1][i                  ] >> 8) + 128) - cx;
			ry = (int)((pLevels->waveform[1][i + SAMPLE_DISTANCE] >> 8) + 128) - cy;
			rr = sqrt(rx * rx + ry * ry);
			rt = atan2(ry, rx);
			rx = (int)(rt / pi * cx + cx);
			//ry = m_BMInf.bV4Height - (int)(1.414 * rr);
			ry = cy - (int)(rr / 1.414);

			ra = (int)(((pLevels->waveform[1][i +                   1] >> 8) + 128)
					 - ((pLevels->waveform[1][i                      ] >> 8) + 128));
			rb = (int)(((pLevels->waveform[1][i + SAMPLE_DISTANCE + 1] >> 8) + 128)
					 - ((pLevels->waveform[1][i + SAMPLE_DISTANCE    ] >> 8) + 128));

			lcr = 12 * (int)sqrt(la * la + lb * lb);
			lcg = 0x1FF - lcr;
			lcr = ((lcr > 0xFF) ? 0xFF : lcr);

			lcg = ((lcg > 0xFF) ? 0xFF : ((lcg < 0) ? 0 : lcg));

			lcb = (int)lr;
			lcb = ((lcb > 0xFF) ? 0xFF : lcb);

			rcr = 12 * (int)sqrt(ra * ra + rb * rb);
			rcb = 0x1FF - rcr;
			rcr = ((rcr > 0xFF) ? 0xFF : rcr);

			rcb = ((rcb > 0xFF) ? 0xFF : ((rcb < 0) ? 0 : rcb));

			rcg = (int)rr;
			rcg = ((rcg > 0xFF) ? 0xFF : rcg);

			if ((lx == rx) && (ly == ry)) {
				DrawPixel(hPaintDC, lx, ly, 0xFFFF00 + ((lcr + rcr) / 2));
			}
			else {
				DrawPixel(hPaintDC, lx, ly, (lcb << 16) + (lcg << 8) + lcr);
				DrawPixel(hPaintDC, rx, ry, (rcb << 16) + (rcg << 8) + rcr);
			}
		}
		break;
	}
	case PRESET_ATTRACTOR2: {
		int i, la, lb, ra, rb;
		int lcr, rcr, lcg, rcg, lcb, rcb;
		double lr, rr, lt, rt;

		for (i = 0; i < (pLevels->bufferSize - SAMPLE_DISTANCE - 1); i++) {
			lx = (int)((pLevels->waveform[0][i                  ] >> 8) + 128) - cx;
			ly = (int)((pLevels->waveform[0][i + SAMPLE_DISTANCE] >> 8) + 128) - cy;
			lr = sqrt(lx * lx + ly * ly);
			lt = atan2(ly, lx);
			lx = (int)(lt / pi * cx + cx);
			//ly = m_BMInf.bV4Height - 1 - (int)(1.414 * lr);
			ly = m_BMInf.bV4Height - 1 - (int)(2 * lr);

			la = (int)(((pLevels->waveform[0][i +                   1] >> 8) + 128)
					 - ((pLevels->waveform[0][i                      ] >> 8) + 128));
			lb = (int)(((pLevels->waveform[0][i + SAMPLE_DISTANCE + 1] >> 8) + 128)
					 - ((pLevels->waveform[0][i + SAMPLE_DISTANCE    ] >> 8) + 128));

			rx = (int)((pLevels->waveform[1][i                  ] >> 8) + 128) - cx;
			ry = (int)((pLevels->waveform[1][i + SAMPLE_DISTANCE] >> 8) + 128) - cy;
			rr = sqrt(rx * rx + ry * ry);
			rt = atan2(ry, rx);
			rx = (int)(rt / pi * cx + cx);
			//ry = (int)(1.414 * rr);
			ry = (int)(2 * rr);

			ra = (int)(((pLevels->waveform[1][i +                   1] >> 8) + 128)
					 - ((pLevels->waveform[1][i                      ] >> 8) + 128));
			rb = (int)(((pLevels->waveform[1][i + SAMPLE_DISTANCE + 1] >> 8) + 128)
					 - ((pLevels->waveform[1][i + SAMPLE_DISTANCE    ] >> 8) + 128));

			lcr = 12 * (int)sqrt(la * la + lb * lb);
			lcg = 0x1FF - lcr;
			lcr = ((lcr > 0xFF) ? 0xFF : lcr);

			lcg = ((lcg > 0xFF) ? 0xFF : ((lcg < 0) ? 0 : lcg));

			lcb = (int)lr;
			lcb = ((lcb > 0xFF) ? 0xFF : lcb);

			rcr = 12 * (int)sqrt(ra * ra + rb * rb);
			rcb = 0x1FF - rcr;
			rcr = ((rcr > 0xFF) ? 0xFF : rcr);

			rcb = ((rcb > 0xFF) ? 0xFF : ((rcb < 0) ? 0 : rcb));

			rcg = (int)rr;
			rcg = ((rcg > 0xFF) ? 0xFF : rcg);

			if ((lx == rx) && (ly == ry)) {
				DrawPixel(hPaintDC, lx, ly, 0xFFFF00 + ((lcr + rcr) / 2));
			}
			else {
				DrawPixel(hPaintDC, lx, ly, (lcb << 16) + (lcg << 8) + lcr);
				DrawPixel(hPaintDC, rx, ry, (rcb << 16) + (rcg << 8) + rcr);
			}
		}
		break;
	}
	default:
		break;
	}

	/*{
		int x, y;
		TARGB *pSrc, *pDst;

		pSrc = m_pBitmapData[m_FrameNo];
		pDst = m_pBitmapData[NextFrame];
		for(x = 0; x < m_BMInf.bV4Width; x++){
			for(y = 0; y < m_BMInf.bV4Height; y++){
				pDst->Int = (pSrc->Int >> 1) & 0x007F7F7F;
				pDst++;
				pSrc++;
			}
		}
	}*/

	{
		int x, y, x1, y1, x2, y2, cx, cy;
		int xd, yd, a, b;
		double sa, ca, r;
		TARGB color;
		TARGB *pSrc, *pDst;

		a = 3;
		b = (m_nPreset == PRESET_ATTRACTOR1) ? 1 : 0;
		r = 1;
		sa = 0;
		ca = 1;
		cx = 0;
		cy = 0;

		pSrc = m_pBitmapData[m_FrameNo];
		pDst = m_pBitmapData[NextFrame];

		for (y = 0; y < m_BMInf.bV4Height; y++) {
			for (x = 0; x < m_BMInf.bV4Width; x++) {
				xd = (int)(r * ((x - cx) * ca - (y - cy) * sa)) + a;
				yd = (int)(r * ((x - cx) * sa + (y - cy) * ca)) + b;

				color.Int = 0x00000000;
				for (y1 = 0; y1 <= 2; y1++) {
					y2 = yd + y1 - 1;
					if ((y2 >= 0) && (y2 < m_BMInf.bV4Height)) {
						for (x1 = 0; x1 <= 2; x1++) {
							x2 = xd + x1 - 1;
							if (x2 < 0) {
								color.Int += (pSrc[(y2 + 1) * m_BMInf.bV4Width + x2].Int >> Shift[x1][y1]) & Mask[x1][y1];
							}
							else if (x2 >= m_BMInf.bV4Width) {
								color.Int += (pSrc[(y2 - 1) * m_BMInf.bV4Width + x2].Int >> Shift[x1][y1]) & Mask[x1][y1];
							}
							else {
								color.Int += (pSrc[y2 *       m_BMInf.bV4Width + x2].Int >> Shift[x1][y1]) & Mask[x1][y1];
							}
						}
					}
				}

				*pDst = color;
				pDst++;
			}
		}
	}

	{
		int x, y;
		TARGB *pSrc, *pDst, Col1, Col2;

		pSrc = m_pBitmapData[NextFrame];
		pSrc->Int = 0;
		pDst = m_pBitmapData[2];
		for (x = 0; x < m_BMInf.bV4Width; x++) {
			for (y = 0; y < m_BMInf.bV4Height; y++) {
				Col1 = pSrc[m_Map[x][y][0]];
				Col2 = pSrc[m_Map[x][y][1]];

				*pDst = Col2;
				AlphaBlendingPixel(128, pDst, Col1);

				pDst++;
			}
		}

		hPaintDC = &m_hDC[2];
	}

	{
		Convert(*hPaintDC, hdc, prc);
	}

	m_FrameNo = NextFrame;

	return 0;
}

void CKKSphere::Convert(LCD_Handler_t &srcMat, LCD_Handler_t &dstMat, TRectangle *prc)
{
	uint32_t *src = (uint32_t *)srcMat._buffer;
	uint16_t *dst = (uint16_t *)dstMat._buffer;
	int w = dstMat._width;
	int t = prc->top, b = prc->bottom;
	int l = prc->left, r = prc->right;

	for (int y = t; y < b; y++) {
		uint16_t *pos = &dst[y * w + l];
		for (int x = l; x < r; x++) {
			uint32_t tmp1 = *src++;
			uint16_t a = (tmp1 & 0x71000000) >> 15;
			uint16_t r = (tmp1 & 0x00F00000) >> 12;
			uint16_t g = (tmp1 & 0x0000F000) >> 8;
			uint16_t b = (tmp1 & 0x000000F0) >> 4;
			*pos++ = a | r | g | b;
		}
	}
}

void CKKSphere::OnStart()
{
	m_Rect.left = (m_Lcd->_width - 256) / 2;
	m_Rect.top = (m_Lcd->_height - 256) / 2;
	m_Rect.right = m_Rect.left + 256;
	m_Rect.bottom = m_Rect.top + 256;
}

void CKKSphere::OnEnd()
{
}

int CKKSphere::GetTimer()
{
	return m_Timer;
}

void CKKSphere::Progress(int elapse)
{
	m_Timer -= elapse;
	if (m_Timer < 0)
		m_Timer = 0;
}

void CKKSphere::ProcessEvent(InterTaskSignals::T signals)
{
}

void CKKSphere::Process()
{
	if (m_State != State::Starting)
		return;

	m_State = State::Progress;
	m_Timer = osWaitForever;

	Render(&m_TimedLevel, *m_Lcd, &m_Rect);

	m_State = State::Ready;
	m_Timer = 10;
}
