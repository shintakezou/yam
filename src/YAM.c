/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2007 by YAM Open Source Team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site :  http://www.yam.ch
 YAM OpenSource project    :  http://sourceforge.net/projects/yamos/

 $Id$

***************************************************************************/

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <clib/alib_protos.h>
#include <exec/execbase.h>
#include <libraries/amisslmaster.h>
#include <libraries/asl.h>
#include <mui/BetterString_mcc.h>
#include <mui/NList_mcc.h>
#include <mui/NListview_mcc.h>
#include <mui/NFloattext_mcc.h>
#include <mui/TextEditor_mcc.h>
#include <mui/TheBar_mcc.h>
#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <proto/codesets.h>
#include <proto/datatypes.h>
#include <proto/diskfont.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/iffparse.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/layers.h>
#include <proto/locale.h>
#include <proto/muimaster.h>
#include <proto/rexxsyslib.h>
#include <proto/timer.h>
#include <proto/utility.h>
#include <proto/wb.h>
#include <proto/xpkmaster.h>

#if defined(__amigaos4__)
#include <proto/application.h>
#include <proto/timezone.h>
#endif

#if !defined(__amigaos4__)
#include <proto/genesis.h>
#endif

#include "extrasrc.h"

#include "NewReadArgs.h"
#include "SDI_hook.h"

#include "YAM.h"
#include "YAM_addressbook.h"
#include "YAM_config.h"
#include "YAM_configFile.h"
#include "YAM_folderconfig.h"
#include "YAM_global.h"
#include "YAM_locale.h"
#include "YAM_main.h"
#include "YAM_mainFolder.h"
#include "YAM_read.h"
#include "YAM_rexx.h"
#include "YAM_write.h"
#include "YAM_utilities.h"

#include "ImageCache.h"
#include "UpdateCheck.h"
#include "BayesFilter.h"

#include "classes/Classes.h"

#include "Debug.h"

/***************************************************************************
 Module: Root
***************************************************************************/

struct Global *G = NULL;

struct Args
{
  char  *user;
  char  *password;
  char  *maildir;
  char  *prefsfile;
  LONG   nocheck;
  LONG   hide;
  LONG   debug;
  char  *mailto;
  char  *subject;
  char  *letter;
  char **attach;
  LONG   noImgWarning;
  LONG   noCatalog;
};

static struct NewRDArgs nrda;
static struct Args args;
static BPTR olddirlock = -1; /* -1 is an unset indicator */

/**************************************************************************/

// TimerIO structures we use
struct TC_Request
{
  struct TimeRequest *tr; // pointer to the timerequest
  BOOL isRunning;         // if the request is currenty active/running
  BOOL isPrepared;        // if the request is prepared to get fired
};

static struct TC_Data
{
  struct MsgPort    *port;
  struct TC_Request timer[TIO_NUM];
} TCData;

/**************************************************************************/

static void Abort(const char *message, ...);

/**************************************************************************/

// AutoDST related variables
enum ADSTmethod { ADST_NONE=0, ADST_TZLIB, ADST_SETDST, ADST_FACTS, ADST_SGUARD, ADST_IXGMT };
static const char *const ADSTfile[] = { "", "ENV:TZONE", "ENV:TZONE", "ENV:FACTS/DST", "ENV:SUMMERTIME", "ENV:IXGMTOFFSET" };
static struct ADST_Data
{
  struct NotifyRequest nRequest;
  enum ADSTmethod method;
} ADSTdata;

// Semaphore related suff
static struct StartupSemaphore
{
  struct SignalSemaphore Semaphore; // a standard semaphore structure
  ULONG UseCount;                   // how many other participants know this semaphore
  char Name[12];                    // an optional name for a public semaphore
} *startupSema;

#define STARTUP_SEMAPHORE_NAME      "YAM_Startup"

/*** Library/MCC check routines ***/
/// InitLib
//  Opens a library & on OS4 also the interface
#if defined(__amigaos4__)
static BOOL InitLib(const char *libname,
                    ULONG version,
                    ULONG revision,
                    struct Library **libbase,
                    const char *iname,
                    struct Interface **iface,
                    BOOL required,
                    const char *homepage)
#else
static BOOL InitLib(const char *libname,
                    ULONG version,
                    ULONG revision,
                    struct Library **libbase,
                    BOOL required,
                    const char *homepage)
#endif
{
  struct Library *base;

  #if defined(__amigaos4__)
  if(!libbase || !iface)
    return FALSE;
  #else
  if(!libbase)
    return FALSE;
  #endif

  // open the library base
  base = OpenLibrary(libname, version);

  if(base && revision)
  {
    if(base->lib_Version == version && base->lib_Revision < revision)
    {
      CloseLibrary(base);
      base = NULL;
    }
  }

  // store base
  *libbase = base;

  // if we end up here, we can open the OS4 base library interface
  if(base)
  {
    #if defined(__amigaos4__)
    struct Interface* i;

    // if we weren`t able to obtain the interface, lets close the library also
    if(GETINTERFACE(iname, i, base) == NULL)
    {
      D(DBF_STARTUP, "InitLib: can`t get '%s' interface of library %s", iname, libname);

      CloseLibrary(base);
      *libbase = NULL;
      base = NULL;
    }
    else
      D(DBF_STARTUP, "InitLib: library %s v%ld.%ld with iface '%s' successfully opened.", libname, base->lib_Version, base->lib_Revision, iname);

    // store interface pointer
    *iface = i;
    #else
    D(DBF_STARTUP, "InitLib: library %s v%ld.%ld successfully opened.", libname, base->lib_Version, base->lib_Revision);
    #endif
  }
  else
    D(DBF_STARTUP, "InitLib: can`t open library %s with minimum version v%ld.%d", libname, version, revision);

  if(!base && required)
  {
    if(homepage != NULL)
      Abort(tr(MSG_ER_LIB_URL), libname, version, revision, homepage);
    else
      Abort(tr(MSG_ER_LIB), libname, version, revision);
  }

  return (BOOL)(base != NULL);
}
///
/// CheckMCC
//  Checks if a certain version of a MCC is available
static BOOL CheckMCC(const char *name, ULONG minver, ULONG minrev, BOOL req, const char *url)
{
  BOOL flush = TRUE;

  ENTER();

  SHOWSTRING(DBF_STARTUP, name);

  for(;;)
  {
    // First we attempt to acquire the version and revision through MUI
    Object *obj;

    if((obj = MUI_NewObject(name, TAG_DONE)) != NULL)
    {
      ULONG ver = xget(obj, MUIA_Version);
      ULONG rev = xget(obj, MUIA_Revision);

      struct Library *base;
      char libname[256];

      MUI_DisposeObject(obj);

      if(ver > minver || (ver == minver && rev >= minrev))
      {
        D(DBF_STARTUP, "%s v%ld.%ld found through MUIA_Version/Revision", name, ver, rev);

        RETURN(TRUE);
        return TRUE;
      }

      // If we did't get the version we wanted, let's try to open the
      // libraries ourselves and see what happens...
      snprintf(libname, sizeof(libname), "PROGDIR:mui/%s", name);

      if ((base = OpenLibrary(&libname[8], 0)) || (base = OpenLibrary(&libname[0], 0)))
      {
        UWORD OpenCnt = base->lib_OpenCnt;

        ver = base->lib_Version;
        rev = base->lib_Revision;

        CloseLibrary(base);

        // we add some additional check here so that eventual broken .mcc also have
        // a chance to pass this test (e.g. _very_ old versions of Toolbar.mcc are broken)
        if (ver > minver || (ver == minver && rev >= minrev))
        {
          D(DBF_STARTUP, "%s v%ld.%ld found through OpenLibrary()", name, ver, rev);

          RETURN(TRUE);
          return TRUE;
        }

        if (OpenCnt > 1)
        {
          if (req && MUI_Request(NULL, NULL, 0L, tr(MSG_ErrorStartup), tr(MSG_RETRY_QUIT_GAD), tr(MSG_ER_MCC_IN_USE), name, minver, minrev, ver, rev, url))
            flush = TRUE;
          else
            break;
        }

        // Attempt to flush the library if open count is 0 or because the
        // user wants to retry (meaning there's a chance that it's 0 now)
        if(flush)
        {
          struct Library *result;
          Forbid();
          if ((result = (struct Library *)FindName(&((struct ExecBase *)SysBase)->LibList, name)))
            RemLibrary(result);
          Permit();
          flush = FALSE;
        }
        else
        {
          E(DBF_STARTUP, "%s: couldn`t find minimum required version.", name);

          // We're out of luck - open count is 0, we've tried to flush
          // and still haven't got the version we want
          if (req && MUI_Request(NULL, NULL, 0L, tr(MSG_ErrorStartup), tr(MSG_RETRY_QUIT_GAD), tr(MSG_ER_MCC_OLD), name, minver, minrev, ver, rev, url))
            flush = TRUE;
          else
            break;
        }
      }
    }
    else
    {
      // No MCC at all - no need to attempt flush
      flush = FALSE;
      if (!MUI_Request(NULL, NULL, 0L, tr(MSG_ErrorStartup), tr(MSG_RETRY_QUIT_GAD), tr(MSG_ER_NO_MCC), name, minver, minrev, url))
        break;
    }

  }

  if(req)
    exit(RETURN_ERROR); // Ugly

  RETURN(FALSE);
  return FALSE;
}
///

/*** Auto-DST management routines ***/
/// ADSTnotify_start
//  AutoDST Notify start function
static BOOL ADSTnotify_start(void)
{
  if(ADSTdata.method != ADST_NONE)
  {
    // prepare the NotifyRequest structure
    BYTE signalAlloc;

    if((signalAlloc = AllocSignal(-1)) >= 0)
    {
      struct NotifyRequest *nr = &ADSTdata.nRequest;

      nr->nr_Name  = (STRPTR)ADSTfile[ADSTdata.method];
      nr->nr_Flags = NRF_SEND_SIGNAL;

      // prepare the nr_Signal now
      nr->nr_stuff.nr_Signal.nr_Task      = FindTask(NULL);
      nr->nr_stuff.nr_Signal.nr_SignalNum = signalAlloc;

      return StartNotify(nr);
    }
    else
    {
      memset(&ADSTdata, 0, sizeof(struct ADST_Data));
    }
  }

  return FALSE;
}

///
/// ADSTnotify_stop
//  AutoDST Notify stop function
static void ADSTnotify_stop(void)
{
  if(ADSTdata.method != ADST_NONE)
  {
    // stop the NotifyRequest
    struct NotifyRequest *nr = &ADSTdata.nRequest;

    if(nr->nr_Name)
    {
      EndNotify(nr);
      FreeSignal((LONG)nr->nr_stuff.nr_Signal.nr_SignalNum);
    }
  }
}

///
/// GetDST
//  Checks if daylight saving time is active
//  return 0 if no DST system was found
//         1 if no DST is set
//         2 if DST is set (summertime)
static int GetDST(BOOL update)
{
  char buffer[50];
  char *tmp;
  int result = 0;

  ENTER();

  // prepare the NotifyRequest structure
  if(update == FALSE)
    memset(&ADSTdata, 0, sizeof(struct ADST_Data));

  // lets check the DaylightSaving stuff now
  // we check in the following order:
  //
  // 1. timezone.library (AmigaOS4 only)
  // 2. SetDST (ENV:TZONE)
  // 3. FACTS (ENV:FACTS/DST)
  // 4. SummertimeGuard (ENV:SUMMERTIME)
  // 5. ixemul (ENV:IXGMTOFFSET)

  #if defined(__amigaos4__)
  // check via timezone.library in case we are compiled for AmigaOS4
  if((!update || ADSTdata.method == ADST_TZLIB))
  {
    if(INITLIB("timezone.library", 52, 1, &TimezoneBase, "main", &ITimezone, TRUE, NULL))
    {
      BYTE dstSetting = TFLG_UNKNOWN;

      // retrieve the current DST setting
      if(GetTimezoneAttrs(NULL, TZA_TimeFlag, &dstSetting, TAG_DONE) && dstSetting != TFLG_UNKNOWN)
      {
        if(dstSetting == TFLG_ISDST)
          result = 2;
        else
          result = 1;

        D(DBF_STARTUP, "Found timezone.library with DST flag: %d", result);

        ADSTdata.method = ADST_TZLIB;
      }

      CLOSELIB(TimezoneBase, ITimezone);
    }
  }
  #endif
        #
  // SetDST saves the DST settings in the TZONE env-variable which
  // is a bit more complex than the others, so we need to do some advance parsing
  if((!update || ADSTdata.method == ADST_SETDST) && result == 0
     && GetVar((STRPTR)&ADSTfile[ADST_SETDST][4], buffer, sizeof(buffer), 0) >= 3)
  {
    int i;

    for(i=0; buffer[i]; i++)
    {
      if(result == 0)
      {
        // if we found the time difference in the TZONE variable we at least found a correct TZONE file
        if(buffer[i] >= '0' && buffer[i] <= '9')
          result = 1;
      }
      else if(isalpha(buffer[i]))
        result = 2; // if it is followed by a alphabetic sign we are in DST mode
    }

    D(DBF_STARTUP, "Found '%s' (SetDST) with DST flag: %d", ADSTfile[ADST_SETDST], result);

    ADSTdata.method = ADST_SETDST;
  }

  // FACTS saves the DST information in a ENV:FACTS/DST env variable which will be
  // Hex 00 or 01 to indicate the DST value.
  if((!update || ADSTdata.method == ADST_FACTS) && result == 0
    && GetVar((STRPTR)&ADSTfile[ADST_FACTS][4], buffer, sizeof(buffer), GVF_BINARY_VAR) > 0)
  {
    ADSTdata.method = ADST_FACTS;

    if(buffer[0] == 0x01)
      result = 2;
    else if(buffer[0] == 0x00)
      result = 1;

    D(DBF_STARTUP, "Found '%s' (FACTS) with DST flag: %d", ADSTfile[ADST_FACTS], result);
  }

  // SummerTimeGuard sets the last string to "YES" if DST is actually active
  if((!update || ADSTdata.method == ADST_SGUARD) && result == 0
     && GetVar((STRPTR)&ADSTfile[ADST_SGUARD][4], buffer, sizeof(buffer), 0) > 3 && (tmp = strrchr(buffer, ':')))
  {
    ADSTdata.method = ADST_SGUARD;

    if(tmp[1] == 'Y')
      result = 2;
    else if(tmp[1] == 'N')
      result = 1;

    D(DBF_STARTUP, "Found '%s' (SGUARD) with DST flag: %d", ADSTfile[ADST_SGUARD], result);
  }

  // ixtimezone sets the fifth byte in the IXGMTOFFSET variable to 01 if
  // DST is actually active.
  if((!update || ADSTdata.method == ADST_IXGMT) && result == 0
    && GetVar((STRPTR)&ADSTfile[ADST_IXGMT][4], buffer, sizeof(buffer), GVF_BINARY_VAR) >= 4)
  {
    ADSTdata.method = ADST_IXGMT;

    if(buffer[4] == 0x01)
      result = 2;
    else if(buffer[4] == 0x00)
      result = 1;

    D(DBF_STARTUP, "Found '%s' (IXGMT) with DST flag: %d", ADSTfile[ADST_IXGMT], result);
  }

  if(!update && result == 0)
  {
    ADSTdata.method = ADST_NONE;

    W(DBF_STARTUP, "Didn't find any AutoDST facility active!");
  }

  // No correctly installed AutoDST tool was found
  // so lets return zero.
  RETURN(result);
  return result;
}
///

/*** TimerIO management routines ***/
/// TC_Prepare
//  prepares a timer for being started with TC_Start later on
static void TC_Prepare(enum TimerIO tio, int seconds, int micros)
{
  ENTER();

  if(seconds > 0 || micros > 0)
  {
    struct TC_Request *timer = &TCData.timer[tio];

    if(timer->isRunning == FALSE && timer->isPrepared == FALSE)
    {
      struct TimeRequest *tr = timer->tr;

      // issue a new timerequest
      tr->Request.io_Command  = TR_ADDREQUEST;
      tr->Time.Seconds        = seconds;
      tr->Time.Microseconds   = micros;

      // flag the timer to be prepared to get fired later on
      timer->isPrepared = TRUE;
    }
    else
      W(DBF_TIMERIO, "timer[%ld]: already running/prepared", tio);
  }
  else
    W(DBF_TIMERIO, "timer[%ld]: secs and micros are zero, no prepare required", tio);

  LEAVE();
}

///
/// TC_Start
//  Start a delay depending on the time specified
static void TC_Start(enum TimerIO tio)
{
  struct TC_Request *timer = &TCData.timer[tio];

  ENTER();

  if(timer->isRunning == FALSE && timer->isPrepared == TRUE)
  {
    #if defined(DEBUG)
    char dateString[64];

    DateStamp2String(dateString, sizeof(dateString), NULL, DSS_DATETIME, TZC_NONE);

    D(DBF_TIMERIO, "timer[%ld]: started @ %s to finish in %ld'%ld secs", tio,
                                                                         dateString,
                                                                         timer->tr->Time.Seconds,
                                                                         timer->tr->Time.Microseconds);
    #endif

    // fire the timer by doing a SendIO()
    SendIO(&timer->tr->Request);

    // signal that our timer is running
    timer->isRunning = TRUE;
    timer->isPrepared = FALSE;
  }
  else
    W(DBF_TIMERIO, "timer[%ld]: either already running or not prepared to get fired", tio);

  LEAVE();
}

///
/// TC_Stop
//  Stop a currently running TimerIO request
//  Please note that this function may NOT be used in the eventloop after having received
//  a timer with GetMsg because CheckIO and friends are not defined to work there correctly.
void TC_Stop(enum TimerIO tio)
{
  struct TC_Request *timer = &TCData.timer[tio];

  ENTER();

  // check if we have a already issued ioreq running
  if(timer->isRunning)
  {
    struct IORequest *ioreq = &timer->tr->Request;

    if(ioreq->io_Command != 0)
    {
      if(CheckIO(ioreq) == NULL)
        AbortIO(ioreq);

      WaitIO(ioreq);

      // make sure the timer is signalled to be NOT running
      timer->isRunning = FALSE;
      timer->isPrepared = FALSE;

      D(DBF_TIMERIO, "timer[%ld]: successfully stopped", tio);
    }
    else
      E(DBF_TIMERIO, "timer[%ld]: is invalid and can't be stopped", tio);
  }
  else
    W(DBF_TIMERIO, "timer[%ld]: already stopped", tio);

  LEAVE();
}

///
/// TC_Restart
//  restarts a particular timer. In fact it makes sure that the timer in question
//  is first stopped via AbortIO() and then issues a new one. Please note that
//  this function may NOT be called from the eventloop because CheckIO and friends
//  are not defined to work there.
void TC_Restart(enum TimerIO tio, int seconds, int micros)
{
  ENTER();

  TC_Stop(tio);
  TC_Prepare(tio, seconds, micros);
  TC_Start(tio);

  LEAVE();
}
///
/// TC_Exit
//  Frees timer resources
static void TC_Exit(void)
{
  ENTER();

  // first we abort & delete the IORequests
  if(TCData.timer[0].tr != NULL)
  {
    enum TimerIO tio;

    // first make sure every TimerIO is stoppped
    for(tio = TIO_WRINDEX; tio < TIO_NUM; tio++)
      TC_Stop(tio);

    // then close the device
    if(TCData.timer[0].tr->Request.io_Device != NULL)
    {
      // drop the OS4 Interface of the TimerBase
      DROPINTERFACE(ITimer);

      CloseDevice(&TCData.timer[0].tr->Request);
    }

    // and then we delete the IO requests
    for(tio = TIO_WRINDEX + 1; tio < TIO_NUM; tio++)
    {
      if(TCData.timer[tio].tr != NULL)
      {
        #if defined(__amigaos4__)
        FreeSysObject(ASOT_IOREQUEST, TCData.timer[tio].tr);
        #else
        FreeMem(TCData.timer[tio].tr, sizeof(struct TimeRequest));
        #endif

        TCData.timer[tio].tr = NULL;
      }
    }

    DeleteIORequest(&TCData.timer[0].tr->Request);
    TCData.timer[0].tr = NULL;
  }

  // remove the MsgPort now.
  if(TCData.port != NULL)
  {
    DeleteMsgPort(TCData.port);
    TCData.port = NULL;
  }

  LEAVE();
}

///
/// TC_Init
//  Initializes timer resources
static BOOL TC_Init(void)
{
  ENTER();

  // clear our static structure first
  memset(&TCData, 0, sizeof(struct TC_Data));

  // create message port
  if((TCData.port = CreateMsgPort()))
  {
    // create the TimerIOs now
    if((TCData.timer[0].tr = (struct TimeRequest *)CreateIORequest(TCData.port, sizeof(struct TimeRequest))))
    {
      // then open the device
      if(!OpenDevice(TIMERNAME, UNIT_VBLANK, &TCData.timer[0].tr->Request, 0L))
      {
        // needed to get GetSysTime() working
        if((TimerBase = (APTR)TCData.timer[0].tr->Request.io_Device) &&
           GETINTERFACE("main", ITimer, TimerBase))
        {
          enum TimerIO tio;

          // create our other TimerIOs now
          for(tio = TIO_WRINDEX + 1; tio < TIO_NUM; tio++)
          {
            #if defined(__amigaos4__)
            // on OS4 we use AllocSysObjectTags to give the OS a better chance to
            // free the data in case YAM crashes
            if(!(TCData.timer[tio].tr = AllocSysObjectTags(ASOT_IOREQUEST,
                                                           ASOIOR_Size,      sizeof(struct TimeRequest),
                                                           ASOIOR_ReplyPort, TCData.port,
                                                           TAG_DONE)))
            {
              break;
            }
            #else
            if(!(TCData.timer[tio].tr = AllocMem(sizeof(struct TimeRequest), MEMF_SHARED)))
              break;
            #endif

            // copy the data of timerIO[0] to the new one
            CopyMem(TCData.timer[0].tr, TCData.timer[tio].tr, sizeof(struct TimeRequest));
          }
        }
      }
    }
  }

  LEAVE();
  return (BOOL)(TCData.timer[TIO_NUM-1].tr != NULL);
}

///
/// TC_ActiveEditor
//  Returns TRUE if the internal editor is currently being used
static BOOL TC_ActiveEditor(int wrwin)
{
   if (G->WR[wrwin])
   {
      APTR ao = (APTR)xget(G->WR[wrwin]->GUI.WI, MUIA_Window_ActiveObject);

      return (BOOL)(ao==G->WR[wrwin]->GUI.TE_EDIT);
   }
   return FALSE;
}

///
/// TC_Dispatcher
//  Dispatcher for timer class
//  WARNING: Do NOT use TC_Start() directly in this function as it is
//           called within the timer eventloop which is undefined!
//           Do a TC_Prepare() instead here and a TC_Start() in the
//           the parent eventloop at the end of the file here.
static void TC_Dispatcher(enum TimerIO tio)
{
  // prepare some debug information
  #if defined(DEBUG)
  char dateString[64];

  DateStamp2String(dateString, sizeof(dateString), NULL, DSS_DATETIME, TZC_NONE);
  #endif

  ENTER();

  // now dispatch between the differnent timerIOs
  switch(tio)
  {
    // in case the WriteIndexes TimerIO request was triggered
    // we first check if no Editor is currently active and
    // if so we write the indexes.
    case TIO_WRINDEX:
    {
      D(DBF_TIMERIO, "timer[%ld]: TIO_WRINDEX received at: %s", tio, dateString);

      // only write the indexes if no Editor is actually in use
      if(!TC_ActiveEditor(0) && !TC_ActiveEditor(1))
        MA_UpdateIndexes(FALSE);

      // prepare the timer to get fired again
      TC_Prepare(tio, C->WriteIndexes, 0);
    }
    break;

    // in case the checkMail timerIO request was triggered we
    // need to check if no writewindow is currently in use and
    // then check for new mail.
    case TIO_CHECKMAIL:
    {
      int i;

      D(DBF_TIMERIO, "timer[%ld]: TIO_CHECKMAIL received at: %s", tio, dateString);

      // only if there is currently no write window open we
      // check for new mail.
      for(i = 0; i < MAXWR && !G->WR[i]; i++) ;

      // also the configuration window needs to be closed
      // or we skip the pop operation
      if(i == MAXWR && !G->CO)
      {
        MA_PopNow(POP_TIMED,-1);
      }

      // prepare the timer to get fired again
      TC_Prepare(tio, C->CheckMailDelay*60, 0);
    }
    break;

    // in case the AutoSave timerIO was triggered we check
    // whether there is really need to autosave the content
    // of the currently used editors.
    case TIO_AUTOSAVE:
    {
      char fileName[SIZE_PATHFILE];
      int i;

      D(DBF_TIMERIO, "timer[%ld]: TIO_AUTOSAVE received at: %s", tio, dateString);

      for(i = 0; i < MAXWR; i++)
      {
        if(G->WR[i] && G->WR[i]->Mode != NEW_BOUNCE)
          EditorToFile(G->WR[i]->GUI.TE_EDIT, WR_AutoSaveFile(i, fileName, sizeof(fileName)));
      }

      // prepare the timer to get fired again
      TC_Prepare(tio, C->AutoSave, 0);
    }
    break;

    // in case the READPANEUPDATE timerIO was triggered the embedded read
    // pane in the main window should get updated. Therefore we get the
    // currently active mail out of the main mail list and display it in the pane
    case TIO_READPANEUPDATE:
    {
      D(DBF_TIMERIO, "timer[%ld]: TIO_READPANEUPDATE received: %s", tio, dateString);

      if(C->EmbeddedReadPane)
      {
        struct MA_GUIData *gui = &G->MA->GUI;
        struct Mail *mail;

        // get the actually active mail
        DoMethod(gui->PG_MAILLIST, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &mail);

        // update the readMailGroup of the main window.
        if(mail != NULL)
          DoMethod(gui->MN_EMBEDDEDREADPANE, MUIM_ReadMailGroup_ReadMail, mail,
                                             MUIF_ReadMailGroup_ReadMail_StatusChangeDelay);
      }
    }
    break;

    // on a READSTATUSUPDATE timerIO request the mail status of the currently active mail
    // should change from new/unread to read. So we get the currently active mail
    // out of the main mail list and modify its status to read.
    case TIO_READSTATUSUPDATE:
    {
      struct MA_GUIData *gui = &G->MA->GUI;
      struct Mail *mail;

      D(DBF_TIMERIO, "timer[%ld]: TIO_READSTATUSUPDATE received: %s", tio, dateString);

      // get the actually active mail
      DoMethod(gui->PG_MAILLIST, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &mail);

      // update the status of the mail to READ now
      if(mail != NULL && (hasStatusNew(mail) || !hasStatusRead(mail)))
      {
        setStatusToRead(mail); // set to OLD
        DisplayStatistics(mail->Folder, TRUE);

        // update the status of the readmaildata (window)
        // of the mail here
        UpdateReadMailDataStatus(mail);
      }
    }
    break;

    // on a PROCESSQUICKSEARCH we signal the quicksearch bar to actually process the
    // search. This is used to let a user type in a string in the quicksearchbar
    // without always reissuing the search process, so only the last search request
    // comes actually through. This should prevent the GUI from blocking in some
    // cases.
    case TIO_PROCESSQUICKSEARCH:
    {
      struct MA_GUIData *gui = &G->MA->GUI;

      D(DBF_TIMERIO, "timer[%ld]: TIO_PROCESSQUICKSEARCH received: %s", tio, dateString);

      // signal the QuickSearchBar now.
      DoMethod(gui->GR_QUICKSEARCHBAR, MUIM_QuickSearchBar_ProcessSearch);
    }
    break;

    // on a POP3_KEEPALIVE we make sure that a currently active, but waiting
    // POP3 connection (preselection) doesn't die by sending NOOP commands regularly
    // to the currently connected POP3 server.
    case TIO_POP3_KEEPALIVE:
    {
      D(DBF_TIMERIO, "timer[%ld]: TIO_POP3_KEEPALIVE received: %s", tio, dateString);

      // send the POP3 server a 'NOOP'
      if(TR_SendPOP3KeepAlive())
      {
        // prepare the timer to get fired again
        TC_Prepare(tio, C->KeepAliveInterval, 0);
      }
    }
    break;

    // on a UPDATECHECK we have to process our update check routines as the
    // user wants to check if there is a new version of YAM available or not.
    case TIO_UPDATECHECK:
    {
      D(DBF_TIMERIO, "timer[%ld]: TIO_UPDATECHECK received: %s", tio, dateString);

      CheckForUpdates();

      // prepare the timer to get fired again
      if(C->UpdateInterval > 0)
        TC_Prepare(tio, C->UpdateInterval, 0);
    }
    break;

    // on a SPAMFLUSHTRAININGDATA we write back the spam training data gathered so far
    case TIO_SPAMFLUSHTRAININGDATA:
    {
      D(DBF_TIMERIO, "timer[%ld]: TIO_SPAMFLUSHTRAININGDATA received: %s", tio, dateString);

      BusyText(tr(MSG_BUSYFLUSHINGSPAMTRAININGDATA), "");
      BayesFilterFlushTrainingData();
      BusyEnd();

      TC_Prepare(tio, C->SpamFlushTrainingDataInterval, 0);
    }
    break;

    // on a DELETEZOMBIEFILES we try to delete zombie files which could not be deleted
    // before. Files which still cannot be deleted will be kept in the list and retried
    // later.
    case TIO_DELETEZOMBIEFILES:
    {
      D(DBF_TIMERIO, "timer[%ld]: TIO_DELETEZOMBIEFILES received: %s", tio, dateString);

      if(DeleteZombieFiles(FALSE) == FALSE)
      {
        // trigger the retry mechanism in 5 minutes
        TC_Prepare(TIO_DELETEZOMBIEFILES, 5 * 60, 0);
      }
    }
    break;

    // dummy to please GCC
    case TIO_NUM:
      // nothing
    break;
  }

  LEAVE();
}

///

/*** XPK Packer initialization routines ***/
/// InitXPKPackerList()
// initializes the internal XPK PackerList
static BOOL InitXPKPackerList(void)
{
  BOOL result = FALSE;
  LONG error = 0;

  ENTER();

  if(XpkBase != NULL)
  {
    struct XpkPackerList xpl;

    if((error = XpkQueryTags(XPK_PackersQuery, &xpl, TAG_DONE)) == 0)
    {
      struct XpkPackerInfo xpi;
      unsigned int i;

      D(DBF_XPK, "Loaded XPK Packerlist: %ld packers found", xpl.xpl_NumPackers);

      for(i=0; i < xpl.xpl_NumPackers; i++)
      {
        if((error = XpkQueryTags(XPK_PackMethod, xpl.xpl_Packer[i], XPK_PackerQuery, &xpi, TAG_DONE)) == 0)
        {
          struct xpkPackerNode *newPacker;

          if((newPacker = memdup(&xpi, sizeof(struct xpkPackerNode))) != NULL)
          {
            // because the short name isn't always equal to the packer short name
            // we work around that problem and make sure they are equal.
            strlcpy((char *)newPacker->info.xpi_Name, (char *)xpl.xpl_Packer[i], sizeof(newPacker->info.xpi_Name));

            D(DBF_XPK, "Found XPKPacker: %ld: [%s] = '%s'", i, xpl.xpl_Packer[i], newPacker->info.xpi_Name);

            // add the new packer to our internal list.
            AddTail((struct List *)&G->xpkPackerList, (struct Node *)newPacker);

            result = TRUE;
          }
        }
        else
        {
          // something failed, so lets query the error!
          #if defined(DEBUG)
          char buf[1024];

          XpkFault(error, NULL, buf, sizeof(buf));

          E(DBF_XPK, "Error on XpkQuery() of packer '%s': '%s'", xpl.xpl_Packer[i], buf);
          #endif

          result = FALSE;
        }
      }
    }
    else
    {
      // something failed, so lets query the error!
      #if defined(DEBUG)
      char buf[1024];

      XpkFault(error, NULL, buf, sizeof(buf));

      E(DBF_XPK, "Error on general XpkQuery(): '%s'", buf);
      #endif
    }
  }

  RETURN((BOOL)(result == TRUE && error == 0));
  return (BOOL)(result == TRUE && error == 0);
}

///
/// FreeXPKPackerList()
// free all content of our previously loaded XPK packer list
static void FreeXPKPackerList(void)
{
  ENTER();

  if(IsListEmpty((struct List *)&G->xpkPackerList) == FALSE)
  {
    struct MinNode *curNode;

    // Now we process the read header to set all flags accordingly
    while((curNode = (struct MinNode *)RemHead((struct List *)&G->xpkPackerList)) != NULL)
    {
      // free everything of the node
      free(curNode);
    }
  }

  LEAVE();
}

///

/*** Synchronization routines ***/
/// CreateStartupSemaphore
//  create a new startup semaphore or find an old instance
static struct StartupSemaphore *CreateStartupSemaphore(void)
{
  ULONG nameLen;
  struct StartupSemaphore *newSema;

  ENTER();

  // if valid name is given round its length to a multiple of 4
  nameLen = ((strlen(STARTUP_SEMAPHORE_NAME) + 3) / 4) * 4;

  // We must allocate the memory for the new semaphore before we look for an old instance,
  // because the Forbid() may be broken by AllocVec().
  if((newSema = AllocVec(sizeof(*newSema) + nameLen + 1, MEMF_SHARED|MEMF_CLEAR)) != NULL)
  {
    struct StartupSemaphore *oldSema;

    // initialize the semaphore structure and start with a use counter of 1
    InitSemaphore(&newSema->Semaphore);
    strlcpy(newSema->Name, STARTUP_SEMAPHORE_NAME, sizeof(newSema->Name));
    newSema->Semaphore.ss_Link.ln_Name = newSema->Name;
    newSema->UseCount = 1;

    // we have to disable multitasking before looking for an old instance with the same name
    Forbid();
    if((oldSema = (struct StartupSemaphore *)FindSemaphore((STRPTR)STARTUP_SEMAPHORE_NAME)) != NULL)
    {
      // the semaphore already exists, so just bump the counter
      oldSema->UseCount++;
    }
    else
    {
      // add the new semaphore to the public list of semaphores
      AddSemaphore(&newSema->Semaphore);
    }
    Permit();

    if(oldSema != NULL)
    {
      // the semaphore already existed, so we can free our new instance and return the old instance
      FreeVec(newSema);
      newSema = oldSema;
    }
  }

  RETURN(newSema);
  return newSema;
}
///
/// DeleteStartupSemaphore
//  delete a public semaphore, removing it from the syamSematem if it is no longer in use
static void DeleteStartupSemaphore(void)
{
  ENTER();

  if(startupSema != NULL)
  {
    BOOL freeIt = FALSE;
    // first obtain the semaphore so that nobody else can interfere
    ObtainSemaphore(&startupSema->Semaphore);
    Forbid();
    // now we can release the semaphore again, because nobody else can steal it
    ReleaseSemaphore(&startupSema->Semaphore);

    // one user less for this semaphore
    startupSema->UseCount--;
    // if nobody else uses this semaphore it can be removed complete
    if(startupSema->UseCount == 0)
    {
      // remove the semaphore from the public list
      RemSemaphore(&startupSema->Semaphore);
      // and the memory can be freed afterwards
      freeIt = TRUE;
    }
    Permit();

    if(freeIt)
      FreeVec(startupSema);
  }

  LEAVE();
}
///

/*** Application Abort/Termination routines ***/
/// Terminate
//  Deallocates used memory and MUI modules and terminates
static void Terminate(void)
{
  int i;

  ENTER();

  D(DBF_STARTUP, "freeing spam filter module...");
  BayesFilterCleanup();

  D(DBF_STARTUP, "freeing config module...");
  if(G->CO != NULL)
  {
    CO_ClearConfig(CE);
    free(CE);
    CE = NULL;

    DisposeModule(&G->CO);
  }

  D(DBF_STARTUP, "freeing addressbook entries...");
  for(i = 0; i < MAXEA; i++)
    DisposeModule(&G->EA[i]);

  D(DBF_STARTUP, "freeing readmailData...");
  // cleanup the still existing readmailData objects
  if(IsListEmpty((struct List *)&G->readMailDataList) == FALSE)
  {
    // search through our ReadDataList
    struct MinNode *curNode;
    for(curNode = G->readMailDataList.mlh_Head; curNode->mln_Succ;)
    {
      struct ReadMailData *rmData = (struct ReadMailData *)curNode;

      // already iterate to the next node as the cleanup
      // will free the memory area
      curNode = curNode->mln_Succ;

      CleanupReadMailData(rmData, TRUE);
    }
  }

  D(DBF_STARTUP, "freeing write mail module...");
  for(i = 0; i <= MAXWR; i++)
  {
    if(G->WR[i] != NULL)
    {
      WR_Cleanup(i);
      DisposeModule(&G->WR[i]);
    }
  }

  D(DBF_STARTUP, "freeing tcp/ip stuff...");
  if(G->TR != NULL)
  {
    TR_Cleanup();
    TR_CloseTCPIP();
    DisposeModule(&G->TR);
  }

  if(G->FO != NULL)
    DisposeModule(&G->FO);

  if(G->FI != NULL)
    DisposeModule(&G->FI);

  if(G->ER != NULL)
    DisposeModule(&G->ER);

  if(G->US != NULL)
    DisposeModule(&G->US);

  D(DBF_STARTUP, "finalizing indexes and closing main window...");
  if(G->MA != NULL)
  {
    MA_UpdateIndexes(FALSE);
    set(G->MA->GUI.WI, MUIA_Window_Open, FALSE);
  }

  D(DBF_STARTUP, "freeing addressbook module...");
  if(G->AB != NULL)
    DisposeModule(&G->AB);

  D(DBF_STARTUP, "freeing main window module...");
  if(G->MA != NULL)
    DisposeModule(&G->MA);

  D(DBF_STARTUP, "freeing FileReqCache structures...");
  for(i = 0; i < ASL_MAX; i++)
  {
    struct FileReqCache *frc;

    if((frc = G->FileReqCache[i]) != NULL)
    {
      FreeFileReqCache(frc);
      free(frc);
    }
  }

  D(DBF_STARTUP, "freeing write window notifies...");
  for(i = 0; i <= MAXWR; i++)
  {
    if(G->WR_NRequest[i].nr_stuff.nr_Msg.nr_Port != NULL)
      DeleteMsgPort(G->WR_NRequest[i].nr_stuff.nr_Msg.nr_Port);
  }

  D(DBF_STARTUP, "freeing AppIcon...");
  if(G->AppIcon != NULL)
    RemoveAppIcon(G->AppIcon);

  D(DBF_STARTUP, "freeing AppPort...");
  if(G->AppPort != NULL)
    DeleteMsgPort(G->AppPort);

  D(DBF_STARTUP, "freeing Arexx port...");
  if(G->RexxHost != NULL)
    CloseDownARexxHost(G->RexxHost);

  D(DBF_STARTUP, "freeing timerIOs...");
  TC_Exit();

  // stop the AutoDST notify
  D(DBF_STARTUP, "stoping ADSTnotify...");
  ADSTnotify_stop();

  // check if we have an allocated NewMailSound_Obj and dispose it.
  D(DBF_STARTUP, "freeing newmailsound object...");
  if(G->NewMailSound_Obj != NULL)
    DisposeDTObject(G->NewMailSound_Obj);

  D(DBF_STARTUP, "freeing hideIcon...");
  if(G->HideIcon != NULL)
    FreeDiskObject(G->HideIcon);

  D(DBF_STARTUP, "deleting zombie files...");
  if(DeleteZombieFiles(FALSE) == FALSE)
  {
    BOOL ignore = FALSE;

    do
    {
      if(MUI_Request(G->App, NULL, MUIF_NONE, tr(MSG_ER_ZOMBIE_FILES_EXIST_TITLE),
                                              tr(MSG_ER_ZOMBIE_FILES_EXIST_BT),
                                              tr(MSG_ER_ZOMBIE_FILES_EXIST)) == 0)
      {
        ignore = TRUE;
      }
    }
    while(DeleteZombieFiles(ignore) == FALSE);
  }

  // we deregister the application from
  // application.library
  #if defined(__amigaos4__)
  D(DBF_STARTUP, "unregister from application.library...");
  if(G->applicationID > 0)
    UnregisterApplication(G->applicationID, NULL);

  CLOSELIB(ApplicationBase, IApplication);
  #endif

  D(DBF_STARTUP, "freeing disk objects...");
  for(i = 0; i < MAXICONS; i++)
  {
    if(G->DiskObj[i] != NULL)
      FreeDiskObject(G->DiskObj[i]);
  }

  D(DBF_STARTUP, "freeing toolbar cache...");
  ToolbarCacheCleanup();

  D(DBF_STARTUP, "freeing config...");
  CO_ClearConfig(C);
  free(C);
  C = NULL;

  // free our private codesets list
  D(DBF_STARTUP, "freeing private codesets list...");
  if(G->codesetsList)
  {
    CodesetsListDelete(CSA_CodesetList, G->codesetsList,
                       TAG_DONE);

    G->codesetsList = NULL;
  }

  // free our private internal XPK PackerList
  D(DBF_STARTUP, "cleaning up XPK stuff...");
  FreeXPKPackerList();
  CLOSELIB(XpkBase, IXpk);

  D(DBF_STARTUP, "freeing main application object...");
  if(G->App != NULL)
    MUI_DisposeObject(G->App);

  D(DBF_STARTUP, "freeing image cache...");
  ImageCacheCleanup();

  D(DBF_STARTUP, "freeing internal MUI classes...");
  YAM_CleanupClasses();

  D(DBF_STARTUP, "deleting semaphore");
  DeleteStartupSemaphore();

  // cleaning up all AmiSSL stuff
  D(DBF_STARTUP, "cleaning up AmiSSL stuff...");
  if(AmiSSLBase)
  {
    CleanupAmiSSLA(NULL);

    DROPINTERFACE(IAmiSSL);
    CloseAmiSSL();
    AmiSSLBase = NULL;
  }
  CLOSELIB(AmiSSLMasterBase, IAmiSSLMaster);

  // close all libraries now.
  D(DBF_STARTUP, "closing all opened libraries...");
  CLOSELIB(CodesetsBase,  ICodesets);
  CLOSELIB(DataTypesBase, IDataTypes);
  CLOSELIB(MUIMasterBase, IMUIMaster);
  CLOSELIB(RexxSysBase,   IRexxSys);
  CLOSELIB(IFFParseBase,  IIFFParse);
  CLOSELIB(KeymapBase,    IKeymap);
  CLOSELIB(LayersBase,    ILayers);
  CLOSELIB(WorkbenchBase, IWorkbench);
  CLOSELIB(GfxBase,       IGraphics);

  // close the catalog and locale now
  D(DBF_STARTUP, "closing catalog...");
  CloseYAMCatalog();
  if(G->Locale)
    CloseLocale(G->Locale);

  CLOSELIB(LocaleBase, ILocale);

  // last, but not clear free the global structure
  free(G);
  G = NULL;

  LEAVE();
}
///
/// Abort
//  Shows error requester, then terminates the program
static void Abort(const char *message, ...)
{
  va_list a;

  ENTER();

  va_start(a, message);

  if(message != NULL)
  {
    static char error[SIZE_LINE];

    vsnprintf(error, sizeof(error), message, a);

    if(MUIMasterBase != NULL && G != NULL && G->App != NULL)
    {
      MUI_Request(G->App, NULL, MUIF_NONE, tr(MSG_ErrorStartup), tr(MSG_Quit), error);
    }
    else if(IntuitionBase != NULL)
    {
      struct EasyStruct ErrReq;

      ErrReq.es_StructSize   = sizeof(struct EasyStruct);
      ErrReq.es_Flags        = 0;
      ErrReq.es_Title        = (STRPTR)tr(MSG_ErrorStartup);
      ErrReq.es_TextFormat   = error;
      ErrReq.es_GadgetFormat = (STRPTR)tr(MSG_Quit);

      EasyRequestArgs(NULL, &ErrReq, NULL, NULL);
    }
    else
      puts(error);
  }

  va_end(a);

  // do a hard exit.
  exit(RETURN_ERROR);

  LEAVE();
}
///
/// yam_exitfunc()
/* This makes it possible to leave YAM without explicitely calling cleanup procedure */
static void yam_exitfunc(void)
{
  ENTER();

  D(DBF_STARTUP, "cleaning up in 'yam_exitfunc'...");

  if(olddirlock != -1)
  {
    Terminate();
    CurrentDir(olddirlock);
  }

  if(nrda.Template)
    NewFreeArgs(&nrda);

  // close some libraries now
  CLOSELIB(DiskfontBase,   IDiskfont);
  CLOSELIB(UtilityBase,    IUtility);
  CLOSELIB(IconBase,       IIcon);
  CLOSELIB(IntuitionBase,  IIntuition);

  LEAVE();
}

///

/// SplashProgress
//  Shows progress of program initialization in the splash window
static void SplashProgress(const char *txt, int percent)
{
  DoMethod(G->SplashWinObject, MUIM_Splashwindow_StatusChange, txt, percent);
}
///
/// PopUp
//  Un-iconify YAM
void PopUp(void)
{
  int i;
  Object *window = G->MA->GUI.WI;

  ENTER();

  nnset(G->App, MUIA_Application_Iconified, FALSE);

  // avoid MUIA_Window_Open's side effect of activating the window if it was already open
  if(!xget(window, MUIA_Window_Open))
    set(window, MUIA_Window_Open, TRUE);

  DoMethod(window, MUIM_Window_ScreenToFront);
  DoMethod(window, MUIM_Window_ToFront);

  // Now we check if there is any read and write window open and bring it also
  // to the front
  if(IsListEmpty((struct List *)&G->readMailDataList) == FALSE)
  {
    // search through our ReadDataList
    struct MinNode *curNode;

    for(curNode = G->readMailDataList.mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
    {
      struct ReadMailData *rmData = (struct ReadMailData *)curNode;

      if(rmData->readWindow != NULL)
      {
        DoMethod(rmData->readWindow, MUIM_Window_ToFront);
        window = rmData->readWindow;
      }
    }
  }

  // Bring the write window to the front
  for(i = 0; i < MAXWR; i++)
  {
    if(G->WR[i] != NULL)
    {
      DoMethod(G->WR[i]->GUI.WI, MUIM_Window_ToFront);
      window = G->WR[i]->GUI.WI;
    }
  }

  // now we activate the window that is on the top
  set(window, MUIA_Window_Activate, TRUE);

  LEAVE();
}
///
/// DoublestartHook
//  A second copy of YAM was started
HOOKPROTONHNONP(DoublestartFunc, void)
{
  if(G->App != NULL && G->MA != NULL && G->MA->GUI.WI != NULL)
    PopUp();
}
MakeStaticHook(DoublestartHook, DoublestartFunc);
///
/// StayInProg
//  Makes sure that the user really wants to quit the program
static BOOL StayInProg(void)
{
  int i;
  BOOL req = FALSE;
  BOOL stayIn = FALSE;

  ENTER();

  if(G->AB->Modified == TRUE)
  {
    if(MUI_Request(G->App, G->MA->GUI.WI, 0, NULL, tr(MSG_MA_ABookModifiedGad), tr(MSG_AB_Modified)))
      CallHookPkt(&AB_SaveABookHook, 0, 0);
  }
  if(C->ConfigIsSaved == FALSE)
  {
    if(MUI_Request(G->App, G->MA->GUI.WI, 0, NULL, tr(MSG_CONFIG_MODIFIED_GAD), tr(MSG_CONFIG_MODIFIED)))
      C->ConfigIsSaved = CO_SaveConfig(C, G->CO_PrefsFile);
  }

  for(i=0; i < MAXEA && req == FALSE; i++)
  {
    if(G->EA[i] != NULL)
      req = TRUE;
  }
  for(i=0; i < MAXWR && req == FALSE; i++)
  {
    if(G->WR[i] != NULL)
      req = TRUE;
  }

  if(req || G->CO != NULL || C->ConfirmOnQuit)
  {
    if(MUI_Request(G->App, G->MA->GUI.WI, 0, tr(MSG_MA_ConfirmReq), tr(MSG_YesNoReq), tr(MSG_QuitYAMReq)) == 0)
      stayIn = TRUE;
  }

  RETURN(stayIn);
  return stayIn;
}
///
/// Root_GlobalDispatcher
//  Processes return value of MUI_Application_NewInput
static int Root_GlobalDispatcher(ULONG app_input)
{
  int ret = 0;

  ENTER();

  switch(app_input)
  {
    case MUIV_Application_ReturnID_Quit:
    {
      if(xget(G->App, MUIA_Application_ForceQuit) == FALSE)
      {
        ret = (int)!StayInProg();
      }
      else
        ret = 1;
    }
    break;

    case ID_CLOSEALL:
    {
      if(C->IconifyOnQuit == FALSE)
        ret = (int)!StayInProg();
      else
        set(G->App, MUIA_Application_Iconified, TRUE);
    }
    break;

    case ID_RESTART:
    {
      ret = 2;
    }
    break;

    case ID_ICONIFY:
    {
      MA_UpdateIndexes(FALSE);
    }
    break;
  }

  RETURN(ret);
  return ret;
}
///
/// Root_New
//  Creates MUI application
static BOOL Root_New(BOOL hidden)
{
  BOOL result = FALSE;

  ENTER();

  // make the following operations single threaded
  // MUI chokes if a single task application is created a second time while the first instance is not yet fully created
  ObtainSemaphore(&startupSema->Semaphore);

  if((G->App = YAMObject, End))
  {
    if(hidden)
      set(G->App, MUIA_Application_Iconified, TRUE);

    DoMethod(G->App, MUIM_Notify, MUIA_Application_DoubleStart, TRUE, MUIV_Notify_Application, 2, MUIM_CallHook, &DoublestartHook);
    DoMethod(G->App, MUIM_Notify, MUIA_Application_Iconified, TRUE, MUIV_Notify_Application, 2, MUIM_Application_ReturnID, ID_ICONIFY);

    // create the splash window object and return true if
    // everything worked out fine.
    if((G->SplashWinObject = SplashwindowObject, End))
    {
      G->InStartupPhase = TRUE;

      set(G->SplashWinObject, MUIA_Window_Open, !hidden);

      result = TRUE;
    }
  }

  // now a second instance may continue
  ReleaseSemaphore(&startupSema->Semaphore);

  RETURN(result);
  return result;
}
///

/// Initialise2
//  Phase 2 of program initialization (after user logs in)
static void Initialise2(void)
{
  struct Folder **oldfolders = NULL;
  BOOL newfolders;
  BOOL splashWasActive;
  int i;

  ENTER();

  SplashProgress(tr(MSG_LoadingConfig), 30);
  CO_SetDefaults(C, cp_AllPages);
  CO_LoadConfig(C, G->CO_PrefsFile, &oldfolders);
  CO_Validate(C, FALSE);
  // mark the current configuration as "saved"
  C->ConfigIsSaved = TRUE;
  SplashProgress(tr(MSG_CreatingGUI), 40);

  // before we go and create the first MUI windows
  // we register the application to application.library
  #if defined(__amigaos4__)
  if(ApplicationBase)
  {
    struct ApplicationIconInfo aii;

    aii.iconType = C->DockyIcon ? APPICONT_CustomIcon : APPICONT_None;
    aii.info.customIcon = G->HideIcon;

    // register YAM to application.library
    G->applicationID = RegisterApplication("YAM", REGAPP_URLIdentifier, "yam.ch",
                                                  REGAPP_AppIconInfo,   (uint32)&aii,
                                                  REGAPP_Hidden,        xget(G->App, MUIA_Application_Iconified),
                                                  TAG_DONE);
    D(DBF_STARTUP, "Registered YAM to application.library with appID: %ld", G->applicationID);
  }
  #endif

  // Create a new Main & Addressbook Window
  if(!(G->MA = MA_New()) || !(G->AB = AB_New()))
     Abort(tr(MSG_ErrorMuiApp));

  // make sure the GUI objects for the embedded read pane are created
  MA_SetupEmbeddedReadPane();

  // Now we have to check on which position we should display the InfoBar and if it`s not
  // center or off we have to resort the main group
  if(C->InfoBar != IB_POS_CENTER && C->InfoBar != IB_POS_OFF)
     MA_SortWindow();

  // setup some dynamic (changing) menus
  MA_SetupDynamicMenus();

  // do some initial call to ChangeSelected() for correctly setting up
  // some mail information
  MA_ChangeSelected(TRUE);

  // load the main window GUI layout from the ENV: variable
  LoadLayout();

  SplashProgress(tr(MSG_LoadingFolders), 50);
  if(FO_LoadTree(CreateFilename(".folders")) == FALSE && oldfolders != NULL)
  {
    for(i = 0; i < 100; i++)
    {
      if(oldfolders[i] != NULL)
        DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_Insert, oldfolders[i]->Name, oldfolders[i], MUIV_NListtree_Insert_ListNode_Root);
    }

    newfolders = TRUE;
  }

  if(oldfolders != NULL)
  {
    for(i = 0; oldfolders[i] != NULL; i++)
      free(oldfolders[i]);
    free(oldfolders);
  }

  newfolders = FALSE;
  if(FO_GetFolderByType(FT_INCOMING, NULL) == NULL)
    newfolders |= FO_CreateFolder(FT_INCOMING, FolderName[FT_INCOMING], tr(MSG_MA_Incoming));

  if(FO_GetFolderByType(FT_OUTGOING, NULL) == NULL)
    newfolders |= FO_CreateFolder(FT_OUTGOING, FolderName[FT_OUTGOING], tr(MSG_MA_Outgoing));

  if(FO_GetFolderByType(FT_SENT, NULL) == NULL)
    newfolders |= FO_CreateFolder(FT_SENT, FolderName[FT_SENT], tr(MSG_MA_Sent));

  if(FO_GetFolderByType(FT_TRASH, NULL) == NULL)
    newfolders |= FO_CreateFolder(FT_TRASH, FolderName[FT_TRASH], tr(MSG_MA_TRASH));

  if(C->SpamFilterEnabled)
  {
    // check if the spam folder has to be created
    if(FO_GetFolderByType(FT_SPAM, NULL) == NULL)
    {
      BOOL createSpamFolder;

      if(FileType(CreateFilename(FolderName[FT_SPAM])) == FIT_NONEXIST)
      {
        // no directory named "spam" exists, so let's create it
        createSpamFolder = TRUE;
      }
      else
      {
        // the directory "spam" already exists, but it is not the standard spam folder
        // let the user decide what to do
        ULONG result;

        result = MUI_Request(G->App, NULL, 0, NULL,
                                              tr(MSG_ER_SPAMDIR_EXISTS_ANSWERS),
                                              tr(MSG_ER_SPAMDIR_EXISTS));
        switch(result)
        {
          default:
          case 0:
            // the user has chosen to disable the spam filter, so we do it
            // or the requester was cancelled
            C->SpamFilterEnabled = FALSE;
            createSpamFolder = FALSE;
            break;

          case 1:
            // delete everything in the folder, the directory itself can be kept
            DeleteMailDir(CreateFilename(FolderName[FT_SPAM]), FALSE);
            createSpamFolder = TRUE;
            break;

          case 2:
            // keep the folder contents
            createSpamFolder = TRUE;
            break;
        }
      }

      if(createSpamFolder)
      {
        struct Folder *spamFolder;

        // try to remove the existing folder named "spam"
        if((spamFolder = FO_GetFolderByPath(FolderName[FT_SPAM], NULL)) != NULL)
        {
          struct MUI_NListtree_TreeNode *tn;

          if(spamFolder->imageObject != NULL)
          {
            // we make sure that the NList also doesn`t use the image in future anymore
            DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NList_UseImage, NULL, spamFolder->ImageIndex, MUIF_NONE);
            spamFolder->imageObject = NULL;
          }
          if((tn = FO_GetFolderTreeNode(spamFolder)) != NULL)
          {
            // remove the folder from the folder list
            DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_Remove, MUIV_NListtree_Insert_ListNode_Root, tn, MUIF_NONE);
          }
        }
        // finally, create the spam folder
        newfolders |= FO_CreateFolder(FT_SPAM, FolderName[FT_SPAM], tr(MSG_MA_SPAM));
      }
    }
  }

  if(newfolders)
  {
    set(G->MA->GUI.NL_FOLDERS, MUIA_NListtree_Active, MUIV_NListtree_Active_FirstVisible);
    FO_SaveTree(CreateFilename(".folders"));
  }

  SplashProgress(tr(MSG_RebuildIndices), 60);
  MA_UpdateIndexes(TRUE);

  SplashProgress(tr(MSG_LOADINGUPDATESTATE), 65);
  LoadUpdateState();

  SplashProgress(tr(MSG_LOADINGSPAMTRAININGDATA), 70);
  BayesFilterInit();

  SplashProgress(tr(MSG_LoadingFolders), 75);
  for(i = 0; ;i++)
  {
    struct MUI_NListtree_TreeNode *tn;
    struct MUI_NListtree_TreeNode *tn_parent;
    struct Folder *folder;

    tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_GetEntry, MUIV_NListtree_GetEntry_ListNode_Root, i, MUIF_NONE);
    if(tn == NULL || tn->tn_User == NULL)
      break;

    folder = tn->tn_User;

    // if this entry is a group lets skip here immediatly
    if(isGroupFolder(folder))
      continue;

    if((isIncomingFolder(folder) || isOutgoingFolder(folder) || isTrashFolder(folder) ||
        C->LoadAllFolders) && !isProtectedFolder(folder))
    {
      // call the getIndex function which on one hand loads the full .index file
      // and makes sure that all "new" mail is marked to unread if the user
      // enabled the C->UpdateNewMail option in the configuration.
      MA_GetIndex(folder);
    }
    else if(folder->LoadedMode != LM_VALID)
    {
      // do not load the full index, do load only the header of the .index
      // which summarizes everything
      folder->LoadedMode = MA_LoadIndex(folder, FALSE);

      // if the user wishs to make sure all "new" mail is flagged as
      // read upon start we go through our folders and make sure they show
      // no "new" mail, even if their .index file is not fully loaded
      if(C->UpdateNewMail && folder->LoadedMode == LM_FLUSHED)
        folder->New = 0;
    }

    // if this folder hasn`t got any own folder image in the folder
    // directory and it is one of our standard folders we have to check which image we put in front of it
    if(folder->imageObject == NULL)
    {
      if(isIncomingFolder(folder))      folder->ImageIndex = (folder->Unread != 0) ? FICON_ID_INCOMING_NEW : FICON_ID_INCOMING;
      else if(isOutgoingFolder(folder)) folder->ImageIndex = (folder->Total != 0) ? FICON_ID_OUTGOING_NEW : FICON_ID_OUTGOING;
      else if(isTrashFolder(folder))    folder->ImageIndex = (folder->Total != 0) ? FICON_ID_TRASH_NEW : FICON_ID_TRASH;
      else if(isSentFolder(folder))     folder->ImageIndex = FICON_ID_SENT;
      else if(isSpamFolder(folder))     folder->ImageIndex = (folder->Total != 0) ? FICON_ID_SPAM_NEW : FICON_ID_SPAM;
      else folder->ImageIndex = -1;
    }

    // now we have to add the amount of mails of this folder to the foldergroup
    // aswell and also the grandparents.
    while((tn_parent = (struct MUI_NListtree_TreeNode *)DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_GetEntry, tn, MUIV_NListtree_GetEntry_Position_Parent, MUIF_NONE)))
    {
      // fo_parent is NULL then it`s ROOT and we have to skip here
      // because we cannot have a status of the ROOT tree.
      struct Folder *fo_parent;

      if((fo_parent = (struct Folder *)tn_parent->tn_User) != NULL)
      {
        fo_parent->Unread    += folder->Unread;
        fo_parent->New       += folder->New;
        fo_parent->Total     += folder->Total;
        fo_parent->Sent      += folder->Sent;
        fo_parent->Deleted   += folder->Deleted;

        // for the next step we set tn to the current parent so that we get the
        // grandparents ;)
        tn = tn_parent;
      }
      else
        break;
    }

    DoMethod(G->App, MUIM_Application_InputBuffered);
  }

  // Now we have to make sure that the current folder is really in "active" state
  // or we risk to get a unsynced message listview.
  MA_ChangeFolder(NULL, TRUE);

  SplashProgress(tr(MSG_LoadingABook), 90);
  AB_LoadTree(G->AB_Filename, FALSE, FALSE);
  if((G->RexxHost = SetupARexxHost("YAM", NULL)) == NULL)
     Abort(tr(MSG_ErrorARexx));

  SplashProgress(tr(MSG_OPENGUI), 100);
  G->InStartupPhase = FALSE;

  // close the splash window right before we open our main YAM window
  // but ask it before closing if it was activated or not.
  splashWasActive = xget(G->SplashWinObject, MUIA_Window_Activate);
  set(G->SplashWinObject, MUIA_Window_Open, FALSE);

  // cleanup the splash window object immediately
  DoMethod(G->App, OM_REMMEMBER, G->SplashWinObject);
  MUI_DisposeObject(G->SplashWinObject);
  G->SplashWinObject = NULL;

  // Only activate the main window if the about window is active and open it immediatly.
  // We always start YAM with Window_Open=TRUE or else the hide functionality does not work as expected.
  SetAttrs(G->MA->GUI.WI,
           MUIA_Window_Activate, splashWasActive,
           MUIA_Window_Open,     TRUE,
           TAG_DONE);

  LEAVE();
}
///
/// Initialise
//  Phase 1 of program initialization (before user logs in)
static void Initialise(BOOL hidden)
{
  int i;
  const char *errorMsg = NULL;
  char pathbuf[SIZE_PATH];
  char filebuf[SIZE_PATHFILE];
  static const char *const icnames[MAXICONS] =
  {
    "empty", "old", "new", "check"
  };

  ENTER();

  // lets save the current date/time in our startDate value
  DateStamp(&G->StartDate);

  // initialize the random number seed.
  srand((unsigned int)GetDateStamp());

  // First open locale.library, so we can display a translated error requester
  // in case some of the other libraries can't be opened.
  if(INITLIB("locale.library", 38, 0, &LocaleBase, "main", &ILocale, TRUE, NULL))
    G->Locale = OpenLocale(NULL);

  // Now load the catalog of YAM
  if(G->NoCatalogTranslation == FALSE && OpenYAMCatalog() == FALSE)
    Abort(NULL);

  // load&initialize all required libraries
  INITLIB("graphics.library",  36, 0, &GfxBase,      "main", &IGraphics, TRUE, NULL);
  INITLIB("layers.library",    39, 0, &LayersBase,   "main", &ILayers,   TRUE, NULL);
  INITLIB("workbench.library", 36, 0, &WorkbenchBase,"main", &IWorkbench,TRUE, NULL);
  INITLIB("keymap.library",    36, 0, &KeymapBase,   "main", &IKeymap,   TRUE, NULL);
  INITLIB("iffparse.library",  36, 0, &IFFParseBase, "main", &IIFFParse, TRUE, NULL);
  INITLIB(RXSNAME,             36, 0, &RexxSysBase,  "main", &IRexxSys,  TRUE, NULL);
  INITLIB("muimaster.library", 19, 0, &MUIMasterBase,"main", &IMUIMaster,TRUE, "http://www.sasg.com/");
  INITLIB("datatypes.library", 39, 0, &DataTypesBase,"main", &IDataTypes,TRUE, NULL);
  INITLIB("codesets.library",   6, 3, &CodesetsBase, "main", &ICodesets, TRUE, "http://www.sf.net/projects/codesetslib/");

  // we check for the amisslmaster.library v3 accordingly
  if(INITLIB("amisslmaster.library", AMISSLMASTER_MIN_VERSION, 5, &AmiSSLMasterBase, "main", &IAmiSSLMaster, FALSE, NULL))
  {
    if(InitAmiSSLMaster(AMISSL_CURRENT_VERSION, TRUE))
    {
      if((AmiSSLBase = OpenAmiSSL()) &&
         GETINTERFACE("main", IAmiSSL, AmiSSLBase))
      {
        G->TR_UseableTLS = TRUE;

        D(DBF_STARTUP, "successfully opened AmiSSL library.");
      }
    }
  }

  // now we try to open the application.library which is part of OS4
  // and will be used to notify YAM of certain events and also manage
  // the docky icon accordingly.
  #if defined(__amigaos4__)
  INITLIB("application.library", 50, 0, &ApplicationBase, "application", &IApplication, FALSE, NULL);
  #endif

  // Lets check for the correct TheBar.mcc version
  CheckMCC(MUIC_TheBar,     21, 3, TRUE, "http://www.sf.net/projects/thebar/");
  CheckMCC(MUIC_TheBarVirt, 21, 3, TRUE, "http://www.sf.net/projects/thebar/");
  CheckMCC(MUIC_TheButton,  21, 3, TRUE, "http://www.sf.net/projects/thebar/");

  // Lets check for the correct BetterString.mcc version
  CheckMCC(MUIC_BetterString, 11, 10, TRUE, "http://www.sf.net/projects/bstring-mcc/");

  // we have to have at least v20.116 of NList.mcc to get YAM working without risking
  // to have it buggy - so we make it a requirement. And also 111 is the fastest one ATM.
  CheckMCC(MUIC_NList, 20, 117, TRUE, "http://www.sf.net/projects/nlist-classes/");

  // we also make sure the user uses the latest brand of all other NList classes, such as
  // NListview, NFloattext etc.
  CheckMCC(MUIC_NListview, 19, 72, TRUE, "http://www.sf.net/projects/nlist-classes/");
  CheckMCC(MUIC_NFloattext, 19, 53, TRUE, "http://www.sf.net/projects/nlist-classes/");

  // we make v18.23 the minimum requirement for YAM because earlier versions are
  // buggy
  CheckMCC(MUIC_NListtree, 18, 24, TRUE, "http://www.sf.net/projects/nlist-classes/");

  // Lets check for the correct TextEditor.mcc version
  CheckMCC(MUIC_TextEditor, 15, 21, TRUE, "http://www.sf.net/projects/texteditor-mcc/");

  // now we search through PROGDIR:Charsets and load all user defined
  // codesets via codesets.library
  G->codesetsList = CodesetsListCreateA(NULL);

  // create a public semaphore which can be used to single thread certain actions
  if((startupSema = CreateStartupSemaphore()) == NULL)
    Abort(tr(MSG_ER_CANNOT_CREATE_SEMAPHORE));

  // Initialise and Setup our own MUI custom classes before we go on
  if(YAM_SetupClasses() == FALSE)
    Abort(tr(MSG_ErrorClasses));

  // allocate the MUI root object and popup the progress/about window
  if(Root_New(hidden) == FALSE)
    Abort(FindPort("YAM") ? NULL : tr(MSG_ErrorMuiApp));

  // signal the splash window to show a 10% gauge
  SplashProgress(tr(MSG_LoadingGFX), 10);

  // before we load our images in YAM:icons we check the image layout
  // by loading the ".imglayout" file, checking if it matches the version
  // we are currently using or present the user a warning requester
  // accordingly.
  strmfp(pathbuf, G->ProgDir, "Icons");
  strmfp(filebuf, pathbuf, ".imglayout");
  if(FileExists(filebuf))
  {
    FILE *fp;

    if((fp = fopen(filebuf, "r")))
    {
      char verBuf[5];

      // we load the first 4 bytes of the file as these bytes contain the
      // necessary information
      if(fread(verBuf, 1, 4, fp) == 4)
      {
        verBuf[4] = '\0';

        if(strnicmp(verBuf, "YIM", 3) == 0)
        {
          if(atoi(&verBuf[3]) != IMGLAYOUT_VERSION)
            errorMsg = tr(MSG_ER_WRONGIMGLAYOUTVER);
        }
        else
          errorMsg = tr(MSG_ER_LOADIMGLAYOUTFAILED);
      }
      else
        errorMsg = tr(MSG_ER_LOADIMGLAYOUTFAILED);

      fclose(fp);
    }
    else
      errorMsg = tr(MSG_ER_LOADIMGLAYOUTFAILED);
  }
  else
    errorMsg = tr(MSG_ER_MISSINGIMGVERFILE);

  if(errorMsg != NULL)
  {
    if(MUI_Request(G->App, NULL, 0, tr(MSG_ER_IMGLAYOUTFAILURE),
                                    tr(MSG_ER_EXITIGNORE),
                                    errorMsg, pathbuf))
    {
      // exit the application now
      Abort(NULL);
    }
  }

  for(i=0; i < MAXICONS; i++)
  {
    strmfp(filebuf, pathbuf, icnames[i]);

    // depending on the icon.library version we use either GetIconTags()
    // or the older GetDiskObject() function
    if(IconBase->lib_Version >= 44)
      G->DiskObj[i] = GetIconTags(filebuf, TAG_DONE);
    else
      G->DiskObj[i] = GetDiskObject(filebuf);

    // load the diskobject and report an error if something went wrong.
    if(G->DiskObj[i] == NULL && G->NoImageWarning == FALSE)
    {
      int reqResult;

      if((reqResult = MUI_Request(G->App, NULL, 0, tr(MSG_ER_ICONOBJECT_TITLE),
                                                   tr(MSG_ER_EXITIGNOREALL),
                                                   tr(MSG_ER_ICONOBJECT),
                                                   icnames[i], pathbuf)))
      {
        if(reqResult == 2)
          G->NoImageWarning = TRUE;
        else
          Abort(NULL); // exit the application
      }
    }
  }

  // make sure we initialize the image Cache which in turn will
  // cause YAM to load all static images from the YAM:Icons directory
  if(ImageCacheInit(pathbuf) == FALSE)
    Abort(NULL); // exit the application

  // make sure we initialize the toolbar Cache which in turn will
  // cause YAM to cache all often used toolbars and their images
  if(ToolbarCacheInit(pathbuf) == FALSE)
    Abort(NULL); // exit the application

  // lets advance the progress bar to 20%
  SplashProgress(tr(MSG_InitLibs), 20);

  // try to open xpkmaster.library v5.0+ as this is somewhat the most
  // stable version available. Previous version might have some issues
  // as documented in our FAQ.
  INITLIB(XPKNAME, 5, 0, &XpkBase, "main", &IXpk, FALSE, NULL);
  InitXPKPackerList();

  // initialize our timers
  if(!TC_Init())
    Abort(tr(MSG_ErrorTimer));

  // initialize our ASL FileRequester cache stuff
  for(i = 0; i < ASL_MAX; i++)
  {
    if((G->FileReqCache[i] = calloc(sizeof(struct FileReqCache), 1)) == NULL)
      Abort(NULL);
  }

  // create the main message port
  if((G->AppPort = CreateMsgPort()) == NULL)
    Abort(NULL);

  // initialize the file nofifications
  for(i=0; i <= MAXWR; i++)
  {
    if((G->WR_NRequest[i].nr_stuff.nr_Msg.nr_Port = CreateMsgPort()) == NULL)
      Abort(NULL);

    G->WR_NRequest[i].nr_Name = (STRPTR)G->WR_Filename[i];
    G->WR_NRequest[i].nr_Flags = NRF_SEND_MESSAGE;
  }

  LEAVE();
}
///
/// SendWaitingMail
//  Sends pending mail on startup
static BOOL SendWaitingMail(BOOL hideDisplay, BOOL skipSend)
{
  struct Folder *fo;
  BOOL sentableMail = FALSE;

  ENTER();

  if((fo = FO_GetFolderByType(FT_OUTGOING, NULL)))
  {
    struct Mail *mail;

    for(mail=fo->Messages; mail; mail = mail->Next)
    {
      if(!hasStatusHold(mail) && !hasStatusError(mail))
      {
        sentableMail = TRUE;
        break;
      }
    }

    // in case the folder contains
    // mail which could be sent, we ask the
    // user what to do with it
    if(sentableMail &&
       (hideDisplay == FALSE && xget(G->App, MUIA_Application_Iconified) == FALSE))
    {
      // change the folder first so that the user
      // might have a look at the mails
      MA_ChangeFolder(fo, TRUE);

      // now ask the user for permission to send the mail.
      sentableMail = MUI_Request(G->App, G->MA->GUI.WI, 0, NULL, tr(MSG_YesNoReq), tr(MSG_SendStartReq));
    }
  }

  if(skipSend == FALSE && sentableMail)
    MA_Send(SEND_ALL);

  RETURN(sentableMail);
  return(sentableMail);
}
///
/// DoStartup
//  Performs different checks/cleanup operations on startup
static void DoStartup(BOOL nocheck, BOOL hide)
{
  ENTER();

  // Display the AppIcon now because if non of the below
  // do it it could happen that no AppIcon will be displayed at all.
  DisplayAppIconStatistics();

  // if the user wishs to delete all old mail during startup of YAM,
  // we do it now
  if(C->CleanupOnStartup)
    DoMethod(G->App, MUIM_CallHook, &MA_DeleteOldHook);

  // if the user wants to clean the trash upon starting YAM, do it
  if(C->RemoveOnStartup)
    DoMethod(G->App, MUIM_CallHook, &MA_DeleteDeletedHook);

  // check for current birth days in our addressbook if the user
  // selected it
  if(C->CheckBirthdates && !nocheck && !hide)
    AB_CheckBirthdates();

  // the rest of the startup jobs require a running TCP/IP stack,
  // so check if it is properly running.
  if(!nocheck && TR_IsOnline())
  {
    BOOL noSendOnStartup = FALSE;

    // first get all mail waiting on the POP3 servers (SMTP-after-POP3
    if(C->GetOnStartup)
    {
      if(C->PreSelection == PSM_NEVER || hide)
      {
        MA_PopNow(POP_START, -1);
        if(G->TR != NULL)
          DisposeModule(&G->TR);

        DoMethod(G->App, MUIM_Application_InputBuffered);
      }
      else
      {
        // if SendOnStartup is active as well we do a full exchange
        // to preserve the SMTP-after-POP3 rule.
        if(C->SendOnStartup)
        {
          // see if there is any mail to send and
          // if so ask the user. However we do not
          // immediately send it, but set the Exchange mode
          // so that the next POP operation will also do
          // and SMTP checkup.
          if(SendWaitingMail(hide, TRUE))
            G->TR_Exchange = TRUE;

          noSendOnStartup = TRUE;
        }

        MA_PopNow(POP_USER, -1);
      }
    }

    // send all wariting mail from the Outgoing folder
    if(C->SendOnStartup && !noSendOnStartup)
    {
      // check for any waiting mail in our
      // outgoing folder and ask the user
      // if he wants to send it.
      SendWaitingMail(hide, FALSE);

      if(G->TR != NULL)
        DisposeModule(&G->TR);

      DoMethod(G->App, MUIM_Application_InputBuffered);
    }
  }

  LEAVE();
}
///
/// Login
//  Allows automatic login for AmiTCP-Genesis users
static void Login(const char *user, const char *password,
                  const char *maildir, const char *prefsfile)
{
  BOOL terminate = FALSE;
  BOOL loggedin = FALSE;

  ENTER();

  // we query genesis.library (from the Genesis TCP/IP stack) for the user
  // name in case the caller doesn't want to force a specific username
  #if !defined(__amigaos4__)
  if(user == NULL)
  {
    struct Library *GenesisBase;

    if((GenesisBase = OpenLibrary("genesis.library", 1L)))
    {
      struct genUser *guser;

      if((guser = GetGlobalUser()))
      {
        D(DBF_STARTUP, "GetGlobalUser returned: '%s'", guser->us_name);

        loggedin = US_Login((const char *)guser->us_name, "\01", maildir, prefsfile);

        D(DBF_STARTUP, "US_Login returned: %ld %ld", terminate, loggedin);

        if(!loggedin && !MUI_Request(G->App, NULL, 0, tr(MSG_ER_GENESISUSER_TITLE),
                                                      tr(MSG_ER_CONTINUEEXIT),
                                                      tr(MSG_ER_GENESISUSER),
                                                      guser->us_name))
        {
          terminate = TRUE;
        }

        FreeUser(guser);
      }
      else
        W(DBF_STARTUP, "GetGlobalUser returned NULL");

      CloseLibrary(GenesisBase);
    }
  }
  #endif

  if(!loggedin && !terminate)
    terminate = !US_Login(user, password, maildir, prefsfile);

  if(terminate)
  {
    E(DBF_STARTUP, "terminating due to incorrect login information");
    exit(RETURN_WARN);
  }

  LEAVE();
}
///

/*** Command-Line Argument parsing routines ***/
/// ParseCommandArgs()
//
static LONG ParseCommandArgs(void)
{
  LONG result = 0;
  char *extHelp;

  ENTER();

  // clear the args structure
  memset(&args, 0, sizeof(struct Args));

  // allocate some memory for the extended help
  #define SIZE_EXTHELP  2048
  if((extHelp = malloc(SIZE_EXTHELP)))
  {
    // set argument template
    nrda.Template = (STRPTR)"USER/K,PASSWORD/K,MAILDIR/K,PREFSFILE/K,NOCHECK/S,HIDE/S,DEBUG/S,MAILTO/K,SUBJECT/K,LETTER/K,ATTACH/M,NOIMGWARNING/S,NOCATALOG/S";

    // now we build an extended help page text
    snprintf(extHelp, SIZE_EXTHELP, "%s (%s)\n%s\n\nUsage: YAM <options>\nOptions:\n"
                                    "  USER=<username>     : Selects the active YAM user and skips\n"
                                    "                        the login process.\n"
                                    "  PASSWORD=<password> : Password of selected user (if required).\n"
                                    "  MAILDIR=<path>      : Sets the home directory for the folders\n"
                                    "                        and configuration.\n"
                                    "  PREFSFILE=<filename>: Configuration file that should be used\n"
                                    "                        instead of the default.\n"
                                    "  NOCHECK             : Starts YAM without trying to receive/send\n"
                                    "                        any mail.\n"
                                    "  HIDE                : Starts YAM in iconify mode.\n"
                                    "  DEBUG               : Sends all conversations between YAM and a\n"
                                    "                        mail server to the console window.\n"
                                    "  MAILTO=<recipient>  : Creates a new mail for the specified\n"
                                    "                        recipients when YAM started.\n"
                                    "  SUBJECT=<subject>   : Sets the subject text for a new mail.\n"
                                    "  LETTER=<file>       : The text file containing the actual mail\n"
                                    "                        text of a new message.\n"
                                    "  ATTACH=<file>       : Attaches the specified file to the new\n"
                                    "                        mail created.\n"
                                    "  NOIMGWARNING        : Supresses all warnings regarding missing\n"
                                    "                        image files.\n"
                                    "  NOCATALOG           : Starts YAM without loading any catalog\n"
                                    "                        translation (english).\n"
                                    "\n%s: ", yamversion,
                                              yamversiondate,
                                              yamcopyright,
                                              nrda.Template);

    // set the extHelp pointer
    nrda.ExtHelp = (STRPTR)extHelp;

    // set rest of new read args structure elements
    nrda.Window = NULL;
    nrda.Parameters = (LONG *)&args;
    nrda.FileParameter = -1;
    nrda.PrgToolTypesOnly = FALSE;

    // now call NewReadArgs to parse all our commandline/tooltype arguments in accordance
    // to the above template
    if((result = NewReadArgs(WBmsg, &nrda)))
    {
      args.hide = -args.hide;
      args.nocheck = -args.nocheck;
    }

    free(extHelp);
    nrda.ExtHelp = NULL;
  }

  RETURN(result);
  return result;
}

///

/*** main entry function ***/
/// main()
//  Program entry point, main loop
int main(int argc, char **argv)
{
   BOOL yamFirst;
   BPTR progdir;
   LONG err;

   // obtain the MainInterface of Exec before anything else.
   #ifdef __amigaos4__
   IExec = (struct ExecIFace *)((struct ExecBase *)SysBase)->MainInterface;

   // check the exec version first and force be at least an 52.2 version
   // from AmigaOS4 final. This should assure we are are using the very
   // latest stable version.
   if(SysBase->lib_Version < 52 ||
      (SysBase->lib_Version == 52 && SysBase->lib_Revision < 2))
   {
      if((IntuitionBase = (APTR)OpenLibrary("intuition.library", 36)) &&
         GETINTERFACE("main", IIntuition, IntuitionBase))
      {
        struct EasyStruct ErrReq;

        ErrReq.es_StructSize = sizeof(struct EasyStruct);
        ErrReq.es_Flags      = 0;

        ErrReq.es_Title        = (STRPTR)"YAM Startup Error";
        ErrReq.es_TextFormat   = (STRPTR)"This version of YAM requires at least\n"
                                         "an AmigaOS4 kernel version 52.2";
        ErrReq.es_GadgetFormat = (STRPTR)"Exit";

        EasyRequestArgs(NULL, &ErrReq, NULL, NULL);

        CLOSELIB(IntuitionBase, IIntuition);
        exit(RETURN_WARN);
      }
   }
   #endif

#if defined(DEVWARNING)
   {
     BOOL goon = TRUE;

     if((IntuitionBase = (APTR)OpenLibrary("intuition.library", 36)) &&
        GETINTERFACE("main", IIntuition, IntuitionBase))
     {
       if((UtilityBase = (APTR)OpenLibrary("utility.library", 36)) &&
          GETINTERFACE("main", IUtility, UtilityBase))
       {
         char var;
         struct EasyStruct ErrReq;
         struct DateStamp ds;
         DateStamp(&ds); // get actual time/date

         ErrReq.es_StructSize = sizeof(struct EasyStruct);
         ErrReq.es_Flags      = 0;

         if(EXPDATE <= ds.ds_Days)
         {
           ErrReq.es_Title        = (STRPTR)"YAM Developer Version Expired!";
           ErrReq.es_TextFormat   = (STRPTR)"This developer version of YAM has expired!\n\n"
                                    "Please note that you may download a new, updated\n"
                                    "version from the YAM nightly build page at:\n\n"
                                    "http://nightly.yam.ch/\n\n"
                                    "All developer versions will automatically expire\n"
                                    "after a certian time interval. This is to insure\n"
                                    "that no old versions are floating around causing\n"
                                    "users to report bugs on old versions.\n\n"
                                    "Thanks for your help in improving YAM!";
           ErrReq.es_GadgetFormat = (STRPTR)"Exit";

           DisplayBeep(NULL);
           EasyRequestArgs(NULL, &ErrReq, NULL, NULL);

           goon = FALSE;
         }

         if(goon && GetVar("I_KNOW_YAM_IS_UNDER_DEVELOPMENT", &var, sizeof(var), 0) == -1)
         {
           ErrReq.es_Title        = (STRPTR)"YAM Developer Version Warning!";
           ErrReq.es_TextFormat   = (STRPTR)"This is an *internal* developer version and\n"
                                    "not recommended or intended for public use.\n"
                                    "It may contain bugs that can lead to any loss\n"
                                    "of data and no regular support for this version\n"
                                    "will be provided in any form.\n\n"
                                    "In addition, this version will automatically\n"
                                    "expire after a certain time interval.\n\n"
                                    "So if you're unsure and prefer to have a stable\n"
                                    "installation instead of a possibly dangerous\n"
                                    "version, please consider to use the current\n"
                                    "stable release version available from:\n\n"
                                    "http://www.yam.ch/\n\n"
                                    "Thanks for your help in improving YAM!";
           ErrReq.es_GadgetFormat = (STRPTR)"Go on|Exit";

           DisplayBeep(NULL);
           if(!EasyRequestArgs(NULL, &ErrReq, NULL, NULL))
             goon = FALSE;
         }

       }

       CLOSELIB(UtilityBase, IUtility);
     }

     CLOSELIB(IntuitionBase, IIntuition);
     if(!goon)
       exit(RETURN_WARN);
   }
#endif

   // initialize our debugging system.
   #if defined(DEBUG)
   SetupDebug();
   #endif

   // signal that on a exit() the 'yam_exitfunc' function
   // should be called.
   atexit(yam_exitfunc);

   WBmsg = (struct WBStartup *)(0 == argc ? argv : NULL);

   INITLIB("intuition.library", 36, 0, &IntuitionBase, "main", &IIntuition, TRUE, NULL);
   INITLIB("icon.library",      36, 0, &IconBase,      "main", &IIcon,      TRUE, NULL);
   INITLIB("utility.library",   36, 0, &UtilityBase,   "main", &IUtility,   TRUE, NULL);
   INITLIB("diskfont.library",  37, 0, &DiskfontBase,  "main", &IDiskfont,  TRUE, NULL);

   // now we parse the command-line arguments
   if((err = ParseCommandArgs()))
   {
     PrintFault(err, "YAM");

     SetIoErr(err);
     exit(RETURN_ERROR);
   }

   // security only, can happen for residents only
   if(!(progdir = GetProgramDir()))
     exit(RETURN_ERROR);

   olddirlock = CurrentDir(progdir);

   for(yamFirst=TRUE;;)
   {
      ULONG signals, timsig, adstsig, rexsig, appsig, applibsig, notsig[MAXWR+1];
      struct User *user;
      int i, ret;

      G = calloc(1, sizeof(struct Global));
      C = calloc(1, sizeof(struct Config));

      if(G == NULL || C == NULL)
        break;

      // get the PROGDIR: and program name and put it into own variables
      NameFromLock(progdir, G->ProgDir, sizeof(G->ProgDir));
      if(WBmsg && WBmsg->sm_NumArgs > 0)
      {
        strlcpy(G->ProgName, (char *)WBmsg->sm_ArgList[0].wa_Name, sizeof(G->ProgName));
      }
      else
      {
        char buf[SIZE_PATHFILE];

        GetProgramName((STRPTR)&buf[0], sizeof(buf));
        strlcpy(G->ProgName, (char *)FilePart(buf), sizeof(G->ProgName));
      }

      D(DBF_STARTUP, "ProgDir.: '%s'", G->ProgDir);
      D(DBF_STARTUP, "ProgName: '%s'", G->ProgName);

      if(!args.maildir)
        strlcpy(G->MA_MailDir, G->ProgDir, sizeof(G->MA_MailDir));

      G->TR_Debug = -args.debug;
      G->TR_Socket = TCP_NO_SOCKET;
      G->TR_Allow = TRUE;
      G->CO_DST = GetDST(FALSE);
      G->NoImageWarning = args.noImgWarning;
      G->NoCatalogTranslation = args.noCatalog;

      // prepare some exec lists of either the Global or Config structure
      NewList((struct List *)&(G->readMailDataList));
      NewList((struct List *)&(C->mimeTypeList));
      NewList((struct List *)&(C->filterList));
      NewList((struct List *)&(G->xpkPackerList));
      NewList((struct List *)&(G->zombieFileList));
      ImageCacheSetup();

      // We have to initialize the ActiveWin flags to -1, so than the
      // the arexx commands for the windows are reporting an error if
      // some window wasn`t set active manually by an own rexx command.
      G->ActiveWriteWin = -1;

      #if defined(__amigaos4__)
      // reset the docky icon id to some sensible default
      // upon restart this makes sure that the docky icon is set to the correct state
      G->LastIconID = -1;
      #endif

      if(yamFirst)
      {
        Initialise((BOOL)args.hide);
        Login(args.user, args.password, args.maildir, args.prefsfile);
        Initialise2();
      }
      else
      {
        Initialise(FALSE);
        Login(NULL, NULL, NULL, NULL);
        Initialise2();
      }

      DoMethod(G->App, MUIM_Application_Load, MUIV_Application_Load_ENVARC);
      AppendToLogfile(LF_ALL, 0, tr(MSG_LOG_Started));
      MA_StartMacro(MACRO_STARTUP, NULL);

      // before we go on we check whether there is any .autosaveX.txt file in the
      // maildir directory. And if so we ask the user what he would like to do with it
      for(i=0; i < MAXWR; i++)
      {
        char fileName[SIZE_PATHFILE];

        // fill fileName with the autosave filename
        WR_AutoSaveFile(i, fileName, sizeof(fileName));

        // check if the file exists
        if(FileExists(fileName))
        {
          int answer;

          answer = MUI_Request(G->App, G->MA->GUI.WI, 0, tr(MSG_MA_AUTOSAVEFOUND_TITLE),
                                                         tr(MSG_MA_AUTOSAVEFOUND_BUTTONS),
                                                         tr(MSG_MA_AUTOSAVEFOUND),
                                                         fileName);
          if(answer == 1)
          {
            // the user wants to put the autosave file on hold in the outgoing folder
            // so lets do it and delete the autosave file afterwards
            int wrwin;

            if((wrwin = MA_NewNew(NULL, NEWF_QUIET)) >= 0)
            {
              // set some default receiver and subject, because the autosave file just contains
              // the message text
              set(G->WR[wrwin]->GUI.ST_TO, MUIA_String_Contents, "no@receiver");
              set(G->WR[wrwin]->GUI.ST_SUBJECT, MUIA_String_Contents, "(subject)");

              // load the file in the new editor gadget
              FileToEditor(fileName, G->WR[wrwin]->GUI.TE_EDIT);

              // make sure the texteditor gadget is marked as being changed
              set(G->WR[wrwin]->GUI.TE_EDIT, MUIA_TextEditor_HasChanged, TRUE);

              // put the new mail on hold
              WR_NewMail(WRITE_HOLD, wrwin);

              // we need to explicitly delete the autosave file here because
              // the delete routine in WR_NewMail() doesn't catch the correct file
              // because it only cares about the autosave file for the newly created
              // write object
              if(!DeleteFile(fileName))
                AddZombieFile(fileName);
            }
          }
          else if(answer == 2)
          {
            char newFileName[SIZE_PATHFILE];

            // the user wants to open the autosave file in an own new write window,
            // so lets do it and delete the autosave file afterwards
            int wrwin;

            if((wrwin = MA_NewNew(NULL, 0)) >= 0)
            {
              // load the file in the new editor gadget
              FileToEditor(fileName, G->WR[wrwin]->GUI.TE_EDIT);

              // make sure the texteditor gadget is marked as being changed
              set(G->WR[wrwin]->GUI.TE_EDIT, MUIA_TextEditor_HasChanged, TRUE);

              // we don't need to delete the autosave file here as the write
              // window itself will delete it when it will be closed. However,
              // we do have to rename the autosave file to the one that new wrwin
              // will expect
              if(i != wrwin)
                RenameFile(fileName, WR_AutoSaveFile(wrwin, newFileName, sizeof(newFileName)));
            }
          }
          else if(answer == 3)
          {
            // just delete the autosave file
            if(!DeleteFile(fileName))
              AddZombieFile(fileName);
          }
        }
      }

      if(yamFirst)
      {
        int wrwin;

        DoStartup((BOOL)args.nocheck, (BOOL)args.hide);
        if((args.mailto || args.letter || args.subject || args.attach) && (wrwin = MA_NewNew(NULL, 0)) >= 0)
        {
          if(args.mailto)
            setstring(G->WR[wrwin]->GUI.ST_TO, args.mailto);

          if(args.subject)
            setstring(G->WR[wrwin]->GUI.ST_SUBJECT, args.subject);

          if(args.letter)
            FileToEditor(args.letter, G->WR[wrwin]->GUI.TE_EDIT);

          if(args.attach)
          {
            char **sptr;

            for(sptr = args.attach; *sptr; sptr++)
            {
              if(FileSize(*sptr) >= 0)
                WR_AddFileToList(wrwin, *sptr, NULL, FALSE);
            }
          }
        }

        yamFirst = FALSE;
      }
      else
        DisplayAppIconStatistics();

      user = US_GetCurrentUser();
      AppendToLogfile(LF_NORMAL, 1, tr(MSG_LOG_LoggedIn), user->Name);
      AppendToLogfile(LF_VERBOSE, 2, tr(MSG_LOG_LoggedInVerbose), user->Name, G->CO_PrefsFile, G->MA_MailDir);

      // Now start the NotifyRequest for the AutoDST file
      if(ADSTnotify_start())
        adstsig = 1UL << ADSTdata.nRequest.nr_stuff.nr_Signal.nr_SignalNum;
      else
        adstsig = 0;

      // get the msgport of the application.library
      #if defined(__amigaos4__)
      if(G->applicationID)
      {
        GetApplicationAttrs(G->applicationID,
                            APPATTR_Port,     (uint32)&G->AppLibPort,
                            TAG_DONE);

        if(G->AppLibPort)
          applibsig = (1UL << G->AppLibPort->mp_SigBit);
        else
        {
          E(DBF_STARTUP, "Error on trying to retrieve application libraries MsgPort for YAM.");
          applibsig = 0;
        }
      }
      else
      #endif
        applibsig = 0;

      // prepare the other signal bits
      timsig    = (1UL << TCData.port->mp_SigBit);
      rexsig    = (1UL << G->RexxHost->port->mp_SigBit);
      appsig    = (1UL << G->AppPort->mp_SigBit);
      notsig[0] = (1UL << G->WR_NRequest[0].nr_stuff.nr_Msg.nr_Port->mp_SigBit);
      notsig[1] = (1UL << G->WR_NRequest[1].nr_stuff.nr_Msg.nr_Port->mp_SigBit);
      notsig[2] = (1UL << G->WR_NRequest[2].nr_stuff.nr_Msg.nr_Port->mp_SigBit);

      D(DBF_STARTUP, "YAM allocated signals:");
      D(DBF_STARTUP, " adstsig  = %08lx", adstsig);
      D(DBF_STARTUP, " timsig   = %08lx", timsig);
      D(DBF_STARTUP, " rexsig   = %08lx", rexsig);
      D(DBF_STARTUP, " appsig   = %08lx", appsig);
      D(DBF_STARTUP, " applibsig= %08lx", applibsig);
      D(DBF_STARTUP, " notsig[0]= %08lx", notsig[0]);
      D(DBF_STARTUP, " notsig[1]= %08lx", notsig[1]);
      D(DBF_STARTUP, " notsig[2]= %08lx", notsig[2]);

      // start our maintanance TimerIO requests for
      // different purposes (writeindexes/mailcheck/autosave)
      TC_Prepare(TIO_WRINDEX,   C->WriteIndexes, 0);
      TC_Prepare(TIO_CHECKMAIL, C->CheckMailDelay*60, 0);
      TC_Prepare(TIO_AUTOSAVE,  C->AutoSave, 0);
      TC_Start(TIO_WRINDEX);
      TC_Start(TIO_CHECKMAIL);
      TC_Start(TIO_AUTOSAVE);

      TC_Prepare(TIO_SPAMFLUSHTRAININGDATA, C->SpamFlushTrainingDataInterval, 0);
      TC_Start(TIO_SPAMFLUSHTRAININGDATA);

      // initialize the automatic UpdateCheck facility and schedule an
      // automatic update check during startup if necessary
      InitUpdateCheck(TRUE);

      // start the event loop
      while (!(ret = Root_GlobalDispatcher(DoMethod(G->App, MUIM_Application_NewInput, &signals))))
      {
         if (signals)
         {
            signals = Wait(signals | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D | SIGBREAKF_CTRL_F | timsig | rexsig | appsig | applibsig | notsig[0] | notsig[1] | notsig[2] | adstsig);

            if (signals & SIGBREAKF_CTRL_C) { ret = 1; break; }
            if (signals & SIGBREAKF_CTRL_D) { ret = 0; break; }
            if (signals & SIGBREAKF_CTRL_F) PopUp();

            // check for a TimerIO event
            if(signals & timsig)
            {
              struct TimeRequest *timeReq;
              BOOL processed = FALSE;

              #if defined(DEBUG)
              char dateString[64];

              DateStamp2String(dateString, sizeof(dateString), NULL, DSS_DATETIME, TZC_NONE);
              D(DBF_TIMERIO, "timer signal received @ %s", dateString);
              #endif

              // check if we have a waiting message
              while((timeReq = (struct TimeRequest *)GetMsg(TCData.port)))
              {
                enum TimerIO tio;

                for(tio = TIO_WRINDEX; tio < TIO_NUM; tio++)
                {
                  struct TC_Request *timer = &TCData.timer[tio];

                  if(timeReq == timer->tr)
                  {
                    // set the timer to be not running and not be prepared for
                    // another shot. Our dispatcher have to do the rest then
                    timer->isRunning = FALSE;
                    timer->isPrepared = FALSE;

                    // call the dispatcher with signalling which timerIO
                    // this request caused
                    TC_Dispatcher(tio);

                    // signal that we processed something
                    processed = TRUE;

                    // break out of the for() loop
                    break;
                  }
                }

                // no ReplyMsg() needed
              }

              // make sure that we are starting the timer again after the GetMsg loop
              if(processed)
              {
                // here we just check for the timers that TC_Dispatcher really
                // prepares and not all of them in a loop

                if(TCData.timer[TIO_WRINDEX].isPrepared)
                  TC_Start(TIO_WRINDEX);

                if(TCData.timer[TIO_CHECKMAIL].isPrepared)
                  TC_Start(TIO_CHECKMAIL);

                if(TCData.timer[TIO_AUTOSAVE].isPrepared)
                  TC_Start(TIO_AUTOSAVE);

                if(TCData.timer[TIO_POP3_KEEPALIVE].isPrepared)
                  TC_Start(TIO_POP3_KEEPALIVE);

                if(TCData.timer[TIO_UPDATECHECK].isPrepared)
                  TC_Start(TIO_UPDATECHECK);

                if(TCData.timer[TIO_SPAMFLUSHTRAININGDATA].isPrepared)
                  TC_Start(TIO_SPAMFLUSHTRAININGDATA);

                if(TCData.timer[TIO_DELETEZOMBIEFILES].isPrepared)
                  TC_Start(TIO_DELETEZOMBIEFILES);
              }
              else
                W(DBF_TIMERIO, "timer signal received, but no timer request was processed!!!");

              #if defined(DEBUG)
              // let us check whether all necessary maintenance timers are running
              // because right here ALL maintenance timers should run or something is definitly wrong!

              if(C->WriteIndexes > 0 && TCData.timer[TIO_WRINDEX].isRunning == FALSE)
                E(DBF_ALWAYS, "timer[%ld]: TIO_WRINDEX is not running and was probably lost!", TIO_WRINDEX);

              if(C->CheckMailDelay > 0 && TCData.timer[TIO_CHECKMAIL].isRunning == FALSE)
                E(DBF_ALWAYS, "timer[%ld]: TIO_CHECKMAIL is not running and was probably lost!", TIO_CHECKMAIL);

              if(C->AutoSave > 0 && TCData.timer[TIO_AUTOSAVE].isRunning == FALSE)
                E(DBF_ALWAYS, "timer[%ld]: TIO_AUTOSAVE is not running and was probably lost!", TIO_AUTOSAVE);
              #endif
            }

            // check for a Arexx signal
            if (signals & rexsig) ARexxDispatch(G->RexxHost);

            // check for a AppMessage signal
            if (signals & appsig)
            {
              struct AppMessage *apmsg;

              while((apmsg = (struct AppMessage *)GetMsg(G->AppPort)) != NULL)
              {
                if(apmsg->am_Type == AMTYPE_APPICON)
                {
                  ULONG action = AMCLASSICON_Open;

                  // now we catch the am_Class member of the APPICON message
                  // which will be set by workbench.library v44+. However,
                  // older workbench versions doesn't seem to have the Class
                  // member and may have it uninitialized, therefore we
                  // check here for the v44+ workbench
                  if(WorkbenchBase && WorkbenchBase->lib_Version >= 44)
                    action = apmsg->am_Class;

                  // check the action
                  switch(action)
                  {
                    // user has pressed "Open" or double-clicked on the
                    // AppIcon, so we popup YAM and eventually load the
                    // drag&dropped file into a new write window.
                    case AMCLASSICON_Open:
                    {
                      // bring all windows of YAM to front.
                      PopUp();

                      // check if something was dropped onto the AppIcon
                      if(apmsg->am_NumArgs != 0)
                      {
                        int wrwin;

                        if(G->WR[0])
                          wrwin = 0;
                        else if(G->WR[1])
                          wrwin = 1;
                        else
                          wrwin = MA_NewNew(NULL, 0);

                        if(wrwin >= 0)
                        {
                          int i;

                          // lets walk through all arguments in the appMessage
                          for(i = 0; i < apmsg->am_NumArgs; i++)
                          {
                            char buf[SIZE_PATHFILE];
                            struct WBArg *ap = &apmsg->am_ArgList[i];

                            NameFromLock(ap->wa_Lock, buf, sizeof(buf));
                            AddPart(buf, (char *)ap->wa_Name, sizeof(buf));

                            // call WR_App to let it put in the text of the file
                            // to the write window
                            WR_App(wrwin, buf);
                          }
                        }
                      }
                    }
                    break;

                    // user has pressed "Snapshot" on the AppIcon
                    case AMCLASSICON_Snapshot:
                    {
                      if(G->CurrentDiskObj != NULL)
                      {
                        // remember the position.
                        C->IconPositionX = G->CurrentDiskObj->do_CurrentX;
                        C->IconPositionY = G->CurrentDiskObj->do_CurrentY;

                        // we also save the configuration here, even if that
                        // will trigger that other configurations will
                        // be saved as well. However, such a snapshot action
                        // is done very rarely and the user would definitly
                        // expect that the position will be saved immediately.
                        CO_SaveConfig(C, G->CO_PrefsFile);
                      }
                    }
                    break;

                    // user has pressed "UnSnapshot" on the AppIcon
                    case AMCLASSICON_UnSnapshot:
                    {
                      // for unsnapshotting the icon position we negate the
                      // IconPosition values. So negative values mean they
                      // are disabled.
                      C->IconPositionX = -abs(C->IconPositionX);
                      C->IconPositionY = -abs(C->IconPositionY);

                      // we also save the configuration here, even if that
                      // will trigger that other configurations will
                      // be saved as well. However, such a snapshot action
                      // is done very rarely and the user would definitly
                      // expect that the position will be saved immediately.
                      CO_SaveConfig(C, G->CO_PrefsFile);

                      // refresh the AppIcon
                      DisplayAppIconStatistics();
                    }
                    break;

                    // user has pressed "Empty Trash" on the AppIcon,
                    // so we go and empty the trash folder accordingly.
                    case AMCLASSICON_EmptyTrash:
                    {
                      // empty the "deleted" folder
                      DoMethod(G->App, MUIM_CallHook, &MA_DeleteDeletedHook, FALSE);
                    }
                    break;
                  }
                }

                ReplyMsg(&apmsg->am_Message);
              }
            }

            #if defined(__amigaos4__)
            if(signals & applibsig)
            {
              struct ApplicationMsg *msg;

              while((msg = (struct ApplicationMsg *)GetMsg(G->AppLibPort)) != NULL)
              {
                switch(msg->type)
                {
                  // ask the user if he really wants to quit the application
                  case APPLIBMT_Quit:
                  {
                    ret = (int)!StayInProg();
                  }
                  break;

                  // exit without bothering the user at all.
                  case APPLIBMT_ForceQuit:
                  {
                    ret = 1;
                  }
                  break;

                  // simply make sure YAM will be iconified/hidden
                  case APPLIBMT_Hide:
                  {
                    set(G->App, MUIA_Application_Iconified, TRUE);
                  }
                  break;

                  // simply make sure YAM will be uniconified
                  case APPLIBMT_Unhide:
                  {
                    set(G->App, MUIA_Application_Iconified, FALSE);
                  }
                  break;

                  // make sure the GUI of YAM is in front
                  // and open with the latest document.
                  case APPLIBMT_ToFront:
                  {
                    PopUp();
                  }
                  break;

                  // make sure YAM is in front and open
                  // the configuration window
                  case APPLIBMT_OpenPrefs:
                  {
                    PopUp();
                    CallHookPkt(&CO_OpenHook, 0, 0);
                  }
                  break;

                  // open YAM in front of everyone and
                  // import the passed document in
                  // a new or existing write window.
                  case APPLIBMT_OpenDoc:
                  {
                    struct ApplicationOpenPrintDocMsg* appmsg = (struct ApplicationOpenPrintDocMsg*)msg;
                    int wrwin;

                    if(G->WR[0])
                      wrwin = 0;
                    else if(G->WR[1])
                      wrwin = 1;
                    else
                      wrwin = MA_NewNew(NULL, 0);

                    if(wrwin >= 0)
                    {
                      PopUp();
                      WR_App(wrwin, appmsg->fileName);
                    }
                  }
                  break;

                  // make sure YAM is in front and open
                  // a new write window.
                  case APPLIBMT_NewBlankDoc:
                  {
                    PopUp();
                    MA_NewNew(NULL, 0);
                  }
                  break;
                }

                ReplyMsg((struct Message *)msg);
              }

              // make sure to break out here in case
              // the Quit or ForceQuit succeeded.
              if(ret == 1)
                break;
            }
            #endif

            // check for the write window signals
            for(i = 0; i <= MAXWR; i++)
            {
               if(signals & notsig[i])
               {
                  struct Message *msg;

                  while((msg = GetMsg(G->WR_NRequest[i].nr_stuff.nr_Msg.nr_Port)))
                    ReplyMsg(msg);

                  if(G->WR[i])
                    FileToEditor(G->WR_Filename[i], G->WR[i]->GUI.TE_EDIT);
               }
            }

            // check for the AutoDST signal
            if(signals & adstsig)
            {
              // check the DST file and validate the configuration once more.
              G->CO_DST = GetDST(TRUE);
              CO_Validate(C, FALSE);
            }
         }
      }

      if(C->SendOnQuit && !args.nocheck && TR_IsOnline())
        SendWaitingMail(FALSE, FALSE);

      if(C->CleanupOnQuit)
        DoMethod(G->App, MUIM_CallHook, &MA_DeleteOldHook);

      if(C->RemoveOnQuit)
        DoMethod(G->App, MUIM_CallHook, &MA_DeleteDeletedHook, TRUE);

      AppendToLogfile(LF_ALL, 99, tr(MSG_LOG_Terminated));
      MA_StartMacro(MACRO_QUIT, NULL);

      // if the user really wants to exit, do it now as Terminate() is broken !
      if(ret == 1)
      {
        // Create the shutdown window object, but only show it if the application is visible, too.
        // This window will be closed and disposed automatically as soon as the application itself
        // is disposed.
        if(G->App != NULL && xget(G->App, MUIA_Application_Iconified) == FALSE)
          ShutdownWindowObject, End;

        SetIoErr(RETURN_OK);
        exit(RETURN_OK);
      }

      D(DBF_STARTUP, "Restart issued");

      // prepare for restart
      Terminate();
   }

   /* not reached */
   SetIoErr(RETURN_OK);
   return RETURN_OK;
}
///

