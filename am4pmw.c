// Thomas Answering machine for PM

// File:          AM4PMW.c
// Description:   Send data to modem

// History
// 930206 TO      Now it exists...
// 930430 TO      Change sub command interface to subroutines
// 940226 TO      Adaptions to Rockwell modems

#define INCL_BASE
#define INCL_WINSHELLDATA
#include <os2.h>

#define INCL_REXXSAA
#include <rexxsaa.h>                   /* needed for RexxStart()     */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <process.h>
#include <stddef.h>
#include <malloc.h>
#include <ctype.h>
#include <io.h>
#include <conio.h>
#include <errno.h>

#include "am4pm.h"

#define RQMAX           100      // Maximum number of records in event queue
#define RQLOW           90       // Records in queue when ok to continue

typedef struct _QUEUERECORD
{
   PVOID pData;
   ULONG ulLen, ulMsg;
   struct _QUEUERECORD * pNext;
} QUEUERECORD;

typedef QUEUERECORD * PQUEUERECORD;

// Controll of the receiving thread
HEV semStopRead;
HEV semStopedReading;
HEV semGoOnRead;
HEV semWordInBuff;


HMTX semCom;   // Requested when the COM port is open

// Variables for the internal que
static PQUEUERECORD pFirstQR=NULL;
static PQUEUERECORD pLastQR=NULL;
static USHORT usQueueLen=0;
static HEV semQueueEmpty;
static HEV semQueueFull;
static CHAR szSemCom[]="\\SEM32\\AM4PM\\COM";

static CHAR szExitName[]="AM4PMExit";

HFILE hRec;
HFILE hCom=0l;
volatile USHORT usGlobalState=GS_START;
volatile ULONG ulRecTime;
volatile BOOL bDLEConv;
static int thidRec;     // Thread id for the receiving thread

volatile CHAR * pchSendBuff;
volatile ULONG ulSendBuffLen;
volatile CHAR szActiveDLECodes[DLECODES] = {'0'};

static const CHAR achZyXKey[]={'Z','y','X','E','L',2};
static const CHAR achAM4PMKey[]={'A','M','4','P','M',2};
static CHAR szCrntMFile[15];

static SHORT CallRexx
(
   PCHAR pszFile,
   ...
)
{

   RXSTRING arg[5];                   /* argument string for REXX  */
   RXSTRING rexxretval;               /* return value from REXX    */
   APIRET   rc;                        /* return code from REXX     */
   SHORT    sRexxRc = 0;               /* return code from function */
   va_list  arg_marker;
   ULONG    ulNr;                      /* Number of parameters */
   static RXSYSEXIT MyRxExit[]=
   {
      {szExitName, RXINI},
      {szExitName, RXTER},
      {szExitName, RXSIO},
      {NULL, RXENDLST}
   };

   rexxretval.strlength = 0L;          /* initialize return to empty*/

   va_start(arg_marker, pszFile);

   for (ulNr=0; ulNr< sizeof arg / sizeof arg[0]; ulNr++)
   {
      arg[ulNr].strptr=va_arg(arg_marker, PCHAR);
      if (arg[ulNr].strptr == NULL)
         break;
      arg[ulNr].strlength=strlen(arg[ulNr].strptr);
   }

   if (fDebug)
      dprintf("Calling REXX '%s(%u)'\n", pszFile, ulNr);
      
   rc=RexxStart(ulNr, arg, pszFile, 0, "CMD", RXSUBROUTINE, MyRxExit, &sRexxRc, &rexxretval);
   DosFreeMem(rexxretval.strptr);          /* Release storage       */
   if (rc)
   {
      if (fDebug)
         dprintf("Error %ul calling REXX\n", rc);
      LogNumMessage(6, rc, pszFile, NULL);
      return 1;
   }

   if (fDebug)
      dprintf("REXX function returned %i\n", sRexxRc);
   return sRexxRc;
}


static ULONG WaitQueue
(
   ULONG ulMsgMask,
   PPVOID ppData,
   PULONG pulLen,
   ULONG ulTO
)
{
   PQUEUERECORD pQR, pPrevQR;
   ULONG ulCount, ulMsg;
   USHORT res;

   for (;;)
   {
      if (usQueueLen)
      {
         DosEnterCritSec();
         for (pQR=pFirstQR, pPrevQR=NULL; pQR != NULL; pPrevQR=pQR, pQR=pQR->pNext)
         {
            if (pQR->ulMsg & ulMsgMask)
            {
               if (pPrevQR==NULL)
                  pFirstQR=pQR->pNext;
               else
                  pPrevQR->pNext=pQR->pNext;
               if (pQR->pNext==NULL)
                  pLastQR=pPrevQR;
                  
               usQueueLen--;
                  
               DosExitCritSec();
               if (usQueueLen==RQLOW)
                  DosPostEventSem(semQueueFull);
               *pulLen=pQR->ulLen;
               *ppData=pQR->pData;
               ulMsg=pQR->ulMsg;
               free(pQR);

               return ulMsg;
            }
         }
         DosExitCritSec();
      }

      if (ulTO==SEM_IMMEDIATE_RETURN)
         return 0;
      res=DosWaitEventSem(semQueueEmpty, ulTO);
      if (res)
         return 0l;
      DosResetEventSem(semQueueEmpty, &ulCount);
   }
   return 0l;
}


static void ClearQueue
(
   ULONG ulMsgMask,
   ULONG ulTO
)
{
   ULONG ulMsg, ulLen;
   PVOID pData;

   for (;;)
   {
      ulMsg = WaitQueue(ulMsgMask, &pData, &ulLen, ulTO);
      if (ulMsg == 0l)
         break;
      free(pData);
   }
}


void QueueData
(
   ULONG ulMsg,
   PVOID pData,
   ULONG ulLen
)
{
   PQUEUERECORD pQR;
   ULONG ulCount;

   pQR=malloc(sizeof(QUEUERECORD));
   if (pQR==NULL)
   {
      LogMessage(1, NULL); // Error allocation memory
      return;
   }
   pQR->ulMsg=ulMsg;
   pQR->ulLen=ulLen;
   pQR->pData=malloc(ulLen | 1l);
   if (pQR->pData==NULL)
   {
      LogMessage(1, NULL); // Error allocating memory
      return;
   }
   pQR->pNext=NULL;
   if (ulLen)
      memcpy(pQR->pData, pData, ulLen);

   DosEnterCritSec();
   if (usQueueLen==RQMAX)
   {
      DosExitCritSec();
      DosWaitEventSem(semQueueFull, SEM_INDEFINITE_WAIT);
      DosResetEventSem(semQueueFull, &ulCount);
      DosEnterCritSec();
   }

   if (usQueueLen)
      pLastQR->pNext=pQR;
   else
      pFirstQR=pQR;

   pLastQR=pQR;
   usQueueLen++;
   DosExitCritSec();

   DosPostEventSem(semQueueEmpty);
}


USHORT SendIt(void * str, USHORT antal)
{
   ULONG n;
   APIRET res;

   res=DosWrite(hCom, str, antal, &n);
   if (res)
   {
      LogDosMessage(res, 5, NULL);  // Error sending
      return 1;
   }
   return 0;
}


static USHORT SendHayes    // Sending to the port and waits for an answer.
(
   PCHAR szStr
)
{
   USHORT i, res;

   for (i=0; szStr[i]; i++)
   {
      res=SendIt(szStr+i, 1);
      if (res)
         return res;
   }
   return 0;
}


static USHORT SendHayesResp // Sending to the port and waits for an answer.
(
   PCHAR szStr,
   PCHAR szResp,
   ULONG ulTO
)
{
   USHORT res;
   ULONG ulLen, ulMsg;
   PVOID pData;

   ClearQueue(IM_STRFROMDCE, SEM_IMMEDIATE_RETURN);

   if (fDebug)
      dprintf("Sending: %s\nWaiting for '%s'.\n", szStr, szResp);

   res=SendIt(szStr, strlen(szStr));
   if (res)
      return res;

   for (;;)
   {
      ulMsg=WaitQueue(IM_STRFROMDCE, &pData, &ulLen, ulTO);
      if (ulMsg != IM_STRFROMDCE)
      {
         if (fDebug)
            dprintf("Timeout waiting for '%s'\n", szResp);
         LogMessage(20, szResp, szStr, NULL); // Timeout waiting for AT command
         return 2;
      }

      if (strcmp(pData, szResp) == 0)
         break;

      if (fDebug)
         dprintf("Waiting for '%s' but got '%s'\n", szResp, pData);

      free(pData);
   }

   free(pData);
   return 0;
}


static USHORT SendHayes2 // Sending to the port and waits for an answer.
(
   PCHAR szStr,   // String with command{response}..
   ULONG ulTO,
   USHORT usRetries
)
{
   USHORT i, res, pos;
   CHAR szCmd[MAXHAYESMSG+1], szResp[MAXHAYESMSG];

   for (pos=0; szStr[pos] != '\0'; pos++)
   {
      for (i=0; szStr[pos] != '\0' && szStr[pos] != '{' && i < sizeof szCmd; i++, pos++)
         szCmd[i] = szStr[pos];

      if (i >= sizeof szCmd || szStr[pos] != '{')
      {
         if (fDebug)
            dprintf("Invalid response string in '%s'\n", szStr);
         return 3;
      }

      szCmd[i] = '\r';
      szCmd[i+1] = '\0';

      pos++;
      for (i=0; szStr[pos] != '\0' && szStr[pos] != '}' && i < sizeof szResp; i++, pos++)
         szResp[i] = szStr[pos];

      if (i >= sizeof szResp || szStr[pos] != '}')
      {
         if (fDebug)
            dprintf("Invalid response string in '%s'\n", szStr);
         return 3;
      }

      szResp[i] = '\0';

      for (i=0; i<usRetries; i++)
      {
         res = SendHayesResp(szCmd, szResp, ulTO);
         if (res == 0)
            break;
      }

      if (res)
         return res;
   }

   return 0;
}


static void SetComTOZero(void)
{
   DCBTYPE DCB;
   USHORT res;
   char szStr[10];
   ULONG ulDLen, ulPLen;

   ulPLen=0;
   ulDLen=sizeof DCB;
   res=DosDevIOCtl(hCom, 1, 0x73, NULL, 0, &ulPLen, &DCB, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "read mode", NULL); //Fel vid IOCtl
      return;
   }

   DCB.usWTime=0;
   DCB.usRTime=0;

   ulDLen=0;
   ulPLen=sizeof DCB;
   res=DosDevIOCtl(hCom, 1, 0x53, &DCB, ulPLen, &ulPLen, NULL, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "set mode", NULL); //Fel vid IOCtl
      return;
   }
};


static void SaveRestComMode(BOOL fSave)
{
   static DCBTYPE DCB;
   USHORT res;
   static UCHAR achLineParams[4];
   char szStr[10];
   ULONG ulDLen, ulPLen;

   if (fSave)
   {
      if (fDebug)
         dprintf("Saving COM parameters\n");

      ulPLen=0;
      ulDLen=sizeof DCB;
      res=DosDevIOCtl(hCom, 1, 0x73, NULL, 0, &ulPLen, &DCB, ulDLen, &ulDLen);
      if (res)
      {
         LogDosMessage(res, 50, _itoa(res, szStr, 10), "read mode", NULL); //Fel vid IOCtl
         exit(1);
      }

      ulPLen=0;
      ulDLen=sizeof achLineParams;
      res=DosDevIOCtl(hCom, 1, 0x62, NULL, 0, &ulPLen, &achLineParams, ulDLen, &ulDLen);
      if (res)
      {
         LogDosMessage(res, 50, _itoa(res, szStr, 10), "line char", NULL); //Fel vid IOCtl
         exit(1);
      }
      return;
   }
   

   if (fDebug)
      dprintf("Restoring COM parameters\n");

   ulDLen=0;
   ulPLen=sizeof DCB;
   res=DosDevIOCtl(hCom, 1, 0x53, &DCB, ulPLen, &ulPLen, NULL, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "set mode", NULL); //Fel vid IOCtl
      exit(1);
   }

   ulDLen=0;
   ulPLen=sizeof achLineParams-1;   // Not 'Transmitting break'
   res=DosDevIOCtl(hCom, 1, 0x42, achLineParams, ulPLen, &ulPLen, NULL, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "line char", NULL); //Fel vid IOCtl
      exit(1);
   }
};


static USHORT OpenCom(void)
{
   USHORT res;
   ULONG action;

   DosRequestMutexSem(semCom, SEM_INDEFINITE_WAIT);

// res=DosOpen(ParamBlock.szCom,&hCom,&action,0l,0,FILE_OPEN,OPEN_ACCESS_READWRITE | OPEN_SHARE_DENYREADWRITE | OPEN_FLAGS_WRITE_THROUGH | OPEN_FLAGS_FAIL_ON_ERROR, 0l);
   res=DosOpen(ParamBlock.szCom,&hCom,&action,0l,0,FILE_OPEN,OPEN_ACCESS_READWRITE | OPEN_SHARE_DENYREADWRITE | OPEN_FLAGS_FAIL_ON_ERROR, 0l);
   if (res)
   {
//    LogDosMessage(res, 14, ParamBlock.szCom, NULL); // Error opening file
      DosReleaseMutexSem(semCom);
      return res;
   }

   return 0;
}


static void SetComMode(void)
{
   DCBTYPE DCB;
   USHORT res;
   static UCHAR achLineParams[3]={8,0,0}; // 8,N,1
   char szStr[10];
   ULONG ulDLen, ulPLen;

   ulPLen=0;
   ulDLen=sizeof DCB;
   res=DosDevIOCtl(hCom, 1, 0x73, NULL, 0, &ulPLen, &DCB, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "read mode", NULL); //Fel vid IOCtl
      exit(1);
   }

   DCB.usWTime=SENDTO*100;
   DCB.usRTime=READTO*100;

   DCB.chFlag1 &= 0x84;
   DCB.chFlag1 |= 0x01;

// DCB.chFlag2 = 0x85;  // XON

// DCB.chFlag3 &= 0xf8;
// DCB.chFlag3 |= 0x04;


   // New test
   DCB.chFlag2 = 0x80;
   DCB.chFlag3 = 0xD4;

   ulDLen=0;
   ulPLen=sizeof DCB;
   res=DosDevIOCtl(hCom, 1, 0x53, &DCB, ulPLen, &ulPLen, NULL, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "set mode", NULL); //Fel vid IOCtl
      exit(1);
   }

   ulDLen=0;
   ulPLen=sizeof ParamBlock.ulBaud;
   res=DosDevIOCtl(hCom, 1, 0x41, &(ParamBlock.ulBaud), ulPLen, &ulPLen, NULL, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "baud", NULL); //Fel vid IOCtl
      exit(1);
   }

   ulDLen=0;
   ulPLen=sizeof achLineParams;
   res=DosDevIOCtl(hCom, 1, 0x42, achLineParams, ulPLen, &ulPLen, NULL, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "line char", NULL); //Fel vid IOCtl
      exit(1);
   }
};


static void ShutDown(void)
{
   usGlobalState=GS_ENDING;
   DosPostEventSem(semGoOnRead);
}


static USHORT HangUp
(
   void
)
{
   return SendHayes2(ParamBlock.szATHangUp, MAXWAITDCE, 3);
}


static HFILE CreateMFile(void)
{
   USHORT res;
   HFILE hF;
   ULONG ulAction;

   for (;;)
   {
      ParamBlock.ulLastFileID++;
      sprintf(szCrntMFile, "M%lu.%s", ParamBlock.ulLastFileID, ParamBlock.szFileExtension);
      if (access(szCrntMFile, 0)==-1)
      {
         if (errno == ENOENT)
            break;
         return 0;
      }
      if (fDebug)
         dprintf("File '%s' already exists\n", szCrntMFile);

   }
   if (fDebug)
      dprintf("Creating file '%s'\n", szCrntMFile);
   res=DosOpen(szCrntMFile, &hF, &ulAction, 0l, FILE_NORMAL, OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_FAIL_IF_EXISTS, OPEN_FLAGS_FAIL_ON_ERROR | OPEN_FLAGS_SEQUENTIAL | OPEN_SHARE_DENYREADWRITE | OPEN_ACCESS_WRITEONLY, 0l);
   if (res)
   {
      LogNumMessage(2, res, szCrntMFile, NULL); // Error opening file
      return 0;
   }

   return hF;
}


static void SetComXONXOFF(BOOL fIn, BOOL fOut)
{
   DCBTYPE DCB;
   USHORT res;
   char szStr[10];
   ULONG ulDLen, ulPLen;

   ulPLen=0;
   ulDLen=sizeof DCB;
   res=DosDevIOCtl(hCom, 1, 0x73, NULL, 0, &ulPLen, &DCB, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "read mode", NULL); //Fel vid IOCtl
      return;
   }

   if (fIn)
      DCB.chFlag2 |= 1; // XON/XOFF on transmit
   else
      DCB.chFlag2 &= ~1; // No XON/XOFF on transmit

   if (fOut)
      DCB.chFlag2 |= 2; // XON/XOFF on receive
   else
      DCB.chFlag2 &= ~2; // No XON/XOFF on receive

   ulDLen=0;
   ulPLen=sizeof DCB;
   res=DosDevIOCtl(hCom, 1, 0x53, &DCB, ulPLen, &ulPLen, NULL, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "set mode", NULL); //Fel vid IOCtl
      return;
   }
};

#if 0
static void PrintComStatus(void)
{
   BYTE bStatus;
   USHORT res;
   char szStr[10];
   ULONG ulDLen, ulPLen;

   ulPLen=0;
   ulDLen=sizeof bStatus;
   res=DosDevIOCtl(hCom, 1, 0x64, NULL, 0, &ulPLen, &bStatus, ulDLen, &ulDLen);
   if (res)
   {
      LogDosMessage(res, 50, _itoa(res, szStr, 10), "get status", NULL); //Fel vid IOCtl
      return;
   }

   if (fDebug)
      dprintf("Com status: %x\n", bStatus);
};
#endif


static void PlayFile
(
   PRXSTRING prxsRet,
   PCHAR pszFile
)
{
   CHAR chRet=' ', szVSMCmd[MAXHAYESMSG];
   ULONG ulMsg, ulLen, ulAction, ulSLen;
   PVOID pData;
   BOOL fFirst, bOtherEvent=FALSE, bDoConvDLE = TRUE;
   HFILE fh;
   ZYXHEAD zh;
   USHORT res, i, usType;
   static CHAR achFileB[SENDBUFFLEN], achOutB[SENDBUFFLEN*2];

   prxsRet->strlength=1;

   // Make sure the input queue is empty
   ClearQueue(IM_STRFROMDCE | IM_DLEFROMDCE | IM_STOP, SEM_IMMEDIATE_RETURN);

   res=DosOpen(pszFile, &fh, &ulAction, 0l, FILE_NORMAL, OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS, OPEN_FLAGS_FAIL_ON_ERROR | OPEN_FLAGS_SEQUENTIAL | OPEN_SHARE_DENYWRITE | OPEN_ACCESS_READONLY, 0l);

   if (res)
   {
      LogNumMessage(2, res, pszFile, NULL); // Error opening file
      prxsRet->strptr[0]='!';
      return;
   }

   // Determine if it is a ZyXEL file. Otherwise just send it as it is.
   DosRead(fh, &zh, sizeof zh, &ulLen);
   if (ulLen==sizeof zh && memcmp(achZyXKey, &zh, sizeof achZyXKey)==0)
   {
      if (zh.usType <= 2)
      {
         if (fDebug)
            dprintf("Old ZyXEL file detected.\n");
         usType = zh.usType+1;
      }
      else
      {
         if (fDebug)
            dprintf("New ZyXEL file detected.\n");
         usType = zh.usType-2;
         bDoConvDLE = FALSE;
      }
   }
   else
   {
      if (ulLen==sizeof zh && memcmp(achAM4PMKey, &zh, sizeof achAM4PMKey)==0)
      {
         if (fDebug)
            dprintf("A generic AM4PM file detected\n");
         usType = zh.usType+1;
         bDoConvDLE = FALSE;
      }
      else
      {
         if (fDebug)
            dprintf("An unkown file detected\n");
         DosSetFilePtr(fh, 0l, FILE_BEGIN, &ulLen);
         usType=1;  // CELP
      }
   }

   sprintf(szVSMCmd, ParamBlock.szATVoiceMode, usType);
   res=SendHayes2(szVSMCmd, 3000, 3);
   if (res)
   {
      if (fDebug)
         dprintf("Send error %u\n", res);
      DosClose(fh);
      prxsRet->strptr[0]='!';
      return;
   }

   SetComXONXOFF(TRUE, FALSE);

   for (fFirst=TRUE;;)
   {
      res=DosRead(fh, achFileB, SENDBUFFLEN, &ulLen);
      if (fDebug && res)
         dprintf("Error %u reading\n", res);
      if (ulLen == 0l)
      {
         SendHayes2(ParamBlock.szATVoiceEndTX, 3000l, 3);
         break;
      }

      if (bDoConvDLE)
      {
         for (i=0, ulSLen=0; i<ulLen; i++)
         {
            achOutB[ulSLen++]=achFileB[i];
            if (achFileB[i]==16) // DLE
               achOutB[ulSLen++]=achFileB[i];
         }
      }

      if (fFirst)
      {
         fFirst = FALSE;

         if (bDoConvDLE)
         {
            pchSendBuff = achOutB;
            ulSendBuffLen = ulSLen;
         }
         else
         {
            pchSendBuff = achFileB;
            ulSendBuffLen = ulLen;
         }

         usGlobalState=GS_INITPLAY;

         res = SendHayes2(ParamBlock.szATVoiceTX, 3000l, 1);
         if (res)
         {
            SetComXONXOFF(FALSE, FALSE);
            DosClose(fh);
            prxsRet->strptr[0]='!';
            return;
         }
      }
      else
      {
         if (bDoConvDLE)
         {
            if (SendIt(achOutB, ulSLen))
            {
               if (fDebug)
                  dprintf("Send error\n");
               break;
            }
         }
         else
         {
            if (SendIt(achFileB, ulLen))
            {
               if (fDebug)
                  dprintf("Send error\n");
               break;
            }
         }
      }

      ulMsg=WaitQueue(IM_DLEFROMDCE | IM_STOP | IM_EXTUSER, &pData, &ulLen, SEM_IMMEDIATE_RETURN);
      if (ulMsg)
      {
         switch (ulMsg)
         {
         case IM_DLEFROMDCE:
            if (fDebug)
               dprintf("Event %c\n", *((PCHAR)pData));
            chRet=*((PCHAR)pData);
            bOtherEvent=TRUE;
            break;
         case IM_EXTUSER:
            if (fDebug)
               dprintf("User event '%s' when playing voice\n", (PCHAR)pData);
            strcpy(prxsRet->strptr, (PCHAR)pData);
            prxsRet->strlength=strlen((PCHAR)pData);
            chRet='\0';
            bOtherEvent=TRUE;
            break;
         case IM_STOP:
            if (fDebug)
               dprintf("Playback STOPPED\n");
            chRet='.';
            bOtherEvent=TRUE;
            break;
         default:
            bOtherEvent=TRUE;
            chRet='!';
         }

         free(pData);
         SendHayes2(ParamBlock.szATVoiceCancelTX, 3000l, 3);
         break;
      }

      if (fAbortCmd)
      {
         if (fDebug)
            dprintf("VTX aborted\n");
         bOtherEvent=TRUE;
         chRet='!';
         break;
      }
   }

   SetComXONXOFF(FALSE, FALSE);
   DosClose(fh);

   if (!bOtherEvent)
   {
      // See if any other events has occurred now
      ulMsg=WaitQueue(IM_DLEFROMDCE | IM_EXTUSER, &pData, &ulLen, SEM_IMMEDIATE_RETURN);
      if (ulMsg)
      {
         switch (ulMsg)
         {
         case IM_DLEFROMDCE:
            if (fDebug)
               dprintf("Event %c\n", *((PCHAR)pData));
            chRet=*((PCHAR)pData);
            bOtherEvent=TRUE;
            break;
         case IM_EXTUSER:
            if (fDebug)
               dprintf("User event '%s' when waiting for ETX\n", (PCHAR)pData);
            strcpy(prxsRet->strptr, (PCHAR)pData);
            prxsRet->strlength=strlen((PCHAR)pData);
            chRet='\0';
            bOtherEvent=TRUE;
            break;
         }
         free(pData);
      }
   }

   if (fDebug)
      dprintf("Playback ended\n");

   if (chRet)
      prxsRet->strptr[0]=chRet;
}



static USHORT StartRec
(
   USHORT usComp  // ZCOMP_*
)
{
   ULONG ulLen;
   CHAR szVSMCmd[MAXHAYESMSG];
   ZYXHEAD zh;
   USHORT res, usType;

   // Make sure the input queue is empty
   ClearQueue(IM_STRFROMDCE | IM_DLEFROMDCE | IM_STOP, SEM_IMMEDIATE_RETURN);

   memset(&zh, 0, sizeof zh);

   switch (ParamBlock.szFileMode[0])
   {
   case 'Z':
      if (usComp > 3)
      {
         bDLEConv = FALSE;
         usType = usComp - 3;
      }
      else
      {
         bDLEConv = TRUE;
         usType = usComp;
      }
      memcpy(&zh, achZyXKey, sizeof achZyXKey);
      break;

   case 'G':
      bDLEConv = FALSE;
      usType = usComp;
      memcpy(&zh, achAM4PMKey, sizeof achAM4PMKey);
      break;

   default:
      if (fDebug)
         dprintf("Unkown file mode '%c'\n", ParamBlock.szFileMode[0]);
      LogMessage(22, ParamBlock.szFileMode, NULL);
      return 1;
   }

   sprintf(szVSMCmd, ParamBlock.szATVoiceMode, usType);
   res=SendHayes2(szVSMCmd, MAXWAITDCE, 1);
   if (res)
   {
      if (fDebug)
         dprintf("Send error %u\n", res);
      return 1;
   }

   if (hRec != 0)
   {
      // Write header
      zh.usType=usComp-1;
      DosWrite(hRec, &zh, sizeof zh, &ulLen);
   }

   ulRecTime=0;

   SetComXONXOFF(FALSE, TRUE);

   usGlobalState=GS_INITREC;

   res=SendHayes2(ParamBlock.szATVoiceRX, 30000, 3);
   if (res)
      return 1;

   if (fDebug)
      dprintf("Recording!\n");

   return 0;

}


static USHORT EndRec(void)
{
   return SendHayes2("{VCON}", 3000, 3); // Anything will abort the recording
}


static void WaitDLECode
(
   ULONG ulTO,
   PRXSTRING prxsRet
)
{
   ULONG ulMsg, ulLen;
   PVOID pData;
   CHAR chRet;
  
   ulMsg=WaitQueue(IM_DLEFROMDCE | IM_ABORT | IM_STOP | IM_EXTUSER, &pData, &ulLen, ulTO);
   if (ulMsg)
   {
      if (fAbortCmd)
      {
         if (fDebug)
            dprintf("Receive aborted\n");
         chRet='!';
      }
      else
      {
         switch (ulMsg)
         {
         case IM_DLEFROMDCE:
            if (fDebug)
               dprintf("Event %c\n", *((PCHAR)pData));
            chRet=*((PCHAR)pData);
            break;

         case IM_EXTUSER:
            if (fDebug)
               dprintf("User event '%s'\n", (PCHAR)pData);
            strcpy(prxsRet->strptr, (PCHAR)pData);
            prxsRet->strlength=strlen((PCHAR)pData);
            free(pData);
            return;
//          break;

         default:
            if (fDebug)
               dprintf("Receive stopped\n");
            chRet='.';
         }
      }

      if (ulMsg)
         free(pData);
   }
   else
   {
      if (fDebug)
         dprintf("Timeout\n");
      chRet='>';
   }

   prxsRet->strptr[0]=chRet;
   prxsRet->strlength=1;
}


static void WaitDCEResp
(
   ULONG ulTO,
   PRXSTRING prxsRet
)
{
   ULONG ulMsg, ulLen;
   PVOID pData;
   CHAR chRet;
  
   ulMsg=WaitQueue(IM_STRFROMDCE | IM_ABORT | IM_STOP, &pData, &ulLen, ulTO);
   if (ulMsg)
   {
      if (fAbortCmd)
      {
         if (fDebug)
            dprintf("Receive aborted\n");
         chRet='!';
      }
      else
      {
         switch (ulMsg)
         {
         case IM_STRFROMDCE:
            if (fDebug)
               dprintf("More DCE: '%s'\n", pData);
            strcpy(prxsRet->strptr, pData);
            prxsRet->strlength=strlen(pData);
            free(pData);
            return;

         default:
            if (fDebug)
               dprintf("Receive stopped\n");
            chRet='.';
         }
      }

      if (ulMsg)
         free(pData);
   }
   else
   {
      if (fDebug)
         dprintf("Timeout\n");
      chRet='>';
   }

   prxsRet->strptr[0]=chRet;
   prxsRet->strlength=1;
}


static void DoRelCom(BOOL fReset)
{
   PVOID pData;
   ULONG ulMsg, ulLen, ulCount;

   if (hCom)
   {
      WinPostMsg(hwndFrame, WMU_NOCOM, 0l, 0l);
      if (fDebug)
         dprintf("Releasing COM port\n");

      if (fReset)
      {
         SendHayes("ATZ\r");
         ulMsg=WaitQueue(IM_STRFROMDCE, &pData, &ulLen, MAXWAITDCE);
         if (ulMsg)
            free(pData);
      }

      DosResetEventSem(semStopedReading, &ulCount);
      DosPostEventSem(semStopRead);
      SetComTOZero();
      if (DosWaitEventSem(semStopedReading, 30000l))
         LogMessage(23, NULL); // Timeout waiting for reading thread
      SaveRestComMode(FALSE);
      DosClose(hCom);
      hCom=0l;
      DosReleaseMutexSem(semCom);
      WinPostMsg(hwndFrame, WMU_STATE, MPFROMP(strdup("COM port released")), 0l);
   }
}


static USHORT Wait4Call(void)
{

   USHORT res;
   ULONG ulMsg, ulLen, ulCrntTime;
   PVOID pData;
   CHAR szStr[10], *pch;
   BOOL fDeleteAborted;
   SHORT iRes;
   static ulRingTime=0l, ulRings=0l;

   // Empty queue
   ClearQueue(IM_STRFROMDCE | IM_DLEFROMDCE, 1000l);

   res=SendHayes("\r\r\r\r");
   if (res)
   {
      if (fDebug)
         dprintf("Send error %u\n", res);
      return res;
   }

   iRes=CallRexx("INIT.AMC", NULL);
   if (iRes)
   {
      if (fDebug)
         dprintf("Init error %u\n", iRes);
      return 0;
   }

   res=SendHayes("ATI\r");
   if (res)
   {
      if (fDebug)
         dprintf("Send error %u\n", res);
      return 0;
   }

   ulMsg=WaitQueue(IM_STRFROMDCE, &pData, &ulLen, MAXWAITDCE);
   if (ulMsg==IM_STRFROMDCE)
   {
      pszDCEVer=pData;
      if (fDebug)
         dprintf("ATI is '%s'\n", pData);
   }

   // Empty queue
   ClearQueue(IM_STRFROMDCE | IM_DLEFROMDCE, 1000l);

   for (; hCom;)
   {
      if (fAbortCmd)
      {
         for (fDeleteAborted=FALSE;;)
         {
            ulMsg=WaitQueue(0xffffffff, &pData, &ulLen, SEM_IMMEDIATE_RETURN);
            if (!ulMsg)
               break;
            free(pData);
            if (ulMsg==IM_DELETE)
               fDeleteAborted=TRUE;
               
         }
         if (fDeleteAborted)
            WinPostMsg(hwndFrame, WMU_UPDLIST, 0l, 0l);
         WinPostMsg(hwndFrame, WMU_LASTDCE, MPFROMP(strdup("Aborted")), 0l);
         fAbortCmd=FALSE;
      }

      WinPostMsg(hwndFrame, WMU_STATE, MPFROMP(strdup("Waiting for call")), 0l);
      if (fDebug)
         dprintf("Waiting for call\n");

      WinPostMsg(hwndFrame, WMU_IDLECMD, 0l, 0l);
      ulMsg=WaitQueue(0xffffffff, &pData, &ulLen, SEM_INDEFINITE_WAIT);
      if (fDebug)
         dprintf("Something happened. ulMsg=%lu\n", ulMsg);
      switch (ulMsg)
      {
         case IM_STRFROMDCE:
            if (strcmp(pData, "VCON")==0 || (ulLen >= 5 && memcmp(pData, "RING", 4)==0))
            {
               WinPostMsg(hwndFrame, WMU_BUSYCMD, 0l, 0l);

               ulCrntTime=GetSysMSecs();

               if (fDebug)
                  dprintf("Ring! Delay %lu\n", ulCrntTime-ulRingTime);

               if (ulCrntTime-ulRingTime >= MAXRINGDELAY)
                  ulRings=1;
               else
                  ulRings++;

               ulRingTime=ulCrntTime;

               iRes=CallRexx("RING.AMC", (PCHAR)pData, _ltoa(ulRings, szStr, 10), NULL);
               free(pData);
               WinPostMsg(hwndFrame, WMU_UPDLIST, 0l, 0l);
               switch (iRes)
               {
               case AMR_NORMAL:
                  res=HangUp();
                  if (res)
                     return 0;
                  break;
               case AMR_REINIT:
                  return 0;
               case AMR_EXIT:
                  return 1;
               default:
                  if (fDebug)
                     dprintf("Unkown return code from RING.AMC %i\n", iRes);
                  return 0;
               }
               break;
            }
            
            if (ulLen >= 5 && memcmp(pData, "TIME", 4)==0)
            {
               CallRexx("CID.AMC", pData, NULL);
               free(pData);
               break;
            }
            break;

         case IM_DOWN:
            free(pData);
            SendHayes("ATZ\r");
            ulMsg=WaitQueue(IM_STRFROMDCE, &pData, &ulLen, MAXWAITDCE);
            if (ulMsg)
               free(pData);
            return 1;

         case IM_PLAY:
            if (fDebug)
               dprintf("Request to play '%s'\n", pData);
            WinPostMsg(hwndFrame, WMU_BUSYCMD, 0l, 0l);
            iRes=CallRexx("PLAY.AMC", (PCHAR)pData, NULL);
            free(pData);
//          WinPostMsg(hwndFrame, WMU_UPDLIST, 0l, 0l);
            switch (iRes)
            {
            case AMR_NORMAL:
               res=HangUp();
               if (res)
                  return 0;
               break;
            case AMR_REINIT:
               return 0;
            case AMR_EXIT:
               return 1;
            default:
               if (fDebug)
                  dprintf("Unkown return code from PLAY.AMC %i\n", iRes);
               return 0;
            }
            break;

         case IM_DELETE:
            WinPostMsg(hwndFrame, WMU_BUSYCMD, 0l, 0l);
            if (fDebug)
               dprintf("Deleting '%s'\n", pData);
            iRes=CallRexx("DELETE.AMC", (PCHAR)pData, NULL);
            if (iRes==1)
               WinPostMsg(hwndFrame, WMU_UPDLIST, 0l, 0l);
            else
               ulRecMessages--;
            free(pData);
            break;

         case IM_RECORD:
            if (fDebug)
               dprintf("Request to record '%s'\n", pData);
            WinPostMsg(hwndFrame, WMU_BUSYCMD, 0l, 0l);
            iRes=CallRexx("RECORD.AMC", (PCHAR)pData, NULL);
            free(pData);
//          WinPostMsg(hwndFrame, WMU_UPDLIST, 0l, 0l);
            switch (iRes)
            {
            case AMR_NORMAL:
               res=HangUp();
               if (res)
                  return 0;
               break;
            case AMR_REINIT:
               return 0;
            case AMR_EXIT:
               return 1;
            default:
               if (fDebug)
                  dprintf("Unkown return code from RECORD.AMC %i\n", iRes);
               return 0;
            }
            break;

         case IM_RELEASE:
            free(pData);
            DoRelCom(TRUE);
            DosSleep(30000l);
            return 0;

         case IM_STARTAMC:
            WinPostMsg(hwndFrame, WMU_BUSYCMD, 0l, 0l);
            pch=strchr((PCHAR)pData, ' ');
            if (pch==NULL)
               pch="";
            else
            {
               *pch='\0';
               pch++;
            }
            iRes=CallRexx((PCHAR)pData, pch, NULL);
            free(pData);
            WinPostMsg(hwndFrame, WMU_UPDLIST, 0l, 0l);
            switch (iRes)
            {
            case AMR_NORMAL:
               res=HangUp();
               if (res)
                  return 0;
               break;
            case AMR_REINIT:
               return 0;
            case AMR_EXIT:
               return 1;
            default:
               if (fDebug)
                  dprintf("Unkown return code from AMC script %i\n", iRes);
               return 0;
            }
            break;

         case IM_EXTUSER:
            WinPostMsg(hwndFrame, WMU_BUSYCMD, 0l, 0l);
            iRes=CallRexx("USER.AMC", (PCHAR)pData, NULL);
            free(pData);
            WinPostMsg(hwndFrame, WMU_UPDLIST, 0l, 0l);
            switch (iRes)
            {
            case AMR_NORMAL:
               res=HangUp();
               if (res)
                  return 0;
               break;
            case AMR_REINIT:
               return 0;
            case AMR_EXIT:
               return 1;
            default:
               if (fDebug)
                  dprintf("Unkown return code from USER.AMC %i\n", iRes);
               return 0;
            }
            break;

         case IM_INICHANGED:
            return 0;

         default:
            free(pData);
      }
   }

   return 0;
}


static void makeit(void)
{
   ULONG ulCount;
   USHORT res;
   static BOOL fFirst=TRUE;


   for (;;)
   {
      WinPostMsg(hwndFrame, WMU_STATE, MPFROMP(strdup("Initializing")), 0l);
      GetIniFile();

      res=OpenCom();
      if (res)
      {
         WinPostMsg(hwndFrame, WMU_STATE, MPFROMP(strdup("Cannot open COM")), 0l);
         DosSleep(WAITRETRYCOM);
         continue;
      }
      if (fFirst)
      {
         fFirst=FALSE;
         SaveRestComMode(TRUE);
      }
      SetComMode();

      usGlobalState=GS_READY;
      DosPostEventSem(semGoOnRead);

      res=Wait4Call();

      if (res)
         CallRexx("EXIT.AMC", hCom ? "1" : "0", NULL);

      WriteState2IniFile();

      if (hCom)
      {
         DosResetEventSem(semStopedReading, &ulCount);
         DosPostEventSem(semStopRead);
         SetComTOZero();
         if (DosWaitEventSem(semStopedReading, 30000l))
            LogMessage(23, NULL); // Timeout waiting for reading thread
      }

      if (res)
         return;

      if (hCom)
      {
//       SaveRestComMode(FALSE);
         DosClose(hCom);
         hCom=0l;
         DosReleaseMutexSem(semCom);
      }

      DosSleep(2500l);
   }
}


USHORT StartProg
(
   PCHAR szCmdLine,
   PUSHORT pusRetCode
)
{
   ULONG res, i, ulLen, ulSid;
   PID pid;
   CHAR szInput[45], szExeFile[255], szProg[40], bPriority;
   STARTDATA StartData;
   PCHAR pszArg;
   HQUEUE hQ;
   static CHAR szQN[]="\\queues\\am4pm\\startp";
   REQUESTDATA rd;
   PVOID pData;

   // Find EXE file
   for (i=0; i < sizeof szProg - 1 && szCmdLine[i] != ' ' && szCmdLine[i] != '\0'; i++)
      szProg[i]=szCmdLine[i];
   szProg[i]='\0';

   pszArg=szCmdLine+i;
   if (szCmdLine[i] != '\0')
      pszArg++;

   strcpy(szInput, szProg),
   strcat(szInput, ".EXE");

   res=DosSearchPath(SEARCH_ENVIRONMENT | SEARCH_CUR_DIRECTORY, "PATH", szInput, szExeFile, 255);
   if (res)
      return res;

   if (fDebug)
      dprintf("Starting '%s' '%s'\n", szExeFile, pszArg);

   StartData.Length=50; // sizeof StartData;
   StartData.Related=SSF_RELATED_CHILD;
   StartData.FgBg=SSF_FGBG_BACK;
   StartData.TraceOpt=SSF_TRACEOPT_NONE;
   StartData.PgmTitle=NULL;
   StartData.PgmName=szExeFile;
   StartData.PgmInputs=pszArg;
   StartData.TermQ=szQN;
   StartData.Environment=NULL;
   StartData.InheritOpt=SSF_INHERTOPT_PARENT;
   StartData.SessionType=SSF_TYPE_FULLSCREEN;
   StartData.IconFile=NULL;
   StartData.PgmHandle=0l;
// if (fDebug)
//    StartData.PgmControl=SSF_CONTROL_VISIBLE | SSF_CONTROL_NOAUTOCLOSE;
// else
      StartData.PgmControl=SSF_CONTROL_VISIBLE | SSF_CONTROL_MINIMIZE;

   res=DosCreateQueue(&hQ, QUE_FIFO, szQN);
   if (res)
   {
      if (fDebug)
         dprintf("Error %u creating queue\n", res);
      return res;
   }

   res=DosStartSession(&StartData, &ulSid, &pid);
   if (res && res!=ERROR_SMG_START_IN_BACKGROUND)
   {
      if (fDebug)
         dprintf("Error %u starting session\n", res);
      DosCloseQueue(hQ);
      return res;
   }

   res=DosReadQueue(hQ, &rd, &ulLen, &pData, 0, DCWW_WAIT, &bPriority, 0l);
   DosCloseQueue(hQ);
   if (res)
   {
      if (fDebug)
         dprintf("Error %u reading queue\n", res);
      return res;
   }

   *pusRetCode=((PUSHORT)pData)[1];

   DosFreeMem(pData);

   DosCloseQueue(hQ);
   return 0;
}


static signed _System AMGetHotComm
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   ULONG ulCount;

   if (ulArgc != 0)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   DosResetEventSem(semStopedReading, &ulCount);
   DosPostEventSem(semStopRead);
   SetComTOZero();
   if (DosWaitEventSem(semStopedReading, 30000l))
      LogMessage(23, NULL); // Timeout waiting for reading thread
   SaveRestComMode(FALSE);
   _ltoa((ULONG)hCom, prxsRet->strptr, 10);
   prxsRet->strlength=strlen(prxsRet->strptr);

   return 0;
}


static signed _System AMReleaseHotComm
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 0)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   SetComMode();

   DosPostEventSem(semGoOnRead);

   prxsRet->strptr=NULL;

   return 0;
}


static signed _System AMDPrint
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 1)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   dprintf("DPRINT '%s'\n", arxsArgv[0].strptr);

   prxsRet->strptr=NULL;

   return 0;
}


static signed _System AMLog
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 1)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   LogMessage(21, arxsArgv[0].strptr, NULL);

   prxsRet->strptr=NULL;

   return 0;
}


static signed _System AMOpenRecFile
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 0)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   hRec=CreateMFile();
   strcpy(prxsRet->strptr, szCrntMFile);

   prxsRet->strlength=strlen(prxsRet->strptr);

   return 0;
}


static void WriteEA
(
   HFILE fh,
   CHAR const * szEA,
   USHORT usEAType,
   PVOID pEAData,
   USHORT usDLen
)
{
   USHORT usEA2Len;
   EAOP2 eaop2;
   PFEA2LIST pfea2list;
   APIRET rc;


   usEA2Len=sizeof *pfea2list + strlen(szEA);

   pfea2list=malloc(usEA2Len + usDLen + 2*sizeof(USHORT));

   memcpy((PCHAR)pfea2list + usEA2Len + 2*sizeof(USHORT), pEAData, usDLen);
   *(PUSHORT)((PCHAR)pfea2list + usEA2Len) = usEAType;
   *(PUSHORT)((PCHAR)pfea2list + usEA2Len + sizeof (USHORT)) = usDLen;

   pfea2list->cbList=sizeof(FEA2) + strlen(szEA) + usDLen + 2*sizeof(USHORT);
   pfea2list->list[0].oNextEntryOffset=0;
   pfea2list->list[0].fEA=0;
   pfea2list->list[0].cbName=strlen(szEA);
   pfea2list->list[0].cbValue=usDLen + 2*sizeof(USHORT);
   strcpy(pfea2list->list[0].szName, szEA);

   eaop2.fpFEA2List=pfea2list;
   rc=DosSetFileInfo(fh, FIL_QUERYEASIZE, &eaop2, sizeof eaop2);
   free(pfea2list);

   if (fDebug && rc != NO_ERROR)
      dprintf("Error %lu setting EA\n", rc);
}


static signed _System AMCloseRecFile
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   ULONG ulTime;

   if (ulArgc != 0)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   if (fDebug)
      dprintf("Recording lasted %lu s\n", ulRecTime/1000);

   ulTime=ulRecTime/1000;
   WriteEA(hRec, szEALen, EAT_BINARY, &ulTime, sizeof ulTime);
   WriteEA(hRec, szEAType, EAT_ASCII, szOurEAType, strlen(szOurEAType));

   DosClose(hRec);

   prxsRet->strptr=NULL;

   return 0;
}


static signed _System AMPlayFile
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc < 1 || ulArgc > 2)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   if (ulArgc >= 2)
      strncpy((PCHAR)szActiveDLECodes, arxsArgv[1].strptr, sizeof szActiveDLECodes - 1);
   else
      szActiveDLECodes[0] = '\0';

   PlayFile(prxsRet, arxsArgv[0].strptr);
   return 0;
}


static signed _System AMStartRec
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 1)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   prxsRet->strptr=NULL;

   if (StartRec(atoi(arxsArgv[0].strptr)))
      return 2;
   return 0;
}


static signed _System AMEndListenDLECode
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 0)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   prxsRet->strptr=NULL;

   if (EndRec())
      return 2;
   return 0;
}


static signed _System AMEndRec
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 0)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   prxsRet->strptr=NULL;

   if (EndRec())
      return 2;
   return 0;
}


static signed _System AMCloseComm
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 0)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   DoRelCom(FALSE);

   strcpy(prxsRet->strptr, ParamBlock.szCom);
   prxsRet->strlength=strlen(ParamBlock.szCom);

   return 0;
}


static signed _System AMOpenComm
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   ULONG i, res;

   if (ulArgc != 0)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   if (fDebug)
      dprintf("Opening COM port again\n");

   for (i=0; i<400; i++)
   {
      WinPostMsg(hwndFrame, WMU_STATE, MPFROMP(strdup("Opening COM")), 0l);
      res=OpenCom();
      if (res)
      {
         WinPostMsg(hwndFrame, WMU_STATE, MPFROMP(strdup("Waiting for COM")), 0l);
         DosSleep(WAITRETRYCOM);
         continue;
      }
      SetComMode();
      prxsRet->strptr=NULL;

      WinPostMsg(hwndFrame, WMU_STATE, MPFROMP(strdup("")), 0l);

      if (fDebug)
         dprintf("COM port opened again\n");

      return 0;
   }

   return 2;
}


static signed _System AMStartListenDLECode
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   USHORT usMode=2;

   if (ulArgc > 0)
   {
      if (ulArgc == 1)
         usMode=atoi(arxsArgv[0].strptr);
      else
      {
         LogMessage(13, pszName, NULL);
         if (fDebug)
            dprintf("Bad number of arguments in '%s'\n", pszName);
         return 1;
      }
   }

   hRec=0;

   prxsRet->strptr=NULL;

   if (StartRec(usMode))
      return 2;
   return 0;
}


static signed _System AMWaitDLECode
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   ULONG ulTO;

   if (ulArgc > 2)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   if (ulArgc >= 1)
      ulTO = atol(arxsArgv[0].strptr);
   else
      ulTO = 120000l;

   if (ulArgc >= 2)
      strncpy((PCHAR)szActiveDLECodes, arxsArgv[1].strptr, sizeof szActiveDLECodes - 1);
   else
      szActiveDLECodes[0] = '\0';

   WaitDLECode(ulTO, prxsRet);

   return 0;
}


static signed _System AMSendW
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   CHAR szHayes[256];
   ULONG ulMsg, ulLen;
   PVOID pData;

   if (ulArgc != 1)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   // Make sure the input queue is empty
   WaitQueue(0, &pData, &ulLen, SEM_IMMEDIATE_RETURN);

   if (fDebug)
      dprintf("Sending '%s'\n", arxsArgv[0].strptr);
   strcpy(szHayes, arxsArgv[0].strptr);
   strcat(szHayes, "\r");
   if (SendHayes(szHayes))
      return 2;

   ulMsg=WaitQueue(IM_STRFROMDCE, &pData, &ulLen, MAXWAITDCE);
   if (ulMsg!=IM_STRFROMDCE)
   {
      strcpy(prxsRet->strptr, "!");
      prxsRet->strlength=1;
   }
   else
   {
      if (fDebug)
         dprintf("DCE returned '%s'\n", pData);

      strcpy(prxsRet->strptr, pData);
      prxsRet->strlength=strlen(pData);
   }

   if (ulMsg)
      free(pData);

   return 0;
}


static signed _System AMSendAT
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   ULONG ulTO = MAXWAITDCE;
   USHORT usRetries = 1;

   prxsRet->strptr=NULL;

   if (ulArgc > 3 || ulArgc < 1)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   if (ulArgc >= 3)
      usRetries = atoi(arxsArgv[2].strptr);

   if (ulArgc >= 2)
      ulTO = atol(arxsArgv[1].strptr);

   return SendHayes2(arxsArgv[0].strptr, ulTO, usRetries) ? 2 : 0;
}


static signed _System AMGetMoreDCEResp
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   ULONG ulTO = MAXWAITDCE;

   if (ulArgc > 0)
   {
      if (ulArgc == 1)
         ulTO = atol(arxsArgv[0].strptr);
      else
      {
         LogMessage(13, pszName, NULL);
         if (fDebug)
            dprintf("Bad number of arguments in '%s'\n", pszName);
         return 1;
      }
   }

   WaitDCEResp(ulTO, prxsRet);

   #if 0
   ULONG ulMsg, ulLen;
   PVOID pData;
   ulMsg=WaitQueue(IM_STRFROMDCE, &pData, &ulLen, ulTO);
   if (ulMsg!=IM_STRFROMDCE)
   {
      if (ulMsg)
         free(pData);
      return 2;
   }

   if (fDebug)
      dprintf("DCE returned '%s'\n", pData);
   strcpy(prxsRet->strptr, pData);
   prxsRet->strlength=strlen(pData);

   free(pData);
   #endif

   return 0;
}


static signed _System AMStartProg
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   USHORT res, usRetCode;

   if (ulArgc != 1)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   res=StartProg(arxsArgv[0].strptr, &usRetCode);
   if (res)
   {
      if (fDebug)
         dprintf("Error %u starting '%s'\n", res, arxsArgv[0].strptr);
      LogNumMessage(9, res, arxsArgv[0].strptr, NULL);
      return 2;
   }

   _itoa(usRetCode, prxsRet->strptr, 10);
   prxsRet->strlength=strlen(prxsRet->strptr);

   return 0;
}


static signed _System AMSetStateText
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 1)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   WinPostMsg(hwndFrame, WMU_STATE, MPFROMP(strdup(arxsArgv[0].strptr)), 0l);

   prxsRet->strptr=NULL;

   return 0;
}


static signed _System AMSetLastEventText
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 1)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   WinPostMsg(hwndFrame, WMU_LASTDCE, MPFROMP(strdup(arxsArgv[0].strptr)), 0l);

   prxsRet->strptr=NULL;

   return 0;
}


static signed _System AMReadIni
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   PCHAR psz;

   if (ulArgc != 2)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   psz=IniGetSZ(arxsArgv[0].strptr, arxsArgv[1].strptr);

   strcpy(prxsRet->strptr, psz);
   prxsRet->strlength=strlen(psz);
   free(psz);
   return 0;
}


static signed _System AMWriteIni
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   if (ulArgc != 2)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   IniPutSZ(arxsArgv[0].strptr, arxsArgv[1].strptr);

   prxsRet->strptr=NULL;

   return 0;
}

static void GetIOAccess(void)
{
   int rc;
   static BOOL bAccess=FALSE;

   if (!bAccess)
   {
      rc=Dos16PortAccess(0,0,0x000,0xFFF);
      if (rc)
         LogDosMessage(rc, 16, NULL);
      else
         bAccess = TRUE;
      if (fDebug)
         dprintf("DosPortAccess=%u\n", rc);
   }
}

static signed _System AMInp
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   USHORT usPort, usData;

   if (ulArgc != 1)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   usPort = atoi(arxsArgv[0].strptr);
   GetIOAccess();

   usData = RPORT(usPort);

   itoa(usData, prxsRet->strptr, 10);
   prxsRet->strlength=strlen(prxsRet->strptr);

   return 0;
}


static signed _System AMOutp
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   USHORT usPort, usData;

   if (ulArgc != 2)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   usPort = atoi(arxsArgv[0].strptr);
   usData = atoi(arxsArgv[1].strptr);
   GetIOAccess();

   WPORT(usPort, usData);

   prxsRet->strptr=NULL;
   return 0;
}


static signed _System AMTrimFileEnd
(
   PSZ pszName,
   ULONG ulArgc,
   RXSTRING arxsArgv[],
   PSZ pszQueueName,
   PRXSTRING prxsRet
)
{
   ULONG ulBytes;
   FILESTATUS3 fs;
   APIRET res;

   if (ulArgc != 1)
   {
      LogMessage(13, pszName, NULL);
      if (fDebug)
         dprintf("Bad number of arguments in '%s'\n", pszName);
      return 1;
   }

   ulBytes = atol(arxsArgv[0].strptr);

   if (hRec == 0l)
   {
      LogMessage(19, NULL);   // No file open for recording
      return 2;
   }

   res = DosQueryFileInfo(hRec, FIL_STANDARD, &fs, sizeof fs);
   if (res)
   {
      LogDosMessage(res, 17, NULL);
      return 2;
   }

   if ((LONG)fs.cbFile - sizeof (ZYXHEAD) > ulBytes)
   {
      res = DosSetFileSize(hRec, fs.cbFile - ulBytes);
      if (res)
      {
         LogDosMessage(res, 18, NULL);
         return 2;
      }
   }

   prxsRet->strptr = NULL;
   return 0;
}


static signed _System MyExitHandler
(
   LONG lFunc,
   LONG lSubFunc,
   PEXIT pchParm
)
{
   static CHAR szVarRings[]="VAMRINGS";
   static CHAR szVarMsgs[]="VAMMSGS";
   static CHAR szVarMsgPat[]="VAMMSGPAT";
   static SHVBLOCK shvb[] = {
                              {shvb+1, {sizeof szVarRings - 1, szVarRings}},
                              {shvb+2, {sizeof szVarMsgs - 1, szVarMsgs}},
                              {NULL, {sizeof szVarMsgPat - 1, szVarMsgPat}}
                            };
   APIRET res;
   CHAR achRings[3], achMsgs[10];
   ULONG ulLen, ulVal;

   switch (lFunc)
   {
   case RXINI:
      if (lSubFunc != RXINIEXT)
         return RXEXIT_NOT_HANDLED;
      if (fDebug)
         dprintf("Initializing Rexx function\n");

      shvb[0].shvcode=RXSHV_SET;
      shvb[0].shvret=0;
      _ltoa(ParamBlock.ulRings, achRings, 10);
      ulLen=strlen(achRings);
      MAKERXSTRING(shvb[0].shvvalue, achRings, ulLen);
      shvb[0].shvvaluelen=ulLen;

      shvb[1].shvcode=RXSHV_SET;
      shvb[1].shvret=0;
      _ltoa(ulRecMessages, achMsgs, 10);
      ulLen=strlen(achMsgs);
      MAKERXSTRING(shvb[1].shvvalue, achMsgs, ulLen);
      shvb[1].shvvaluelen=ulLen;

      shvb[2].shvcode=RXSHV_SET;
      shvb[2].shvret=0;
      ulLen=strlen(ParamBlock.szMessagePattern);
      MAKERXSTRING(shvb[2].shvvalue, ParamBlock.szMessagePattern, ulLen);
      shvb[2].shvvaluelen=ulLen;

      res=RexxVariablePool(shvb);
      if (fDebug)
         dprintf("RexxVariablePool=%u\n", res);
      return RXEXIT_HANDLED;

   case RXTER:
      if (lSubFunc != RXTEREXT)
         return RXEXIT_NOT_HANDLED;
      if (fDebug)
         dprintf("Cleaning up after Rexx function\n");

      shvb[0].shvcode=RXSHV_FETCH;
      shvb[0].shvret=0;
      MAKERXSTRING(shvb[0].shvvalue, achRings, sizeof achRings - 1);
      shvb[0].shvvaluelen=sizeof achRings - 1;
      res=RexxVariablePool(shvb);
      if (fDebug)
         dprintf("RexxVariablePool=%u\n", res);

      achRings[RXSTRLEN(shvb[0].shvvalue)]='\0';
      ulVal=atol(achRings);
      if (ulVal != ParamBlock.ulRings)
      {
         if (fDebug)
            dprintf("RINGS have changed %u => %u\n", ParamBlock.ulRings, ulVal);
         ParamBlock.ulRings=ulVal;
         IniPutL("Rings", ParamBlock.ulRings); // Rings before answer
      }
      return RXEXIT_HANDLED;

   case RXSIO:
      if (lSubFunc == RXSIOSAY || lSubFunc == RXSIOTRC)
      {
         PrintRxSIO(((RXSIOSAY_PARM *)pchParm)->rxsio_string.strptr);
         return RXEXIT_HANDLED;
      }
      break;
   }
   return RXEXIT_NOT_HANDLED;
}


static APIRET RegMyREXXFun(void)
{
   static struct
   {
      PSZ pszName;
      PFN pfnEntry;
   } Am4PmCmds[]=
   {
      {"AMGetHotComm",          AMGetHotComm},
      {"AMReleaseHotComm",      AMReleaseHotComm},
      {"AMDPrint",              AMDPrint},
      {"AMOpenRecFile",         AMOpenRecFile},
      {"AMCloseRecFile",        AMCloseRecFile},
      {"AMPlayFile",            AMPlayFile},
      {"AMStartRec",            AMStartRec},
      {"AMEndRec",              AMEndRec},
      {"AMStartListenDLECode",  AMStartListenDLECode},
      {"AMEndListenDLECode",    AMEndListenDLECode},
      {"AMWaitDLECode",         AMWaitDLECode},
      {"AMSendW",               AMSendW},
      {"AMGetMoreDCEResp",      AMGetMoreDCEResp},
      {"AMStartProg",           AMStartProg},
      {"AMSetStateText",        AMSetStateText},
      {"AMSetLastEventText",    AMSetLastEventText},
      {"AMOpenComm",            AMOpenComm},
      {"AMCloseComm",           AMCloseComm},
      {"AMReadIni",             AMReadIni},
      {"AMWriteIni",            AMWriteIni},
      {"AMInp",                 AMInp},
      {"AMOutp",                AMOutp},
      {"AMTrimFileEnd",         AMTrimFileEnd},
      {"AMSendAT",              AMSendAT},
      {"AMLog",                 AMLog},
   };

   ULONG res;
   USHORT i;


   for (i=0; i<sizeof Am4PmCmds / sizeof Am4PmCmds[0]; i++)
   {
      res=RexxRegisterFunctionExe(Am4PmCmds[i].pszName, Am4PmCmds[i].pfnEntry);
      if (res)
      {
         LogNumMessage(8, res, Am4PmCmds[i].pszName, NULL);
         return res;
      }
   }

   res=RexxRegisterExitExe(szExitName, MyExitHandler, NULL);
   if (res)
   {
      LogNumMessage(15, res, NULL);
      return res;
   }

   return 0;
}


void WriteModemThread(PVOID lpVar)
{
   dprintf("Writing thread started\n");

   RegMyREXXFun();

   DosCreateMutexSem(szSemCom, &semCom, 0, 0);

   DosCreateEventSem(NULL, &semStopRead, 0, TRUE);
   DosCreateEventSem(NULL, &semStopedReading, 0, FALSE);
   DosCreateEventSem(NULL, &semGoOnRead, 0, FALSE);
   DosCreateEventSem(NULL, &semQueueFull, 0, FALSE);
   DosCreateEventSem(NULL, &semQueueEmpty, 0, FALSE);

   DosSetPriority(PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, 0);

   thidRec = _beginthread(ReadModem, NULL, RSTACKSIZE, NULL);
   DosWaitEventSem(semStopedReading, SEM_INDEFINITE_WAIT);
   WinPostMsg(hwndFrame, WMU_THREADSUP, 0l, 0l);

   // Start processing
   makeit();

   ShutDown();

   if (hCom)
   {
      SaveRestComMode(FALSE);
      DosClose(hCom);
      hCom=0l;
      DosReleaseMutexSem(semCom);
   }


   DosCloseEventSem(semStopRead);
   DosCloseEventSem(semStopedReading);
   DosCloseEventSem(semGoOnRead);
   DosCloseEventSem(semQueueFull);
   DosCloseEventSem(semQueueEmpty);
   WinPostMsg(hwndFrame, WMU_THREADSDOWN, 0l, 0l);
}

