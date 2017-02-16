// Приведенный ниже блок ifdef - это стандартный метод создания макросов, упрощающий процедуру 
// экспорта из библиотек DLL. Все файлы данной DLL скомпилированы с использованием символа MARCAN_EXPORTS,
// в командной строке. Этот символ не должен быть определен в каком-либо проекте
// использующем данную DLL. Благодаря этому любой другой проект, чьи исходные файлы включают данный файл, видит 
// функции MARCAN_API как импортированные из DLL, тогда как данная DLL видит символы,
// определяемые данным макросом, как экспортированные.

#ifndef __API_H__
#define __API_H__

#define EXPORT extern "C" __declspec(dllexport)

#include "chai.h"

//typedef struct {
//	UINT32 id;
//	BYTE data[8];
//	BYTE len;
//	UINT16 flags;            /* bit 0 - RTR, 2 - EFF */
//	UINT32 ts;
//} RX_TX_Buffer, *pRX_TX_Buffer;

#define MarCAN_OK			0	///< Операция завершена успешно
#define MarCAN_ERR_PARM		-1	///< Ошибка входных параметров
#define MarCAN_ERR_SYS		-2	///< Системная ошибка
#define MarCAN_ERR_MODE		-3	///< Режим работы не соответствует требуемому
#define MarCAN_ERR_BUSY		-4	///< Буфер выдачи занят

EXPORT INT16 __stdcall MarCAN_Open(UINT16 speed);
EXPORT INT16 __stdcall MarCAN_Close(void);
EXPORT INT16 __stdcall MarCAN_SetCANSpeed(UINT16 speed);
EXPORT INT16 __stdcall MarCAN_ClearRX(void);
EXPORT INT16 __stdcall MarCAN_GetStatus(chipstat_t *Status);
EXPORT INT16 __stdcall MarCAN_Write(pRX_TX_Buffer Buffer);
EXPORT INT16 __stdcall MarCAN_GetErrorCounter(canerrs_t *Counter);
EXPORT INT16 __stdcall MarCAN_HardReset();
EXPORT UINT64 __stdcall MarCAN_GetAPIVer(void);
EXPORT BYTE __stdcall MarCAN_GetByte(int num);
EXPORT void __stdcall MarCAN_Recv_Enable(void);
EXPORT void __stdcall MarCAN_Recv_Disable(void);
EXPORT INT16 __stdcall MarCAN_Pop(pRX_TX_Buffer Buffer);
EXPORT INT16 __stdcall MarCAN_VecSize(void);
EXPORT INT16 __stdcall MarCAN_BoardInfo(canboard_t *binfo);
#endif