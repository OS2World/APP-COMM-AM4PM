// Thomas Answering machine for PM

// File:          AM4PMCMD.c
// Description:   Program form sending commands to AM4PM

// History
// 930517 TO      Now it exists...
// 930531 TO      New command 'c'


#define INCL_BASE

#include <os2.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "am4pm.h"

static CHAR szVer[]="v0.1b (31 May 1993)";
static CHAR szExtQueue[]="\\QUEUES\\AM4PM";
static CHAR szSemCom[]="\\SEM32\\AM4PM\\COM";

static int SendExtQMsg
(
   USHORT usEvent,
   PVOID pData,
   ULONG ulLen
)
{
   USHORT res;
   ULONG ulPID;
   HQUEUE hQueue;
   PVOID pItem;

   res=DosOpenQueue(&ulPID, &hQueue, szExtQueue);
   if (res)
   {
      printf("AM4PM not running\n");
      return 2;
   }

   if (pData != NULL)
   {
      DosAllocSharedMem(&pItem, NULL, ulLen, PAG_COMMIT | OBJ_GIVEABLE | PAG_READ | PAG_WRITE);
      memcpy(pItem, pData, ulLen);
      DosGiveSharedMem(pItem, ulPID, PAG_READ | PAG_WRITE);
      DosFreeMem(pItem);
   }
   else
      pItem=NULL;

   res=DosWriteQueue(hQueue, usEvent, ulLen, pItem, 0);

   DosCloseQueue(hQueue);
   if (res)
      return 2;
   return 0;
}


static int RequestCom(void)
{
   USHORT res;
   HMTX sem=0;

   printf("Sending message to AM4PM to release COM port\n");
   res=SendExtQMsg(EQ_RELCOM, NULL, 0);
   if (res)
      return res;

   res=DosOpenMutexSem(szSemCom, &sem);
   if (res)
   {
      printf("Error %u opening semaphore\n", res);
      return 3;
   }

   res=DosRequestMutexSem(sem, 120000l);
   if (res)
   {
      DosCloseMutexSem(sem);
      if (res==ERROR_TIMEOUT)
      {
         printf("Timeout waiting for AM4PM to release COM port\n");
         return 4;
      }
      printf("Error %u waiting for semaphore\n", res);
      return 3;
   }

   DosReleaseMutexSem(sem);
   DosCloseMutexSem(sem);
   return 0;
}


int main
(
   USHORT usArgc,
   PCHAR pchArgv[]
)
{
   printf("AM4PMCMD %s\n\n", szVer);

   if (usArgc <= 1)
   {
      printf("Usage: AM4PMCMD cmd <p1>\n");
      printf("\tcmd r\trelease COM port for 30 s\n");
      printf("\t    c\tstart AMC file p1\n");
      printf("\t    u\tsend user defined message p1\n");
      return 1;
   }

   switch (tolower(pchArgv[1][0]))
   {
   case 'r':
      return RequestCom();

   case 'c':
      if (usArgc <= 2)
      {
         printf("Too few parameters\n");
         return 1;
      }
      return SendExtQMsg(EQ_STARTAMC, pchArgv[2], strlen(pchArgv[2])+1);

   case 'u':
      if (usArgc <= 2)
      {
         printf("Too few parameters\n");
         return 1;
      }
      return SendExtQMsg(EQ_USER, pchArgv[2], strlen(pchArgv[2])+1);

   default:
      printf("Unkown command '%c'\n", tolower(pchArgv[1][0]));
      return 1;
   }
}
