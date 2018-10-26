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

Lepton::Lepton(PinName sda, PinName scl, PinName ss,
	PinName mosi, PinName miso, PinName sclk, PinName ssel) :
	Task(osWaitForever),
	_state(State::PowerOff),
	_wire(sda, scl),
	_ss(ss),
	_spi(mosi, miso, sclk, ssel),
	image_index(0)
{
}

Lepton::~Lepton()
{
}

void Lepton::Init()
{
	_spi.format(8, 3/*?*/);
}

int Lepton::spi_read_word(int data)
{
	uint8_t write_data[2];
	uint8_t read_data[2] = { 0, 0 };

	// take the SS pin low to select the chip:
	_ss = false;
	//  send in the address and value via SPI:
	write_data[0] = (uint8_t)(data >> 8);
	write_data[1] = (uint8_t)(data);
	_spi.write((char *)write_data, sizeof(write_data), (char *)read_data, sizeof(read_data));
	// take the SS pin high to de-select the chip:
	_ss = true;
	return (read_data[0] << 8) | read_data[1];
}

void Lepton::read_lepton_frame(void)
{
	int i;
	uint8_t write_data[2] = { 0, 0 };
	uint8_t read_data[2] = { 0, 0 };

	for (i = 0; i < (VOSPI_FRAME_SIZE / 2); i++) {
		_ss = false;
		//  send in the address and value via SPI:
		_spi.write((char *)write_data, sizeof(write_data), (char *)read_data, sizeof(read_data));
		lepton_frame_packet[2 * i] = read_data[0];
		lepton_frame_packet[2 * i + 1] = read_data[1];

		// take the SS pin high to de-select the chip:
		_ss = true;
	}
}

void Lepton::lepton_sync(void)
{
	int i;
	int data = 0x0f;
	uint8_t write_data[2] = { 0, 0 };
	uint8_t read_data[2] = { 0, 0 };

	_ss = true;
	osDelay(185);
	while ((data & 0x0f) == 0x0f) {
		_ss = false;
		_spi.write((char *)write_data, sizeof(write_data), (char *)read_data, sizeof(read_data));
		data = (read_data[0] << 8) | read_data[1];
		_ss = true;

		for (i = 0; i < ((VOSPI_FRAME_SIZE - 2) / 2); i++) {
			_ss = false;
			_spi.write((char *)write_data, sizeof(write_data), (char *)read_data, sizeof(read_data));
			_ss = true;
		}
	}
}

void Lepton::print_lepton_frame(void)
{
	int i;
	for (i = 0; i < (VOSPI_FRAME_SIZE); i++) {
		printf("%x,", lepton_frame_packet[i]);
	}
	printf(" \n");
}

void Lepton::print_image(void)
{
	int i;
	for (i = 0; i < (IMAGE_SIZE); i++) {
		printf("%x,", image[i]);
	}
	printf(" \n");
}

void Lepton::lepton_command(unsigned int moduleID, unsigned int commandID, unsigned int command)
{
	uint8_t error;
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

void Lepton::agc_enable()
{
	uint8_t error;
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

void Lepton::set_reg(unsigned int reg)
{
	uint8_t error;
	uint8_t write_data[2];

	write_data[0] = reg >> 8 & 0xff;
	write_data[0] = reg & 0xff;            // sends one uint8_t

	error = _wire.write(ADDRESS, (char *)write_data, sizeof(write_data));
	if (error != 0) {
		printf("error=%d\n", error);
	}
}

int Lepton::read_reg(unsigned int reg)
{
	int reading = 0;
	uint8_t read_data[2] = { 0, 0 };
	char temp[20];

	set_reg(reg);

	_wire.read(ADDRESS, (char *)read_data, sizeof(read_data));

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

int Lepton::read_data()
{
	int i;
	int data;
	int payload_length;

	while (read_reg(0x2) & 0x01) {
		printf("busy\n");
	}

	payload_length = read_reg(0x6);
	printf("payload_length=%d\n", payload_length);

	_wire.read(ADDRESS, NULL, 0, true);
	//set_reg(0x08);
	for (i = 0; i < (payload_length / 2); i++) {
		data = _wire.read(1) << 8;
		data |= _wire.read(1);
		printf("%x\n", data);
	}
	_wire.stop();

	return 0;
}

void Lepton::OnStart()
{
	std::string debugString();
	printf("beginTransmission\n");

	//set_reg(0);

	//read_reg(0x0);

	read_reg(0x2);


	printf("SYS Camera Customer Serial Number\n");
	lepton_command(SYS, 0x28 >> 2, GET);
	read_data();

	printf("SYS Flir Serial Number\n");
	lepton_command(SYS, 0x2, GET);
	read_data();

	printf("SYS Camera Uptime\n");
	lepton_command(SYS, 0x0C >> 2, GET);
	read_data();

	printf("SYS Fpa Temperature Kelvin\n");
	lepton_command(SYS, 0x14 >> 2, GET);
	read_data();

	printf("SYS Aux Temperature Kelvin\n");
	lepton_command(SYS, 0x10 >> 2, GET);
	read_data();

	printf("OEM Chip Mask Revision\n");
	lepton_command(OEM, 0x14 >> 2, GET);
	read_data();

	//printf("OEM Part Number\n");
	//lepton_command(OEM, 0x1C >> 2 , GET);
	//read_data();

	printf("OEM Camera Software Revision\n");
	lepton_command(OEM, 0x20 >> 2, GET);
	read_data();

	printf("AGC Enable\n");
	//lepton_command(AGC, 0x01  , SET);
	agc_enable();
	read_data();

	printf("AGC READ\n");
	lepton_command(AGC, 0x00, GET);
	read_data();

	// printf("SYS Telemetry Enable State\n");
	//lepton_command(SYS, 0x19>>2 ,GET);
	// read_data();
}

void Lepton::ProcessEvent(InterTaskSignals::T signals)
{
	if ((signals & InterTaskSignals::PowerOn) != 0) {
		_state = State::Viewing;
		_timer = 100;
	}
	if ((signals & InterTaskSignals::PowerOff) != 0) {
		_state = State::PowerOff;
		_timer = osWaitForever;
	}
}

void Lepton::Process()
{
	if (_timer != 0)
		return;

	_timer = 100;

	//lepton_sync();
	read_lepton_frame();
	//if(lepton_frame_packet[i]&0x0f != 0x0f )
	{
	  //print_lepton_frame();
	}
}
