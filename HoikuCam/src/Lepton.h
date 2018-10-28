#ifndef LEPTON_H
#define LEPTON_H

#include "TaskBase.h"

//#define ADDRESS  (0x2A)
#define ADDRESS  (0x54)

#define AGC (0x01)
#define SYS (0x02)
#define VID (0x03)
#define OEM (0x08)

#define GET (0x00)
#define SET (0x01)
#define RUN (0x02)

#define VOSPI_FRAME_SIZE (164)
#define IMAGE_SIZE (800)

class SensorTask;

class LeptonTask : public Task
{
public:
	class State
	{
	public:
		enum T {
			PowerOff,
			Viewing,
		};
	};
public:
	LeptonTask(SensorTask *sensorTask);
	virtual ~LeptonTask();
private:
	State::T _state;
	SensorTask *_sensorTask;
	mbed::SPI _spi;
	mbed::I2C _wire;
	mbed::DigitalOut _ss;
	uint8_t lepton_frame_packet[VOSPI_FRAME_SIZE];
	uint8_t image[IMAGE_SIZE];
	int image_index;
	int spi_read_word(int data);
	void read_lepton_frame(void);
	void lepton_sync(void);
	void print_lepton_frame(void);
	void print_image(void);
	void lepton_command(unsigned int moduleID, unsigned int commandID, unsigned int command);
	void agc_enable();
	void set_reg(unsigned int reg);
	int read_reg(unsigned int reg);
	int read_data();
public:
	void Init();
	void OnStart() override;
	void ProcessEvent(InterTaskSignals::T signals) override;
	void Process() override;
};

#endif // LEPTON_H
