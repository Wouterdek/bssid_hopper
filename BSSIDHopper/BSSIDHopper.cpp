#include "WLan.h"
#undef max

#include <iostream>
#include <string>
#include <locale>

unsigned int read_int(int minVal, int maxVal, const std::string& errorMsg)
{
	int value = -1;
	while (value < 0)
	{
		std::cin >> value;
		if (std::cin.fail() || value < minVal || value >= maxVal)
		{
			std::cout << errorMsg << std::endl;
			value = -1;
		}
		if (std::cin.fail())
		{
			std::cin.clear();
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
		}
	}
	return value;
}

int main(int argc, char* argv[])
{
	bool autoHop = false;
	if(argc > 1)
	{
		autoHop = std::string(argv[1]) == "-a";
	}

	wprintf(L"Connecting to WiFi service\n");
	WLanController controller;


	wprintf(L"Enumerating interfaces\n");
	const auto interfaces = controller.EnumInterfaces();

	wprintf(L"Found %d interfaces:\n", interfaces->dwNumberOfItems);
	for(interfaces->dwIndex = 0; interfaces->dwIndex < interfaces->dwNumberOfItems; interfaces->dwIndex++)
	{
		const auto info = &interfaces->InterfaceInfo[interfaces->dwIndex];
		wprintf(L" %d: %ws\n", interfaces->dwIndex, info->strInterfaceDescription);
	}
	
	GUID iface;
	if (interfaces->dwNumberOfItems == 0)
	{
		return -1;
	}
	else if(interfaces->dwNumberOfItems == 1)
	{
		iface = interfaces->InterfaceInfo[0].InterfaceGuid;
		wprintf(L"Selected first and only interface\n");
	}
	else
	{
		std::cout << "Which interface should be used?" << std::endl;

		const int ifaceIndex = read_int(0, interfaces->dwNumberOfItems, "Please enter a valid interface index.");
		iface = interfaces->InterfaceInfo[ifaceIndex].InterfaceGuid;
	}


	wprintf(L"Querying connection details\n");
	DOT11_SSID ssid;
	DOT11_MAC_ADDRESS_S curBssid;
	std::wstring profileName;
	try
	{
		const auto attr = controller.QueryInterface<WLAN_CONNECTION_ATTRIBUTES>(&iface, wlan_intf_opcode_current_connection, NULL);
		ssid = attr->wlanAssociationAttributes.dot11Ssid;
		profileName = std::wstring(attr->strProfileName);
		const auto ssidString = std::string(ssid.ucSSID, ssid.ucSSID + ssid.uSSIDLength);
		curBssid = DOT11_MAC_ADDRESS_S(attr->wlanAssociationAttributes.dot11Bssid);

		std::cout << "Current SSID: " << ssidString << std::endl;
		std::cout << "Current BSSID: " << curBssid.ToString() << std::endl;
	}catch(WinException& ex)
	{
		std::cerr << "[ERROR] " << ex.what() << std::endl;
		std::cerr << "[ERROR] make sure the adapter is on and connected to a network. " << std::endl;
		return -1;
	}

	
	wprintf(L"Scanning for networks with same SSID\n");
	auto scanError = controller.Scan(&iface, &ssid, NULL).get();
	if(scanError.length() != 0)
	{
		std::cerr << "[WARNING] scanning failed with error message:" << std::endl;
		std::cerr << "[WARNING] '" << scanError << "'" << std::endl;
		std::cerr << "[WARNING] continuing with cached values" << std::endl;
	}

	const auto bssList = controller.GetNetworkBssList(&iface, &ssid, dot11_BSS_type_infrastructure, true);
	wprintf(L"Found %d networks:\n", bssList->dwNumberOfItems);
	for (DWORD i = 0; i < bssList->dwNumberOfItems; i++)
	{
		const auto info = &bssList->wlanBssEntries[i];
		const auto bssid = info->dot11Bssid;
		wprintf(L" %d: BSSID: %02x:%02x:%02x:%02x:%02x:%02x, link quality = %d\n", i, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], info->uLinkQuality);
	}

	if(bssList->dwNumberOfItems == 0)
	{
		return -1;
	}

	int networkIndex = -1;
	if(autoHop)
	{
		ULONG curBestLinkQuality = 0;
		for (DWORD i = 0; i < bssList->dwNumberOfItems; i++)
		{
			const auto info = &bssList->wlanBssEntries[i];
			if(info->uLinkQuality > curBestLinkQuality && DOT11_MAC_ADDRESS_S(info->dot11Bssid) != curBssid)
			{
				networkIndex = i;
				curBestLinkQuality = info->uLinkQuality;
			}
		}

		if(networkIndex == -1)
		{
			std::cerr << "[ERROR] no suitable alternative networks found" << std::endl;
			return -1;
		}
	}else
	{
		std::cout << "Which network do you want to connect to?" << std::endl;
		networkIndex = read_int(0, bssList->dwNumberOfItems, "Please enter a valid network ID");
	}
	const std::vector<DOT11_MAC_ADDRESS_S> bssids { DOT11_MAC_ADDRESS_S(bssList->wlanBssEntries[networkIndex].dot11Bssid) };

	std::string connectError = controller.Connect(&iface, &ssid, bssids, profileName.c_str(), dot11_BSS_type_infrastructure, 0, wlan_connection_mode_profile).get();
	if(connectError.length() != 0)
	{
		std::cerr << "[ERROR] connection failed: " << connectError << std::endl;
	}

	std::cout << "Done" << std::endl;
}
