/////////////////////////////////////////////////////////////////////////////
//
// KKSphere.h : Declaration of the CKKSphere
//
/////////////////////////////////////////////////////////////////////////////

#ifndef __KKSPHERE_H_
#define __KKSPHERE_H_

#include <stdint.h>
#include "TaskBase.h"
#include "adafruit_gfx.h"
#include "opencv2/imgproc.hpp"

class GlobalState;

typedef uint32_t TColor;

typedef struct TRectangle {
	int32_t			left;
	int32_t			top;
	int32_t			right;
	int32_t			bottom;
} TRectangle;

#pragma pack(push, 1)
typedef struct TBitmapHeader {
	uint32_t		bV4Size;
	int32_t			bV4Width;
	int32_t			bV4Height;
	uint16_t		bV4Planes;
	uint16_t		bV4BitCount;
	uint32_t		bV4V4Compression;
	uint32_t		bV4SizeImage;
	int32_t			bV4XPelsPerMeter;
	int32_t			bV4YPelsPerMeter;
	uint32_t		bV4ClrUsed;
	uint32_t		bV4ClrImportant;
	uint32_t		bV4RedMask;
	uint32_t		bV4GreenMask;
	uint32_t		bV4BlueMask;
	uint32_t		bV4AlphaMask;
	uint32_t		bV4CSType;
	uint8_t			bV4Endpoints[4 * 3 * 3];
	uint32_t		bV4GammaRed;
	uint32_t		bV4GammaGreen;
	uint32_t		bV4GammaBlue;
} __attribute__((packed)) TBitmapHeader;
#pragma pack(pop)

#define BI_BITFIELDS 3

typedef struct {
	int bufferSize;
	uint16_t *frequency[2];
	int16_t *waveform[2];
} TTimedLevel;

// preset values
typedef enum TPresetValues {
	PRESET_FREQ1 = 0,
	PRESET_FREQ1_MIX,
	PRESET_FREQ2,
	PRESET_FREQ2_MIX,
	PRESET_WAVE,
	PRESET_WAVE_MIX,
	PRESET_ATTRACTOR1,
	PRESET_ATTRACTOR2,
	PRESET_COUNT
} TPresetValues;

/////////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
typedef union TARGB {
	struct {
		uint8_t Blue;
		uint8_t Green;
		uint8_t Red;
		uint8_t Alpha;
	};
	uint32_t Int;
} __attribute__((packed)) TARGB;
#pragma pack(pop)

/////////////////////////////////////////////////////////////////////////////
// CKKSphere
class CKKSphere : public TaskThread, public ITask
{
public:
	CKKSphere(GlobalState *globalState, LCD_Handler_t *lcd);
	~CKKSphere();
private:
	GlobalState		*m_GlobalState;
	enum class State {
		Ready,
		Starting,
		Progress,
	};
	State			m_State;
	int				m_Timer;
	TColor			m_clrLeft;     // foreground color
	TColor			m_clrRight;    // foreground color
	TPresetValues	m_nPreset;     // current preset
	LCD_Handler_t	m_hDC[3];
	int				m_FrameNo;
	TBitmapHeader	m_BMInf;
	TARGB			m_pBitmapData[3][256 * 256];
	TRectangle		m_Rect;
	uint16_t		m_Map[256][256][2];
	LCD_Handler_t	*m_Lcd;
	TTimedLevel		m_TimedLevel;

	inline uint32_t SwapBytes(uint32_t dwRet)
	{
		return ((dwRet & 0x0000FF00) | ((dwRet & 0x00FF0000) >> 16) | ((dwRet & 0x000000FF) << 16));
	}
	inline void DrawPixel(LCD_Handler_t* hlcd, int16_t x, int16_t y, uint32_t color)
	{
		int t = x + y * hlcd->_width;
		if ((t < 0) || (t >= hlcd->_width * hlcd->_height))
			return;
		((uint32_t *)hlcd->_buffer)[t] = 0xFF000000 | color;
	}
	void Mapping();
	void Convert(LCD_Handler_t& src, LCD_Handler_t& dst, TRectangle *prc);
public:
	bool IsReady() { return m_State == State::Ready; }
	int Render(TTimedLevel *pLevels, LCD_Handler_t &hdc, TRectangle *prc);
	void Update(TTimedLevel &pLevels) {
		if (m_State != State::Ready)
			return;

		m_TimedLevel = pLevels;
		m_State = State::Starting;
		m_Timer = 0;
		//Signal(InterTaskSignals::UpdateImage);
	}
public:
	void OnStart() override;
	void OnEnd() override;
	int GetTimer() override;
	void Progress(int elapse) override;
	void ProcessEvent(InterTaskSignals::T signals) override;
	void Process() override;
};

#endif //__KKSPHERE_H_
