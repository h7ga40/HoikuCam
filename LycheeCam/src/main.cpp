// LycheeCam

#include "mbed.h"
#include "SdUsbConnect.h"
#include "GlobalState.h"
#include "MediaTask.h"
#include "NetTask.h"
#include "SensorTask.h"
#include "expat.h"
#include "draw_font.h"
#include "adafruit_gfx.h"
#include "bh1792.h"
#include "ZXingTask.h"
#include "EasyAttach_CameraAndLCD.h"
#include "qrcode.h"
#include "KKSphere.h"

#define FACE_DETECTOR_MODEL     "/storage/lbpcascade_frontalface.xml"

extern uint8_t user_frame_buffer_result[];
LCD_Handler_t lcd = { LCD_PIXEL_WIDTH, LCD_PIXEL_HEIGHT, user_frame_buffer_result };
uint16_t lcd_init_width = LCD_PIXEL_WIDTH;
uint16_t lcd_init_height = LCD_PIXEL_HEIGHT;

extern LCD_Handler_t audio_frame;

/* Application variables */
DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);
DigitalOut led4(LED4);

static GlobalState globalState;
static SdUsbConnect sdUsbConnect("storage");
//static FaceDetectTask faceDetectTask(&globalState);
//static CKKSphere kkSphereTask(&globalState, &audio_frame);
//static MediaTask mediaTask(&globalState, &faceDetectTask.face_roi, &kkSphereTask);
cv::Rect face_roi;
//static MediaTask mediaTask(&globalState, &face_roi, &kkSphereTask);
static MediaTask mediaTask(&globalState, &face_roi, NULL);
static SensorTask sensorTask(&globalState);
static ESP32Interface wifi(P5_3, P3_14, P7_1, P0_1);
static NetTask netTask(&globalState, &wifi);
static LeptonTaskThread leptonTask(&globalState);
static ZXingTask zxingTask(&globalState);

enum parse_state_t {
	psRoot,
	psWifi,
	psWifiSsid,
	psWifiPassword,
	psWifiHostName,
	psUpload,
	psUploadServer,
	psUploadStorage,
	psLepton,
	psLeptonAgc,
	psLeptonRadiometry,
	psLeptonFFCNorm,
	psLeptonTelemetry,
	psLeptonOffset,
	psLeptonSlope,
	psLeptonReference,
	psLeptonColor,
	psError
};

struct wifi_config_t {
	char ssid[32];
	char password[32];
	char host_name[32];
};

struct upload_config_t {
	char server[32];
	char storage[32];
};

struct config_data_t {
	parse_state_t state;
	int pos;
	char temp[16];
	wifi_config_t wifi;
	upload_config_t upload;
	lepton_config_t lepton;
};

static void XMLCALL
start(void *data, const XML_Char *el, const XML_Char **attr)
{
	config_data_t *cd = (config_data_t *)data;

	switch (cd->state) {
	case psRoot:
		if (strcmp(el, "wifi") == 0) {
			cd->state = psWifi;
		}
		else if (strcmp(el, "upload") == 0) {
			cd->state = psUpload;
		}
		else if (strcmp(el, "lepton") == 0) {
			cd->state = psLepton;
		}
		break;
	case psWifi:
		if (strcmp(el, "ssid") == 0) {
			cd->wifi.ssid[0] = '\0';
			cd->pos = 0;
			cd->state = psWifiSsid;
		}
		else if (strcmp(el, "password") == 0) {
			cd->wifi.password[0] = '\0';
			cd->pos = 0;
			cd->state = psWifiPassword;
		}
		else if (strcmp(el, "host_name") == 0) {
			cd->wifi.host_name[0] = '\0';
			cd->pos = 0;
			cd->state = psWifiHostName;
		}
		break;
	case psUpload:
		if (strcmp(el, "server") == 0) {
			cd->upload.server[0] = '\0';
			cd->pos = 0;
			cd->state = psUploadServer;
		}
		else if (strcmp(el, "storage") == 0) {
			cd->upload.storage[0] = '\0';
			cd->pos = 0;
			cd->state = psUploadStorage;
		}
		break;
	case psLepton:
		if (strcmp(el, "agc") == 0) {
			cd->lepton.agc = 0;
			cd->state = psLeptonAgc;
		}
		else if (strcmp(el, "radiometry") == 0) {
			cd->lepton.radiometry = 0;
			cd->state = psLeptonRadiometry;
		}
		else if (strcmp(el, "ffcnorm") == 0) {
			cd->lepton.ffcnorm = 0;
			cd->state = psLeptonFFCNorm;
		}
		else if (strcmp(el, "telemetry") == 0) {
			cd->lepton.telemetry = 0;
			cd->state = psLeptonTelemetry;
		}
		else if (strcmp(el, "offset") == 0) {
			cd->temp[0] = '\0';
			cd->pos = 0;
			cd->state = psLeptonOffset;
		}
		else if (strcmp(el, "slope") == 0) {
			cd->temp[0] = '\0';
			cd->pos = 0;
			cd->state = psLeptonSlope;
		}
		else if (strcmp(el, "reference") == 0) {
			cd->temp[0] = '\0';
			cd->pos = 0;
			cd->state = psLeptonReference;
		}
		else if (strcmp(el, "color") == 0) {
			cd->temp[0] = '\0';
			cd->pos = 0;
			cd->state = psLeptonColor;
		}
		break;
	default:
		break;
	}
}

static void XMLCALL
end(void *data, const XML_Char *el)
{
	config_data_t *cd = (config_data_t *)data;
	(void)el;

	switch (cd->state) {
	case psRoot:
		break;
	case psWifi:
		if (strcmp(el, "wifi") == 0) {
			cd->state = psRoot;
		}
		break;
	case psWifiSsid:
		if (strcmp(el, "ssid") == 0) {
			cd->state = psWifi;
		}
		break;
	case psWifiPassword:
		if (strcmp(el, "password") == 0) {
			cd->state = psWifi;
		}
		break;
	case psWifiHostName:
		if (strcmp(el, "host_name") == 0) {
			cd->state = psWifi;
		}
		break;
	case psUpload:
		if (strcmp(el, "upload") == 0) {
			cd->state = psRoot;
		}
		break;
	case psUploadServer:
		if (strcmp(el, "server") == 0) {
			cd->state = psUpload;
		}
		break;
	case psUploadStorage:
		if (strcmp(el, "storage") == 0) {
			cd->state = psUpload;
		}
		break;
	case psLeptonAgc:
		if (strcmp(el, "agc") == 0) {
			cd->state = psLepton;
		}
		break;
	case psLeptonRadiometry:
		if (strcmp(el, "radiometry") == 0) {
			cd->state = psLepton;
		}
		break;
	case psLeptonFFCNorm:
		if (strcmp(el, "ffcnorm") == 0) {
			cd->state = psLepton;
		}
		break;
	case psLeptonTelemetry:
		if (strcmp(el, "telemetry") == 0) {
			cd->state = psLepton;
		}
		break;
	case psLeptonOffset:
		if (strcmp(el, "offset") == 0) {
			cd->lepton.offset = atoi(cd->temp);
			cd->state = psLepton;
		}
		break;
	case psLeptonSlope:
		if (strcmp(el, "slope") == 0) {
			cd->lepton.slope = atoi(cd->temp);
			cd->state = psLepton;
		}
		break;
	case psLeptonReference:
		if (strcmp(el, "reference") == 0) {
			cd->lepton.reference = atoi(cd->temp);
			cd->state = psLepton;
		}
		break;
	case psLeptonColor:
		if (strcmp(el, "color") == 0) {
			cd->lepton.color = atoi(cd->temp);
			cd->state = psLepton;
		}
		break;
	default:
		break;
	}
}

static void XMLCALL
text(void *data, const XML_Char *s, int len)
{
	config_data_t *cd = (config_data_t *)data;
	int l, maxlen;

	switch (cd->state) {
	case psRoot:
	case psWifi:
		break;
	case psWifiSsid:
		maxlen = sizeof(cd->wifi.ssid) - 1;
		l = cd->pos + len;
		if (l > maxlen)
			len = maxlen - cd->pos;

		if (len > 0) {
			memcpy(cd->wifi.ssid + cd->pos, s, len);
			cd->pos += len;
			cd->wifi.ssid[cd->pos] = '\0';
		}
		break;
	case psWifiPassword:
		maxlen = sizeof(cd->wifi.password) - 1;
		l = cd->pos + len;
		if (l > maxlen)
			len = maxlen - cd->pos;

		if (len > 0) {
			memcpy(cd->wifi.password + cd->pos, s, len);
			cd->pos += len;
			cd->wifi.password[cd->pos] = '\0';
		}
		break;
	case psWifiHostName:
		maxlen = sizeof(cd->wifi.host_name) - 1;
		l = cd->pos + len;
		if (l > maxlen)
			len = maxlen - cd->pos;

		if (len > 0) {
			memcpy(cd->wifi.host_name + cd->pos, s, len);
			cd->pos += len;
			cd->wifi.host_name[cd->pos] = '\0';
		}
		break;
	case psUploadServer:
		maxlen = sizeof(cd->upload.server) - 1;
		l = cd->pos + len;
		if (l > maxlen)
			len = maxlen - cd->pos;

		if (len > 0) {
			memcpy(cd->upload.server + cd->pos, s, len);
			cd->pos += len;
			cd->upload.server[cd->pos] = '\0';
		}
		break;
	case psUploadStorage:
		maxlen = sizeof(cd->upload.storage) - 1;
		l = cd->pos + len;
		if (l > maxlen)
			len = maxlen - cd->pos;

		if (len > 0) {
			memcpy(cd->upload.storage + cd->pos, s, len);
			cd->pos += len;
			cd->upload.storage[cd->pos] = '\0';
		}
		break;
	case psLeptonAgc:
		if ((*s != '0') || (len != 1))
			cd->lepton.agc = 1;
		break;
	case psLeptonRadiometry:
		if ((*s != '0') || (len != 1))
			cd->lepton.radiometry = 1;
		break;
	case psLeptonFFCNorm:
		if ((*s != '0') || (len != 1))
			cd->lepton.ffcnorm = 1;
		break;
	case psLeptonTelemetry:
		if ((*s != '0') || (len != 1))
			cd->lepton.telemetry = 1;
		break;
	case psLeptonOffset:
	case psLeptonSlope:
	case psLeptonReference:
	case psLeptonColor:
		maxlen = sizeof(cd->temp) - 1;
		l = cd->pos + len;
		if (l > maxlen)
			len = maxlen - cd->pos;

		if (len > 0) {
			memcpy(cd->temp + cd->pos, s, len);
			cd->pos += len;
			cd->temp[cd->pos] = '\0';
		}
		break;
	default:
		break;
	}
}

config_data_t config;
int8_t temp[256];

bool ReadIniFile(std::string filename)
{
	XML_Parser parser;

	memset(&config, 0, sizeof(config));
	parser = XML_ParserCreate(NULL);
	if (!parser) {
		printf("Couldn't allocate memory for parser\n");
		return false;
	}

	XML_SetUserData(parser, &config);
	XML_SetElementHandler(parser, start, end);
	XML_SetCharacterDataHandler(parser, text);

	size_t len;
	FILE *fp = fopen(filename.c_str(), "rb");
	if (fp != NULL) {
		for (;;) {
			len = fread(temp, 1, sizeof(temp), fp);
			if (len <= 0)
				break;

			if ((config.state != psError) && (XML_Parse(parser, (char *)temp, len, 0) == 0)) {
				XML_Error error_code = XML_GetErrorCode(parser);
				printf("Parsing response buffer of size %uz failed"
					" with error code %d (%s).\n",
					len, error_code, XML_ErrorString(error_code));
				config.state = psError;
			}
		}

		fclose(fp);
	}

	XML_ParserFree(parser);

	return true;
}

void zxing_callback(const char *addr, int size)
{
	if (size <= 0) {
		lcd_fillRect(&lcd, 0, 20, lcd._width / 2, lcd._height / 2, 0x0000);
		return;
	}

	lcd_drawString(&lcd, addr, 0, 20, 0xFCCC, 0x0000);

	// The structure to manage the QR code
	QRCode qrcode;

	// Allocate a chunk of memory to store the QR code
	uint8_t *qrcodeBytes = (uint8_t *)malloc(qrcode_getBufferSize(3));
	if (qrcodeBytes != NULL) {
		qrcode_initText(&qrcode, qrcodeBytes, 3, ECC_LOW, addr);

		for (int y = 0; y < qrcode.size; y++) {
			for (int x = 0; x < qrcode.size; x++) {
				if (qrcode_getModule(&qrcode, x, y)) {
					lcd_fillRect(&lcd, 2 * x + 32, 2 * y + 32, 2, 2, 0xF000);
					//lcd_drawPixel(&lcd, x, y, 0xFFFF);
				}
				else {
					lcd_fillRect(&lcd, 2 * x + 32, 2 * y + 32, 2, 2, 0xCCCC);
					//lcd_drawPixel(&lcd, x, y, 0xFFFF);
				}
			}
		}

		free(qrcodeBytes);
	}
}

int main(void)
{
	char textbuf[80];
	LeptonTask *lepton;

	globalState.storage = &sdUsbConnect;
	globalState.wifi = &wifi;
	globalState.netTask = &netTask;
	globalState.mediaTask = &mediaTask;
	//globalState.faceDetectTask = &faceDetectTask;
	globalState.sensorTask = &sensorTask;
	globalState.leptonTask = &leptonTask;
	globalState.zxingTask = &zxingTask;

	globalState.storage->wait_connect();

	ReadIniFile("/storage/setting.xml");

	netTask.Init(config.wifi.ssid, config.wifi.password, config.wifi.host_name,
		config.upload.server, config.upload.storage);
	//faceDetectTask.Init(FACE_DETECTOR_MODEL);
	lepton = leptonTask.GetLeptonTask();
	lepton->SetConfig(&config.lepton);
	zxingTask.Init(zxing_callback);

	netTask.Start();
	sensorTask.Start();
	mediaTask.Start();
	//faceDetectTask.Start();
	leptonTask.Start();
	zxingTask.Start();
	//kkSphereTask.Start();

	us_timestamp_t now, org = ticker_read_us(get_us_ticker_data());
	while (true) {
		now = ticker_read_us(get_us_ticker_data());
		us_timestamp_t diff = now - org;
		if (diff > 1000000) {
			org += (diff / 1000000) * 1000000;
			led1 = !led1;

			std::string temp;
			time_t tm = time(NULL);

			if (netTask.IsConnected())
				temp = std::string(config.wifi.ssid) + std::string(" Connected    ");
			else
				temp = std::string("Connecting to ") + std::string(config.wifi.ssid);
			lcd_drawString(&lcd, temp.c_str(), 0, 248, 0xFCCC, 0x0000);

			if (netTask.IsDetectedServer())
				temp = std::string(config.upload.server) + std::string(" detected, IP address ") + std::string(netTask.GetServerAddr().get_ip_address());
			else
				temp = std::string(config.upload.server) + std::string(" no detected                          ");
			lcd_drawString(&lcd, temp.c_str(), 0, 260, 0xFCCC, 0x0000);

			temp = std::string(ctime(&tm));
			lcd_drawString(&lcd, temp.c_str(), 330, 0, 0xFCCC, 0x0000);

			int fpatemp = lepton->GetFpaTemperature() - 27315;
			snprintf(textbuf, sizeof(textbuf), "FPA:%4d.%02u℃", fpatemp / 100, (fpatemp > 0) ? (fpatemp % 100) : -(fpatemp % 100));
			lcd_drawString(&lcd, textbuf, 400, 160, 0xFCCC, 0x0000);

			int auxtemp = lepton->GetAuxTemperature() - 27315;
			snprintf(textbuf, sizeof(textbuf), "AUX:%4d.%02u℃", auxtemp / 100, (auxtemp > 0) ? (auxtemp % 100) : -(auxtemp % 100));
			lcd_drawString(&lcd, textbuf, 400, 172, 0xFCCC, 0x0000);

			int reference = fpatemp;
			if (config.lepton.reference != 0)
				reference = auxtemp;

			int minValue = lepton->GetMinValue() - 27315;
			//int minValue = (int)((config.lepton.slope / 1000.0) * (lepton->GetMinValue() - 8192)) + config.lepton.offset + reference;
			snprintf(textbuf, sizeof(textbuf), "min:%4d.%02u℃", minValue / 100, (minValue > 0) ? (minValue % 100) : -(minValue % 100));
			lcd_drawString(&lcd, textbuf, 400, 184, 0xFCCC, 0x0000);

			int maxValue = lepton->GetMaxValue() - 27315;
			//int maxValue = (int)((config.lepton.slope / 1000.0) * (lepton->GetMaxValue() - 8192)) + config.lepton.offset + reference;
			snprintf(textbuf, sizeof(textbuf), "max:%4d.%02u℃", maxValue / 100, (maxValue > 0) ? (maxValue % 100) : -(maxValue % 100));
			lcd_drawString(&lcd, textbuf, 400, 196, 0xFCCC, 0x0000);
		}

		led2 = netTask.IsConnected();
		led3 = netTask.IsActive();
		led4 = mediaTask.IsRecording();

		ThisThread::sleep_for((diff / 1000) % 100);
	}

	return 0;
}
