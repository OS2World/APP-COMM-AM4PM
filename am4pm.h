// Thomas Answering machine for PM

// File:          AM4PMR.h
// Description:   Header file

// History
// 930206 TO      Now it exists...


#define MAXHAYESMSG     80       // Maximum length of hayes message
#define RECBUFFLEN      64       // Number of characters to receive at the same time
#define RECFBUFFLEN     1000     // Number of characters to write to disk at the same time
#define SENDBUFFLEN     1000     // Number of characters to receive at the same time
#define MAXWAITDCE      60000l
#define SENDTO          3        // Timeout when sending
#define READTO          5        // Timeout when receiving
#define RSTACKSIZE      8192     // Stack size of other threads
#define MAXCMD          5        // Maximum length of command string incl null
#define WAITRETRYCOM    10000l   // Time retries to open COM port
#define MAXRINGDELAY    9000l    // Max delay between rings for same call
#define MAXDESCR        80       // Max length of description text
#define DLECODES        50       // Max number of different DLE codes

// Values of usGlobalState
#define GS_START        0        // Initializing
#define GS_READY        1        // Waiting for call
#define GS_ENDING       2        // Ending
#define GS_INITREC      3        // Starting to record. Waiting for CONNECT.
#define GS_RECORDING    4        // Saving to file
#define GS_DONE         5        // Recording or playback finnished
#define GS_INITPLAY     6        // Starting to play.

// Types of internal messages
#define IM_CMDFLAG      0x80000000l       // Flag to indicate command
#define IM_NULL         0l                // No message
#define IM_DOWN         (1l+IM_CMDFLAG)   // The thread should be shut down
#define IM_DLEFROMDCE   2l                // DLE code from DCE
#define IM_STRFROMDCE   4l                // String from DCE
#define IM_XOFF         8l                // Stop sending to DCE
#define IM_XON          16l               // Start sending to DCE
#define IM_ABORT        32l               // bAbortCmd is set
#define IM_PLAY         (64l+IM_CMDFLAG)  // Play file found in list
#define IM_DELETE       (128l+IM_CMDFLAG) // Delete file
#define IM_RELEASE      (256l+IM_CMDFLAG) // Release COM port
#define IM_STARTAMC     (512l+IM_CMDFLAG) // Start AMC script
#define IM_INICHANGED   (1024l+IM_CMDFLAG) // Start AMC script
#define IM_RECORD       (2048l+IM_CMDFLAG) // Start recording
#define IM_STOP         (4096l)           // Stop recording
#define IM_EXTUSER      (8192l)           // External user message


// Messages to the client window
#define  WMU_THREADSUP     (WM_USER+0)
#define  WMU_THREADSDOWN   (WM_USER+1)
#define  WMU_STATE         (WM_USER+2)
#define  WMU_LASTDCE       (WM_USER+3)
#define  WMU_IDLECMD       (WM_USER+4)
#define  WMU_BUSYCMD       (WM_USER+5)
#define  WMU_UPDLIST       (WM_USER+6)
// #define WMU_ADDLIST     (WM_USER+7)
#define  WMU_NOCOM         (WM_USER+8)
#define  WM_CREATED        (WM_USER+9)


// Return codes from RING.AM
#define AMR_NORMAL               0  // Normal. Only AT+VLS=0 is done before waiting
#define AMR_REINIT               1  // Full init. Reinit COM-port and everything.
#define AMR_EXIT                 2  // Terminate AM4PM.

// Event codes for the external queue
#define EQ_RELCOM                0  // Release the COM port for 30s
#define EQ_STARTAMC              1  // Start AMC script
#define EQ_USER                  2  // User defined message

// Compression types used by ZyXEL
#define ZCOMP_CELP      1
#define ZCOMP_ADPCM2    2
#define ZCOMP_ADPCM3    3


// Types
typedef struct // Parameters
{
   ULONG fsFlags;                // Flags of type PF_*
   PCHAR szCom;                  // Com device
   ULONG ulBaud;                 // Baud rate
   ULONG ulRings;                // Rings before answer
   ULONG ulLastFileID;           // Number of last file
   BOOL fTitles;                 // If titles in container should be displayed
   PCHAR szFileMode;             // Z(yXEL) or G(eneric)
   PCHAR szMessagePattern;       // Message file pattern
   PCHAR szFileExtension;        // Message file extension
   PCHAR szATHangUp;
   PCHAR szATVoiceMode;
   PCHAR szATVoiceTX;
   PCHAR szATVoiceRX;
   PCHAR szATVoiceEndTX;
   PCHAR szATVoiceCancelTX;
} PARAMETERBLOCK;


#pragma pack(1)
typedef struct
{
   USHORT usWTime, usRTime;
   UCHAR chFlag1, chFlag2, chFlag3, chErr, chBreak, chXON, chXOFF;
} DCBTYPE;

typedef struct
{
   CHAR achTitle[5]; // Must be 'ZyXEL'
   CHAR chTwo;       // Must be 2
   USHORT usRes1;
   USHORT usRes2;
   USHORT usType;    // ZCOMP_* - 1
   USHORT usRes3;
   USHORT usRes4;
} ZYXHEAD;
#pragma pack()


// Functions
void WriteModemThread(PVOID lpVar);
void QueueData
(
   ULONG ulMsg,   // Constant of type IM_*
   PVOID pData,
   ULONG ulLen
);
void ReadModem(PVOID lpVar);
USHORT SendIt(void * str, USHORT antal);

void dprintf
(
   PCHAR szStr,
   ...
);

USHORT ShowMessage
(
   USHORT usMsgNr,
   HWND hwnd,
   USHORT flStyle,
   USHORT idHelp,
   ...
);

void InitDebug(void);

void LogMessage
(
   USHORT usMsgNr,
   ...
);

void LogDosMessage
(
   USHORT usDosErr,
   USHORT usMsgNr,
   ...
);

void LogNumMessage
(
   USHORT usMsgNr,
   USHORT usNum,
   ...
);

void PrintRxSIO(PCHAR pszStr);

void GetIniFile(void);
void WriteState2IniFile(void);
BOOL IniPutSZ
(
   CHAR * szKey,
   CHAR * szStr
);

PCHAR IniGetSZ
(
   CHAR * szKey,
   CHAR * szDefault
);

BOOL IniPutBool
(
   CHAR * szKey,
   BOOL bValue
);

BOOL IniGetBool
(
   CHAR * szKey
);

BOOL IniPutL
(
   CHAR * szKey,
   LONG lValue
);

LONG IniGetL
(
   CHAR * szKey,
   LONG lDefault
);

ULONG GetSysMSecs(void);

APIRET16 APIENTRY16 RPORT(USHORT);
void APIENTRY16 WPORT(USHORT,USHORT);
APIRET16 APIENTRY16 Dos16PortAccess(USHORT,USHORT,USHORT,USHORT);

// External variables
extern HAB hab;
extern HWND hwndFrame;
extern HEV semStopRead;
extern HEV semStopedReading;
extern HEV semGoOnRead;
extern HFILE hCom;
extern HFILE hRec;
extern volatile PARAMETERBLOCK ParamBlock;
extern volatile USHORT usGlobalState;
extern volatile BOOL fDebug, fWarnings, fAbortCmd;
extern CHAR szMsgFile[];
extern CHAR szAppName[];
extern CHAR szVersion[];
extern CHAR szIniFile[];
extern CHAR szIniAm4Pm[];
extern PCHAR pszDCEVer;
extern volatile ULONG ulRecTime;
extern const CHAR szEALen[];
extern const CHAR szEAType[];
extern CHAR szOurEAType[];
extern volatile ULONG ulRecMessages;
extern volatile BOOL bDLEConv;
extern volatile CHAR * pchSendBuff;
extern volatile ULONG ulSendBuffLen;
extern volatile CHAR szActiveDLECodes[DLECODES];   // "" means all

