#include <StdInc.h>
#include <CustomText.h>

#include <DrawCommands.h>
#include <nutsnbolts.h>

#include <fiDevice.h>
#include <Hooking.h>

#include <CrossBuildRuntime.h>
#include <LaunchMode.h>
#include <MinHook.h>

#include <boost/algorithm/string.hpp>

static bool IsNewInstall()
{
	if (GetFileAttributes(MakeRelativeCitPath(L"citizen/new_vid_behavior.txt").c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		return true;
	}

	auto cachePath = MakeRelativeCitPath(L"crashes");

	HANDLE hFile = CreateFileW(cachePath.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		FILETIME ctime;
		GetFileTime(hFile, &ctime, NULL, NULL);

		CloseHandle(hFile);

		ULARGE_INTEGER t;
		t.LowPart = ctime.dwLowDateTime;
		t.HighPart = ctime.dwHighDateTime;

		return (t.QuadPart > 132356430250000000ULL);
	}

	return false;
}

static bool isNewSettingFile;

static uint8_t* g_displayTraceGameBase;
static size_t g_displayTraceGameSize;

static decltype(&EnumDisplaySettingsW) g_origEnumDisplaySettingsW;
static decltype(&EnumDisplayDevicesA) g_origEnumDisplayDevicesA;
static decltype(&GetMonitorInfoA) g_origGetMonitorInfoA;
static decltype(&MonitorFromPoint) g_origMonitorFromPoint;
static decltype(&GetSystemMetrics) g_origGetSystemMetrics;
static decltype(&QueryDisplayConfig) g_origQueryDisplayConfig;
static decltype(&DisplayConfigGetDeviceInfo) g_origDisplayConfigGetDeviceInfo;
static decltype(&GetDeviceCaps) g_origGetDeviceCaps;

static bool GetDisplayTraceCaller(void* caller, uintptr_t* callerRva)
{
	const auto address = reinterpret_cast<uintptr_t>(caller);
	const auto base = reinterpret_cast<uintptr_t>(g_displayTraceGameBase);

	if (!base || address < base || address >= (base + g_displayTraceGameSize))
	{
		return false;
	}

	*callerRva = address - base;
	return true;
}

static const char* GetDisplayModeName(DWORD modeNum)
{
	if (modeNum == ENUM_CURRENT_SETTINGS)
	{
		return "ENUM_CURRENT_SETTINGS";
	}

	if (modeNum == ENUM_REGISTRY_SETTINGS)
	{
		return "ENUM_REGISTRY_SETTINGS";
	}

	return "INDEX";
}

template<size_t Size>
static const char* GetDisplayTraceUtf8(LPCWSTR value, char (&buffer)[Size])
{
	if (!value)
	{
		return "<null>";
	}

	if (WideCharToMultiByte(CP_UTF8, 0, value, -1, buffer, static_cast<int>(Size), nullptr, nullptr) == 0)
	{
		return "<conversion-failed>";
	}

	return buffer;
}

static BOOL WINAPI TraceEnumDisplaySettingsW(LPCWSTR deviceName, DWORD modeNum, DEVMODEW* mode)
{
	void* caller = _ReturnAddress();
	const BOOL result = g_origEnumDisplaySettingsW(deviceName, modeNum, mode);
	const DWORD apiLastError = GetLastError();
	uintptr_t callerRva;
	const bool isGameMainCaller = GetDisplayTraceCaller(caller, &callerRva);

	if (isGameMainCaller)
	{
		char deviceNameUtf8[512];
		const char* traceDeviceName = GetDisplayTraceUtf8(deviceName, deviceNameUtf8);

		if (result && mode)
		{
			trace("[WineDisplayApi] api=EnumDisplaySettingsW device=%s mode=%s modeValue=0x%08lx result=%d width=%lu height=%lu orientation=%lu frequency=%lu fields=0x%08lx position=(%ld,%ld) bitsPerPel=%lu flags=0x%08lx callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
				traceDeviceName,
				GetDisplayModeName(modeNum),
				static_cast<unsigned long>(modeNum),
				result,
				static_cast<unsigned long>(mode->dmPelsWidth),
				static_cast<unsigned long>(mode->dmPelsHeight),
				static_cast<unsigned long>(mode->dmDisplayOrientation),
				static_cast<unsigned long>(mode->dmDisplayFrequency),
				static_cast<unsigned long>(mode->dmFields),
				static_cast<long>(mode->dmPosition.x),
				static_cast<long>(mode->dmPosition.y),
				static_cast<unsigned long>(mode->dmBitsPerPel),
				static_cast<unsigned long>(mode->dmDisplayFlags),
				caller,
				static_cast<unsigned long long>(callerRva),
				static_cast<unsigned long>(GetCurrentThreadId()));
		}
		else
		{
			trace("[WineDisplayApi] api=EnumDisplaySettingsW device=%s mode=%s modeValue=0x%08lx result=%d lastError=%lu callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
				traceDeviceName,
				GetDisplayModeName(modeNum),
				static_cast<unsigned long>(modeNum),
				result,
				static_cast<unsigned long>(apiLastError),
				caller,
				static_cast<unsigned long long>(callerRva),
				static_cast<unsigned long>(GetCurrentThreadId()));
		}
	}

	if (CfxIsWine() && isGameMainCaller && result && mode &&
		(mode->dmFields & DM_DISPLAYORIENTATION) && mode->dmDisplayOrientation == DMDO_270)
	{
		mode->dmDisplayOrientation = DMDO_DEFAULT;
		trace("[WineOrientationFix] api=EnumDisplaySettingsW original=3 presented=0 width=%u height=%u callerRva=0x%llx tid=%lu\n",
			static_cast<unsigned int>(mode->dmPelsWidth),
			static_cast<unsigned int>(mode->dmPelsHeight),
			static_cast<unsigned long long>(callerRva),
			static_cast<unsigned long>(GetCurrentThreadId()));
	}

	SetLastError(apiLastError);
	return result;
}

static BOOL WINAPI TraceEnumDisplayDevicesA(LPCSTR deviceName, DWORD deviceNum, PDISPLAY_DEVICEA displayDevice, DWORD flags)
{
	void* caller = _ReturnAddress();
	const BOOL result = g_origEnumDisplayDevicesA(deviceName, deviceNum, displayDevice, flags);
	const DWORD apiLastError = GetLastError();
	uintptr_t callerRva;

	if (GetDisplayTraceCaller(caller, &callerRva))
	{
		if (result && displayDevice)
		{
			trace("[WineDisplayApi] api=EnumDisplayDevicesA device=%s deviceNum=%lu flags=0x%08lx result=%d outName=%s outString=%s stateFlags=0x%08lx outId=%s outKey=%s callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
				deviceName ? deviceName : "<null>",
				static_cast<unsigned long>(deviceNum),
				static_cast<unsigned long>(flags),
				result,
				displayDevice->DeviceName,
				displayDevice->DeviceString,
				static_cast<unsigned long>(displayDevice->StateFlags),
				displayDevice->DeviceID,
				displayDevice->DeviceKey,
				caller,
				static_cast<unsigned long long>(callerRva),
				static_cast<unsigned long>(GetCurrentThreadId()));
		}
		else
		{
			trace("[WineDisplayApi] api=EnumDisplayDevicesA device=%s deviceNum=%lu flags=0x%08lx result=%d lastError=%lu callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
				deviceName ? deviceName : "<null>",
				static_cast<unsigned long>(deviceNum),
				static_cast<unsigned long>(flags),
				result,
				static_cast<unsigned long>(apiLastError),
				caller,
				static_cast<unsigned long long>(callerRva),
				static_cast<unsigned long>(GetCurrentThreadId()));
		}
	}

	SetLastError(apiLastError);
	return result;
}

static BOOL WINAPI TraceGetMonitorInfoA(HMONITOR monitor, LPMONITORINFO monitorInfo)
{
	void* caller = _ReturnAddress();
	const DWORD inputSize = monitorInfo ? monitorInfo->cbSize : 0;
	const BOOL result = g_origGetMonitorInfoA(monitor, monitorInfo);
	const DWORD apiLastError = GetLastError();
	uintptr_t callerRva;

	if (GetDisplayTraceCaller(caller, &callerRva))
	{
		if (result && monitorInfo)
		{
			if (inputSize >= sizeof(MONITORINFOEXA))
			{
				const auto monitorInfoEx = reinterpret_cast<const MONITORINFOEXA*>(monitorInfo);
				trace("[WineDisplayApi] api=GetMonitorInfoA monitor=%p cbSize=%lu result=%d rcMonitor=(%ld,%ld,%ld,%ld) rcWork=(%ld,%ld,%ld,%ld) flags=0x%08lx device=%s callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
					static_cast<void*>(monitor),
					static_cast<unsigned long>(inputSize),
					result,
					monitorInfo->rcMonitor.left,
					monitorInfo->rcMonitor.top,
					monitorInfo->rcMonitor.right,
					monitorInfo->rcMonitor.bottom,
					monitorInfo->rcWork.left,
					monitorInfo->rcWork.top,
					monitorInfo->rcWork.right,
					monitorInfo->rcWork.bottom,
					static_cast<unsigned long>(monitorInfo->dwFlags),
					monitorInfoEx->szDevice,
					caller,
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long>(GetCurrentThreadId()));
			}
			else
			{
				trace("[WineDisplayApi] api=GetMonitorInfoA monitor=%p cbSize=%lu result=%d rcMonitor=(%ld,%ld,%ld,%ld) rcWork=(%ld,%ld,%ld,%ld) flags=0x%08lx device=<unavailable> callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
					static_cast<void*>(monitor),
					static_cast<unsigned long>(inputSize),
					result,
					monitorInfo->rcMonitor.left,
					monitorInfo->rcMonitor.top,
					monitorInfo->rcMonitor.right,
					monitorInfo->rcMonitor.bottom,
					monitorInfo->rcWork.left,
					monitorInfo->rcWork.top,
					monitorInfo->rcWork.right,
					monitorInfo->rcWork.bottom,
					static_cast<unsigned long>(monitorInfo->dwFlags),
					caller,
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long>(GetCurrentThreadId()));
			}
		}
		else
		{
			trace("[WineDisplayApi] api=GetMonitorInfoA monitor=%p cbSize=%lu result=%d lastError=%lu callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
				static_cast<void*>(monitor),
				static_cast<unsigned long>(inputSize),
				result,
				static_cast<unsigned long>(apiLastError),
				caller,
				static_cast<unsigned long long>(callerRva),
				static_cast<unsigned long>(GetCurrentThreadId()));
		}
	}

	SetLastError(apiLastError);
	return result;
}

static HMONITOR WINAPI TraceMonitorFromPoint(POINT point, DWORD flags)
{
	void* caller = _ReturnAddress();
	const HMONITOR result = g_origMonitorFromPoint(point, flags);
	const DWORD apiLastError = GetLastError();
	uintptr_t callerRva;

	if (GetDisplayTraceCaller(caller, &callerRva))
	{
		trace("[WineDisplayApi] api=MonitorFromPoint point=(%ld,%ld) flags=0x%08lx result=%p lastError=%lu callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
			point.x,
			point.y,
			static_cast<unsigned long>(flags),
			static_cast<void*>(result),
			static_cast<unsigned long>(apiLastError),
			caller,
			static_cast<unsigned long long>(callerRva),
			static_cast<unsigned long>(GetCurrentThreadId()));
	}

	SetLastError(apiLastError);
	return result;
}

static const char* GetSystemMetricName(int index)
{
	switch (index)
	{
		case SM_CXSCREEN: return "SM_CXSCREEN";
		case SM_CYSCREEN: return "SM_CYSCREEN";
		case SM_XVIRTUALSCREEN: return "SM_XVIRTUALSCREEN";
		case SM_YVIRTUALSCREEN: return "SM_YVIRTUALSCREEN";
		case SM_CXVIRTUALSCREEN: return "SM_CXVIRTUALSCREEN";
		case SM_CYVIRTUALSCREEN: return "SM_CYVIRTUALSCREEN";
		case SM_CMONITORS: return "SM_CMONITORS";
		default: return nullptr;
	}
}

static int WINAPI TraceGetSystemMetrics(int index)
{
	void* caller = _ReturnAddress();
	const int result = g_origGetSystemMetrics(index);
	const DWORD apiLastError = GetLastError();
	const char* metricName = GetSystemMetricName(index);
	uintptr_t callerRva;

	if (metricName && GetDisplayTraceCaller(caller, &callerRva))
	{
		trace("[WineDisplayApi] api=GetSystemMetrics index=%d metric=%s result=%d lastError=%lu callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
			index,
			metricName,
			result,
			static_cast<unsigned long>(apiLastError),
			caller,
			static_cast<unsigned long long>(callerRva),
			static_cast<unsigned long>(GetCurrentThreadId()));
	}

	SetLastError(apiLastError);
	return result;
}

static LONG WINAPI TraceQueryDisplayConfig(UINT32 flags, UINT32* numPathArrayElements, DISPLAYCONFIG_PATH_INFO* pathArray, UINT32* numModeInfoArrayElements, DISPLAYCONFIG_MODE_INFO* modeInfoArray, DISPLAYCONFIG_TOPOLOGY_ID* currentTopologyId)
{
	void* caller = _ReturnAddress();
	const UINT32 pathCapacity = numPathArrayElements ? *numPathArrayElements : 0;
	const UINT32 modeCapacity = numModeInfoArrayElements ? *numModeInfoArrayElements : 0;
	const LONG result = g_origQueryDisplayConfig(flags, numPathArrayElements, pathArray, numModeInfoArrayElements, modeInfoArray, currentTopologyId);
	const DWORD apiLastError = GetLastError();
	const UINT32 pathCount = numPathArrayElements ? *numPathArrayElements : 0;
	const UINT32 modeCount = numModeInfoArrayElements ? *numModeInfoArrayElements : 0;
	uintptr_t callerRva;
	const bool isGameMainCaller = GetDisplayTraceCaller(caller, &callerRva);

	if (isGameMainCaller)
	{
		trace("[WineDisplayApi] api=QueryDisplayConfig flags=0x%08x pathCapacity=%u modeCapacity=%u result=%ld pathCount=%u modeCount=%u topology=%u lastError=%lu callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
			flags,
			pathCapacity,
			modeCapacity,
			result,
			pathCount,
			modeCount,
			currentTopologyId ? static_cast<unsigned int>(*currentTopologyId) : 0,
			static_cast<unsigned long>(apiLastError),
			caller,
			static_cast<unsigned long long>(callerRva),
			static_cast<unsigned long>(GetCurrentThreadId()));

		if (result == ERROR_SUCCESS && pathArray && numPathArrayElements)
		{
			const UINT32 safePathCount = (pathCount < pathCapacity) ? pathCount : pathCapacity;
			for (UINT32 i = 0; i < safePathCount; ++i)
			{
				const auto& path = pathArray[i];
				trace("[WineDisplayApi] api=QueryDisplayConfigPath index=%u sourceAdapter=(%ld,0x%08lx) sourceId=%u sourceModeInfoIdx=%u sourceStatus=0x%08x targetAdapter=(%ld,0x%08lx) targetId=%u targetModeInfoIdx=%u outputTechnology=%u rotation=%u scaling=%u refresh=%u/%u scanLineOrdering=%u targetAvailable=%d targetStatus=0x%08x callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
					i,
					path.sourceInfo.adapterId.HighPart,
					static_cast<unsigned long>(path.sourceInfo.adapterId.LowPart),
					path.sourceInfo.id,
					path.sourceInfo.modeInfoIdx,
					path.sourceInfo.statusFlags,
					path.targetInfo.adapterId.HighPart,
					static_cast<unsigned long>(path.targetInfo.adapterId.LowPart),
					path.targetInfo.id,
					path.targetInfo.modeInfoIdx,
					static_cast<unsigned int>(path.targetInfo.outputTechnology),
					static_cast<unsigned int>(path.targetInfo.rotation),
					static_cast<unsigned int>(path.targetInfo.scaling),
					path.targetInfo.refreshRate.Numerator,
					path.targetInfo.refreshRate.Denominator,
					static_cast<unsigned int>(path.targetInfo.scanLineOrdering),
					path.targetInfo.targetAvailable,
					path.targetInfo.statusFlags,
					caller,
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long>(GetCurrentThreadId()));
			}
		}

		if (result == ERROR_SUCCESS && modeInfoArray && numModeInfoArrayElements)
		{
			const UINT32 safeModeCount = (modeCount < modeCapacity) ? modeCount : modeCapacity;
			for (UINT32 i = 0; i < safeModeCount; ++i)
			{
				const auto& mode = modeInfoArray[i];
				if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
				{
					trace("[WineDisplayApi] api=QueryDisplayConfigMode index=%u type=source adapter=(%ld,0x%08lx) id=%u width=%u height=%u pixelFormat=%u position=(%ld,%ld) callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
						i,
						mode.adapterId.HighPart,
						static_cast<unsigned long>(mode.adapterId.LowPart),
						mode.id,
						mode.sourceMode.width,
						mode.sourceMode.height,
						static_cast<unsigned int>(mode.sourceMode.pixelFormat),
						mode.sourceMode.position.x,
						mode.sourceMode.position.y,
						caller,
						static_cast<unsigned long long>(callerRva),
						static_cast<unsigned long>(GetCurrentThreadId()));
				}
				else if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
				{
					const auto& signal = mode.targetMode.targetVideoSignalInfo;
					trace("[WineDisplayApi] api=QueryDisplayConfigMode index=%u type=target adapter=(%ld,0x%08lx) id=%u active=%ux%u total=%ux%u vSync=%u/%u hSync=%u/%u pixelRate=%llu scanLineOrdering=%u callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
						i,
						mode.adapterId.HighPart,
						static_cast<unsigned long>(mode.adapterId.LowPart),
						mode.id,
						signal.activeSize.cx,
						signal.activeSize.cy,
						signal.totalSize.cx,
						signal.totalSize.cy,
						signal.vSyncFreq.Numerator,
						signal.vSyncFreq.Denominator,
						signal.hSyncFreq.Numerator,
						signal.hSyncFreq.Denominator,
						static_cast<unsigned long long>(signal.pixelRate),
						static_cast<unsigned int>(signal.scanLineOrdering),
						caller,
						static_cast<unsigned long long>(callerRva),
						static_cast<unsigned long>(GetCurrentThreadId()));
				}
				else
				{
					trace("[WineDisplayApi] api=QueryDisplayConfigMode index=%u type=%u adapter=(%ld,0x%08lx) id=%u callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
						i,
						static_cast<unsigned int>(mode.infoType),
						mode.adapterId.HighPart,
						static_cast<unsigned long>(mode.adapterId.LowPart),
						mode.id,
						caller,
						static_cast<unsigned long long>(callerRva),
						static_cast<unsigned long>(GetCurrentThreadId()));
				}
			}
		}
	}

	if (CfxIsWine() && isGameMainCaller && result == ERROR_SUCCESS && pathArray &&
		numPathArrayElements && pathCount <= pathCapacity)
	{
		for (UINT32 i = 0; i < pathCount; ++i)
		{
			auto& path = pathArray[i];
			if (path.targetInfo.rotation == DISPLAYCONFIG_ROTATION_ROTATE270)
			{
				UINT32 sourceWidth = 0;
				UINT32 sourceHeight = 0;
				UINT32 targetWidth = 0;
				UINT32 targetHeight = 0;

				if (modeInfoArray && numModeInfoArrayElements && modeCount <= modeCapacity)
				{
					if (path.sourceInfo.modeInfoIdx < modeCount)
					{
						const auto& sourceMode = modeInfoArray[path.sourceInfo.modeInfoIdx];
						if (sourceMode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
						{
							sourceWidth = sourceMode.sourceMode.width;
							sourceHeight = sourceMode.sourceMode.height;
						}
					}

					if (path.targetInfo.modeInfoIdx < modeCount)
					{
						const auto& targetMode = modeInfoArray[path.targetInfo.modeInfoIdx];
						if (targetMode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
						{
							targetWidth = targetMode.targetMode.targetVideoSignalInfo.activeSize.cx;
							targetHeight = targetMode.targetMode.targetVideoSignalInfo.activeSize.cy;
						}
					}
				}

				path.targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
				trace("[WineOrientationFix] api=QueryDisplayConfig path=%u original=4 presented=1 source=%ux%u target=%ux%u callerRva=0x%llx tid=%lu\n",
					i,
					sourceWidth,
					sourceHeight,
					targetWidth,
					targetHeight,
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long>(GetCurrentThreadId()));
			}
		}
	}

	SetLastError(apiLastError);
	return result;
}

static LONG WINAPI TraceDisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_HEADER* requestPacket)
{
	void* caller = _ReturnAddress();
	const DISPLAYCONFIG_DEVICE_INFO_TYPE type = requestPacket ? requestPacket->type : static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(0);
	const UINT32 size = requestPacket ? requestPacket->size : 0;
	const LUID adapterId = requestPacket ? requestPacket->adapterId : LUID{};
	const UINT32 id = requestPacket ? requestPacket->id : 0;
	const LONG result = g_origDisplayConfigGetDeviceInfo(requestPacket);
	const DWORD apiLastError = GetLastError();
	uintptr_t callerRva;

	if (GetDisplayTraceCaller(caller, &callerRva))
	{
		trace("[WineDisplayApi] api=DisplayConfigGetDeviceInfo type=%u size=%u adapter=(%ld,0x%08lx) id=%u result=%ld lastError=%lu callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
			static_cast<unsigned int>(type),
			size,
			adapterId.HighPart,
			static_cast<unsigned long>(adapterId.LowPart),
			id,
			result,
			static_cast<unsigned long>(apiLastError),
			caller,
			static_cast<unsigned long long>(callerRva),
			static_cast<unsigned long>(GetCurrentThreadId()));

		if (result == ERROR_SUCCESS && requestPacket)
		{
			if (type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME && size >= sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME))
			{
				const auto packet = reinterpret_cast<const DISPLAYCONFIG_SOURCE_DEVICE_NAME*>(requestPacket);
				char deviceNameUtf8[512];
				trace("[WineDisplayApi] api=DisplayConfigGetDeviceInfoResult type=source-name gdiDevice=%s callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
					GetDisplayTraceUtf8(packet->viewGdiDeviceName, deviceNameUtf8),
					caller,
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long>(GetCurrentThreadId()));
			}
			else if (type == DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME && size >= sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME))
			{
				const auto packet = reinterpret_cast<const DISPLAYCONFIG_TARGET_DEVICE_NAME*>(requestPacket);
				char friendlyNameUtf8[256];
				char monitorPathUtf8[512];
				trace("[WineDisplayApi] api=DisplayConfigGetDeviceInfoResult type=target-name outputTechnology=%u edidManufactureId=%u edidProductCodeId=%u connectorInstance=%u friendlyName=%s monitorPath=%s callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
					static_cast<unsigned int>(packet->outputTechnology),
					packet->edidManufactureId,
					packet->edidProductCodeId,
					packet->connectorInstance,
					GetDisplayTraceUtf8(packet->monitorFriendlyDeviceName, friendlyNameUtf8),
					GetDisplayTraceUtf8(packet->monitorDevicePath, monitorPathUtf8),
					caller,
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long>(GetCurrentThreadId()));
			}
			else if (type == DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE && size >= sizeof(DISPLAYCONFIG_TARGET_PREFERRED_MODE))
			{
				const auto packet = reinterpret_cast<const DISPLAYCONFIG_TARGET_PREFERRED_MODE*>(requestPacket);
				const auto& signal = packet->targetMode.targetVideoSignalInfo;
				trace("[WineDisplayApi] api=DisplayConfigGetDeviceInfoResult type=target-preferred width=%u height=%u active=%ux%u total=%ux%u vSync=%u/%u hSync=%u/%u pixelRate=%llu scanLineOrdering=%u callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
					packet->width,
					packet->height,
					signal.activeSize.cx,
					signal.activeSize.cy,
					signal.totalSize.cx,
					signal.totalSize.cy,
					signal.vSyncFreq.Numerator,
					signal.vSyncFreq.Denominator,
					signal.hSyncFreq.Numerator,
					signal.hSyncFreq.Denominator,
					static_cast<unsigned long long>(signal.pixelRate),
					static_cast<unsigned int>(signal.scanLineOrdering),
					caller,
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long>(GetCurrentThreadId()));
			}
			else if (type == DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME && size >= sizeof(DISPLAYCONFIG_ADAPTER_NAME))
			{
				const auto packet = reinterpret_cast<const DISPLAYCONFIG_ADAPTER_NAME*>(requestPacket);
				char adapterPathUtf8[512];
				trace("[WineDisplayApi] api=DisplayConfigGetDeviceInfoResult type=adapter-name path=%s callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
					GetDisplayTraceUtf8(packet->adapterDevicePath, adapterPathUtf8),
					caller,
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long>(GetCurrentThreadId()));
			}
		}
	}

	SetLastError(apiLastError);
	return result;
}

static const char* GetDeviceCapName(int index)
{
	switch (index)
	{
		case HORZSIZE: return "HORZSIZE";
		case VERTSIZE: return "VERTSIZE";
		case HORZRES: return "HORZRES";
		case VERTRES: return "VERTRES";
		case LOGPIXELSX: return "LOGPIXELSX";
		case LOGPIXELSY: return "LOGPIXELSY";
		case VREFRESH: return "VREFRESH";
		case DESKTOPHORZRES: return "DESKTOPHORZRES";
		case DESKTOPVERTRES: return "DESKTOPVERTRES";
		default: return nullptr;
	}
}

static int WINAPI TraceGetDeviceCaps(HDC deviceContext, int index)
{
	void* caller = _ReturnAddress();
	const int result = g_origGetDeviceCaps(deviceContext, index);
	const DWORD apiLastError = GetLastError();
	const char* capName = GetDeviceCapName(index);
	uintptr_t callerRva;

	if (capName && GetDisplayTraceCaller(caller, &callerRva))
	{
		trace("[WineDisplayApi] api=GetDeviceCaps hdc=%p index=%d cap=%s result=%d lastError=%lu callerModule=game-main caller=%p callerRva=0x%llx tid=%lu\n",
			static_cast<void*>(deviceContext),
			index,
			capName,
			result,
			static_cast<unsigned long>(apiLastError),
			caller,
			static_cast<unsigned long long>(callerRva),
			static_cast<unsigned long>(GetCurrentThreadId()));
	}

	SetLastError(apiLastError);
	return result;
}

static void InstallWineDisplayApiTrace()
{
	g_displayTraceGameBase = reinterpret_cast<uint8_t*>(hook::getRVA<void>(0));
	const auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_displayTraceGameBase);
	if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE)
	{
		const auto ntHeader = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_displayTraceGameBase + dosHeader->e_lfanew);
		if (ntHeader->Signature == IMAGE_NT_SIGNATURE)
		{
			g_displayTraceGameSize = ntHeader->OptionalHeader.SizeOfImage;
		}
	}

	g_origEnumDisplaySettingsW = hook::iat("user32.dll", TraceEnumDisplaySettingsW, "EnumDisplaySettingsW");
	g_origEnumDisplayDevicesA = hook::iat("user32.dll", TraceEnumDisplayDevicesA, "EnumDisplayDevicesA");
	g_origGetMonitorInfoA = hook::iat("user32.dll", TraceGetMonitorInfoA, "GetMonitorInfoA");
	g_origMonitorFromPoint = hook::iat("user32.dll", TraceMonitorFromPoint, "MonitorFromPoint");
	g_origGetSystemMetrics = hook::iat("user32.dll", TraceGetSystemMetrics, "GetSystemMetrics");
	g_origQueryDisplayConfig = hook::iat("user32.dll", TraceQueryDisplayConfig, "QueryDisplayConfig");
	g_origDisplayConfigGetDeviceInfo = hook::iat("user32.dll", TraceDisplayConfigGetDeviceInfo, "DisplayConfigGetDeviceInfo");
	g_origGetDeviceCaps = hook::iat("gdi32.dll", TraceGetDeviceCaps, "GetDeviceCaps");

	const DWORD lastError = GetLastError();
	trace("[WineDisplayApi] stage=installed gameBase=%p gameSize=0x%llx enumSettings=%d enumDevices=%d monitorInfo=%d monitorFromPoint=%d systemMetrics=%d queryConfig=%d deviceInfo=%d deviceCaps=%d callerFilter=game-main-range tid=%lu\n",
		g_displayTraceGameBase,
		static_cast<unsigned long long>(g_displayTraceGameSize),
		g_origEnumDisplaySettingsW != nullptr,
		g_origEnumDisplayDevicesA != nullptr,
		g_origGetMonitorInfoA != nullptr,
		g_origMonitorFromPoint != nullptr,
		g_origGetSystemMetrics != nullptr,
		g_origQueryDisplayConfig != nullptr,
		g_origDisplayConfigGetDeviceInfo != nullptr,
		g_origGetDeviceCaps != nullptr,
		static_cast<unsigned long>(GetCurrentThreadId()));
	SetLastError(lastError);
}

static void TraceWineResolutionSettings(const char* settings)
{
	const DWORD lastError = GetLastError();
	trace("[WineResSettings] stage=post-parse width=%d height=%d window=%d vsync=%d isNew=%d tid=%lu\n",
		*(const int*)(settings + 248),
		*(const int*)(settings + 252),
		*(const int*)(settings + 260),
		*(const int*)(settings + 264),
		isNewSettingFile,
		static_cast<unsigned long>(GetCurrentThreadId()));
	SetLastError(lastError);
}

static hook::cdecl_stub<bool(void*, void*)> _saveSettings([]()
{
	return hook::get_pattern("66 39 34 48 75 F7 8D 41 01 48 8B CE", -0x55);
});

static void SetDefaults(char* settings)
{
	// default to borderless
	*(int*)(settings + 260) = 2;

	// and vsync off
	*(int*)(settings + 264) = 0;

	// and screen-native width
	POINT p = { 0, 0 };
	HMONITOR monitor = MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);

	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	GetMonitorInfoW(monitor, &mi);

	*(int*)(settings + 248) = mi.rcMonitor.right - mi.rcMonitor.left;
	*(int*)(settings + 252) = mi.rcMonitor.bottom - mi.rcMonitor.top;
}

static void (*g_origLoadSettingsFromParams)(void*);

static void LoadSettingsFromParams(char* settings)
{
	g_origLoadSettingsFromParams(settings);

	if (CfxIsWine())
	{
		TraceWineResolutionSettings(settings);
	}

	if (isNewSettingFile)
	{
		SetDefaults(settings);

		_saveSettings(settings, settings + 8);
	}
}

static void (*g_origResetSettings)(void*);

static void ResetSettings(char* settings)
{
	g_origResetSettings(settings);

	SetDefaults(settings);
}

extern DLL_IMPORT fwEvent<bool*> OnFlipModelHook;

#include <wrl.h>

namespace WRL = Microsoft::WRL;

#include <dxgi1_6.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

static HookFunction hookFunction([]()
{
	if (CfxIsWine())
	{
		InstallWineDisplayApiTrace();
	}

	// settings.xml moving for new installs
	if (IsNewInstall())
	{
		auto location = hook::get_address<const char**>(hook::get_pattern("48 8D 54 24 30 48 2B D1 8A 01 88 04 0A", -4));
		hook::put(location, "fxd:/gta5_settings.xml");

		MH_Initialize();
		MH_CreateHook(hook::get_pattern("84 C0 74 16 8B 45 ? 85 C0", -0x3C), LoadSettingsFromParams, (void**)&g_origLoadSettingsFromParams);
		MH_CreateHook(hook::get_call(hook::get_pattern("89 8D ? ? 00 00 74 0A 48 8B CB E8 ? ? ? ? EB", 11)), ResetSettings, (void**)&g_origResetSettings);
		MH_EnableHook(MH_ALL_HOOKS);

		// flip model is currently disabled as it breaks some cases of fullscreen resizing (open in borderless -> alt-enter -> alt-tab)
		// due to it being non-trivial to get the game to call ResizeBuffers.
		//
		// this needs some remote debugging attempt in order to correctly breakpoint all the state changes involved as fullscreen/focus are global
		// and therefore the debugger can't be used to investigate on a live system.
#if 0
		OnFlipModelHook.Connect([](bool* flip)
		{
			if (!*flip)
			{
				{
					WRL::ComPtr<IDXGIFactory> dxgiFactory;
					CreateDXGIFactory(IID_IDXGIFactory, &dxgiFactory);

					WRL::ComPtr<IDXGIAdapter1> adapter;
					WRL::ComPtr<IDXGIFactory6> factory6;
					HRESULT hr = dxgiFactory.As(&factory6);
					if (SUCCEEDED(hr))
					{
						std::wstring hpGpu;
						std::wstring lpGpu;

						for (UINT adapterIndex = 0;
							 DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
							 adapterIndex++)
						{
							DXGI_ADAPTER_DESC1 desc;
							adapter->GetDesc1(&desc);

							if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
							{
								// Don't select the Basic Render Driver adapter.
								continue;
							}

							hpGpu = desc.Description;
							break;
						}

						for (UINT adapterIndex = 0;
							 DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_MINIMUM_POWER, IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
							 adapterIndex++)
						{
							DXGI_ADAPTER_DESC1 desc;
							adapter->GetDesc1(&desc);

							if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
							{
								// Don't select the Basic Render Driver adapter.
								continue;
							}

							lpGpu = desc.Description;
							break;
						}

						// if they're the same GPU, or only one exists
						if (hpGpu.empty() || lpGpu.empty() || lpGpu == hpGpu)
						{
							*flip = true;
						}
					}
				}
			}
		});
#endif

		rage::fiDevice::OnInitialMount.Connect([]()
		{
			auto targetDevice = rage::fiDevice::GetDevice("fxd:/gta5_settings.xml", true);

			if (targetDevice)
			{
				auto handle = targetDevice->Open("fxd:/gta5_settings.xml", true);

				if (handle != -1)
				{
					targetDevice->Close(handle);
					return;
				}
			}

			isNewSettingFile = true;

			// migrate old settings
			auto origDevice = rage::fiDevice::GetDevice("user:/settings.xml", true);

			if (origDevice)
			{
				auto handle = origDevice->Open("user:/settings.xml", true);

				if (handle != -1)
				{
					auto len = origDevice->GetFileLength(handle);
					std::vector<char> data(len);

					origDevice->Read(handle, data.data(), len);

					origDevice->Close(handle);

					auto targetDevice = rage::fiDevice::GetDevice("fxd:/gta5_settings.xml", false);

					if (targetDevice)
					{
						auto handle = targetDevice->Create("fxd:/gta5_settings.xml");

						if (handle != -1)
						{
							targetDevice->Write(handle, data.data(), data.size());
							targetDevice->Close(handle);
						}
					}
				}
			}
		}, INT32_MAX);
	}

	// move settings around

	// aspect ratios
	hook::put<int>(hook::get_pattern("48 8D 68 A1 48 81 EC A0 00 00 00 8B D9 B9 89", 14), 0x8A);

	// window modes
	hook::put<int>(hook::get_pattern("45 8D 7C 24 02 8D 43 01 48 C1 E6 05", -0x50), 0x8A);

	// resolutions
	hook::put<int>(hook::get_pattern("48 8D 68 A9 48 81 EC 90 00 00 00 44 8B F1 B9 89", 15), 0x8A);

	// refresh rates
	hook::put<int>(hook::get_pattern("48 81 EC 80 00 00 00 8B D9 B9 89 00 00 00 44 8A E2", 10), 0x8A);

	// scaling rates
	{
		auto location = hook::get_pattern<char>("B9 8A 00 00 00 44 8A F2 E8", 1);
		hook::put<int>(location, 0x89);

		const char* repStr = "MO_GFX_1o1";
		auto mem = (char*)hook::AllocateStubMemory(strlen(repStr) + 1);
		strcpy(mem, repStr);

		hook::put<int32_t>(location + 0x4C, mem - (location + 0x4C) - 4);
	}
});

static InitFunction initFunction([]()
{
	game::AddCustomText("GFX_SCALING", "Render Resolution");

	OnMainGameFrame.Connect([]()
	{
		static int lastResX, lastResY;
		int resX, resY;

		GetGameResolution(resX, resY);

		if (lastResX != resX || lastResY != resY)
		{
			lastResX = resX;
			lastResY = resY;

			struct ResPair
			{
				std::string_view label;
				std::string_view origLabel;
				double ratio;
			};

			ResPair resPairs[] = {
				{ "MO_GFX_1o1", "", 1.0 },
				{ "MO_GFX_1o2", "1/2", 1.0 / 2.0 },
				{ "MO_GFX_2o3", "2/3", 2.0 / 3.0 },
				{ "MO_GFX_3o4", "3/4", 3.0 / 4.0 },
				{ "MO_GFX_5o6", "5/6", 5.0 / 6.0 },
				{ "MO_GFX_5o4", "5/4", 5.0 / 4.0 },
				{ "MO_GFX_3o2", "3/2", 3.0 / 2.0 },
				{ "MO_GFX_7o4", "7/4", 7.0 / 4.0 },
				{ "MO_GFX_2o1", "2/1", 2.0 / 1.0 },
				{ "MO_GFX_5o2", "5/2", 5.0 / 2.0 },
			};

			for (auto& pair : resPairs)
			{
				game::AddCustomText(std::string{ pair.label }, fmt::sprintf("%ix%i%s",
					(int)round(resX * pair.ratio),
					(int)round(resY * pair.ratio),
					pair.origLabel.empty() ? "" : fmt::sprintf(" (%s)", pair.origLabel)));
			}

			HMONITOR monitor = MonitorFromWindow(CoreGetGameWindow(), MONITOR_DEFAULTTOPRIMARY);

			MONITORINFO mi;
			mi.cbSize = sizeof(mi);
			GetMonitorInfoW(monitor, &mi);

			bool isFullSize = (resX == (mi.rcMonitor.right - mi.rcMonitor.left)) && (resY == (mi.rcMonitor.bottom - mi.rcMonitor.top));

			game::AddCustomText("VID_SCR_BORDERLESS", isFullSize ? "Full Screen" : "Windowed (Borderless)");
			game::AddCustomText("VID_FULLSCREEN", "Full Screen (Exclusive)");
		}
	});
});
