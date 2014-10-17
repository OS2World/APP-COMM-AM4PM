// Thomas Answering machine for PM

// File:          AM4PMR.c
// Description:   Reads data from modem

// History
// 930206 TO      Now it exists...

#define INCL_BASE
#include <os2.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <process.h>
#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <stdarg.h>

#include <fcntl.h>
#include <sys/types.h>
#include <time.h>
#include <sys/stat.h>
#include <io.h> 

#include "am4pm.h"

#if 0
static char * TknStr(char * szStr, unsigned char tkn)
{
   static PCHAR asctxt[32]=
   {
      "Null",
      "SOH",
      "STX",
      "ETX",
      "EOT",
      "ENQ",
      "ACK",
      "BELL",
      "BS",
      "HT",
      "LF",
      "VT",
      "FF",
      "CR",
      "SO",
      "SI",
      "DLE",
      "DC1",
      "DC2",
      "DC3",
      "DC4",
      "NAC",
      "SYN",
      "ETB",
      "CAN",
      "EM",
      "SUB",
      "ESC",
      "FS",
      "GS",
      "RS",
      "US"
   };
   if (tkn >= 32)
      sprintf(szStr, "%c", tkn);
   else
      sprintf(szStr, "<%s>", asctxt[tkn]);
   return szStr;
}


ULONG TimeDiff   // Calculates time between time2-time1
(
   ULONG time1,
   ULONG time2
)
{
   return time2-time1;
}


ULONG GetMyTID(void)
{
   PTIB pTib;
   PPIB pPib;

   DosGetInfoBlocks(&pTib, &pPib);
   return pTib->tib_ptib2->tib2_ultid;
}
#endif


void ReadModem(PVOID lpVar)
{
   static USHORT i;
   static ULONG n, ulLen, ulStartRecTime;
   static CHAR achHayesBuff[MAXHAYESMSG]; // Buffer for DCE answers
   static USHORT ichHayesBuff=0, usFBuffPos=0;
   static ULONG ulCount;
   static UCHAR buff[RECBUFFLEN], achFileB[RECFBUFFLEN];
   static BOOL bLastCharDLE=FALSE;

   if (fDebug)
      dprintf("Thread for reading started...\n");

   DosSetPriority(PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, 0);

   for (;;)
   {
      if (DosWaitEventSem(semStopRead, SEM_IMMEDIATE_RETURN)==0)
      {
         if (fDebug)
            dprintf("Reading thread halted. usGlobalState=%u\n", usGlobalState);
         DosResetEventSem(semStopRead, &ulCount);
         DosResetEventSem(semGoOnRead, &ulCount);
         DosPostEventSem(semStopedReading);
         DosWaitEventSem(semGoOnRead, SEM_INDEFINITE_WAIT);
         if (fDebug)
            dprintf("Reading thread goes on... usGlobalState=%u\n", usGlobalState);
         bLastCharDLE=FALSE;
         ichHayesBuff=0;
      }

      if (usGlobalState==GS_ENDING) // Check if it's time to die
      {
         if (fDebug)
            dprintf("\nReading thread dying...\n");
         return;
      }
      
      if (DosRead(hCom, buff, RECBUFFLEN, &n) || !n) // L„s in fr†n porten
         continue;

      for (i=0; i<n; i++)
      {
         if (usGlobalState==GS_READY || usGlobalState==GS_INITREC || usGlobalState==GS_DONE || usGlobalState==GS_INITPLAY)
         {
            switch (buff[i])
            {
            case '\r':
               break;

            case '\n':
               if (ichHayesBuff)
               {
                  achHayesBuff[ichHayesBuff]='\0';
                  if (fDebug)
                     dprintf("DCE: '%s'\n", achHayesBuff);
                  if (usGlobalState==GS_INITREC && strcmp((PCHAR)achHayesBuff, "CONNECT")==0)
                  {
                     usGlobalState=GS_RECORDING;
                     bLastCharDLE=FALSE;
                     ulStartRecTime=GetSysMSecs();
                  }
                  else if (usGlobalState==GS_DONE && strcmp((PCHAR)achHayesBuff, "VCON")==0)
                  {
                     if (fDebug)
                        dprintf("<Ready>\n");
                     usGlobalState=GS_READY;
                  }
                  else if (usGlobalState==GS_INITPLAY)
                  {
                     if (fDebug)
                        dprintf("Play started\n");
                     SendIt(pchSendBuff, ulSendBuffLen);
                     usGlobalState=GS_READY;
                  }
                  QueueData(IM_STRFROMDCE, achHayesBuff, ichHayesBuff+1);
                  ichHayesBuff=0;
               }
               break;

#if 0   // XON/XOFF now handled by OS/2 COM driver
            case 17:
               QueueData(IM_XON, NULL, 0l);
               if (fDebug)
                  dprintf("XON\n");
               break;

            case 19:
               QueueData(IM_XOFF, NULL, 0l);
               if (fDebug)
                  dprintf("XOFF\n");
               break;
#endif
            case 16: // DLE
               if (!bLastCharDLE)
               {
                  bLastCharDLE=TRUE;
                  break;
               }
               else
                  bLastCharDLE=FALSE;
               // Fall through

            default:
               if (bLastCharDLE)
               {
 //               ichHayesBuff=0;
                  bLastCharDLE=FALSE;
                  if (buff[i] >= 32 && (szActiveDLECodes[0]=='\0' || strchr((PCHAR)szActiveDLECodes, buff[i]) != NULL))
                  {
                     QueueData(IM_DLEFROMDCE, buff+i, 1);
                     if (fDebug)
                        dprintf("DCE: <DLE> %c\n", buff[i]);
                  }
                  else
                     if (fDebug)
                        dprintf("DCE: <DLE> %c (ignored)\n", buff[i]);
               }
               else if (ichHayesBuff+1 < MAXHAYESMSG)
               {
                  achHayesBuff[ichHayesBuff]=buff[i];
                  ichHayesBuff++;
               }
               break;
            }
         }
         else if (usGlobalState==GS_RECORDING)
         {
            if (bLastCharDLE)
            {
               bLastCharDLE=FALSE;
               if (buff[i]==3) // ETX
               {
//                SetComXONXOFF(FALSE);
                  usGlobalState=GS_DONE;
                  ichHayesBuff=0;
                  ulRecTime+=GetSysMSecs()-ulStartRecTime;
                  usFBuffPos--;  // Remove stored DLE
               }
               else
               {
                  if (buff[i] >= 32)
                  {
                     if (szActiveDLECodes[0]=='\0' || strchr((PCHAR)szActiveDLECodes, buff[i]) != NULL)
                     {
                        QueueData(IM_DLEFROMDCE, buff+i, 1);
                        if (fDebug)
                           dprintf("DCE: <DLE> %c\n", buff[i]);
                     }
                     else
                        if (fDebug)
                           dprintf("DCE: <DLE> %c (ignored)\n", buff[i]);
                     usFBuffPos--;  // Remove stored DLE
                  }
                  else
                  {
                     if (!bDLEConv)
                        achFileB[usFBuffPos++]=buff[i];
                  }
               }
            }
            else
            {
               bLastCharDLE = buff[i] == 16;
               achFileB[usFBuffPos++]=buff[i];
            }

            if (usFBuffPos >= sizeof achFileB || (usGlobalState!=GS_RECORDING && usFBuffPos > 0))
            {
               if (hRec != 0)
                  DosWrite(hRec, achFileB, usFBuffPos, &ulLen);
               usFBuffPos=0;
            }
         }
      }
   }
}
