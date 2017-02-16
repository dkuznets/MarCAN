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

#define VER 0x0000070420160925
UINT64 ver = VER;
BOOLEAN flag_thr = true;

SECURITY_ATTRIBUTES sa4Event;
HANDLE hEvent, hMain;
int prt;

#if defined(_UNICODE) || defined(UNICODE)
#include <strsafe.h>
#endif /* UNICODE */

#include "can200-api.h"

/**
 * \file can200-api.cpp
 * \brief Файл содержит реализацию функций для работы с платами CAN200.
 */
RX_TX_Buffer msgbuf[20000];
BYTE outbuf[100000];
std::vector<RX_TX_Buffer> mbuf;
std::thread thr;
std::mutex mtx;

#pragma region FUNC
/** \cond */
#define MAX_DEVPATH_LENGTH 256

/* Функции для внутреннего использования */
//-----------------------------------------------------------------------------
#if defined(_UNICODE) || defined(UNICODE)
static BOOL GetDevicePath(LPGUID InterfaceGuid, int number, PWCHAR DevicePath, size_t BufLen);
#else
static BOOL GetDevicePath(LPGUID InterfaceGuid, int number, PCHAR DevicePath, size_t BufLen);
#endif /* UNICODE */

//------------------------------------------------------------------------------------------------------------------
static BOOL Is64BitWindows();
/* Определяем на 64-х или 32-х битной системе мы работаем */

//-----------------------------------------------------------------------------
static BOOL GetDeviceCount(LPGUID InterfaceGuid, int *number);

//
// Определяем Guid для доступа к устройству.
//

DEFINE_GUID (GUID_DEVINTERFACE_CAN200PCIE,
    0x6f5e060c,0xbc5a,0x48b2,0x83,0x01,0xa9,0x62,0x46,0x03,0xda,0xcd);
// {6f5e060c-bc5a-48b2-8301-a9624603dacd}
/** \endcond */

/**
 * \fn int CAN200_GetNumberDevice(int *count)
 * \brief Определение количества установленных плат
 *
 * Функция определяет количество установленных в компьютере плат CAN-200PCI и CAN-200PCIe
 * \param count (OUT) Указатель на переменную типа int, где будет сохранено количество установленных плат
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_GetNumberDevice(int *count)
{
	/* Корректность входных параметров */
	assert(NULL != count);

	if (NULL == count)
	{
		return CAN200_ERR_PARM;
	}

	if (!GetDeviceCount(
		(LPGUID)&GUID_DEVINTERFACE_CAN200PCIE, count))
	{
		return CAN200_ERR_SYS;
	}

	return CAN200_OK;
}
/**
 * \fn HANDLE CAN200_Open(int number)
 * \brief Открытие одной из плат для работы
 *
 * Функция позволяет открыть одну из плат для работы. Номер открываемой платы определяется входным параметром
 * \param number (IN) Номер открываемой платы
 * \return INVALID_HANDLE_VALUE - ошибка открытия платы с указанным номером
 * \return иначе - хэндл для работы с платой
 * \warning Нумерация устройств начинается с нуля
 */
EXPORT HANDLE __stdcall CAN200_Open(int number)
{
#if defined(_UNICODE) || defined(UNICODE)
	WCHAR completeDeviceName[MAX_DEVPATH_LENGTH];
#else
	char  completeDeviceName[MAX_DEVPATH_LENGTH];
#endif /* UNICODE */
	HANDLE hh;
	/* Корректность входных параметров */
	assert(number >= 0);

	if (number < 0)
	{
		return INVALID_HANDLE_VALUE;
	}

	if (!GetDevicePath(
		(LPGUID)&GUID_DEVINTERFACE_CAN200PCIE, number,
		completeDeviceName,
		sizeof(completeDeviceName)))
	{
		return INVALID_HANDLE_VALUE;
	}

	mbuf.clear();
	hh = CreateFile(completeDeviceName,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
		);
	hMain = hh;
	return hh;
}
/**
 * \fn int CAN200_Close(HANDLE Handle)
 * \brief Закрытие ранее открытой платы
 *
 * Функция позволяет закрыть ранее открытую плату
 * \param Handle (IN) Хэндл закрываемой платы (возвращенный функцией #CAN200_Open)
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_Close(HANDLE Handle)
{
	BOOL res;
	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	for (int i = 0; i < 100; i++)
	{
		ResetEvent(hEvent);
	}
	res = CloseHandle(Handle);
	//flag_thr = false;
	//thr.join();
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return CAN200_OK;
}
/**
 * \fn int CAN200_SetWorkMode(HANDLE Handle, int Channel, int Mode)
 * \brief Настройка режима работы одного из каналов платы
 *
 * Функция позволяет настроить один из каналов платы в режим BasicCAN или PeliCAN
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Mode (IN) Режим работы канала платы (#BasicCAN или #PeliCAN)
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_SetWorkMode(HANDLE Handle, int Channel, int Mode)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((BasicCAN == Mode) || (PeliCAN == Mode));
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if ((BasicCAN != Mode) && (PeliCAN != Mode))
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.Mode = Mode;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_SetWorkMode, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_GetWorkMode(HANDLE Handle, int Channel, int *Mode)
 * \brief Получение текущего режима работы одного из каналов платы
 *
 * Функция позволяет получить текущий режим работы одного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Mode (OUT) Указатель на переменную типа int, где будет сохранен текущий режим работы канала платы
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_GetWorkMode(HANDLE Handle, int Channel, int *Mode)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != Mode);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Mode)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_GetWorkMode, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Mode = ioctlRes.Data.Mode;
	}

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_SetDriverMode(HANDLE Handle, int Channel, int Mode)
 * \brief Настройка выходных формирователей канала платы
 *
 * Функция позволяет настроить выходные формирователи заданного канала платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Mode (IN) Параметр настройки выходных формирователей (для нормальной работы должен быть равен 0x1B)
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 * \warning Функцию необходимо вызвать один раз при инициализации CAN-контроллера после включения питания.
 * Без вызова этой функции прием/передача данных CAN-контроллером будет невозможна!
 */
EXPORT int __stdcall CAN200_SetDriverMode(HANDLE Handle, int Channel, int Mode)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0xff >= Mode);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0xff < Mode)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.Mode = Mode;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_SetDriverMode, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_GetConfig(HANDLE Handle, pTCAN_VPDData Buffer)
 * \brief Получение описания платы
 *
 * Функция возвращает описание платы (тип, серийный номер и т.д.)
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Buffer (OUT) Указатель на структуру #TCAN_VPDData, где будет сохранена информация о плате
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_GetConfig(HANDLE Handle, pTCAN_VPDData Buffer)
{
	TCAN_VPDData ioctlRes;
	DWORD        ret;
	BOOL         res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert(0 != Buffer);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Buffer)
	{
		return CAN200_ERR_PARM;
	}

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_GetConfig, NULL, 0,
		&ioctlRes, sizeof(TCAN_VPDData), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	memcpy(Buffer, &ioctlRes, sizeof(TCAN_VPDData));

	return CAN200_OK;
}
/**
 * \fn int CAN200_SetCANSpeed(HANDLE Handle, int Channel, unsigned int Speed)
 * \brief Настройка скорости обмена одного из каналов платы
 *
 * Функция позволяет настроить скорость обмена по шине CAN одиного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Speed (IN) Значение скорости обмена по шине CAN (одно из значений #CAN_SPEED_1000,
 * #CAN_SPEED_800, #CAN_SPEED_500, #CAN_SPEED_250, #CAN_SPEED_125, #CAN_SPEED_50, #CAN_SPEED_20,
 * #CAN_SPEED_10 или #CAN_SPEED_USER_DEFINED)
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_SetCANSpeed(HANDLE Handle, int Channel, unsigned int Speed)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert( (CAN_SPEED_1000 == Speed) || (CAN_SPEED_800 == Speed) ||
			(CAN_SPEED_500  == Speed) || (CAN_SPEED_250 == Speed) ||
			(CAN_SPEED_125  == Speed) || (CAN_SPEED_50  == Speed) ||
			(CAN_SPEED_20   == Speed) || (CAN_SPEED_10  == Speed) ||
			(IS_CAN_SPEED_USER_DEFINED(Speed)));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_SPEED_1000 != Speed) && (CAN_SPEED_800 != Speed) &&
		(CAN_SPEED_500  != Speed) && (CAN_SPEED_250 != Speed) &&
		(CAN_SPEED_125  != Speed) && (CAN_SPEED_50  != Speed) &&
		(CAN_SPEED_20   != Speed) && (CAN_SPEED_10  != Speed) &&
		(!IS_CAN_SPEED_USER_DEFINED(Speed)))
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.Speed = Speed;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_SetCANSpeed, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_GetCANSpeed(HANDLE Handle, int Channel, unsigned int *Speed)
 * \brief Получение текущей скорости обмена по CAN одного из каналов платы
 *
 * Функция позволяет получить текущее значение скорости обмена по CAN одного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Speed (OUT) Указатель на переменную типа int, где будет сохранено текущее значение скорости обмена по CAN
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_GetCANSpeed(HANDLE Handle, int Channel, unsigned int *Speed)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != Speed);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Speed)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_GetCANSpeed, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Speed = ioctlRes.Data.Speed;
	}

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_GetStatus(HANDLE Handle, int Channel, int *Status)
 * \brief Получение статуса одного из каналов платы
 *
 * Функция позволяет получить статус одного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Status (OUT) Указатель на переменную типа int, где будет сохранен статус канала платы
 * (используются младшие 8 бит, для расшифровки значений можно использовать структуру #CAN_status_t)
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_GetStatus(HANDLE Handle, int Channel, int *Status)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != Status);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Status)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_GetStatus, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Status = ioctlRes.Data.Status;
	}

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_SetInterruptSource(HANDLE Handle, int Channel, int Source)
 * \brief Настройка разрешения прерываний для одного из каналов платы
 *
 * Функция позволяет настроить разрешение (маску) прерываний для одного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Source (IN) Маска прерываний (используются младшие 8 бит, бит равный 1 разрешает соответствующее прерывание,
 * для расшифровки значений отдельных битов можно использовать структуру #CAN_interrupt_t)
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 * \warning Бит №4 в режиме #BasicCAN не маскирует соответствующую причину прерывания (выход контроллера из режима "сна").
 * Эта причина в режиме #BasicCAN не маскируется и всегда приводит к возникновению прерывания от платы.
 */
EXPORT int __stdcall CAN200_SetInterruptSource(HANDLE Handle, int Channel, int Source)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;
	int result = 0;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(Source <= 0xFF);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (Source > 0xFF)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.Interrupt = Source;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_SetInterruptSource, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	//sa4Event.nLength = sizeof(sa4Event);
	//sa4Event.lpSecurityDescriptor = NULL;
	//sa4Event.bInheritHandle = TRUE;

	//if (!(hEvent = CreateEvent(&sa4Event, TRUE, FALSE, NULL)))
	//{
	//	return CAN200_ERR_SYS;
	//}
	////for (int i = 0; i < 1000; i++)
	////{
	////	ResetEvent(hEvent);
	////}
	//result = CAN200_DefEvent(Handle, Channel, hEvent);
	//if (result != CAN200_OK)
	//	return CAN200_ERR_SYS;

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_GetInterruptSource(HANDLE Handle, int Channel, int *Source)
 * \brief Получение маски прерываний
 *
 * Функция позволяет получить маску прерываний для одного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Source (OUT) Указатель на переменную типа int, где будет сохранена маска прерываний канала платы (используются младшие 8 бит,
 * для расшифровки значений отдельных битов можно использовать структуру #CAN_interrupt_t)
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_GetInterruptSource(HANDLE Handle, int Channel, int *Source)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != Source);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Source)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_GetInterruptSource, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Source = ioctlRes.Data.Mask;
	}
	return ioctlRes.Status;
}
/**
 * \fn int CAN200_SetCommand(HANDLE Handle, int Channel, int Command)
 * \brief Выдача команды CAN контроллеру одного из каналов платы
 *
 * Функция позволяет выдать команду управления CAN контроллеру одного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Command (IN) Команда (используются младшие 8 бит, для расшифровки можно использовать
 * структуру #CAN_command_t)
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_SetCommand(HANDLE Handle, int Channel, int Command)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(Command <= 0xFF);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (Command > 0xFF)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.Command = Command;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_SetCommand, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_SetTxBuffer(HANDLE Handle, int Channel, pRX_TX_Buffer Buffer)
 * \brief Запись кадра в буфер выдачи
 *
 * Функция позволяет записать CAN кадр в буфер выдачи одного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Buffer (IN) Указатель на структуру #RX_TX_Buffer, где содержится выдаваемый кадр
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_BUSY - буфер выдачи занят
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_SetTxBuffer(HANDLE Handle, int Channel, pRX_TX_Buffer Buffer)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(Buffer->DLC <= 8);
	assert((0 == Buffer->RTR) || (1 == Buffer->RTR));
	assert((BasicCAN == Buffer->FF) || (PeliCAN == Buffer->FF));
	assert(
		((BasicCAN == Buffer->FF) && (Buffer->sID <= 0x7FF)) ||
		((PeliCAN == Buffer->FF)  && (Buffer->extID <= 0x1FFFFFFF)));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (
		((Buffer->FF != BasicCAN) && (Buffer->FF != PeliCAN))      ||
		((Buffer->FF == BasicCAN) && (Buffer->sID > 0x7FF))        ||
		((Buffer->FF == PeliCAN)  && (Buffer->extID > 0x1FFFFFFF)) ||
		((Buffer->RTR != 0)       && (Buffer->RTR != 1))           ||
		 (Buffer->DLC > 8))
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	memcpy(&ioctlReq.CommandData.Buffer, Buffer, sizeof(RX_TX_Buffer));

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_SetTxBuffer, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_DefEvent(HANDLE Handle, int Channel, HANDLE hEvent)
 * \brief Передача события синхронизации драйверу
 *
 * Функция позволяет передать событие синхронизации (созданное функцией CreateEvent) драйверу.
 * Данное событие будет привязано в драйвере к данной плате и указанному каналу.
 * Событие может быть использование для организации приема данных без циклического опроса статуса платы.
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param hEvent (IN) Хэндл события синхронизации
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_DefEvent(HANDLE Handle, int Channel, HANDLE hEvent)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
#if defined(_WIN64)
	ioctlReq.CommandData.Event = hEvent;
#elif defined(_WIN32)
	if (FALSE == Is64BitWindows())
	{
		/* 32-х битная программа на 32-х битной системе */
		ioctlReq.CommandData.Event = hEvent;
	}
	else
	{
		/* 32-х битная программа на 64-х битной системе */
		ioctlReq.CommandData.Event64 = HandleToHandle64(hEvent);
	}
#else
    return CAN200_ERR_SYS;
#endif

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_DefEvent, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_GetEventData(HANDLE Handle, int Channel, pTEventData Buffer)
 * \brief Получение причины события (из буфера драйвера)
 *
 * Функция возвращает причину возникновения события (прием кадра, ошибка и т.д.) из буфера приема драйвера для одного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Buffer (OUT) Указатель на переменную типа #TEventData, где будет сохранена причина прерывания
 * и, в случае приема кадра, принятый кадр
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_GetEventData(HANDLE Handle, int Channel, pTEventData Buffer)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(0 != Buffer);
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Buffer)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	
	res = DeviceIoControl(Handle, IOCTL_CAN200PC_GetEventData, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		memcpy(Buffer, &ioctlRes.Data.EventData, sizeof(TEventData));
	}

	return ioctlRes.Status;
}
/**
 * \fn int CAN200_GetRxBuffer(HANDLE Handle, int Channel, pRX_TX_Buffer Buffer)
 * \brief Получение содержимого буфера приема
 *
 * Функция возвращает содержимое буфера приема контроллера CAN одного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Buffer (OUT) Указатель на переменную типа #RX_TX_Buffer, где будет сохранено содержимое буфера приема
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 * \warning Функция возвратит содержимое буфера приема, даже если этот буфер пустой (содержит случайные данные).
 * Для получения принятого кадра перед вызовом этой функции необходимо использовать функцию #CAN200_GetStatus.
 */
EXPORT int __stdcall CAN200_GetRxBuffer(HANDLE Handle, int Channel, pRX_TX_Buffer Buffer)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	assert(0 != Buffer);
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Buffer)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_GetRxBuffer, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		memcpy(Buffer, &ioctlRes.Data.Buffer, sizeof(RX_TX_Buffer));
	}

	return ioctlRes.Status;
}

/* Функции для режима BasicCAN */
/** \addtogroup BasicCAN
 * \{
 * \fn int CAN200_B_SetInputFilter(HANDLE Handle, int Channel, bFilter_t *filter)
 * \brief Настройка фильтра входных кадров для режима #BasicCAN
 *
 * Функция позволяет настроить фильтр входных кадров для режима #BasicCAN (для одного из каналов)
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param filter (IN) Указатель на структуру #bFilter_t с описанием фильтра
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #BasicCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 * \par Функционирование входного фильтра в режиме BasicCAN.
 * Таблица, поясняющая функционирование входного фильтра
 * <TABLE>
 * <TR>
 * <TD rowspan=2 style='background:#D9D9D9'></TD> <TD colspan=11 align=center style='background:#D9D9D9'>Бит</TD>
 * </TR>
 * <TR>
 * <TD style='background:#D9D9D9'>10</TD> <TD style='background:#D9D9D9'>9</TD> <TD style='background:#D9D9D9'>8</TD> <TD style='background:#D9D9D9'>7</TD> <TD style='background:#D9D9D9'>6</TD> <TD style='background:#D9D9D9'>5</TD> <TD style='background:#D9D9D9'>4</TD> <TD style='background:#D9D9D9'>3</TD> <TD style='background:#D9D9D9'>2</TD> <TD style='background:#D9D9D9'>1</TD> <TD style='background:#D9D9D9'>0</TD>
 * </TR>
 * <TR>
 * <TD>Идентификатор кадра</TD> <TD>1</TD> <TD>0</TD> <TD>1</TD> <TD>0</TD> <TD>1</TD> <TD>0</TD> <TD>1</TD> <TD>0</TD> <TD>1</TD> <TD>0</TD> <TD>1</TD>
 * </TR>
 * <TR>
 * <TD colspan=12>&nbsp;</TD>
 * </TR>
 * <TR>
 * <TD rowspan=2 style='background:#D9D9D9'></TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит</TD>
 * </TR>
 * <TR>
 * <TD style='background:#D9D9D9'>7</TD> <TD style='background:#D9D9D9'>6</TD> <TD style='background:#D9D9D9'>5</TD> <TD style='background:#D9D9D9'>4</TD> <TD style='background:#D9D9D9'>3</TD> <TD style='background:#D9D9D9'>2</TD> <TD style='background:#D9D9D9'>1</TD> <TD style='background:#D9D9D9'>0</TD>
 * </TR>
 * <TR>
 * <TD>Маска</TD> <TD>1</TD> <TD>1</TD> <TD>1</TD> <TD>1</TD> <TD>0</TD> <TD>0</TD> <TD>0</TD> <TD>0</TD>
 * </TR>
 * <TR>
 * <TD>Идентификатор входного фильтра</TD> <TD>x</TD> <TD>x</TD> <TD>x</TD> <TD>x</TD> <TD>1</TD> <TD>0</TD> <TD>1</TD> <TD>0</TD>
 * </TR>
 * <TR>
 * <TD>Результат сравнения</TD> <TD>1</TD> <TD>1</TD> <TD>1</TD> <TD>1</TD> <TD>1</TD> <TD>1</TD> <TD>1</TD> <TD>1</TD>
 * </TR>
 * </TABLE>
 * х – любое значение.
 *
 * Входной фильтр функционирует следующим образом:
 * Биты 3-10 идентификатора кадра сравниваются на идентичность (поразрядно) с идентификатором входного фильтра.
 * Если в соответствующем разряде маски стоит логическая 1, то биты идентификатора кадра и идентификатора входного
 * фильтра в этом разряде не сравниваются и результат сравнения в данном разряде принимается равным логической 1.
 * Кадр проходит приемный фильтр, если результат сравнения во всех разрядах содержит логические 1.
 *
 * Пример:
 *
 * В таблице выше показан пример сравнения идентификатора кадра в приёмном фильтре.
 * Результат сравнения положительный и кадр будет записан в буфер приёма.
 * \note Для приема всех кадров необходимо установить маску равную FFh (значение идентификатора входного фильтра можно установить любое).
 */
EXPORT int __stdcall CAN200_B_SetInputFilter(HANDLE Handle, int Channel, bFilter_t *filter)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != filter);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.bFilter.Mask = filter->Mask;
	ioctlReq.CommandData.bFilter.Code = filter->Code;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_B_SetInputFilter, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/** \addtogroup BasicCAN
 * \{
 * \fn int CAN200_B_GetInputFilter(HANDLE Handle, int Channel, bFilter_t *filter)
 * \brief Получение фильтра входных кадров для режима #BasicCAN
 *
 * Функция позволяет получить текущие настройки фильтра входных кадров для режима #BasicCAN (для одного из каналов)
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param filter (IN) Указатель на структуру #bFilter_t, где будет сохранено описание фильтра
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #BasicCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_B_GetInputFilter(HANDLE Handle, int Channel, bFilter_t *filter)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != filter);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == filter)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_B_GetInputFilter, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		filter->Mask = ioctlRes.Data.bFilter.Mask;
		filter->Code = ioctlRes.Data.bFilter.Code;
	}

	return ioctlRes.Status;
}
/* Функции для режима PeliCAN */
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_SetInputFilter(HANDLE Handle, int Channel, pFilter_t *filter)
 * \brief Настройка фильтра входных кадров для режима #PeliCAN
 *
 * Функция позволяет настроить фильтр входных кадров для режима #PeliCAN (для одного из каналов)
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param filter (IN) Указатель на структуру #pFilter_t с описанием фильтра
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 * \par Функционирование входного фильтра в режиме PeliCAN.
 * Фильтр входных пакетов может функционировать в одном из двух режимов.
 * В следующих таблицах приведено отображение битов полей Filter и Mask на биты входного кадра.
 *
 * Таблица, поясняющая функционирование входного фильтра при приеме стандартного кадра (режим Single, Mode = 1);
 * <TABLE>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[0]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[1]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[2]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[3]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD colspan=4>unused</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * </TR>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[0]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[1]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[2]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[3]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD colspan=4>unused</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * </TR>
 * <TR>
 * <TD colspan=32 align=center style='background:#D9D9D9'>Входной кадр</TD>
 * </TR>
 * <TR>
 * <TD>ID.28</TD> <TD>ID.27</TD> <TD>ID.26</TD> <TD>ID.25</TD> <TD>ID.24</TD> <TD>ID.23</TD> <TD>ID.22</TD> <TD>ID.21</TD>
 * <TD>ID.20</TD> <TD>ID.19</TD> <TD>ID.18</TD> <TD>RTR</TD> <TD colspan=4>&nbsp;</TD>
 * <TD>DB1.7</TD> <TD>DB1.6</TD> <TD>DB1.5</TD> <TD>DB1.4</TD> <TD>DB1.3</TD> <TD>DB1.2</TD> <TD>DB1.1</TD> <TD>DB1.0</TD>
 * <TD>DB2.7</TD> <TD>DB2.6</TD> <TD>DB2.5</TD> <TD>DB2.4</TD> <TD>DB2.3</TD> <TD>DB2.2</TD> <TD>DB2.1</TD> <TD>DB2.0</TD>
 * </TR>
 * </TABLE>
 * Таблица, поясняющая функционирование входного фильтра при приеме расширенного кадра (режим Single, Mode = 1);
 * <TABLE>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[0]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[1]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[2]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[3]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD colspan=2>unused</TD>
 * </TR>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[0]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[1]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[2]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[3]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD colspan=2>unused</TD>
 * </TR>
 * <TR>
 * <TD colspan=32 align=center style='background:#D9D9D9'>Входной кадр</TD>
 * </TR>
 * <TR>
 * <TD>ID.28</TD> <TD>ID.27</TD> <TD>ID.26</TD> <TD>ID.25</TD> <TD>ID.24</TD> <TD>ID.23</TD> <TD>ID.22</TD> <TD>ID.21</TD>
 * <TD>ID.20</TD> <TD>ID.19</TD> <TD>ID.18</TD> <TD>ID.17</TD> <TD>ID.16</TD> <TD>ID.15</TD> <TD>ID.14</TD> <TD>ID.13</TD>
 * <TD>ID.12</TD> <TD>ID.11</TD> <TD>ID.10</TD> <TD>ID.9</TD> <TD>ID.8</TD> <TD>ID.7</TD> <TD>ID.6</TD> <TD>ID.5</TD>
 * <TD>ID.4</TD> <TD>ID.3</TD> <TD>ID.2</TD> <TD>ID.1</TD> <TD>ID.0</TD> <TD>RTR</TD> <TD colspan=2>&nbsp;</TD>
 * </TR>
 * </TABLE>
 * Таблица, поясняющая функционирование входного фильтра при приеме стандартного кадра (режим Dual, Mode = 0);
 * <TABLE>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[0]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[1]</TD> <TD colspan=4 align=center style='background:#D9D9D9'>Бит Filter[3]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * </TR>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[0]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[1]</TD> <TD colspan=4 align=center style='background:#D9D9D9'>Бит Mask[3]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * </TR>
 * <TR>
 * <TD colspan=20 align=center style='background:#D9D9D9'>Входной кадр</TD>
 * </TR>
 * <TR>
 * <TD>ID.28</TD> <TD>ID.27</TD> <TD>ID.26</TD> <TD>ID.25</TD> <TD>ID.24</TD> <TD>ID.23</TD> <TD>ID.22</TD> <TD>ID.21</TD>
 * <TD>ID.20</TD> <TD>ID.19</TD> <TD>ID.18</TD> <TD>RTR</TD> <TD>DB1.7</TD> <TD>DB1.6</TD> <TD>DB1.5</TD> <TD>DB1.4</TD> <TD>DB1.3</TD> <TD>DB1.2</TD> <TD>DB1.1</TD> <TD>DB1.0</TD>
 * </TR>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[2]</TD> <TD colspan=4 align=center style='background:#D9D9D9'>Бит Mask[3]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD>
 * </TR>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[2]</TD> <TD colspan=4 align=center style='background:#D9D9D9'>Бит Filter[3]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD>
 * </TR>
 * </TABLE>
 * Таблица, поясняющая функционирование входного фильтра при приеме расширенного кадра (режим Dual, Mode = 0);
 * <TABLE>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[0]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[1]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * </TR>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[0]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[1]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * </TR>
 * <TR>
 * <TD colspan=20 align=center style='background:#D9D9D9'>Входной кадр</TD>
 * </TR>
 * <TR>
 * <TD>ID.28</TD> <TD>ID.27</TD> <TD>ID.26</TD> <TD>ID.25</TD> <TD>ID.24</TD> <TD>ID.23</TD> <TD>ID.22</TD> <TD>ID.21</TD>
 * <TD>ID.20</TD> <TD>ID.19</TD> <TD>ID.18</TD> <TD>ID.17</TD> <TD>ID.16</TD> <TD>ID.15</TD> <TD>ID.14</TD> <TD>ID.13</TD>
 * </TR>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[2]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Mask[3]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * </TR>
 * <TR>
 * <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[2]</TD> <TD colspan=8 align=center style='background:#D9D9D9'>Бит Filter[3]</TD>
 * </TR>
 * <TR>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * <TD>7</TD> <TD>6</TD> <TD>5</TD> <TD>4</TD> <TD>3</TD> <TD>2</TD> <TD>1</TD> <TD>0</TD>
 * </TR>
 * </TABLE>
 *
 * Входной фильтр функционирует следующим образом:
 * Биты кадра (приведенные в соответствующей таблице) сравниваются на идентичность (поразрядно) с идентификатором входного фильтра.
 * Если в соответствующем разряде маски стоит логическая 1, то биты кадра и идентификатора входного
 * фильтра в этом разряде не сравниваются и результат сравнения в данном разряде принимается равным логической 1.
 * Кадр проходит приемный фильтр, если результат сравнения во всех разрядах содержит логические 1.
 *
 * \note Для приема всех кадров необходимо установить маску равную FFh-FFh-FFh-FFh (значение идентификатора входного фильтра можно установить любое).
 */
EXPORT int __stdcall CAN200_P_SetInputFilter(HANDLE Handle, int Channel, pFilter_t *filter)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != filter);
	assert((0 == filter->Mode) || (1 == filter->Mode));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == filter)
	{
		return CAN200_ERR_PARM;
	}
	if ((0 != filter->Mode) && (1 != filter->Mode))
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.pFilter.Filter[0] = filter->Filter[0];
	ioctlReq.CommandData.pFilter.Filter[1] = filter->Filter[1];
	ioctlReq.CommandData.pFilter.Filter[2] = filter->Filter[2];
	ioctlReq.CommandData.pFilter.Filter[3] = filter->Filter[3];
	ioctlReq.CommandData.pFilter.Mask[0] = filter->Mask[0];
	ioctlReq.CommandData.pFilter.Mask[1] = filter->Mask[1];
	ioctlReq.CommandData.pFilter.Mask[2] = filter->Mask[2];
	ioctlReq.CommandData.pFilter.Mask[3] = filter->Mask[3];
	ioctlReq.CommandData.pFilter.Mode = filter->Mode;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_SetInputFilter, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_GetInputFilter(HANDLE Handle, int Channel, pFilter_t *filter)
 * \brief Получение фильтра входных кадров для режима #PeliCAN
 *
 * Функция позволяет получить текущие настройки фильтра входных кадров для режима #PeliCAN (для одного из каналов)
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param filter (IN) Указатель на структуру #pFilter_t, где будет сохранено описание фильтра
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_P_GetInputFilter(HANDLE Handle, int Channel, pFilter_t *filter)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != filter);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == filter)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_GetInputFilter, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		filter->Filter[0] = ioctlRes.Data.pFilter.Filter[0];
		filter->Filter[1] = ioctlRes.Data.pFilter.Filter[1];
		filter->Filter[2] = ioctlRes.Data.pFilter.Filter[2];
		filter->Filter[3] = ioctlRes.Data.pFilter.Filter[3];
		filter->Mask[0] = ioctlRes.Data.pFilter.Mask[0];
		filter->Mask[1] = ioctlRes.Data.pFilter.Mask[1];
		filter->Mask[2] = ioctlRes.Data.pFilter.Mask[2];
		filter->Mask[3] = ioctlRes.Data.pFilter.Mask[3];
		filter->Mode = ioctlRes.Data.pFilter.Mode;
	}

	return ioctlRes.Status;
}
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_SetRxErrorCounter(HANDLE Handle, int Channel, int Counter)
 * \brief Установка счетчика ошибок приема по шине CAN (только для режима #PeliCAN)
 *
 * Функция позволяет установить значение счетчика ошибок приема по шине CAN для одного из каналов
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Counter (IN) Значение счетчика ошибок
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_P_SetRxErrorCounter(HANDLE Handle, int Channel, int Counter)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(Counter <= 0xFF);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (Counter > 0xFF)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.Counter = Counter;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_SetRxErrorCounter, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_GetRxErrorCounter(HANDLE Handle, int Channel, int *Counter)
 * \brief Получение счетчика ошибок приема по шине CAN (только для режима #PeliCAN)
 *
 * Функция позволяет получить значение счетчика ошибок приема по шине CAN для одного из каналов
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Counter (IN) Указатель на переменную типа int, где будет сохранено значение счетчика ошибок
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_P_GetRxErrorCounter(HANDLE Handle, int Channel, int *Counter)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != Counter);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Counter)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_GetRxErrorCounter, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Counter = ioctlRes.Data.Counter;
	}
	return ioctlRes.Status;
}
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_SetTxErrorCounter(HANDLE Handle, int Channel, int Counter)
 * \brief Установка счетчика ошибок выдачи по шине CAN (только для режима #PeliCAN)
 *
 * Функция позволяет установить значение счетчика ошибок выдачи по шине CAN для одного из каналов
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Counter (IN) Значение счетчика ошибок
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_P_SetTxErrorCounter(HANDLE Handle, int Channel, int Counter)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(Counter <= 0xFF);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (Counter > 0xFF)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.Counter = Counter;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_SetTxErrorCounter, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_GetTxErrorCounter(HANDLE Handle, int Channel, int *Counter)
 * \brief Получение счетчика ошибок выдачи по шине CAN (только для режима #PeliCAN)
 *
 * Функция позволяет получить значение счетчика ошибок выдачи по шине CAN для одного из каналов
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Counter (IN) Указатель на переменную типа int, где будет сохранено значение счетчика ошибок
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_P_GetTxErrorCounter(HANDLE Handle, int Channel, int *Counter)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(0 != Counter);
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Counter)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_GetTxErrorCounter, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Counter = ioctlRes.Data.Counter;
	}
	return ioctlRes.Status;
}
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_SetErrorWarningLimit(HANDLE Handle, int Channel, int Limit)
 * \brief Установка верхнего предела счетчика ошибок CAN (только для режима #PeliCAN)
 *
 * Функция позволяет установить значение верхнего предела счетчика ошибок CAN для одного из каналов.
 * Если хоть один из счетчиков ошибок (при приеме или выдаче) превысит значение предела заданного этой функцией,
 * произойдет установка бита "Наличие ошибок" (см. #CAN200_GetStatus).
 * По умолчанию верхний предел счётчиков ошибок равен 96.
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Limit (IN) Значение верхнего предела счетчика ошибок
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_P_SetErrorWarningLimit(HANDLE Handle, int Channel, int Limit)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(Limit <= 0xFF);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (Limit > 0xFF)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.Counter = Limit;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_SetErrorWarningLimit, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_GetErrorWarningLimit(HANDLE Handle, int Channel, int *Limit)
 * \brief Получение верхнено предела счетчика ошибок CAN (только для режима #PeliCAN)
 *
 * Функция позволяет получить значение верхнего предела счетчика ошибок CAN для одного из каналов
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Limit (IN) Указатель на переменную типа int, где будет сохранено значение верхнего предела счетчика ошибок
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_P_GetErrorWarningLimit(HANDLE Handle, int Channel, int *Limit)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(0 != Limit);
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Limit)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_GetErrorWarningLimit, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Limit = ioctlRes.Data.Counter;
	}
	return ioctlRes.Status;
}
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_GetArbitrationLostCapture(HANDLE Handle, int Channel, int *Data)
 * \brief Получение номера бита, на котором был проигран арбитраж (только для режима #PeliCAN)
 *
 * Функция позволяет получить значение Получение номера бита, на котором был проигран арбитраж для одного из каналов
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Data (IN) Указатель на переменную типа int, где будет сохранено значение бита, на котором был проигран арбитраж
 * (используются младшие 8 бит, для расшифровки значения можно использовать структуру #CAN_arbitration_lost_capture_t)
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_P_GetArbitrationLostCapture(HANDLE Handle, int Channel, int *Data)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(0 != Data);
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Data)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_GetArbitrationLostCapture, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Data = ioctlRes.Data.Counter;
	}
	return ioctlRes.Status;
}
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_GetRxMessageCounter(HANDLE Handle, int Channel, int *Counter)
 * \brief Получение количества принятых (и не прочитанных) кадров в буфере приёма (только для режима #PeliCAN)
 *
 * Функция позволяет получить количество принятых (и не прочитанных) кадров в буфере приёма для одного из каналов
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Counter (IN) Указатель на переменную типа int, где будет сохранено значение количество принятых (и не прочитанных)
 * кадров в буфере приема
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_P_GetRxMessageCounter(HANDLE Handle, int Channel, int *Counter)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(0 != Counter);
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Counter)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_GetRxMessageCounter, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Counter = ioctlRes.Data.Counter;
	}
	return ioctlRes.Status;
}
/** \addtogroup PeliCAN
 * \{
 * \fn int CAN200_P_GetErrorCode(HANDLE Handle, int Channel, int *Code)
 * \brief Получение типа и местоположения ошибки при обмене по шине CAN (только для режима #PeliCAN)
 *
 * Функция позволяет получить тип и местоположение ошибки при обмене по шине CAN для одного из каналов
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Code (IN) Указатель на переменную типа int, где будет сохранен тип и местоположение ошибки при обмене по шине CAN
 * кадров в буфере приема (используются младшие 8 бит, для расшифровки значения можно использовать структуру #CAN_error_t)
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_MODE - режим работы канала не соответствует #PeliCAN
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_P_GetErrorCode(HANDLE Handle, int Channel, int *Code)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(0 != Code);
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Code)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_P_GetErrorCode, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Code = ioctlRes.Data.Error;
	}
	return ioctlRes.Status;
}
/**
 * \fn int CAN200_GetOverCounter(HANDLE Handle, int Channel, int *Counter)
 * \brief Получение текущего кол-ва кадров отбрашенных из-за нехватки места в буфере
 *
 * Функция позволяет получить текущий режим работы одного из каналов платы
 * \param Handle (IN) Хэндл платы (возвращенный функцией #CAN200_Open)
 * \param Channel (IN) Номер канала платы (#CAN_CHANNEL_1 или #CAN_CHANNEL_2)
 * \param Counter (OUT) Указатель на переменную типа int, где будет сохранено кол-во кадров отбрашенное из-за нехватки места
 * \return CAN200_OK - успешное завершение
 * \return CAN200_ERR_PARM - ошибка входных параметров
 * \return CAN200_ERR_SYS - системная ошибка (для определения причины используйте (GetLastError())
 */
EXPORT int __stdcall CAN200_GetOverCounter(HANDLE Handle, int Channel, int *Counter)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != Counter);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (0 == Counter)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_GetOverCounter, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Counter = ioctlRes.Data.OverCounter;
	}

	return ioctlRes.Status;
}

EXPORT void __stdcall CAN200_HardReset(HANDLE Handle, int Channel)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return;
	}

	ioctlReq.Channel = Channel;

	DeviceIoControl(Handle, IOCTL_CAN200PC_HardReset, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);

	return;
}
EXPORT UINT64 __stdcall CAN200_GetAPIVer()
{
	return ver;
}
//EXPORT void __stdcall CAN200_GetBuf(UCHAR *arr, int count_pack)
//{
//	int i = 0, j = 0, k = 0;
//	for (i = 0; i < count_pack; i++)
//		for (j = 0; j < 8; j++)
//			arr[k++] = (UCHAR)msgbuf[i].DataByte[j];
//}
EXPORT BYTE __stdcall CAN200_GetByte(int num)
{
	return outbuf[num];
}

//-----------------------------------------------------------------------------
//
// Функции для внутреннего использования.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
EXPORT int __stdcall CAN200_SetCANReg(HANDLE Handle, int Channel, int Port, int Data)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(Port <= 0xFF);
	assert(Data <= 0xFF);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (Port > 0xFF)
	{
		return CAN200_ERR_PARM;
	}
	if (Data > 0xFF)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.Data[0] = Port;
	ioctlReq.CommandData.Data[1] = Data;

	res = DeviceIoControl(Handle, IOCTL_CAN200PC_SetCANReg, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	return ioctlRes.Status;
}

//-----------------------------------------------------------------------------
EXPORT int __stdcall CAN200_GetCANReg(HANDLE Handle, int Channel, int Port, int *Data)
{
	IoctlReq_t ioctlReq;
	IoctlRes_t ioctlRes;
	DWORD      ret;
	BOOL       res;

	/* Корректность входных параметров */
	assert(INVALID_HANDLE_VALUE != Handle);
	assert((CAN_CHANNEL_1 == Channel) || (CAN_CHANNEL_2 == Channel));
	assert(0 != Data);
	assert(Port <= 0xFF);

	if (INVALID_HANDLE_VALUE == Handle)
	{
		return CAN200_ERR_PARM;
	}
	if ((CAN_CHANNEL_1 != Channel) && (CAN_CHANNEL_2 != Channel))
	{
		return CAN200_ERR_PARM;
	}
	if (Port > 0xFF)
	{
		return CAN200_ERR_PARM;
	}

	ioctlReq.Channel = Channel;
	ioctlReq.CommandData.Data[0] = Port;
	
	res = DeviceIoControl(Handle, IOCTL_CAN200PC_GetCANReg, &ioctlReq, sizeof(IoctlReq_t),
		&ioctlRes, sizeof(IoctlRes_t), &ret, NULL);
	if (FALSE == res)
	{
		return CAN200_ERR_SYS;
	}

	if (CAN200_OK == ioctlRes.Status)
	{
		*Data = ioctlRes.Data.Data;
	}

	return ioctlRes.Status;
}


typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);

//------------------------------------------------------------------------------------------------------------------

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

//-----------------------------------------------------------------------------
#if defined(_UNICODE) || defined(UNICODE)
static BOOL GetDevicePath(LPGUID InterfaceGuid, int number, PWCHAR DevicePath, size_t BufLen)
#else
static BOOL GetDevicePath(LPGUID InterfaceGuid, int number, PCHAR DevicePath, size_t BufLen)
#endif /* UNICODE */
{
	HDEVINFO HardwareDeviceInfo;
	SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
	PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData = NULL;
	ULONG Length, RequiredLength = 0;
	BOOL bResult;
	int count;
#if defined(_UNICODE) || defined(UNICODE)
	HRESULT hr;
#endif /* UNICODE */

	HardwareDeviceInfo = SetupDiGetClassDevs(
		InterfaceGuid,
		NULL,
		NULL,
		(DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
	if (INVALID_HANDLE_VALUE == HardwareDeviceInfo)
	{
		return FALSE;
	}

	DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	//
	// Определяем кол-во подключенных устройств.
	//
	count = 0;
	while (SetupDiEnumDeviceInterfaces(HardwareDeviceInfo,
		NULL,
		InterfaceGuid,
		count++,
		&DeviceInterfaceData));
	count--;
	if (number >= count)
	{
		return false;
	}

	//
	//  Получаем информацию о выбранном устройстве.
	//
	bResult = SetupDiEnumDeviceInterfaces(HardwareDeviceInfo,
		NULL,
		(LPGUID)InterfaceGuid,
		number, // Индекс устройства
		&DeviceInterfaceData);
	if (false == bResult)
	{
		SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
		return false;
	}
    
	//
	// Определяем размер для структуры DeviceInterfaceData
	//
	SetupDiGetDeviceInterfaceDetail(HardwareDeviceInfo,
		&DeviceInterfaceData,
		NULL,
		0,
		&RequiredLength,
		NULL);
	if (ERROR_INSUFFICIENT_BUFFER != GetLastError())
	{
		SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
		return false;
	}

	//
	// Выделяем память
	//
	DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(RequiredLength);
	if (NULL == DeviceInterfaceDetailData)
	{
		SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
		return false;
	}

	//
	// Инициализируем структуру и запрашиваем данные.
	//
	DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
	Length = RequiredLength;
	bResult = SetupDiGetDeviceInterfaceDetail(HardwareDeviceInfo,
		&DeviceInterfaceData,
		DeviceInterfaceDetailData,
		Length,
		&RequiredLength,
		NULL);
	if (false == bResult)
	{
		SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
		free(DeviceInterfaceDetailData);
		return false;
	}

	//
	//  Переписываем результат.
	//
#if defined(_UNICODE) || defined(UNICODE)
	hr = StringCchCopy((STRSAFE_LPWSTR)DevicePath,
		BufLen,
		DeviceInterfaceDetailData->DevicePath);
	if (FAILED(hr))
	{
		SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
		free(DeviceInterfaceDetailData);
		return FALSE;
	}
#else
	strcpy(DevicePath, DeviceInterfaceDetailData->DevicePath);
#endif /* UNICODE */

	free(DeviceInterfaceDetailData);
	SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);

	return TRUE;
}

//-----------------------------------------------------------------------------
static BOOL GetDeviceCount(LPGUID InterfaceGuid, int *number)
{
	HDEVINFO HardwareDeviceInfo;
	SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
	int count;

	HardwareDeviceInfo = SetupDiGetClassDevs(
		InterfaceGuid,
		NULL,
		NULL,
		(DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
	if (INVALID_HANDLE_VALUE == HardwareDeviceInfo)
	{
		return FALSE;
	}

	DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	//
	// Определяем кол-во подключенных устройств.
	//
	count = 0;
	while (SetupDiEnumDeviceInterfaces(HardwareDeviceInfo,
		NULL,
		InterfaceGuid,
		count++,
		&DeviceInterfaceData));

	*number = count - 1;

	return TRUE;
}
/** \endcond */
#pragma endregion

EXPORT int __stdcall CAN200_ClearBuf(HANDLE Handle, int Channel)
{
	int result = 0;
	int cc = 0;
	TEventData evd = { 0 };
	do
	{
		int result = CAN200_GetEventData(hMain, 1, &evd);
		if (evd.IntrID)
		{
			//			mbuf.push_back(evd.rxtxbuf);
			//			//					printf_s("TH Vector size = %d\r\n", mbuf.size());
			cc++;
		}
	} while (result == 0 && evd.IntrID == 1);
	return cc;
}

void t_recv()
{
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

	CAN200_ClearBuf(hMain, 1);
	result = CAN200_DefEvent(hMain, 1, recvev);
#ifdef DEBUG
	if (result != CAN200_OK)
	{
		ATLTRACE(_T("Не задано событие для элкус\r\n"));
	}
#endif

	result = CAN200_SetCommand(hMain, 1, 0x02);
	result = CAN200_SetCommand(hMain, 1, 0x04);
	result = CAN200_SetCommand(hMain, 1, 0x08);

	while (flag_thr)
	{
		switch (WaitForSingleObject(recvev, 1000))
		{
			case WAIT_OBJECT_0:
				do
				{
					int result = CAN200_GetEventData(hMain, 1, &evd);
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
	CAN200_ClearBuf(hMain, 1);
	ExitThread(0);
#ifdef DEBUG
	ATLTRACE(_T("Выход из потока 2...\r\n"));
#endif
	return;
}

EXPORT void __stdcall CAN200_Recv_Enable(HANDLE Handle, int Channel, int timeout)
{
	flag_thr = true;
	thr = std::thread(t_recv);
}

EXPORT void __stdcall CAN200_Recv_Disable()
{
	flag_thr = false;
	if (thr.joinable())
		thr.join();
//	thr = NULL;
//	TerminateThread(thr.native_handle(), 0);
//	CloseHandle(thr.native_handle());
	return;
}

EXPORT int __stdcall CAN200_Pop(pRX_TX_Buffer Buffer)
{
	mtx.lock();
	*Buffer = mbuf.front();
	mbuf.erase(mbuf.begin());
	mtx.unlock();
	return 0;
}
EXPORT int __stdcall CAN200_VecSize()
{
	mtx.lock();
	int ii = mbuf.size();
	mtx.unlock();
	return ii;
}

EXPORT int __stdcall CAN200_Recv(HANDLE Handle, int Channel, pRX_TX_Buffer Buffer, int timeout)
{
	TEventData evd = { 0 };
	TCAN_VPDData conf = { 0 };
	int result = 0;

	result = CAN200_SetCommand(Handle, Channel, 8);
	result = CAN200_SetCommand(Handle, Channel, 4);

	CAN200_HardReset(Handle, Channel);

	switch (WaitForSingleObject(hEvent, timeout))
	{
	case WAIT_OBJECT_0:
		ResetEvent(hEvent);
		CAN200_GetEventData(Handle, Channel, &evd);
		if (evd.IntrID)
		{
			mtx.lock();
			mbuf.push_back(evd.rxtxbuf);
			mtx.unlock();
			CopyMemory(Buffer, &(evd.rxtxbuf), sizeof(RX_TX_Buffer));
			return CAN200_OK;
		}
		break;

	case WAIT_TIMEOUT:
	default:
		//			CAN200_GetRxBuffer(Handle, Channel, Buffer);
		//			CAN200_GetEventData(Handle, Channel, &evd);
		return CAN200_ERR_BUSY; // Timeout
		break;
	}
}

EXPORT int __stdcall CAN200_RecvPack(HANDLE Handle, int Channel, int *count, int timeout)
{
	TEventData evd = { 0 };
	TCAN_VPDData conf = { 0 };
	int result = 0;
	int cnt = 0;
	int count2 = *count;
	RX_TX_Buffer bbb;
	int i = 0, j = 0, k = 0;
	//result = CAN200_SetCommand(Handle, Channel, 0x06);
	do
	{
		switch (WaitForSingleObject(hEvent, timeout))
		{
		case WAIT_OBJECT_0:
			ResetEvent(hEvent);
			do
			{
				int result = CAN200_GetEventData(Handle, Channel, &evd);
				if (evd.IntrID)
				{
					mtx.lock();
					mbuf.push_back(evd.rxtxbuf);
					mtx.unlock();
					msgbuf[cnt].sID = evd.rxtxbuf.sID;
					msgbuf[cnt].DLC = evd.rxtxbuf.DLC;
					for (size_t i = 0; i < 8; i++)
						msgbuf[cnt].DataByte[i] = evd.rxtxbuf.DataByte[i];
					cnt++;
				}
			} while (result == 0 && evd.IntrID == 1 && cnt < count2);
			//				result = CAN200_SetCommand(Handle, Channel, 0x06);
			break;
		case WAIT_TIMEOUT:
		default:
			//				CAN200_GetRxBuffer(Handle, Channel, &bbb);
			//for (int i1 = 0; i1 < 20000; i1++)
			//	CAN200_GetEventData(Handle, Channel, &evd);
			//for (int i2 = 0; i2 < cnt; i2++)
			//	for (j = 0; j < 8; j++)
			//		outbuf[k++] = (BYTE)msgbuf[i2].DataByte[j];
			//	*count = cnt;
			return CAN200_ERR_BUSY; // Timeout
			break;
		}
	} while (cnt < count2);
	*count = cnt;

	//for (i = 0; i < 100000; i++)
	//	outbuf[i] = 0;

	for (i = 0; i < cnt; i++)
	for (j = 0; j < 8; j++)
		outbuf[k++] = (BYTE)msgbuf[i].DataByte[j];

	//for (i = 0; i < 20000; i++)
	//	msgbuf[i] = { 0 };

	return CAN200_OK;

}

EXPORT int __stdcall CAN200_GetLastError(void)
{
	return GetLastError();
}
