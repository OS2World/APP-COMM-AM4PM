// Thomas Answering machine for PM

// File:          PMLOG.h
// Description:   Routines for debug printing

// History
// 930213 TO      Now it exists...


#define INCL_WIN
#define INCL_BASE
#include <os2.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#include "am4pm.h"

static HQUEUE hDebQueue, hRxSIOQueue;
static PID pidDeb, pidRxSIO;
static BOOL fRxSIO = FALSE;

void InitDebug(void)
{
   USHORT res;
         
   res=DosOpenQueue(&pidDeb, &hDebQueue, "\\queues\\vfedeb");
   fDebug = res==0;
}


void dprintf
(
   PCHAR szStr,
   ...
)
{
   va_list arg_marker;
   PVOID pSendBuff;
   USHORT res;

   if (!fDebug)
      return;

   res=DosAllocSharedMem(&pSendBuff, NULL, 200, PAG_COMMIT | PAG_READ | PAG_WRITE | OBJ_GIVEABLE);
   if (res)
   {
      fDebug=FALSE;
      return;
   }

   va_start(arg_marker, szStr);

   vsprintf(pSendBuff, szStr, arg_marker);

   va_end(arg_marker);

   res=DosGiveSharedMem(pSendBuff, pidDeb, PAG_READ | PAG_WRITE);
   if (res)
   {
      fDebug=FALSE;
      return;
   }

   res=DosFreeMem(pSendBuff);
   if (res)
   {
      fDebug=FALSE;
      return;
   }

   res=DosWriteQueue(hDebQueue, 0, 200, pSendBuff, 0);
   if (res)
   {
      fDebug=FALSE;
      return;
   }

}


USHORT ShowMessage
(
   USHORT usMsgNr,
   HWND hwnd,
   USHORT flStyle,
   USHORT idHelp,
   ...
)
{
   va_list arg_marker;
   ULONG ulLen;
   CHAR szStr[200], szStr2[200];

   if (idHelp==0)
      idHelp=usMsgNr + 10000;

   va_start(arg_marker, idHelp);

   DosGetMessage(NULL, 0, szStr, 200, usMsgNr, szMsgFile, &ulLen);
   if (szStr[ulLen-1]=='\n')
      szStr[ulLen-2]='\0';
   else
      szStr[ulLen]='\0';

   vsprintf(szStr2, szStr, arg_marker);

   va_end(arg_marker);

   return WinMessageBox(HWND_DESKTOP, hwnd, szStr2, szAppName, idHelp, flStyle | MB_MOVEABLE);
}


static void PrintString
(
   PCHAR szStr,
   ULONG ulLen
)
{
   HFILE hFile;
   USHORT res;
   ULONG ulPos, ulAction;
   static CHAR szFileError[]="Error opening logfile!!\r\n";

   DosPutMessage(2, ulLen, szStr);
   res=DosOpen("am4pm.log", &hFile, &ulAction, 0l, FILE_NORMAL, FILE_OPEN | FILE_CREATE, OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYNONE, 0l);
   if (res)
   {
      DosPutMessage(2, sizeof szFileError, szFileError);
   }
   else
   {
      DosChgFilePtr(hFile, 0l, FILE_END, &ulPos);
      DosPutMessage(hFile, ulLen, szStr);
      DosClose(hFile);
   }
}


static void PrintMessage
(
   USHORT usMsgNr,
   PCHAR szMsgFileName,
   va_list arg_marker
)
{
   PCHAR ppBuff[10];
   USHORT usNr=0;
   CHAR szStr[200];
   ULONG ulLen;

   while (ppBuff[usNr]=va_arg(arg_marker, PCHAR))
      usNr++;

   DosGetMessage(ppBuff, usNr, szStr, 200, usMsgNr, szMsgFileName, &ulLen);
   PrintString(szStr, ulLen);
   if (szStr[ulLen-1]=='\n')
      szStr[ulLen-2]='\0';
   else
      szStr[ulLen]='\0';
// WinPostMsg(hwndClient, WM_NEWMSG, MPFROMP(strdup(szStr)), 0l);
}


static void PrintNumMessage
(
   USHORT usMsgNr,
   PCHAR szMsgFileName,
   USHORT usNum,
   va_list arg_marker
)
{
   PCHAR ppBuff[10];
   USHORT usNr=1;
   CHAR szStr[200], szNum[10];
   ULONG ulLen;

   ppBuff[0]=_itoa(usNum, szNum, 10);
   while (ppBuff[usNr]=va_arg(arg_marker, PCHAR))
      usNr++;

   DosGetMessage(ppBuff, usNr, szStr, 200, usMsgNr, szMsgFileName, &ulLen);
   PrintString(szStr, ulLen);
   if (szStr[ulLen-1]=='\n')
      szStr[ulLen-2]='\0';
   else
      szStr[ulLen]='\0';
// WinPostMsg(hwndClient, WM_NEWMSG, MPFROMP(strdup(szStr)), 0l);
}



static void LogPrintTime
(
   void
)
{
   long ltime;
   CHAR szMsg[200], * szTime;

   strcpy(szMsg, "\r\n** ");
   time(&ltime);
   szTime=ctime(&ltime);
   strcat(szMsg, szTime);
   szMsg[strlen(szMsg)-1]='\0';
   strcat(szMsg, " **\r\n");
   PrintString(szMsg, strlen(szMsg));
// WinPostMsg(hwndClient, WM_NEWMSG, MPFROMP(strdup("")), 0l);
   strcpy(szMsg, szTime);
   szMsg[strlen(szMsg)-1]='\0';
// WinPostMsg(hwndClient, WM_NEWMSG, MPFROMP(strdup(szMsg)), 0l);
}


void LogMessage
(
   USHORT usMsgNr,
   ...
)
{
   va_list arg_marker;

   va_start(arg_marker, usMsgNr);

   LogPrintTime();
   PrintMessage(usMsgNr, szMsgFile, arg_marker);
}

#if 0
Not used for time being
static void LogPrintLine
(
   USHORT usMsgNr,
   ...
)
{
   va_list arg_marker;

   va_start(arg_marker, usMsgNr);

   PrintMessage(usMsgNr, szMsgFile, arg_marker);
}
#endif


void LogNumMessage
(
   USHORT usMsgNr,
   USHORT usNum,
   ...
)
{
   va_list arg_marker;

   va_start(arg_marker, usNum);

   LogPrintTime();
   PrintNumMessage(usMsgNr, szMsgFile, usNum, arg_marker);
}


void LogDosMessage
(
   USHORT usDosErr,
   USHORT usMsgNr,
   ...
)
{
   va_list arg_marker;

   va_start(arg_marker, usMsgNr);

   LogPrintTime();
   PrintMessage(usMsgNr, szMsgFile, arg_marker);
   PrintMessage(0, szMsgFile, arg_marker);
   PrintMessage(usDosErr, "oso001.msg", arg_marker);
}



static void InitRxSIO(void)
{
   USHORT res;
   
   res=DosOpenQueue(&pidRxSIO, &hRxSIOQueue, "\\queues\\rxsio");
   fRxSIO = res==0;
}


void PrintRxSIO
(
   PCHAR szStr
)
{
   PVOID pSendBuff;
   USHORT res, usLen;
   static CHAR szCR[] = "\n";

   if (!fRxSIO)
      InitRxSIO();

   if (!fRxSIO)
      return;

   usLen = strlen(szStr);

   res=DosAllocSharedMem(&pSendBuff, NULL, usLen+3, PAG_COMMIT | PAG_READ | PAG_WRITE | OBJ_GIVEABLE);
   if (res)
   {
      fRxSIO=FALSE;
      return;
   }

   memcpy(pSendBuff, szStr, usLen);
   memcpy(((PCHAR)pSendBuff)+usLen, szCR, sizeof szCR);

   res=DosGiveSharedMem(pSendBuff, pidRxSIO, PAG_READ | PAG_WRITE);
   if (res)
   {
      fRxSIO=FALSE;
      return;
   }

   res=DosFreeMem(pSendBuff);
   if (res)
   {
      fRxSIO=FALSE;
      return;
   }

   res=DosWriteQueue(hRxSIOQueue, 0, usLen+3, pSendBuff, 0);
   if (res)
   {
      fRxSIO=FALSE;
      return;
   }

}
