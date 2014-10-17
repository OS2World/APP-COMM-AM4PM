// Thomas Answering machine for PM

// File:          AM4PM.c
// Description:   The PM code

// History
// 930214 TO      Now it exists...
// 930430 TO      Change sub command interface to subroutines
// 930510 TO      Sets timeout to zero to force reading thread to exit
// 930517 TO      External queue for releasing COM port
// 930525 TO      Can read and create ZyXEL files
// 930528 TO      Speed impovments
// 930529 TO      More speed impovments
// 930530 TO      AMStartListenDLECode can set receive mode
// 930531 TO      RING.CMD renamed to RING.AMC
// 930531 TO      Can start AMC scripts from AM4PMCmd. Calls PLAY.AMC and DELETE.AMC
// 930601 TO      Uses EAs for message length
// 930603 TO      Empties input queue before playing or recording file
// 930604 TO      EA for CID text
// 930605 TO      Calls CID.AMC if a string starting with 'TIME' is received.
// 930607 TO      Can release COM-port from script
// 930613 TO      New functions: AMWriteIni, AMReadIni
// 930614 TO      Uses the variable pool for the variable vAMRings
// 930615 TO      New variable: vAMMsgs
// 930619 TO      Uses container class
// 930620 TO      Saves window pos and restores COM settings for hot handle
// 930620 TO      Restores setting when closing COM port
// 930628 TO      New user interface: container
// 930710 TO      Default position if no info saved
// 930711 TO      Recording of messages
// 930718 TO      AMSendW returns "!" when error
// 931002 TO      New functions: AMInpm AMOutp
// 931003 TO      New function: AMTrimFileEnd
// 931006 TO      AMPlayFile can return user events
// 931011 TO      Calls EXIT.AMC at exit
// 931013 TO      New ZyXEL file formats that store DLE codes
// 940125 TO      New parameter to AMPlayFile, AMWaitDLECode indicating what DLE code to listen for
// 940226 TO      Better Hayes command processing
// 940227 TO      Added AMLog and Rexx exits for SAY and TRC output
// 940312 TO      Cleaning up queue in SendHayes2
// 940522 TO      Calling SendHayes2 in EndRec

#define INCL_WIN
#define INCL_BASE

#include <os2.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "am4pm.h"
#include "am4pmdlg.h"


#define CNRCOLS 4 // Number of fields in container

// Core structure for container
typedef struct
{
   MINIRECORDCORE mc;
   HPOINTER hptr;
   CDATE date;
   CTIME time;
   ULONG ulLen;
   PSZ pszDescr;
   PSZ pszFile;
} MYCORE;

typedef MYCORE * PMYCORE;


MRESULT EXPENTRY ClientWndProc(HWND, USHORT, MPARAM, MPARAM);
MRESULT EXPENTRY Am4PmWndProc(HWND, ULONG, MPARAM, MPARAM);


CHAR szVersion[]="v0.13d (22 May 1994)";
CHAR szIniFile[]="AM4PM.INI";
CHAR szIniAm4Pm[]="AM4PM";
CHAR szMsgFile[]="AM4PM.MSG";
CHAR szAppName[]="Am4Pm";

static CHAR szAppPosKey[]="WinPosOrg";
static CHAR szAppPosOrgKey[]="WinPos";
// static CHAR szAM4PMClass[]="AM4PM";

PCHAR pszDCEVer=NULL;

HAB hab;
HWND hwndFrame, hwndClient, hwndMenu;
PFNWP pfnwpFrame;

volatile PARAMETERBLOCK ParamBlock;
volatile BOOL fDebug=FALSE, fWarnings=FALSE, fAbortCmd=FALSE;
static BOOL fNewStyle=TRUE;
HINI hiniAM;
volatile ULONG ulRecMessages=0;

static HPOINTER hptrMsg;

const CHAR szEALen[]="AM4PM.LEN";
const CHAR szEADescr[]=".SUBJECT";
const CHAR szEAType[]=".TYPE";
CHAR szOurEAType[]="ZyXEL Voice";

static SWCNTRL swctl;
static HSWITCH hswitch;
static PID pid;
static CHAR szTitle[]="Am4Pm";
static CHAR szExtQueue[]="\\QUEUES\\AM4PM";

static PCHAR Rates[]=
{
   "9600",
   "19200",
   "38400",
   "57600",
   "76800"
};


static PCHAR ComDev[]=
{
   "COM1",
   "COM2",
   "COM3",
   "COM4"
};


BOOL IniPutL
(
   CHAR * szKey,
   LONG lValue
)
{
   CHAR szStr[10];

   return PrfWriteProfileString(hiniAM, szIniAm4Pm, szKey, _ltoa(lValue, szStr, 10));
}


LONG IniGetL
(
   CHAR * szKey,
   LONG lDefault
)
{
   CHAR szStr[10], szDef[10];

   PrfQueryProfileString(hiniAM, szIniAm4Pm, szKey, _ltoa(lDefault, szDef, 10), szStr, sizeof szStr);
   return atol(szStr);
}


BOOL IniPutSZ
(
   CHAR * szKey,
   CHAR * szStr
)
{
   return PrfWriteProfileString(hiniAM, szIniAm4Pm, szKey, szStr);
}


PCHAR IniGetSZ
(
   CHAR * szKey,
   CHAR * szDefault
)
{
   CHAR szStr[128], * pszNewStr;

   PrfQueryProfileString(hiniAM, szIniAm4Pm, szKey, szDefault, szStr, sizeof szStr);
   pszNewStr=strdup(szStr);
   return pszNewStr;
}


BOOL IniPutBool
(
   CHAR * szKey,
   BOOL bValue
)
{
   return PrfWriteProfileString(hiniAM, szIniAm4Pm, szKey, bValue ? "Y" : "N");
}


BOOL IniGetBool
(
   CHAR * szKey
)
{
   CHAR szStr[10];

   PrfQueryProfileString(hiniAM, szIniAm4Pm, szKey, "N", szStr, sizeof szStr);
   return *szStr=='Y';
}

static void IniGetSZP
(
   CHAR * szKey,
   CHAR * szDefault,
   CHAR * volatile * ppch
)
{
   if (*ppch)
      free(*ppch);

   *ppch = IniGetSZ(szKey, szDefault);
}

void GetIniFile(void)
{
   ParamBlock.fsFlags = 0l;
   ParamBlock.ulBaud=IniGetL("Baud", 38400l); // Baud rate
   ParamBlock.ulRings=IniGetL("Rings", 1l); // Rings before answer
   ParamBlock.ulLastFileID=IniGetL("LastFileID", 0l); // Last ID used for creating file
   DosEnterCritSec();
   IniGetSZP("Com", "COM1", &ParamBlock.szCom); // Com port
   IniGetSZP("ATHangUp", "AT+VLS=0{OK}", &ParamBlock.szATHangUp);
   IniGetSZP("ATVoiceMode", "AT+VSM=%u{OK}", &ParamBlock.szATVoiceMode);
   IniGetSZP("ATVoiceTX", "AT+VTX{CONNECT}", &ParamBlock.szATVoiceTX);
   IniGetSZP("ATVoiceRX", "AT+VRX{CONNECT}", &ParamBlock.szATVoiceRX);
   IniGetSZP("ATVoiceEndTX", "\x10\x03{VCON}", &ParamBlock.szATVoiceEndTX);
   IniGetSZP("ATVoiceCancelTX", "\x10\x14{VCON}", &ParamBlock.szATVoiceCancelTX);
   IniGetSZP("FileMode", "Z", &ParamBlock.szFileMode);
   IniGetSZP("MessagePattern", "M*.ZVD", &ParamBlock.szMessagePattern);
   IniGetSZP("FileExtension", "ZVD", &ParamBlock.szFileExtension);
   DosExitCritSec();
          
   ParamBlock.fTitles=IniGetBool("Titles");

// fDebug=IniGetBool("Debug");
   fWarnings=IniGetBool("Warnings");
}


static void WriteSettings2IniFile(void)
{
   if (fDebug)
      dprintf("Writing settings to ini file\n");
   IniPutSZ("Com", ParamBlock.szCom); // Com port
   IniPutL("Baud", ParamBlock.ulBaud); // Baud rate
   IniPutL("Rings", ParamBlock.ulRings); // Rings before answer
   IniPutBool("Titles", ParamBlock.fTitles);

   IniPutBool("Debug", fDebug);
   IniPutBool("Warnings", fWarnings);
}


void WriteState2IniFile(void)
{
   if (fDebug)
      dprintf("Writing state to ini file\n");
   IniPutL("LastFileID", ParamBlock.ulLastFileID);
}


#if 0
static ULONG APIENTRY ExcpHandler
(
   PEXCEPTIONREPORTRECORD pExcpRep,
   PEXCEPTIONREGISTRATIONRECORD pExcpReg,
   PCONTEXTRECORD pContext,
   PVOID pData
)
{
   ULONG ulCount;

   dprintf("ExceptioNum=%u\n", pExcpRep->ExceptionNum);
// DosAcknowledgeSignalException(XCPT_SIGNAL_INTR);

   DosResetEventSem(semStopedReading, &ulCount);
   DosPostEventSem(semStopRead);
   if (fDebug)
      dprintf("Waiting for the other thread to die...\n");
   DosWaitEventSem(semStopedReading, 30000l);
   usGlobalState=GS_ENDING;
   DosPostEventSem(semGoOnRead);
   return 0;
}


static void CtrlCSig(void)
{
   EXCEPTIONREGISTRATIONRECORD ExcpReg;

   ExcpReg.prev_structure=NULL;
   ExcpReg.ExceptionHandler=ExcpHandler;

   DosSetExceptionHandler(&ExcpReg);
}
#endif


ULONG GetSysMSecs(void)
{
   ULONG ulTime;

   DosQuerySysInfo(QSV_MS_COUNT, QSV_MS_COUNT, &ulTime, sizeof ulTime);
   return ulTime;
};


void ExtEventThread(PVOID lpVar)
{
   USHORT res;
   ULONG scBuff;
   HQUEUE hQue;
   REQUESTDATA QueueResult;
   BYTE bPrty;
   PVOID pData;

   dprintf("Thread for external events started\n");

   res=DosCreateQueue(&hQue, QUE_FIFO, szExtQueue);
   if (res)
   {
      LogNumMessage(10, res, NULL); // Error creating queue
      return;
   }

   for (;;)
   {
//    DosReadQueue(HQUEUE hq, PREQUESTDATA pRequest, PULONG pcbData, PPVOID ppbuf, ULONG element, BOOL32 wait, PBYTE ppriority, HEV hsem);
      res=DosReadQueue(hQue, &QueueResult, &scBuff, &pData, 0, DCWW_WAIT, &bPrty, 0l);
      if (res)
      {
         LogNumMessage(11, res, NULL); // Error reading queue
         return;
      }

      switch (QueueResult.ulData)
      {
      case EQ_RELCOM:
         QueueData(IM_RELEASE, pData, scBuff);
         break;

      case EQ_STARTAMC:
         QueueData(IM_STARTAMC, pData, scBuff);
         break;

      case EQ_USER:
         QueueData(IM_EXTUSER, pData, scBuff);
         break;

      default:
         LogNumMessage(12, QueueResult.ulData, NULL); // Unkown event code
      }

      if (pData != NULL)
         DosFreeMem(pData);
   }
}


static void SizeTheWindow(HWND hwnd)
{
   SHORT cyTop, cyBottom, cyLeft, cyRight;

   cyLeft=0;
   cyRight=WinQuerySysValue(HWND_DESKTOP, SV_CYMENU)*20;
   cyTop=WinQuerySysValue(HWND_DESKTOP, SV_CYMENU)*7;
   cyBottom=0;

   WinSetWindowPos(hwnd, NULLHANDLE, cyLeft, cyBottom, cyRight-cyLeft, cyTop-cyBottom, SWP_SIZE | SWP_MOVE | SWP_SHOW);
}


int main
(
   USHORT usArgc,
   PCHAR pchArgv[]
)
{
   HMQ     hmq;
   QMSG    qmsg;
   USHORT i;
   static ULONG flFrameFlags = FCF_TITLEBAR | FCF_SYSMENU | FCF_SIZEBORDER |
                               FCF_MINMAX | FCF_ICON |
//                             FCF_MENU |
//                             FCF_NOBYTEALIGN |
                               FCF_ACCELTABLE |
                               FCF_TASKLIST;

   static ULONG flClientStyle = CCS_MULTIPLESEL | CCS_MINIRECORDCORE | WS_GROUP | WS_TABSTOP | WS_VISIBLE;

   hab = WinInitialize(0);

// DosError(FERR_DISABLEHARDERR);

   hiniAM=PrfOpenProfile(hab, szIniFile);
   if (hiniAM==0l)
   {
      LogMessage(26, NULL);
      return 1;
   }

   memset(&ParamBlock, 0, sizeof ParamBlock);

   GetIniFile();

   for (i=1; i<usArgc; i++)
   {
      if (pchArgv[i][0]=='-' || pchArgv[i][0]=='/')
      {
         switch (pchArgv[i][1])
         {
         case 'd':
            fDebug=TRUE;
            break;

         case 'o':
            fNewStyle=FALSE;
            break;

         default:
            if (fDebug)
               dprintf("Unkown switch '%c'\n", pchArgv[i][1]);
         }
      }
   }

   hmq = WinCreateMsgQueue(hab, 0);

   if (fDebug)
   {
      InitDebug();
      dprintf("\nAnswering machine for PM\nVer %s\n", szVersion);
      dprintf("Debugmode=ON\n");
   }

   if (fWarnings)
      dprintf("Warnings=ON\n");

   // Load resources
   hptrMsg = WinLoadPointer(HWND_DESKTOP, NULLHANDLE, IDI_MSG);

   if (fNewStyle)
   {
//    WinRegisterClass(hab, szAM4PMClass, Am4PmWndProc, CS_SIZEREDRAW, 0l);

      hwndFrame=WinCreateStdWindow(HWND_DESKTOP, 0, &flFrameFlags, WC_CONTAINER, szTitle, flClientStyle, 0, IDD_MAIN, &hwndClient);
      WinSetOwner(hwndClient, hwndFrame);
      pfnwpFrame=WinSubclassWindow(hwndFrame, Am4PmWndProc);
      hwndMenu=WinLoadMenu(HWND_OBJECT, 0, IDM_PU);
      WinSendMsg(hwndFrame, WM_CREATED, 0l, 0l);
      if (!WinRestoreWindowPos(szAppName, szAppPosKey, hwndFrame))
         SizeTheWindow(hwndFrame);
   }
   else
   {
      hwndFrame=WinLoadDlg(HWND_DESKTOP, HWND_DESKTOP, (PFNWP)ClientWndProc, 0l, IDD_MAIN, 0l);

      // Attach menu to window
      WinLoadMenu(hwndFrame, 0, IDD_MAIN);
      WinSendMsg(hwndFrame, WM_UPDATEFRAME, MPFROM2SHORT(FCF_MENU, 0), 0l);
   
      // Add to switch list
      WinQueryWindowProcess(hwndFrame, &pid, NULL);
      swctl.hwnd = hwndFrame;                /* window handle      */
      swctl.hwndIcon = 0l;
      swctl.hprog = 0l;                      /* program handle     */
      swctl.idProcess = pid;                 /* process identifier */
      swctl.idSession = 0l;                  /* session identifier */
      swctl.uchVisibility = SWL_VISIBLE;     /* visibility         */
      swctl.fbJump = SWL_JUMPABLE;           /* jump indicator     */
      strcpy(swctl.szSwtitle, szTitle);      /* program name       */
      hswitch = WinAddSwitchEntry(&swctl);
   }


   _beginthread(ExtEventThread, NULL, RSTACKSIZE, NULL);
   _beginthread(WriteModemThread, NULL, RSTACKSIZE, NULL);

// WinSetFocus(HWND_DESKTOP, hwndClient);

   while (WinGetMsg(hab, &qmsg, 0l, 0, 0))
         WinDispatchMsg(hab, &qmsg);

   if (fNewStyle)
      WinStoreWindowPos(szAppName, szAppPosKey, hwndFrame);

   WinDestroyWindow(hwndMenu);
   WinDestroyWindow(hwndFrame);
   WinDestroyMsgQueue(hmq);
   WriteState2IniFile();

   PrfCloseProfile(hiniAM);

   // Release PM resources
   WinDestroyPointer(hptrMsg);
   WinTerminate(hab);
   return 0;
}


MRESULT EXPENTRY AboutDlgProc
(
   HWND hwnd,
   USHORT msg,
   MPARAM mp1,
   MPARAM mp2
)
{
   switch (msg)
   {
   case WM_INITDLG:
      WinSetDlgItemText(hwnd, IDC_VERSION, szVersion);
      if (pszDCEVer != NULL)
         WinSetDlgItemText(hwnd, IDC_DCEVER, pszDCEVer);
      return FALSE;

   case WM_COMMAND:
      switch (COMMANDMSG(&msg)->cmd)
      {
      case DID_OK:
         WinDismissDlg(hwnd, TRUE);
         break;

      case DID_CANCEL:
         WinDismissDlg(hwnd, FALSE);
         break;
      }
      break;
   }
   return WinDefDlgProc(hwnd, msg, mp1, mp2);
}



MRESULT EXPENTRY SettingsDlgProc
(
   HWND hwnd,
   USHORT msg,
   MPARAM mp1,
   MPARAM mp2
)
{
   CHAR szStr[10];
   USHORT i;

   switch (msg)
   {
   case WM_INITDLG:
      WinSendDlgItemMsg(hwnd, IDC_COM, EM_SETTEXTLIMIT, MPFROMSHORT(12), 0l);
      for (i=0; i < sizeof ComDev/sizeof ComDev[0]; i++)
         WinSendDlgItemMsg(hwnd, IDC_COM, LM_INSERTITEM, MPFROMSHORT(LIT_END), MPFROMP(ComDev[i]));

      WinSendDlgItemMsg(hwnd, IDC_BAUD, EM_SETTEXTLIMIT, MPFROMSHORT(8), 0l);
      for (i=0; i < sizeof Rates/sizeof Rates[0]; i++)
         WinSendDlgItemMsg(hwnd, IDC_BAUD, LM_INSERTITEM, MPFROMSHORT(LIT_END), MPFROMP(Rates[i]));

      WinSetDlgItemText(hwnd, IDC_COM, ParamBlock.szCom);
      WinSetDlgItemText(hwnd, IDC_BAUD, _ltoa(ParamBlock.ulBaud, szStr, 10));
      WinSendDlgItemMsg(hwnd, IDC_RINGS, SPBM_SETLIMITS, MPFROMLONG(99l), MPFROMLONG(1l));
      WinSendDlgItemMsg(hwnd, IDC_RINGS, SPBM_SETCURRENTVALUE, MPFROMLONG(ParamBlock.ulRings), NULL);
      WinSendDlgItemMsg(hwnd, IDC_TITLES, BM_SETCHECK, MPFROM2SHORT(ParamBlock.fTitles, 0), NULL);
      return FALSE;

   case WM_COMMAND:
      switch (COMMANDMSG(&msg)->cmd)
      {
      case DID_OK:
         WinQueryDlgItemText(hwnd, IDC_COM, sizeof szStr, szStr);
         free(ParamBlock.szCom);
         ParamBlock.szCom=strdup(szStr);
         WinQueryDlgItemText(hwnd, IDC_BAUD, sizeof szStr, szStr);
         ParamBlock.ulBaud=atol(szStr);
         WinSendDlgItemMsg(hwnd, IDC_RINGS, SPBM_QUERYVALUE, MPFROMP(&ParamBlock.ulRings), MPFROM2SHORT(0, 0));
         ParamBlock.fTitles=(USHORT)WinSendDlgItemMsg(hwnd, IDC_TITLES, BM_QUERYCHECK, NULL, NULL);
         WinDismissDlg(hwnd, TRUE);

         WriteSettings2IniFile();
         QueueData(IM_INICHANGED, NULL, 0l);
         break;

      case DID_CANCEL:
         WinDismissDlg(hwnd, FALSE);
         break;
      }
      break;
   }
   return WinDefDlgProc(hwnd, msg, mp1, mp2);
}


static void  AddMtoCnr
(
   HWND hwndCnr,
   PEAOP2 peaop2
)
{
   CHAR szDescr[MAXDESCR];
   PFILEFINDBUF3 pfb;
   ULONG ulLen, ulTimelength;
   PFEA2LIST pfea2list;
   PFEA2 pfea2;
   PMYCORE pmc;
   RECORDINSERT ri;
   PCHAR pszName;

   pfb=(PFILEFINDBUF3)(peaop2+1);
   pfea2list=(PFEA2LIST)&pfb->cchName;
   pszName=(PCHAR)pfea2list+pfea2list->cbList+1;
   pfea2=pfea2list->list;

   if (pfea2->cbValue == 2*sizeof (USHORT) + sizeof (ULONG) && *(PUSHORT)(pfea2->szName+pfea2->cbName+1) == EAT_BINARY)
      ulTimelength=*(PULONG)(pfea2->szName+pfea2->cbName+1+2*sizeof(USHORT));
   else
      ulTimelength=pfb->cbFile / 2000; // Estimate time based on size

   pfea2= (PFEA2)((PCHAR)pfea2+pfea2->oNextEntryOffset);

   *szDescr='\0';
   if (pfea2->cbValue >= 2*sizeof (USHORT) && *(PUSHORT)(pfea2->szName+pfea2->cbName+1) == EAT_ASCII)
   {
      ulLen = *(PUSHORT)(pfea2->szName+pfea2->cbName+1+sizeof(USHORT));
      if (ulLen > 0 && ulLen <= pfea2->cbValue - 2*sizeof (USHORT))
      {
         ulLen = min(ulLen, sizeof szDescr-1);
         memcpy(szDescr, pfea2->szName+pfea2->cbName+1+2*sizeof(USHORT), ulLen);
         szDescr[ulLen]='\0';
      }
   }

   pmc = WinSendMsg(hwndCnr, CM_ALLOCRECORD, MPFROMLONG(sizeof (MYCORE) - sizeof (MINIRECORDCORE)), MPFROMSHORT(1));
   pmc->hptr=hptrMsg;
   pmc->pszDescr=strdup(szDescr);
   pmc->pszFile=strdup(pszName);
   pmc->date.year=pfb->fdateLastWrite.year+1980;
   pmc->date.month=pfb->fdateLastWrite.month;
   pmc->date.day=pfb->fdateLastWrite.day;
   pmc->time.hours=pfb->ftimeLastWrite.hours;
   pmc->time.minutes=pfb->ftimeLastWrite.minutes;
   pmc->time.seconds=0;
   pmc->ulLen=ulTimelength;
   ri.cb = sizeof ri;
   ri.pRecordOrder = (PRECORDCORE)CMA_END;
   ri.pRecordParent = NULL;
   ri.zOrder = CMA_TOP;
   ri.cRecordsInsert = 1;
   ri.fInvalidateRecord = TRUE;

   WinSendMsg(hwndCnr, CM_INSERTRECORD, MPFROMP(pmc), MPFROMP(&ri));
}


static PGEA2LIST GetGEA2List
(
   void
)
{
   PGEA2LIST pgea2list;
   PGEA2 pgea2;

   pgea2list = malloc(2*(sizeof *pgea2list - 1 + 3) + sizeof szEALen + sizeof szEADescr); // Allocates a litle extra (I'm lazy)


   pgea2 = pgea2list->list;

   pgea2->oNextEntryOffset = (sizeof *pgea2 - 1 + sizeof szEALen + 3) & ~3;
   pgea2->cbName = sizeof szEALen-1;
   memcpy(pgea2->szName, szEALen, sizeof szEALen);

   pgea2 = (PGEA2)((PCHAR)pgea2+pgea2->oNextEntryOffset);

   pgea2->oNextEntryOffset = 0;
   pgea2->cbName = sizeof szEADescr-1;
   memcpy(pgea2->szName, szEADescr, sizeof szEADescr);

   pgea2list->cbList = ((PCHAR)pgea2+sizeof *pgea2 - 1 + sizeof szEADescr) - (PCHAR)pgea2list->list;

   return pgea2list;
}


static void ScanDir4M
(
   HWND hwndCnr
)
{
   HDIR hDir=HDIR_CREATE;
   ULONG usFiles=1, usFound=0, res, ulBuffLen;
   PEAOP2 peaop2;
   PMYCORE pmc=NULL;
   CHAR szFilePattern[80];

   WinEnableWindowUpdate(hwndCnr, FALSE);

   // Remove old contents
   for (;;)
   {
      pmc = WinSendMsg(hwndCnr, CM_QUERYRECORD, MPFROMP(pmc), MPFROM2SHORT(pmc ? CMA_NEXT : CMA_FIRST, CMA_ITEMORDER));
      if (pmc == NULL || (ULONG)pmc == 0xffff)
         break;

      if (pmc->pszDescr != NULL)
         free(pmc->pszDescr);
      free(pmc->pszFile);
   }

   WinSendMsg(hwndCnr, CM_REMOVERECORD, 0, MPFROM2SHORT(0, CMA_FREE | CMA_INVALIDATE));

   ulBuffLen = sizeof *peaop2 + 2*sizeof(GEA2LIST) + sizeof(FILEFINDBUF3) + 6*sizeof (ULONG) + 100;
   peaop2 = malloc(ulBuffLen);
   peaop2->fpGEA2List = GetGEA2List();

   sprintf(szFilePattern, ParamBlock.szMessagePattern, "*");
   res=DosFindFirst(szFilePattern, &hDir, FILE_NORMAL, peaop2, ulBuffLen, &usFiles, FIL_QUERYEASFROMLIST);
   while (res==0)
   {
      usFound++;

      AddMtoCnr(hwndCnr, peaop2);

      res=DosFindNext(hDir, peaop2, ulBuffLen, &usFiles);
   }

   ulRecMessages=usFound;
   free(peaop2->fpGEA2List);
   free(peaop2);

   WinShowWindow(hwndCnr, TRUE);

   if (usFound)
      DosFindClose(hDir);
}


#if 0
static void AddMFile
(
   HWND hwndLB,
   PCHAR pszFile
)
{
   HDIR hDir=HDIR_CREATE;
   ULONG usFiles=1, res, ulBuffLen;
   PEAOP2 peaop2;

   ulBuffLen = sizeof *peaop2 + 2*sizeof(GEA2LIST) + sizeof(FILEFINDBUF3) + 6*sizeof (ULONG) + 100;
   peaop2 = malloc(ulBuffLen);
   peaop2->fpGEA2List = GetGEA2List();

   res=DosFindFirst(pszFile, &hDir, FILE_NORMAL, peaop2, ulBuffLen, &usFiles, FIL_QUERYEASFROMLIST);
   if (res==0)
   {
      AddMtoLB(hwndLB, peaop2);
      DosFindClose(hDir);
   }

   free(peaop2->fpGEA2List);
   free(peaop2);
}
#endif

static void PlaySelected
(
   HWND hwndCnr
)
{
   PMYCORE pmc=(PMYCORE)CMA_FIRST;

   for (;;)
   {
      pmc = WinSendMsg(hwndCnr, CM_QUERYRECORDEMPHASIS, MPFROMP(pmc), MPFROMSHORT(CRA_SELECTED));
      if (pmc == NULL || (ULONG)pmc == 0xffff)
         break;

      QueueData(IM_PLAY, pmc->pszFile, strlen(pmc->pszFile)+1);
   }
}


static void DeleteSelected
(
   HWND hwndCnr
)
{
   PMYCORE pmc;

   WinEnableWindowUpdate(hwndCnr, FALSE);
   for (;;)
   {
      pmc=(PMYCORE)CMA_FIRST;

      pmc = WinSendMsg(hwndCnr, CM_QUERYRECORDEMPHASIS, MPFROMP(pmc), MPFROMSHORT(CRA_SELECTED));
      if (pmc == NULL || (ULONG)pmc == 0xffff)
         break;

      QueueData(IM_DELETE, pmc->pszFile, strlen(pmc->pszFile)+1);

      if (pmc->pszDescr != NULL)
         free(pmc->pszDescr);

      WinSendMsg(hwndCnr, CM_REMOVERECORD, MPFROMP(&pmc), MPFROM2SHORT(1, CMA_FREE | CMA_INVALIDATE));
   }
   WinShowWindow(hwndCnr, TRUE);
}


static void InitCnr
(
   HWND hwndCnr
)
{
   CNRINFO cnrInfo;
   PFIELDINFO pfiStart, pfi;
   FIELDINFOINSERT fii;
   USHORT res;

   pfiStart = WinSendMsg(hwndCnr, CM_ALLOCDETAILFIELDINFO, MPFROMSHORT(CNRCOLS), 0l);
   pfi = pfiStart;

   #if 0
   pfi->flData = CFA_BITMAPORICON | CFA_VCENTER | CFA_CENTER | CFA_SEPARATOR | CFA_FIREADONLY;
   pfi->flTitle = CFA_FITITLEREADONLY;
   pfi->pTitleData = "";
   pfi->offStruct = sizeof (MINIRECORDCORE);
   pfi = pfi->pNextFieldInfo;
   #endif

   pfi->flData = CFA_DATE | CFA_VCENTER | CFA_LEFT | CFA_HORZSEPARATOR | CFA_SEPARATOR | CFA_FIREADONLY;
   pfi->flTitle = CFA_FITITLEREADONLY;
   pfi->pTitleData = "Date";
   pfi->offStruct = sizeof (MINIRECORDCORE) + sizeof (HPOINTER);

   pfi = pfi->pNextFieldInfo;
   pfi->flData = CFA_TIME | CFA_VCENTER | CFA_LEFT | CFA_HORZSEPARATOR| CFA_SEPARATOR | CFA_FIREADONLY;
   pfi->flTitle = CFA_FITITLEREADONLY;
   pfi->pTitleData = "Time";
   pfi->offStruct = sizeof (MINIRECORDCORE) + sizeof (HPOINTER) + sizeof (CDATE);

   pfi = pfi->pNextFieldInfo;
   pfi->flData = CFA_ULONG | CFA_VCENTER | CFA_RIGHT | CFA_HORZSEPARATOR| CFA_SEPARATOR | CFA_FIREADONLY;
   pfi->flTitle = CFA_FITITLEREADONLY;
   pfi->pTitleData = "Length";
   pfi->offStruct = sizeof (MINIRECORDCORE) + sizeof (HPOINTER) + sizeof (CDATE) + sizeof(CTIME);

   pfi = pfi->pNextFieldInfo;
   pfi->flData = CFA_STRING | CFA_VCENTER | CFA_LEFT | CFA_HORZSEPARATOR| CFA_SEPARATOR | CFA_FIREADONLY;
   pfi->flTitle = CFA_FITITLEREADONLY;
   pfi->pTitleData = "Description";
   pfi->offStruct = sizeof (MINIRECORDCORE) + sizeof (HPOINTER) + sizeof (CDATE) + sizeof(CTIME) + sizeof (ULONG);

   fii.cb = sizeof fii;
   fii.pFieldInfoOrder = (PFIELDINFO)CMA_END;
   fii.cFieldInfoInsert = CNRCOLS;
   fii.fInvalidateFieldInfo = TRUE;

   res=(USHORT)WinSendMsg(hwndCnr, CM_INSERTDETAILFIELDINFO, MPFROMP(pfiStart), MPFROMP(&fii));
   if (fDebug)
      dprintf("%u fields added to container\n", res);

   cnrInfo.cb = sizeof cnrInfo;
   cnrInfo.flWindowAttr = CV_DETAIL;
   cnrInfo.slBitmapOrIcon.cx=WinQuerySysValue(HWND_DESKTOP, SV_CYMENU)*4/5;
   cnrInfo.slBitmapOrIcon.cy=WinQuerySysValue(HWND_DESKTOP, SV_CYMENU)*4/5;

   if (ParamBlock.fTitles)
      cnrInfo.flWindowAttr |= CA_DETAILSVIEWTITLES;

// cnrInfo.pFieldInfoObject = afi;

   WinSendMsg(hwndCnr, CM_SETCNRINFO, MPFROMP(&cnrInfo), MPFROMLONG(CMA_FLWINDOWATTR | CMA_SLBITMAPORICON));
}


static void UpdateViewSettings
(
   HWND hwndCnr
)
{
   CNRINFO cnrInfo;

   WinSendMsg(hwndCnr, CM_QUERYCNRINFO, MPFROMP(&cnrInfo), MPFROMSHORT(sizeof cnrInfo));

   if (ParamBlock.fTitles)
      cnrInfo.flWindowAttr |= CA_DETAILSVIEWTITLES;
   else
      cnrInfo.flWindowAttr &= ~CA_DETAILSVIEWTITLES;

   WinSendMsg(hwndCnr, CM_SETCNRINFO, MPFROMP(&cnrInfo), MPFROMLONG(CMA_FLWINDOWATTR));
}


static void DoARecording
(
   HWND hwnd
)
{
   FILEDLG pfdFiledlg;
    
   static char pszTitle[] = "Record File";
   char pszFullFile[CCHMAXPATH];
   HWND hwndDlg;
   static char * apszFilter[] = {szOurEAType, NULL};
   strcpy(pszFullFile, "*.");
   strcat(pszFullFile, ParamBlock.szFileExtension);
 
   memset(&pfdFiledlg, 0, sizeof(FILEDLG));
 
   pfdFiledlg.cbSize = sizeof(FILEDLG);
   pfdFiledlg.fl = FDS_CENTER | FDS_SAVEAS_DIALOG | FDS_ENABLEFILELB;
   pfdFiledlg.pszTitle = pszTitle;
   strcpy(pfdFiledlg.szFullFile, pszFullFile);
   pfdFiledlg.papszITypeList=(PAPSZ)apszFilter;
// pfdFiledlg.pszIType=apszFilter[0];
 
   hwndDlg = WinFileDlg(HWND_DESKTOP, hwnd, &pfdFiledlg);
 
   if (hwndDlg && (pfdFiledlg.lReturn == DID_OK))
   {
      if (fDebug)
         dprintf("File '%s' selected\n", pfdFiledlg.szFullFile);
      QueueData(IM_RECORD, pfdFiledlg.szFullFile, strlen(pfdFiledlg.szFullFile)+1);
   }
}


static void DoPlayFile
(
   HWND hwnd
)
{
   FILEDLG pfdFiledlg;
    
   static char pszTitle[] = "Play File";
   char pszFullFile[CCHMAXPATH];
   HWND hwndDlg;
   static char * apszFilter[] = {szOurEAType, NULL};

   strcpy(pszFullFile, "*.");
   strcat(pszFullFile, ParamBlock.szFileExtension);
 
   memset(&pfdFiledlg, 0, sizeof(FILEDLG));
 
   pfdFiledlg.cbSize = sizeof(FILEDLG);
   pfdFiledlg.fl = FDS_CENTER | FDS_OPEN_DIALOG;
   pfdFiledlg.pszTitle = pszTitle;
   strcpy(pfdFiledlg.szFullFile, pszFullFile);
   pfdFiledlg.papszITypeList=(PAPSZ)apszFilter;
// pfdFiledlg.pszIType=apszFilter[0];
 
   hwndDlg = WinFileDlg(HWND_DESKTOP, hwnd, &pfdFiledlg);
 
   if (hwndDlg && (pfdFiledlg.lReturn == DID_OK))
   {
      if (fDebug)
         dprintf("File '%s' selected\n", pfdFiledlg.szFullFile);
      QueueData(IM_PLAY, pfdFiledlg.szFullFile, strlen(pfdFiledlg.szFullFile)+1);
   }
}


MRESULT EXPENTRY ClientWndProc
(
   HWND   hwnd,
   USHORT msg,
   MPARAM mp1,
   MPARAM mp2
)
{
   HWND hwndTmp;
   SWP swp;
   RECTL rectl, rectl2;
   HPS hps;

   switch (msg)
   {
   case WM_INITDLG:
      WinRestoreWindowPos(szAppName, szAppPosOrgKey, hwnd);
      hwndTmp=WinWindowFromID(hwnd, IDC_CALLLIST);
      InitCnr(hwndTmp);
      ScanDir4M(hwndTmp);
      return (MRESULT)TRUE;

   case WM_CLOSE:
      if (fDebug)
         dprintf("Close pressed\n");
      QueueData(IM_DOWN, NULL, 0l);
      WinStoreWindowPos(szAppName, szAppPosOrgKey, hwnd);
      break;

   case WM_PAINT:
      WinQueryWindowPos(hwnd, &swp);
      if (swp.fl & SWP_MINIMIZE)
      {
         WinQueryWindowRect(hwnd, &rectl);
         hps=WinBeginPaint(hwnd, NULLHANDLE, &rectl2);
         WinFillRect(hps, &rectl2, ulRecMessages ? CLR_RED : CLR_GREEN);
         WinEndPaint(hps);
         return 0;
      }
      break;

   case WM_COMMAND:
      switch (COMMANDMSG(&msg)->cmd)
      {
      case 1:  // Ok
      case DID_CANCEL:
         return 0;

      case IDM_ABOUT:
         WinDlgBox(HWND_DESKTOP, hwnd, (PFNWP)AboutDlgProc, 0l, IDD_ABOUT, 0l);
         return 0;

      case IDM_SETTINGS:
         if (WinDlgBox(HWND_DESKTOP, hwnd, (PFNWP)SettingsDlgProc, 0l, IDD_SETTINGS, 0l))
            UpdateViewSettings(WinWindowFromID(hwnd, IDC_CALLLIST));
         return 0;

      case IDC_ABORT:
         if (fDebug)
            dprintf("Abort pressed\n");
         fAbortCmd=TRUE;
         WinEnableWindow(WinWindowFromID(hwnd, IDC_ABORT), FALSE);
         QueueData(IM_ABORT, NULL, 0l);
         return 0;

      case IDC_RELEASE:
         QueueData(IM_RELEASE, NULL, 0l);
         return 0;

      case IDC_PLAY:
         PlaySelected(WinWindowFromID(hwnd, IDC_CALLLIST));
         return 0;

      case IDC_DELETE:
         DeleteSelected(WinWindowFromID(hwnd, IDC_CALLLIST));
         return 0;

      case IDM_RECORD:
         DoARecording(hwnd);
         return 0;

      case IDM_PLAYFILE:
         DoPlayFile(hwnd);
         return 0;
      }
      break;

   case WM_CONTROL:
      switch (SHORT1FROMMP(mp1))
      {
      case IDC_CALLLIST:
         if (SHORT2FROMMP(mp1)==CN_ENTER)
            PlaySelected(WinWindowFromID(hwnd, IDC_CALLLIST));
         break;
      }
      break;

   case WMU_THREADSUP:
      if (fDebug)
         dprintf("Got THREADSUP\n");
      return 0;
   case WMU_BUSYCMD:
      WinEnableWindow(WinWindowFromID(hwnd, IDC_ABORT), TRUE);
      WinEnableWindow(WinWindowFromID(hwnd, IDC_RELEASE), FALSE);
      return 0;
   case WMU_IDLECMD:
      WinEnableWindow(WinWindowFromID(hwnd, IDC_ABORT), FALSE);
      WinEnableWindow(WinWindowFromID(hwnd, IDC_RELEASE), TRUE);
      WinEnableWindow(WinWindowFromID(hwnd, IDC_PLAY), TRUE);
      WinEnableWindow(WinWindowFromID(hwnd, IDC_DELETE), TRUE);
      return 0;
   case WMU_NOCOM:
      WinEnableWindow(WinWindowFromID(hwnd, IDC_RELEASE), FALSE);
      WinEnableWindow(WinWindowFromID(hwnd, IDC_PLAY), FALSE);
      WinEnableWindow(WinWindowFromID(hwnd, IDC_DELETE), FALSE);
      return 0;
   case WMU_THREADSDOWN:
      if (fDebug)
         dprintf("Got THREADSDOWN\n");
      WinPostMsg(hwnd, WM_QUIT, 0L, 0L);
      return 0;
   case WMU_STATE:
      WinSetDlgItemText(hwnd, IDC_STATE, PVOIDFROMMP(mp1));
      free(PVOIDFROMMP(mp1));
      return 0;
   case WMU_LASTDCE:
      WinSetDlgItemText(hwnd, IDC_DCERESPONSE, PVOIDFROMMP(mp1));
      free(PVOIDFROMMP(mp1));
      return 0;
   case WMU_UPDLIST:
      if (fDebug)
         dprintf("Got UPDLIST\n");
      ScanDir4M(WinWindowFromID(hwnd, IDC_CALLLIST));
      return 0;
   #if 0
   case WMU_ADDLIST:
      if (fDebug)
         dprintf("Got ADDLIST\n");
      AddMFile(WinWindowFromID(hwnd, IDC_CALLLIST), PVOIDFROMMP(mp1));
      free(PVOIDFROMMP(mp1));
      return 0;
   #endif
   }
   return WinDefDlgProc (hwnd, msg, mp1, mp2);
}


MRESULT EXPENTRY Am4PmWndProc
(
   HWND   hwnd,
   ULONG  msg,
   MPARAM mp1,
   MPARAM mp2
)
{
   PSWP pswp;
   POINTL ptl;
   MRESULT mr;
   static LONG lCyMenu;


   switch (msg)
   {
   case WM_CREATED:
      lCyMenu=WinQuerySysValue(HWND_DESKTOP, SV_CYMENU);
      WinCreateWindow(hwnd, WC_STATIC, "", WS_VISIBLE | SS_TEXT | DT_CENTER, 0, 0, 0, 0, hwnd, HWND_TOP, IDC_DCERESPONSE, NULL, NULL);
      WinCreateWindow(hwnd, WC_STATIC, "Started", WS_VISIBLE | SS_TEXT, 0, 0, 0, 0, hwnd, HWND_TOP, IDC_STATE, NULL, NULL);

      if (fDebug)
         dprintf("Got CREATED\n");
      InitCnr(hwndClient);
      ScanDir4M(hwndClient);
      return 0;

   case WM_CLOSE:
      if (fDebug)
         dprintf("Close pressed\n");
      QueueData(IM_DOWN, NULL, 0l);
      return 0;

   #if 0
   case WM_PAINT:
      WinQueryWindowPos(hwnd, &swp);
      if (swp.fl & SWP_MINIMIZE)
      {
         WinQueryWindowRect(hwnd, &rectl);
         hps=WinBeginPaint(hwnd, NULLHANDLE, &rectl2);
         WinFillRect(hps, &rectl2, ulRecMessages ? CLR_RED : CLR_GREEN);
         WinEndPaint(hps);
         return 0;
      }
      break;
   #endif

   case WM_SYSCOMMAND:
      if (SHORT1FROMMP(mp1) == SC_CLOSE)
      {
         if (fDebug)
            dprintf("Close pressed\n");
         QueueData(IM_DOWN, NULL, 0l);
         return 0;
      }
      break;

   case WM_COMMAND:
      switch (COMMANDMSG(&msg)->cmd)
      {
      case 1:  // Ok
      case DID_CANCEL:
         return 0;

      case IDM_ABOUT:
         WinDlgBox(HWND_DESKTOP, hwnd, (PFNWP)AboutDlgProc, 0l, IDD_ABOUT, 0l);
         return 0;

      case IDM_SETTINGS:
         if (WinDlgBox(HWND_DESKTOP, hwnd, (PFNWP)SettingsDlgProc, 0l, IDD_SETTINGS, 0l))
            UpdateViewSettings(hwndClient);
         return 0;

      case IDM_ABORT:
         if (fDebug)
            dprintf("Abort pressed\n");
         fAbortCmd=TRUE;
         WinEnableMenuItem(hwndMenu, IDM_ABORT, FALSE);
         WinEnableMenuItem(hwndMenu, IDM_STOP, FALSE);
         QueueData(IM_ABORT, NULL, 0l);
         return 0;

      case IDM_RELEASE:
         QueueData(IM_RELEASE, NULL, 0l);
         return 0;

      case IDM_STOP:
         QueueData(IM_STOP, NULL, 0l);
         return 0;

      case IDM_PLAY:
         PlaySelected(hwndClient);
         return 0;

      case IDM_DELETE:
         DeleteSelected(hwndClient);
         return 0;

      case IDM_RECORD:
         DoARecording(hwndClient);
         return 0;

      case IDM_PLAYFILE:
         DoPlayFile(hwndClient);
         return 0;
      }
      break;

   case WM_CONTROL:
      if (SHORT1FROMMP(mp1) == FID_CLIENT)
      {
         switch(SHORT2FROMMP(mp1))
         {
         case CN_ENTER:
            PlaySelected(hwndClient);
            return 0;

         case CN_CONTEXTMENU:
            if (fDebug)
               dprintf("WM_CONTEXTMENU\n");
            WinQueryMsgPos(hab, &ptl);
            WinPopupMenu(HWND_DESKTOP, hwnd, hwndMenu, ptl.x, ptl.y, IDM_PLAY, PU_POSITIONONITEM | PU_HCONSTRAIN | PU_VCONSTRAIN | PU_KEYBOARD | PU_MOUSEBUTTON1);
            return 0;
         }
         break;
      }
      break;

   case WM_OPEN:
      if (fDebug)
         dprintf("WM_OPEN\n");
      break;

   case WMU_THREADSUP:
      if (fDebug)
         dprintf("Got THREADSUP\n");
      return 0;

   case WMU_BUSYCMD:
      WinEnableMenuItem(hwndMenu, IDM_STOP, TRUE);
      WinEnableMenuItem(hwndMenu, IDM_ABORT, TRUE);
      WinEnableMenuItem(hwndMenu, IDM_RELEASE, FALSE);
      return 0;

   case WMU_IDLECMD:
      WinEnableMenuItem(hwndMenu, IDM_STOP, FALSE);
      WinEnableMenuItem(hwndMenu, IDM_ABORT, FALSE);
      WinEnableMenuItem(hwndMenu, IDM_RELEASE, TRUE);
      WinEnableMenuItem(hwndMenu, IDM_PLAY, TRUE);
      WinEnableMenuItem(hwndMenu, IDM_DELETE, TRUE);
      return 0;

   case WMU_NOCOM:
      WinEnableMenuItem(hwndMenu, IDM_RELEASE, FALSE);
      WinEnableMenuItem(hwndMenu, IDM_PLAY, FALSE);
      WinEnableMenuItem(hwndMenu, IDM_DELETE, FALSE);
      return 0;

   case WMU_THREADSDOWN:
      if (fDebug)
         dprintf("Got THREADSDOWN\n");
      WinPostMsg(hwnd, WM_QUIT, 0L, 0L);
      return 0;
   case WMU_STATE:
      WinSetDlgItemText(hwnd, IDC_STATE, PVOIDFROMMP(mp1));
      free(PVOIDFROMMP(mp1));
      return 0;
   case WMU_LASTDCE:
      WinSetDlgItemText(hwnd, IDC_DCERESPONSE, PVOIDFROMMP(mp1));
      free(PVOIDFROMMP(mp1));
      return 0;
   case WMU_UPDLIST:
      if (fDebug)
         dprintf("Got UPDLIST\n");
      ScanDir4M(hwndClient);
      return 0;

   case WM_CALCFRAMERECT:
      if (!mp2)
         ((PRECTL)(mp1))->yBottom -= lCyMenu;
      mr = pfnwpFrame(hwnd, msg, mp1, mp2);
      if (mr && mp2)
         ((PRECTL)(mp1))->yBottom += lCyMenu;
      return mr;

   case WM_FORMATFRAME:
      mr = pfnwpFrame(hwnd, msg, mp1, mp2);
      pswp = ((PSWP)mp1) + (SHORT)mr - 1; // Point to client window

//    pswp[1] = pswp[0];

      pswp[2].cy = lCyMenu;
      pswp[2].cx = pswp[0].cx/2;
      pswp[2].y  = pswp[0].y;
      pswp[2].x  = pswp[0].x;
      pswp[2].fl = pswp[0].fl;
      pswp[2].hwnd = WinWindowFromID(hwnd, IDC_STATE);
      pswp[2].hwndInsertBehind = WinWindowFromID(hwnd, IDC_DCERESPONSE);

      pswp[1].cy = lCyMenu;
      pswp[1].cx = pswp[0].cx - pswp[2].cx;
      pswp[1].y  = pswp[0].y;
      pswp[1].x  = pswp[0].x + pswp[0].cx/2;
      pswp[1].fl = pswp[0].fl;
      pswp[1].hwnd = WinWindowFromID(hwnd, IDC_DCERESPONSE);
      pswp[1].hwndInsertBehind = pswp[0].hwndInsertBehind;

      pswp[0].y += lCyMenu;
      pswp[0].cy -= lCyMenu;


      return MRFROMSHORT(((SHORT)mr)+2);

   case WM_QUERYFRAMECTLCOUNT:
      mr = pfnwpFrame(hwnd, msg, mp1, mp2);
      return MRFROMSHORT(((SHORT)mr)+2);

   #if 0
   case WMU_ADDLIST:
      if (fDebug)
         dprintf("Got ADDLIST\n");
      AddMFile(hwndClient, PVOIDFROMMP(mp1));
      free(PVOIDFROMMP(mp1));
      return 0;
   #endif
   }

   return pfnwpFrame(hwnd, msg, mp1, mp2);
}
