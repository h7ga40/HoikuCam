
#ifndef _NETTASK_H_
#define _NETTASK_H_

#include <string>
#include "TaskBase.h"
#include "ESP32Interface.h"

class NetTask;

class NtpTask : public Task
{
public:
	class State
	{
	public:
		enum T {
			Unsynced,
			Synced,
		};
	};
public:
	NtpTask(NetTask *owner);
	virtual ~NtpTask();
private:
	NetTask *_owner;
	State::T _state;
	int _retry;
public:
	State::T GetState() { return _state; }
	void ProcessEvent(InterTaskSignals::T signals) override;
	void Process() override;
};

class UploadTask : public Task
{
public:
	class State
	{
	public:
		enum T {
			Undetected,
			Detected,
			Update
		};
	};
public:
	UploadTask(NetTask *owner);
	virtual ~UploadTask();
private:
	NetTask *_owner;
	State::T _state;
	int _retry;
	std::string _server;
	SocketAddress _serverAddr;
	std::string _storage;
	SocketAddress _storageAddr;
	bool _update_req;
public:
	void Init(std::string server, std::string storage);
	State::T GetState() { return _state; }
	SocketAddress GetServerAddr() { return _serverAddr; }
	void ProcessEvent(InterTaskSignals::T signals) override;
	void Process() override;
};

class WifiTask : public Task
{
public:
	class State
	{
	public:
		enum T {
			Disconnected,
			Connecting,
			Connected,
		};
	};
public:
	WifiTask(NetTask *owner, ESP32Interface *wifi);
	virtual ~WifiTask();
private:
	NetTask *_owner;
	ESP32Interface *_wifi;
	std::string _ssid;
	std::string _password;
	std::string _host_name;
	State::T _state;
	static void wifi_status(nsapi_event_t evt, intptr_t obj);
public:
	State::T GetState() { return _state; }
	void Init(std::string ssid, std::string password, std::string host_name);
	void OnStart() override;
	void ProcessEvent(InterTaskSignals::T signals) override;
	void Process() override;
};

class GlobalState;

class NetTask : public TaskThread
{
public:
	NetTask(GlobalState *globalState, ESP32Interface *wifi);
	virtual ~NetTask();
private:
	Tasks _task;
	ITask *_tasks[3];
	GlobalState *_globalState;
	ESP32Interface *_wifi;
	NtpTask _ntpTask;
	UploadTask _uploadTask;
	WifiTask _wifiTask;
public:
	void Init(std::string ssid, std::string password, std::string host_name,
		std::string server, std::string storage);
	bool InvokeNtp();
	bool IsActive();
	bool IsConnected() { return _wifiTask.GetState() == WifiTask::State::Connected; }
	bool IsDetectedServer() { return _uploadTask.GetState() != UploadTask::State::Undetected; }
	SocketAddress GetServerAddr() { return _uploadTask.GetServerAddr(); }
	void WifiStatus(nsapi_event_t evt);
	void WifiConnected();
	bool QuerySever(const std::string hostname, SocketAddress &addr);
	bool Update(SocketAddress server, SocketAddress storage);
	bool WifiSleep(bool enable);
};

#endif // _NETTASK_H_
