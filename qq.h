#ifdef __cplusplus
extern "C" {
#endif
	void WINAPI InitDll(TDllCallback CallBack);
	void WINAPI CloseDll(void);
	void WINAPI InitCom(int Id, int Com);
	void WINAPI CloseCom(int Id);
#ifdef __cplusplus
}
#endif