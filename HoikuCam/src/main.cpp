// GR-LYCHEE OpenCV Face Detection Example
// Public Domain

#include "mbed.h"
#include "SdUsbConnect.h"
#include "GlobalState.h"
#include "MediaTask.h"
#include "NetTask.h"
#include "SensorTask.h"
#include <expat.h>
#include "draw_font.h"
#include "bh1792.h"

#define FACE_DETECTOR_MODEL     "/storage/lbpcascade_frontalface.xml"

/* Application variables */
Mat frame_gray;     // Input frame (in grayscale)

DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);
DigitalOut led4(LED4);

static GlobalState globalState;
static SdUsbConnect sdUsbConnect("storage");
static FaceDetectTask faceDetectTask(&globalState);
static MediaTask mediaTask(&globalState, &faceDetectTask.face_roi);
static SensorTask sensorTask(&globalState);
static ESP32Interface wifi(P5_3, P3_14, P7_1, P0_1);
static NetTask netTask(&globalState, &wifi);

extern "C" void _kill()
{
}

extern "C" int _getpid()
{
	return 1;
}

extern "C" int gettimeofday(struct timeval *__restrict __p, void *__restrict __tz)
{
    us_timestamp_t time;
    if (!__p) return 0;
    time = ticker_read_us(get_us_ticker_data());
    __p->tv_sec = time / 1000000;
    __p->tv_usec = time - (__p->tv_sec * 1000000);
    return 0;
}

enum parse_state_t {
	psRoot,
	psWifi,
	psWifiSsid,
	psWifiPassword,
	psWifiHostName,
	psUpload,
	psUploadServer,
	psUploadStorage,
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
	wifi_config_t wifi;
	upload_config_t upload;
};

static void XMLCALL
start(void *data, const XML_Char *el, const XML_Char **attr)
{
	config_data_t *cd = (config_data_t *)data;

	switch (cd->state)
	{
	case psRoot:
		if (strcmp(el, "wifi") == 0) {
			cd->state = psWifi;
		}
		else if (strcmp(el, "upload") == 0) {
			cd->state = psUpload;
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
	default:
		break;
	}
}

static void XMLCALL
end(void *data, const XML_Char *el)
{
	config_data_t *cd = (config_data_t *)data;
	(void)el;

	switch (cd->state)
	{
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
	default:
		break;
	}
}

static void XMLCALL
text(void *data, const XML_Char *s, int len)
{
	config_data_t *cd = (config_data_t *)data;
	int l, maxlen;

	switch (cd->state)
	{
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

	int len;
	FILE *fp = fopen(filename.c_str(), "rb");
	if (fp != NULL){
		for (;;) {
			len = fread(temp, 1, sizeof(temp), fp);
			if (len <= 0)
				break;

			if((config.state != psError) && (XML_Parse(parser, (char *)temp, len, 0) == 0)) {
				XML_Error error_code = XML_GetErrorCode(parser);
				printf("Parsing response buffer of size %lu failed"
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

int main(void)
{
	globalState.storage = &sdUsbConnect;
	globalState.wifi = &wifi;
	globalState.netTask = &netTask;
	globalState.mediaTask = &mediaTask;
	globalState.faceDetectTask = &faceDetectTask;
	globalState.sensorTask = &sensorTask;

	globalState.storage->wait_connect();

	ReadIniFile("/storage/setting.xml");

	netTask.Init(config.wifi.ssid, config.wifi.password, config.wifi.host_name,
		config.upload.server, config.upload.storage);
	faceDetectTask.Init(FACE_DETECTOR_MODEL);

	netTask.Start();
	sensorTask.Start();
	mediaTask.Start();
	faceDetectTask.Start();

	us_timestamp_t now, org = ticker_read_us(get_us_ticker_data());
	while (true) {
		now = ticker_read_us(get_us_ticker_data());
		int diff = now - org;
		if (diff > 1000000) {
			org += (diff / 1000000) * 1000000;
			led1 = !led1;

			std::string temp;
			time_t tm = time(NULL);

			if (netTask.IsConnected())
				temp = std::string(config.wifi.ssid) + std::string(" Connected    ");
			else
				temp = std::string("Connecting to ") + std::string(config.wifi.ssid);
			lcd_drawString(temp.c_str(), 0, 248, 0xFCCC, 0x0000);

			if (netTask.IsDetectedServer())
				temp = std::string(config.upload.server) + std::string(" detected, IP address ") + std::string(netTask.GetServerAddr().get_ip_address());
			else
				temp = std::string(config.upload.server) + std::string(" no detected                          ");
			lcd_drawString(temp.c_str(), 0, 260, 0xFCCC, 0x0000);

			temp = std::string(ctime(&tm));
			lcd_drawString(temp.c_str(), 330, 0, 0xFCCC, 0x0000);
		}

		led2 = netTask.IsConnected();
		led3 = netTask.IsActive();
		led4 = mediaTask.IsRecording();

		osDelay((diff / 1000) % 100);
	}
}
