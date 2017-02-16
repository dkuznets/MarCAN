// marCAN.cpp: определяет экспортированные функции для приложения DLL.
//

#include "stdafx.h"
#include "marCAN.h"
//#include "chai.h"

#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <setupapi.h>
#include <assert.h>
#include <tchar.h>
#include <conio.h>
#include <vector>
#include <iostream>
#include <thread>
#include <mutex>
#include "atltrace.h"


#define VER 0x0000290820161556
UINT64 ver = VER;
BOOLEAN flag_thr = true;

SECURITY_ATTRIBUTES sa4Event;
HANDLE hEvent, hMain;
int prt;

canerrs_t errs;
canwait_t cw;
canmsg_t frame;

#if defined(_UNICODE) || defined(UNICODE)
#include <strsafe.h>
#endif /* UNICODE */

//#include "can200-api.h"

/**
* \file can200-api.cpp
* \brief Файл содержит реализацию функций для работы с платами MarCAN.
*/
RX_TX_Buffer msgbuf[20000];
BYTE outbuf[100000];
std::vector<RX_TX_Buffer> mbuf;
std::thread thr;
std::mutex mtx;


//-----------------------------------------------------------------------------
#if defined(_UNICODE) || defined(UNICODE)
static BOOL GetDevicePath(LPGUID InterfaceGuid, int number, PWCHAR DevicePath, size_t BufLen);
#else
static BOOL GetDevicePath(LPGUID InterfaceGuid, int number, PCHAR DevicePath, size_t BufLen);
#endif /* UNICODE */

//------------------------------------------------------------------------------------------------------------------
static BOOL Is64BitWindows();
/* Определяем на 64-х или 32-х битной системе мы работаем */

/*
* \return MarCAN_OK - успешное завершение
* \return MarCAN_ERR_PARM - ошибка входных параметров
* \return MarCAN_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
*/
void t_recv()
{
/*
#ifdef DEBUG
	ATLTRACE(_T("Создание потока\r\n"));
#endif
	TEventData evd = { 0 };
	TCAN_VPDData conf = { 0 };
	int result = 0;
	SECURITY_ATTRIBUTES recvattr;
	HANDLE recvev;
	int ccc = 30;
	recvattr.nLength = sizeof(recvattr);
	recvattr.lpSecurityDescriptor = NULL;
	recvattr.bInheritHandle = TRUE;

#ifdef DEBUG
	if (!(recvev = CreateEvent(&recvattr, TRUE, FALSE, NULL)))
		ATLTRACE(_T("Не создан эвент\r\n"));
#else
	recvev = CreateEvent(&recvattr, TRUE, FALSE, NULL);
#endif

	MarCAN_ClearBuf(hMain, 1);
	result = MarCAN_DefEvent(hMain, 1, recvev);
#ifdef DEBUG
	if (result != MarCAN_OK)
	{
		ATLTRACE(_T("Не задано событие для элкус\r\n"));
	}
#endif

	result = MarCAN_SetCommand(hMain, 1, 0x02);
	result = MarCAN_SetCommand(hMain, 1, 0x04);
	result = MarCAN_SetCommand(hMain, 1, 0x08);

	while (flag_thr)
	{
		switch (WaitForSingleObject(recvev, 1000))
		{
		case WAIT_OBJECT_0:
			do
			{
				int result = MarCAN_GetEventData(hMain, 1, &evd);
				if (evd.IntrID)
				{
					mtx.lock();
					mbuf.push_back(evd.rxtxbuf);
					mtx.unlock();
				}
			} while (result == 0 && evd.IntrID == 1);
			ResetEvent(recvev);
			break;
		case WAIT_TIMEOUT:
#ifdef DEBUG
			ATLTRACE(_T("Таймаут...\r\n"));
#endif
		default:
			break;
		}
	}
#ifdef DEBUG
	ATLTRACE(_T("Выход из потока 1...\r\n"));
#endif
	ResetEvent(recvev);
	result = CloseHandle(recvev);
	MarCAN_ClearBuf(hMain, 1);
	ExitThread(0);
#ifdef DEBUG
	ATLTRACE(_T("Выход из потока 2...\r\n"));
#endif
	return;
*/
	canmsg_t mess;
	memset(mess.data, 0, sizeof(mess.data));
	canwait_t cw;
	canerrs_t ce;
	cw.chan = 0;
	cw.wflags = CI_WAIT_RC | CI_WAIT_ER;
	INT16 ret = 0;
	printf("run thread\r\n");
	CiStart(0);
	CiErrsGetClear(0, &ce);
	while (flag_thr)
	{
		try
		{
			CiErrsGetClear(0, &ce);
			ret = CiWaitEvent(&cw, 1, 1000);
		}
		catch (...)
		{
			printf("CiWaitEvent() failed\r\n");
		} 
		if (ret == 0)
		{
//			printf("CiWaitEvent timeout\r\n");
			continue;
		}
		else if (ret > 0)
		{
			if ((cw.rflags & CI_WAIT_RC) > 0)
			{
				UINT16 num = 0;
				int qq = CiRcQueGetCnt(0, &num);
				if (num > 1)
				{
					canmsg_t arrmsg[1000] = { 0 };
					ret = CiRead(0, arrmsg, num);
					mtx.lock();
					for (int i = 0; i < num; i++)
						mbuf.push_back((RX_TX_Buffer)arrmsg[i]);
					mtx.unlock();
				}
				else
				{
					ret = CiRead(0, &mess, 1);
					if (ret >= 1)
					{
						mtx.lock();
						mbuf.push_back((RX_TX_Buffer)mess);
						mtx.unlock();
					}
					else
						printf("CiRead error %d\r\n", ret);
				}
			}
			if ((cw.rflags & CI_WAIT_ER) > 0)
			{
				canerrs_t errs;
				ret = CiErrsGetClear(0, &errs);
				if (ret >= 0)
				{
					if (errs.ewl > 0)
						printf("EWL %d times\r\n", errs.ewl);
					if (errs.boff > 0)
						printf("BOFF %d times\r\n", errs.boff);
					if (errs.hwovr > 0)
						printf("HOVR %d times\r\n", errs.hwovr);
					if (errs.swovr > 0)
						printf("SOVR %d times\r\n", errs.swovr);
					if (errs.wtout > 0)
						printf("WTOUT %d times\r\n", errs.wtout);
				}
			}
			continue;
		}
		else
		{
			printf("thread timeout\r\n");
		}
	}
	printf("close thread\r\n");
}

EXPORT INT16 __stdcall MarCAN_Open(UINT16 speed)
{
	mbuf.clear();
	if (CiInit() < 0)
		return MarCAN_ERR_SYS;
	if(CiOpen(0, CIO_CAN11 | CIO_CAN29))
		return MarCAN_ERR_SYS;

	BYTE bt0, bt1;
	switch (speed)
	{
		case 0:
			bt0 = 0x00;
			bt1 = 0x14;
			break;
		case 1:
			bt0 = 0x00;
			bt1 = 0x16;
			break;
		case 2:
			bt0 = 0x00;
			bt1 = 0x1c;
			break;
		case 3:
			bt0 = 0x01;
			bt1 = 0x1c;
			break;
		case 4:
			bt0 = 0x03;
			bt1 = 0x1c;
			break;
		case 5:
			bt0 = 0x04;
			bt1 = 0x1c;
			break;
		case 6:
			bt0 = 0x09;
			bt1 = 0x1c;
			break;
		case 7:
			bt0 = 0x18;
			bt1 = 0x1c;
			break;
		case 8:
			bt0 = 0x31;
			bt1 = 0x1c;
			break;
		default:
			bt0 = 0x00;
			bt1 = 0x1c;
			break;
	}
	if (CiSetBaud(0, bt0, bt1) < 0)
		return MarCAN_ERR_SYS;

	if (CiRcQueResize(0, UINT16_MAX) < 0)
		return MarCAN_ERR_SYS;

	CiStart(0);
	CiHwReset(0);
	cw.chan = 0;
	cw.wflags = CI_WAIT_RC | CI_WAIT_ER;
	memset(frame.data, 0, 8 * sizeof(BYTE));
	canerrs_t errs;
	CiErrsGetClear(0, &errs);
	return MarCAN_OK;
}
EXPORT INT16 __stdcall MarCAN_Close(void)
{
	CiStop(0);
	if (CiClose(0) < 0)
		return MarCAN_ERR_SYS;
	return MarCAN_OK;
}
EXPORT INT16 __stdcall MarCAN_SetCANSpeed(UINT16 speed)
{
	assert(speed < 9);

	CiStop(0);
	BYTE bt0, bt1;
	switch (speed)
	{
		case 0:
			bt0 = 0x00;
			bt1 = 0x14;
			break;
		case 1:
			bt0 = 0x00;
			bt1 = 0x16;
			break;
		case 2:
			bt0 = 0x00;
			bt1 = 0x1c;
			break;
		case 3:
			bt0 = 0x01;
			bt1 = 0x1c;
			break;
		case 4:
			bt0 = 0x03;
			bt1 = 0x1c;
			break;
		case 5:
			bt0 = 0x04;
			bt1 = 0x1c;
			break;
		case 6:
			bt0 = 0x09;
			bt1 = 0x1c;
			break;
		case 7:
			bt0 = 0x18;
			bt1 = 0x1c;
			break;
		case 8:
			bt0 = 0x31;
			bt1 = 0x1c;
			break;
		default:
			bt0 = 0x00;
			bt1 = 0x1c;
			break;
		}
	if (CiSetBaud(0, bt0, bt1) < 0)
		return MarCAN_ERR_SYS;

	if (CiRcQueResize(0, UINT16_MAX) < 0)
		return MarCAN_ERR_SYS;
	if (CiStart(0) < 0)
	{
		return MarCAN_ERR_SYS;
	}
	return MarCAN_OK;
}
EXPORT INT16 __stdcall MarCAN_ClearRX(void)
{
	UINT16 stat;
	if (CiRcQueCancel(0, &stat) < 0)
		return MarCAN_ERR_SYS;
	return MarCAN_OK;
}
EXPORT INT16 __stdcall MarCAN_GetStatus(chipstat_t *Status)
{
	sja1000stat_t stat;
	CiChipStat(0, (chipstat_t *)&stat);
	Status = (chipstat_t*)&stat;
	return MarCAN_OK;
}
EXPORT INT16 __stdcall MarCAN_Write(pRX_TX_Buffer Buffer)
{
	if (CiWrite(0, (canmsg_t*)Buffer, 1) < 0)
		return MarCAN_ERR_SYS;
	return MarCAN_OK;
}
EXPORT INT16 __stdcall MarCAN_GetErrorCounter(canerrs_t *Counter)
{
	if (CiErrsGetClear(0, Counter) < 0)
		return MarCAN_ERR_SYS;
	return MarCAN_OK;
}
EXPORT INT16 __stdcall MarCAN_HardReset(void)
{
	if (CiHwReset(0) < 0)
		return MarCAN_ERR_SYS;
	return MarCAN_OK;
}
EXPORT UINT64 __stdcall MarCAN_GetAPIVer(void)
{
	return ver;
}
EXPORT BYTE __stdcall MarCAN_GetByte(int num)
{
	return outbuf[num];
}
EXPORT void __stdcall MarCAN_Recv_Enable(void)
{
	flag_thr = true;
	thr = std::thread(t_recv);
}
EXPORT void __stdcall MarCAN_Recv_Disable(void)
{
	flag_thr = false;
	if (thr.joinable())
		thr.join();
	return;
}
EXPORT INT16 __stdcall MarCAN_Pop(pRX_TX_Buffer Buffer)
{
	mtx.lock();
	*Buffer = mbuf.front();
	mbuf.erase(mbuf.begin());
	mtx.unlock();
	return 0;
}
EXPORT INT16 __stdcall MarCAN_VecSize(void)
{
	mtx.lock();
	int ii = mbuf.size();
	mtx.unlock();
	return ii;
}
EXPORT INT16 __stdcall MarCAN_BoardInfo(canboard_t *binfo)
{
	binfo->brdnum = 0;
	if (CiBoardInfo(binfo) < 0)
		return MarCAN_ERR_SYS;
	return MarCAN_OK;
}
typedef BOOL(WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);

#pragma region Is64Bit
#pragma comment (lib, "setupapi.lib")

static BOOL Is64BitWindows()
{
	/* Определяем на 64-х или 32-х битной системе мы работаем */
#if defined(_WIN64)
	return TRUE;  // 64-битные программы работают только на 64-х битной системе
#elif defined(_WIN32)
	// 64-битные программы работают на 32-х и 64-х битной системе
	BOOL f64 = FALSE;
	LPFN_ISWOW64PROCESS fnIsWow64Process;
#if defined(_UNICODE) || defined(UNICODE)
	fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandle(L"kernel32"), "IsWow64Process");
#else
	fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandle(_T("kernel32")), "IsWow64Process");
#endif /* UNICODE */
	if (NULL != fnIsWow64Process)
	{
		return fnIsWow64Process(GetCurrentProcess(), &f64) && f64;
	}
	return FALSE;
#else
	return FALSE; // 16 бит
#endif
}

#pragma endregion

