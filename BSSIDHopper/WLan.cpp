#include "WLan.h"

#include <string>
#include <vector>
#include <codecvt>

std::string FormatError(const std::string& userMessage, DWORD errorCode)
{
	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	std::string message(messageBuffer, size);

	//Free the buffer.
	LocalFree(messageBuffer);

	return userMessage + " (error = " + message + ", code = " + std::to_string(errorCode) + ")";
}


WinException::WinException(const std::string& userMessage, DWORD errorCode)
	: std::runtime_error(FormatError(userMessage, errorCode))
{}

std::string WlanReasonCodeToStdString(DWORD code)
{
	WCHAR* buffer = new WCHAR[2048];
	DWORD result = WlanReasonCodeToString(code, 2048, buffer, NULL);
	if (result != ERROR_SUCCESS)
	{
		delete[] buffer;
		return "";
	}
	const std::wstring w_msg(buffer);
	delete[] buffer;

	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	return converter.to_bytes(w_msg);
}

/////////////////////
WLanController::WLanController() : notificationCallbacks(), notificationsEnabled(false), dwPrevNotif(), notificationMutex()
{
	DWORD apiVersion = 2;
	HANDLE handle;
	DWORD result = WlanOpenHandle(apiVersion, NULL, &apiVersion, &handle);
	if (result != ERROR_SUCCESS)
	{
		throw WinException("Failed to open connection to the WLan controller server", result);
	}

	this->handle = handle;
}

WLanController::~WLanController()
{
	if(this->notificationsEnabled)
	{
		WlanRegisterNotification(this->handle, WLAN_NOTIFICATION_SOURCE_NONE, true, NULL, NULL, NULL, &this->dwPrevNotif);
	}
	WlanCloseHandle(this->handle, NULL);
}

WLanData<WLAN_INTERFACE_INFO_LIST> WLanController::EnumInterfaces() const
{
	PWLAN_INTERFACE_INFO_LIST interfaces;
	const DWORD result = WlanEnumInterfaces(this->handle, NULL, &interfaces);
	if (result != ERROR_SUCCESS)
	{
		throw WinException("Failed to enumerate interfaces", result);
	}
	return WLanData<WLAN_INTERFACE_INFO_LIST>(interfaces);
}

std::future<std::string> WLanController::Scan(
	GUID* pInterfaceGuid,
	PDOT11_SSID pDot11Ssid,
	PWLAN_RAW_DATA pIeData
)
{
	auto promise = std::make_shared<std::promise<std::string>>();

	this->RegisterNotificationCallback([pInterfaceGuid, promise](WLAN_NOTIFICATION_DATA* scanNotificationData)
	{
		if (memcmp(pInterfaceGuid, &scanNotificationData->InterfaceGuid, sizeof(GUID)) != 0)
		{
			return false;
		}

		if (scanNotificationData->NotificationCode == wlan_notification_acm_scan_fail)
		{
			auto reason = (WLAN_REASON_CODE*)scanNotificationData->pData;
			const std::string errorMsg = WlanReasonCodeToStdString(*reason) + " (" + std::to_string(*reason) + ")";
			promise->set_value(errorMsg);
			return true;
		}
		
		if (scanNotificationData->NotificationCode == wlan_notification_acm_scan_complete)
		{
			promise->set_value("");
			return true;
		}

		return false;
	});

	DWORD result = WlanScan(handle, pInterfaceGuid, pDot11Ssid, pIeData, NULL);
	if (result != ERROR_SUCCESS)
	{
		throw WinException("Failed to initiate wlan scan", result);
	}

	return promise->get_future();
}

WLanData<WLAN_BSS_LIST> WLanController::GetNetworkBssList(
	const GUID* pInterfaceGuid,
	PDOT11_SSID pDot11Ssid,
	DOT11_BSS_TYPE dot11BssType, 
	BOOL bSecurityEnabled) const
{
	PWLAN_BSS_LIST bssList;
	const DWORD result = WlanGetNetworkBssList(handle, pInterfaceGuid, pDot11Ssid, dot11BssType, bSecurityEnabled, NULL, &bssList);
	if (result != ERROR_SUCCESS)
	{
		throw WinException("Failed to retrieve network bss list", result);
	}

	return WLanData<WLAN_BSS_LIST>(bssList);
}

struct WlanConnectNotifCallbackCtx
{
	std::promise<bool> promise;
	HANDLE handle;
	DWORD dwPrevNotif;

	WlanConnectNotifCallbackCtx(HANDLE handle) : promise(), handle(handle), dwPrevNotif() {}
};

std::future<std::string> WLanController::Connect(
	const GUID* iface,
	const PDOT11_SSID ssid,
	const std::vector<DOT11_MAC_ADDRESS_S>& bssids,
	LPCWSTR profile,
	DOT11_BSS_TYPE dot11BssType,
	DWORD dwFlags,
	WLAN_CONNECTION_MODE mode
	)
{
	auto promise = std::make_shared<std::promise<std::string>>();

	this->RegisterNotificationCallback([promise](WLAN_NOTIFICATION_DATA* scanNotificationData)
	{
		if (scanNotificationData->NotificationCode == wlan_notification_acm_connection_complete)
		{
			const auto connInfo = (WLAN_CONNECTION_NOTIFICATION_DATA*)scanNotificationData->pData;
			const bool connSucceeded = connInfo->wlanReasonCode == WLAN_REASON_CODE_SUCCESS;
			if(connSucceeded)
			{
				promise->set_value("");
			}else
			{
				std::string errorMsg = WlanReasonCodeToStdString(connInfo->wlanReasonCode) + " (" + std::to_string(connInfo->wlanReasonCode) + ")";
				promise->set_value(errorMsg);
			}
			return true;
		}
		return false;
	});

	DOT11_BSSID_LIST bssConnectList;
	bssConnectList.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
	bssConnectList.Header.Revision = DOT11_BSSID_LIST_REVISION_1;
	bssConnectList.Header.Size = sizeof(DOT11_BSSID_LIST);
	bssConnectList.uNumOfEntries = bssConnectList.uTotalNumOfEntries = bssids.size();
	for(DWORD i = 0; i < bssids.size(); i++)
	{
		std::copy(&bssids[i].addr[0], &bssids[i].addr[6], bssConnectList.BSSIDs[i]);
	}

	WLAN_CONNECTION_PARAMETERS connectionParams;
	connectionParams.pDot11Ssid = ssid;
	connectionParams.dot11BssType = dot11BssType;
	connectionParams.dwFlags = dwFlags;
	connectionParams.pDesiredBssidList = &bssConnectList;
	connectionParams.strProfile = profile;
	connectionParams.wlanConnectionMode = mode;

	const DWORD result = WlanConnect(handle, iface, &connectionParams, NULL);
	if (result != ERROR_SUCCESS)
	{
		throw WinException("Failed to initiate wlan connection", result);
	}

	return promise->get_future();
}

void WLanController::RegisterNotificationCallback(std::function<bool(WLAN_NOTIFICATION_DATA* scanNotificationData)> cb)
{
	{
		this->notificationMutex.lock();
		this->notificationCallbacks.emplace_back(cb);
		this->notificationMutex.unlock();
	}

	if(!this->notificationsEnabled)
	{
		DWORD result = WlanRegisterNotification(handle, WLAN_NOTIFICATION_SOURCE_ALL, TRUE, [](WLAN_NOTIFICATION_DATA* notificationData, PVOID pCtx) {
			WLanController* ctx = (WLanController*)pCtx;
			if (ctx == nullptr)
			{
				return;
			}

			ctx->notificationMutex.lock();
			for(int i = ctx->notificationCallbacks.size() - 1; i >= 0; i--)
			{
				if(ctx->notificationCallbacks[i](notificationData))
				{
					ctx->notificationCallbacks.erase(ctx->notificationCallbacks.begin() + i);
				}
			}
			ctx->notificationMutex.unlock();
		}, (void*)this, NULL, &this->dwPrevNotif);
		if(result != ERROR_SUCCESS)
		{
			throw WinException("Failed to register notification callback", result);
		}

		this->notificationsEnabled = true;
	}
}
