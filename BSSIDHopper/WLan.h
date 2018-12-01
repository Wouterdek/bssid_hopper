#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Wlanapi.h>

#include <future>
#include <vector>

class WinException : public std::runtime_error
{
public:
	WinException(const std::string& userMessage, DWORD errorCode);
};

template<typename T>
class WLanData
{
public:
	WLanData(T* data) : data(data){}
	WLanData() : data(nullptr) {}
	WLanData(WLanData&&) = default;
	WLanData& operator=(WLanData&& b) = default;
	WLanData(const WLanData&) = delete;
	WLanData& operator=(const WLanData& b) = delete;

	~WLanData()
	{
		if(data != nullptr)
		{
			WlanFreeMemory((PVOID)data);
			data = nullptr;
		}
	}

	T& get()
	{
		return data;
	}

	T* operator->() const {
		return data;
	}

private:
	T* data;
};

struct DOT11_MAC_ADDRESS_S
{
	DOT11_MAC_ADDRESS addr;

	DOT11_MAC_ADDRESS_S(DOT11_MAC_ADDRESS addr) : addr()
	{
		std::copy(&addr[0], &addr[6], &this->addr[0]);
	}

	DOT11_MAC_ADDRESS_S() : addr()
	{ }

	std::string ToString()
	{
		char buff[18];
		snprintf(buff, sizeof(buff), "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
		return std::string(buff);
	}

	bool operator==(const DOT11_MAC_ADDRESS_S& b) const
	{
		return std::memcmp(&addr[0], &b.addr[0], sizeof(DOT11_MAC_ADDRESS)) == 0;
	}

	bool operator!=(const DOT11_MAC_ADDRESS_S& b) const
	{
		return !(*this == b);
	}
};

class WLanController
{
public:
	WLanController();
	~WLanController();
	WLanController(WLanController&&) = default;
	WLanController& operator=(WLanController&& b) = default;
	WLanController(const WLanController&) = delete;
	WLanController& operator=(const WLanController& b) = delete;

	WLanData<WLAN_INTERFACE_INFO_LIST> EnumInterfaces() const;

	template<typename T>
	WLanData<T> QueryInterface(
		const GUID* pInterfaceGuid,
		WLAN_INTF_OPCODE OpCode,
		PWLAN_OPCODE_VALUE_TYPE pWlanOpcodeValueType
	) {
		T* attr;
		DWORD attrSize = sizeof(T);
		const DWORD result = WlanQueryInterface(this->handle, pInterfaceGuid, OpCode, NULL, &attrSize, (PVOID*)&attr, pWlanOpcodeValueType);
		if (result != ERROR_SUCCESS)
		{
			throw WinException("Failed to query interface", result);
		}
		return WLanData<T>(attr);
	}

	std::future<std::string> Scan(
		GUID *pInterfaceGuid,
		PDOT11_SSID pDot11Ssid,
		PWLAN_RAW_DATA pIeData
	);

	WLanData<WLAN_BSS_LIST> GetNetworkBssList(
		const GUID        *pInterfaceGuid,
		PDOT11_SSID		  pDot11Ssid,
		DOT11_BSS_TYPE    dot11BssType,
		BOOL              bSecurityEnabled
	) const;

	std::future<std::string> Connect(
		const GUID* iface,
		const PDOT11_SSID ssid,
		const std::vector<DOT11_MAC_ADDRESS_S>& bssids,
		LPCWSTR profile,
		DOT11_BSS_TYPE dot11BssType,
		DWORD dwFlags,
		WLAN_CONNECTION_MODE mode
	);

private:
	HANDLE handle;

	void RegisterNotificationCallback(std::function<bool(WLAN_NOTIFICATION_DATA* scanNotificationData)> cb);
	std::vector<std::function<bool(WLAN_NOTIFICATION_DATA* scanNotificationData)>> notificationCallbacks;
	bool notificationsEnabled;
	DWORD dwPrevNotif;
	std::mutex notificationMutex;
};
