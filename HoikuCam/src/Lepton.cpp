/*
Copyright (c) 2014, Pure Engineering LLC
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <mbed.h>
#include <string>
#include "Lepton.h"
#include "Palettes.h"
#include "EasyAttach_CameraAndLCD.h"

//#define ADDRESS  (0x2A)
#define ADDRESS  (0x54)

#define AGC (0x01)
#define SYS (0x02)
#define VID (0x03)
#define OEM (0x08)

#define GET (0x00)
#define SET (0x01)
#define RUN (0x02)

#define RESULT_BUFFER_BYTE_PER_PIXEL  (2u)
#define RESULT_BUFFER_STRIDE          (((LCD_PIXEL_WIDTH * RESULT_BUFFER_BYTE_PER_PIXEL) + 31u) & ~31u)
#define RESULT_BUFFER_HEIGHT          (LCD_PIXEL_HEIGHT)
extern uint8_t user_frame_buffer_result[RESULT_BUFFER_STRIDE * RESULT_BUFFER_HEIGHT]__attribute((section("NC_BSS"), aligned(32)));

LeptonTask::LeptonTask(SensorTask *sensorTask) :
	Task(osWaitForever),
	_state(State::PowerOff),
	_sensorTask(sensorTask),
	_spi(P5_6, P5_7, P5_4, NC),
	//_spi(P4_6, P4_7, P4_4, NC),
	_wire(I2C_SDA, I2C_SCL),
	_ss(P5_8),
	//_ss(P4_5),
	resets(0),
	_packet_per_frame(60)
{
}

LeptonTask::~LeptonTask()
{
}

void LeptonTask::Init()
{
	_spi.format(8, 3/*?*/);
	//_spi.frequency(20000000);
	_spi.frequency(16000000);
}

void LeptonTask::command(unsigned int moduleID, unsigned int commandID, unsigned int command)
{
	int error;
	uint8_t write_data[4];

	// Command Register is a 16-bit register located at Register Address 0x0004
	write_data[0] = 0x00;
	write_data[1] = 0x04;

	if (moduleID == 0x08) //OEM module ID
	{
		write_data[2] = 0x48;
	}
	else {
		write_data[2] = moduleID & 0x0f;
	}
	write_data[3] = ((commandID << 2) & 0xfc) | (command & 0x3);

	error = _wire.write(ADDRESS, (char *)write_data, sizeof(write_data));
	if (error != 0) {
		printf("error=%d\n", error);
	}
}

void LeptonTask::agc_enable()
{
	int error;
	uint8_t write_data[4];

	write_data[0] = 0x01;
	write_data[1] = 0x05;
	write_data[2] = 0x00;
	write_data[3] = 0x01;

	error = _wire.write(ADDRESS, (char *)write_data, sizeof(write_data));
	if (error != 0) {
		printf("error=%d\n", error);
	}
}

void LeptonTask::set_reg(unsigned int reg)
{
	int error;
	uint8_t write_data[2];

	write_data[0] = reg >> 8 & 0xff;
	write_data[1] = reg & 0xff;            // sends one uint8_t

	error = _wire.write(ADDRESS, (char *)write_data, sizeof(write_data));
	if (error != 0) {
		printf("error=%d\n", error);
	}
}

int LeptonTask::read_reg(unsigned int reg)
{
	int error;
	int reading = 0;
	uint8_t read_data[2] = { 0, 0 };
	char temp[20];

	set_reg(reg);

	error = _wire.read(ADDRESS, (char *)read_data, sizeof(read_data));
	if (error != 0) {
		printf("error=%d\n", error);
	}

	reading = read_data[0];  // receive high uint8_t (overwrites previous reading)
	//Serial.println(reading);
	reading = reading << 8;    // shift high uint8_t to be high 8 bits

	reading |= read_data[1]; // receive low uint8_t as lower 8 bits
	for (int i = 0, b = 1 << 15; i < 16; i++, b >>= 1) {
		temp[i] = (reading & b) ? '1' : '0';
	}
	temp[16] = '\0';
	printf("reg:%d==0x%x binary:%s\n", reg, reading, temp);
	return reading;
}

int LeptonTask::read_data(uint8_t *data, int len)
{
	int i;
	int payload_length;

	while (read_reg(0x2) & 0x01) {
		printf("busy\n");
	}

	payload_length = read_reg(0x6);
	printf("payload_length=%d\n", payload_length);
	memset(data, 0xFF, len);
	_wire.read(ADDRESS, (char *)data, payload_length);
	for (i = 0; i < payload_length; i += 2) {
		printf("%04x\n", (data[i] << 8) | data[i + 1]);
	}

	return 0;
}

void LeptonTask::OnStart()
{
	uint8_t data[32];

	_ss = 1;

	_ss = 0;

	_ss = 1;

	Thread::wait(185);

	printf("beginTransmission\n");

	//set_reg(0);

	//read_reg(0x0);

	read_reg(0x2);

	printf("SYS Camera Customer Serial Number\n");
	command(SYS, 0x28 >> 2, GET);
	read_data(data, sizeof(data));

	printf("SYS Flir Serial Number\n");
	command(SYS, 0x2, GET);
	read_data(data, sizeof(data));

	printf("SYS Camera Uptime\n");
	command(SYS, 0x0C >> 2, GET);
	read_data(data, sizeof(data));

	printf("SYS Fpa Temperature Kelvin\n");
	command(SYS, 0x14 >> 2, GET);
	read_data(data, sizeof(data));

	printf("SYS Aux Temperature Kelvin\n");
	command(SYS, 0x10 >> 2, GET);
	read_data(data, sizeof(data));

	printf("OEM Chip Mask Revision\n");
	command(OEM, 0x14 >> 2, GET);
	read_data(data, sizeof(data));

	//printf("OEM Part Number\n");
	//command(OEM, 0x1C >> 2 , GET);
	//read_data(data, sizeof(data));

	printf("OEM Camera Software Revision\n");
	command(OEM, 0x20 >> 2, GET);
	read_data(data, sizeof(data));
#if 0
	printf("AGC Enable\n");
	//command(AGC, 0x01, SET);
	agc_enable();
	read_data(data, sizeof(data));
#endif
	printf("AGC READ\n");
	command(AGC, 0x00, GET);
	read_data(data, sizeof(data));

	printf("SYS Telemetry Enable State");
	command(SYS, 0x18 >> 2 ,GET);
	read_data(data, sizeof(data));

	int telemetory = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	if (telemetory != 0) {
		_packet_per_frame = 63;
	}
	else {
		_packet_per_frame = 60;
	}

	printf("VID Focus ROI Select\n");
	command(/*VID*/0x03, 0x10>>2, GET);
	read_data(data, sizeof(data));

	_timer = 500;
	_state = State::Capture;
}

void LeptonTask::ProcessEvent(InterTaskSignals::T signals)
{
	if ((signals & InterTaskSignals::PowerOn) != 0) {
		_state = State::Capture;
		_timer = 0;
	}
	if ((signals & InterTaskSignals::PowerOff) != 0) {
		_state = State::PowerOff;
		_timer = -1;
	}
}

void LeptonTask::Process()
{
	uint8_t *result = _frame_packet;
	uint16_t *frameBuffer;
	uint16_t value, minValue, maxValue;
	float diff, scale;
	int packet_id;

	if (_timer != 0)
		return;

	switch (_state) {
	case State::Resets:
		_ss = 1;
		_state = State::Capture;
		_timer = 1;
		break;
	case State::Capture:
		minValue = 65535;
		maxValue = 0;
		packet_id = -1;
		for (int row = 0; row < PACKETS_PER_FRAME; )
		{
			_ss = 0;
			//read data packets from lepton over SPI
			_spi.write(NULL, 0, (char *)result, PACKET_SIZE);
			_ss = 1;

			int id = (result[0] << 8) | result[1];
			if (id == packet_id) {
				row++;
				continue;
			}
			else if ((id & 0x0F00) == 0x0F00) {
				if (packet_id != -1) {
					row++;
					continue;
				}
				_state = State::Capture;
				_timer = 0;
				return;
			}

			if ((packet_id == -1) && ((id & 0x0FFF) != 0x7FF)) {
				int r = 2 * (id & 0x003F);
				if (r >= PACKETS_PER_FRAME)
					continue;
				row = r;
			}
			packet_id = id;

			uint16_t *pixel = &image[PIXEL_PER_LINE * row];
			frameBuffer = (uint16_t *)result;
			//skip the first 2 uint16_t's of every packet, they're 4 header bytes
			for (int i = 2; i < PACKET_SIZE_UINT16; i++) {
				//flip the MSB and LSB at the last second
				value = frameBuffer[i];
				value = (value >> 8) | (value << 8);
				//frameBuffer[i] = value;

				if (value > maxValue) {
					maxValue = value;
				}
				if (value < minValue) {
					minValue = value;
				}
				*pixel++ = value;
			}

			row++;
		}

		if (packet_id == -1) {
			_state = State::Capture;
			_timer = 0;
			return;
		}

		_maxValue = maxValue;
		_minValue = minValue;

		resets++;
		_state = State::Viewing;
		_timer = 0;
		break;
	case State::Viewing:
		//lets emit the signal for update
		minValue = _minValue;
		diff = _maxValue - minValue;
		scale = 255.9 / diff;
		for (int row = 0; row < PACKETS_PER_FRAME; row++) {
			uint16_t *values = &image[PIXEL_PER_LINE * row];
			uint16_t *pixel = (uint16_t *)&user_frame_buffer_result[RESULT_BUFFER_BYTE_PER_PIXEL * LCD_PIXEL_WIDTH * row];
			for (int column = 0; column < PIXEL_PER_LINE; column++) {
				//uint8_t index = (*values - minValue) * scale;
				//uint8_t index = *values;
				uint8_t index = (uint8_t)(*values >> 1);
				const uint8_t *colormap = &colormap_rainbow[3 * index];
				// ARGB4444
				*pixel++ = 0xF000 | ((*colormap++ >> 4) << 16) | ((*colormap++ >> 4) << 8) | ((*colormap++ >> 4) << 0);
			}
		}

		//Note: we've selected 750 resets as an arbitrary limit, since there should never be 750 "null" packets between two valid transmissions at the current poll rate
		//By polling faster, developers may easily exceed this count, and the down period between frames may then be flagged as a loss of sync
		if (resets == 750) {
			resets = 0;
			_ss = 0;
			_state = State::Resets;
			_timer = 750;
		}
		else {
			_state = State::Capture;
			_timer = 100;
		}
		break;
	}
}
