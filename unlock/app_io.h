#pragma once
#include "port.h"
EFI_STATUS GuIoOpen(EFI_HANDLE Image,EFI_SYSTEM_TABLE *Table);
EFI_STATUS GuIoClose(VOID);
EFI_STATUS GuLoadAssets(VOID);
VOID GuReleaseAssets(VOID);
EFI_STATUS GuFileDumpInRun(const CHAR16 *Name,const char *Label,VOID *Data,UINTN Bytes);
BOOLEAN GuLogReady(VOID);
VOID GuConsole(const char *Text);
