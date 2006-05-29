/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2006 by YAM Open Source Team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site : http://www.yam.ch
 YAM OpenSource project     : http://sourceforge.net/projects/yamos/

 $Id$

 Superclass:  MUIC_Group
 Description: Mail read GUI group which include a texteditor and a header display

***************************************************************************/

#include "ReadMailGroup_cl.h"

#include "YAM_error.h"
#include "YAM_read.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  struct ReadMailData *readMailData;

  Object *headerGroup;
  Object *headerList;
  Object *senderImageGroup;
  Object *senderImage;
  Object *senderImageSpace;
  Object *balanceObject;
  Object *mailBodyGroup;
  Object *mailTextObject;
  Object *textEditScrollbar;
  Object *attachmentGroup;
  Object *contextMenu;

  char menuTitle[SIZE_DEFAULT];

  struct MinList senderInfoHeaders;

  BOOL hasContent;
};
*/

/* EXPORT
#define MUIF_ReadMailGroup_ReadMail_UpdateOnly        (1<<0) // the call to ReadMail is just because of an update of the same mail
#define MUIF_ReadMailGroup_ReadMail_StatusChangeDelay  (1<<1) // the mail status should not be change immediatley but with a specified time interval

#define hasUpdateOnlyFlag(v)        (isFlagSet((v), MUIF_ReadMailGroup_ReadMail_UpdateOnly))
#define hasStatusChangeDelayFlag(v)  (isFlagSet((v), MUIF_ReadMailGroup_ReadMail_StatusChangeDelay))
*/

/// Menu enumerations
enum { RMEN_HNONE=100, RMEN_HSHORT, RMEN_HFULL, RMEN_SNONE, RMEN_SDATA, RMEN_SFULL, RMEN_SIMAGE, RMEN_WRAPH,
       RMEN_TSTYLE, RMEN_FFONT, RMEN_EXTKEY, RMEN_CHKSIG, RMEN_SAVEDEC, RMEN_DISPLAY, RMEN_DETACH, RMEN_CROP,
       RMEN_PRINT, RMEN_SAVE, RMEN_REPLY, RMEN_FORWARD, RMEN_MOVE, RMEN_COPY, RMEN_DELETE };
///

/* Hooks */
/// HeaderDisplayHook
//  Header listview display hook
HOOKPROTONH(HeaderDisplayFunc, long, char **array, struct HeaderNode *hdrNode)
{
  // set the array now so that the NList shows the correct values.
  array[0] = hdrNode->name;
  array[1] = hdrNode->content;

  return 0;
}
MakeStaticHook(HeaderDisplayHook, HeaderDisplayFunc);
///
/// TextEditDoubleClickHook
//  Handles double-clicks on an URL
HOOKPROTONHNO(TextEditDoubleClickFunc, BOOL, struct ClickMessage *clickmsg)
{
  char *p;
  BOOL result = FALSE;

  D(DBF_GUI, "DoubleClick: %ld - [%s]", clickmsg->ClickPosition, clickmsg->LineContents);

  DoMethod(G->App, MUIM_Application_InputBuffered);

  // for safety reasons
  if(!clickmsg->LineContents)
    return FALSE;

  // if the user clicked on space we skip the following
  // analysis of a URL and just check if it was an attachment the user clicked at
  if(!isspace(clickmsg->LineContents[clickmsg->ClickPosition]))
  {
    int pos = clickmsg->ClickPosition;
    char *line, *surl;
    static char url[SIZE_URL];
    enum tokenType type;

    // then we make a copy of the LineContents
    if(!(line = StrBufCpy(NULL, clickmsg->LineContents)))
      return FALSE;

    // find the beginning of the word we clicked at
    surl = &line[pos];
    while(surl != &line[0] && !isspace(*(surl-1)))
      surl--;

    // now find the end of the word the user clicked at
    p = &line[pos];
    while(p+1 != &line[strlen(line)] && !isspace(*(p+1)))
      p++;
    
    *(++p) = '\0';

    // now we start our quick lexical analysis to find a clickable element within
    // the doubleclick area
    if((type = ExtractURL(surl, url)))
    {
      switch(type)
      {
        case tEMAIL:
        {
          RE_ClickedOnMessage(url);
        }
        break;

        case tMAILTO:
        {
          RE_ClickedOnMessage(&url[7]);
        }
        break;

        case tHTTP:
        case tHTTPS:
        case tFTP:
        case tGOPHER:
        case tTELNET:
        case tNEWS:
        case tURL:
        {
          GotoURL(url);
        }
        break;

        default:
          // nothing
        break;
      }

      result = TRUE;
    }

    FreeStrBuf(line);
  }

  return result;
}
MakeStaticHook(TextEditDoubleClickHook, TextEditDoubleClickFunc);
///

/* Private Functions */

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  struct Data *data;
  struct Data *tmpData;
  struct ReadMailData *rmData;
  LONG hgVertWeight = 5;
  LONG tgVertWeight = 100;
  BOOL withContextMenu = FALSE;

  // get eventually set attributes first
  struct TagItem *tags = inittags(msg), *tag;
  while((tag = NextTagItem(&tags)))
  {
    switch(tag->ti_Tag)
    {
      ATTR(HGVertWeight): hgVertWeight = tag->ti_Data; break;
      ATTR(TGVertWeight): tgVertWeight = tag->ti_Data; break;
      ATTR(ContextMenu) : withContextMenu = tag->ti_Data; break;
    }
  }

  // generate a temporar struct Data to which we store our data and
  // copy it later on
  if(!(data = tmpData = calloc(1, sizeof(struct Data))))
    return 0;

  // allocate the readMailData structure
  if(!(data->readMailData = rmData = calloc(1, sizeof(struct ReadMailData))))
    return 0;

  // set some default values for a new readMailGroup
  rmData->headerMode = C->ShowHeader;
  rmData->senderInfoMode = C->ShowSenderInfo;
  rmData->wrapHeaders = C->WrapHeader;
  rmData->noTextstyles = !C->UseTextstyles;
  rmData->useFixedFont = C->FixedFontEdit;
  
  // create some object before the real object
  data->textEditScrollbar = ScrollbarObject, End;

  if((obj = DoSuperNew(cl, obj,

    MUIA_Group_Horiz, FALSE,
    GroupSpacing(1),
    MUIA_ContextMenu, withContextMenu,
    Child, data->headerGroup = HGroup,
      GroupSpacing(0),
      MUIA_VertWeight, hgVertWeight,
      MUIA_ShowMe,     rmData->headerMode != HM_NOHEADER,
      Child, NListviewObject,
        MUIA_NListview_NList, data->headerList = NListObject,
          InputListFrame,
          MUIA_NList_DisplayHook,          &HeaderDisplayHook,
          MUIA_NList_Format,               "P=\033r\0338 W=-1 MIW=-1,",
          MUIA_NList_Input,                FALSE,
          MUIA_NList_TypeSelect,           MUIV_NList_TypeSelect_Char,
          MUIA_NList_DefaultObjectOnClick, FALSE,
          MUIA_ContextMenu,                FALSE,
          MUIA_CycleChain,                 TRUE,
        End,
      End,
      Child, data->senderImageGroup = VGroup,
        GroupSpacing(0),
        InnerSpacing(0,0),
        MUIA_CycleChain,    FALSE,
        MUIA_ShowMe,         FALSE,
        MUIA_Weight,         1,
        Child, data->senderImageSpace = RectangleObject,
          MUIA_Weight, 1,
        End,
      End,
    End,
    Child, data->balanceObject = BalanceObject,
      MUIA_ShowMe, rmData->headerMode != HM_NOHEADER,
    End,
    Child, data->mailBodyGroup = VGroup,
      MUIA_VertWeight, tgVertWeight,
      GroupSpacing(0),
      Child, HGroup,
        GroupSpacing(0),
        Child, data->mailTextObject = MailTextEditObject,
          InputListFrame,
          MUIA_TextEditor_Slider,           data->textEditScrollbar,
          MUIA_TextEditor_FixedFont,        rmData->useFixedFont,
          MUIA_TextEditor_DoubleClickHook, &TextEditDoubleClickHook,
          MUIA_TextEditor_ImportHook,      MUIV_TextEditor_ImportHook_Plain,
          MUIA_TextEditor_ExportHook,      MUIV_TextEditor_ExportHook_Plain,
          MUIA_TextEditor_ReadOnly,        TRUE,
          MUIA_TextEditor_ColorMap,        G->EdColMap,
          MUIA_CycleChain,                  TRUE,
        End,
        Child, data->textEditScrollbar,
      End,
      Child, data->attachmentGroup = AttachmentGroupObject,
        MUIA_ShowMe, FALSE,
      End,
    End,

    TAG_MORE, inittags(msg))))
  {
    if(!(data = (struct Data *)INST_DATA(cl, obj)))
      return 0;

    // copy back the data stored in our temporarly struct Data
    memcpy(data, tmpData, sizeof(struct Data));

    // prepare the senderInfoHeader list
    NewList((struct List *)&data->senderInfoHeaders);

    // place our data in the node and add it to the readMailDataList
    rmData->readMailGroup = obj;
    AddTail((struct List *)&(G->readMailDataList), (struct Node *)data->readMailData);
  }

  // free the temporary mem we allocated before
  free(tmpData);

  return (ULONG)obj;
}
///
/// OVERLOAD(OM_DISPOSE)
OVERLOAD(OM_DISPOSE)
{
  GETDATA;

  // clear the senderInfoHeaders
  FreeHeaderList(&data->senderInfoHeaders);

  if(data->readMailData)
  {
    // Remove our readWindowNode and free it afterwards
    Remove((struct Node *)data->readMailData);
    free(data->readMailData);
    data->readMailData = NULL;
  }

  return DoSuperMethodA(cl, obj, msg);
}

///
/// OVERLOAD(OM_GET)
OVERLOAD(OM_GET)
{
  GETDATA;
  ULONG *store = ((struct opGet *)msg)->opg_Storage;

  switch(((struct opGet *)msg)->opg_AttrID)
  {
    ATTR(HGVertWeight) : *store = xget(data->headerGroup, MUIA_VertWeight); return TRUE;
    ATTR(TGVertWeight) : *store = xget(data->mailBodyGroup, MUIA_VertWeight); return TRUE;
    ATTR(ReadMailData) : *store = (ULONG)data->readMailData; return TRUE;
  }

  return DoSuperMethodA(cl, obj, msg);
}
///
/// OVERLOAD(OM_SET)
OVERLOAD(OM_SET)
{
  GETDATA;

  struct TagItem *tags = inittags(msg), *tag;
  while((tag = NextTagItem(&tags)))
  {
    switch(tag->ti_Tag)
    {
      ATTR(HGVertWeight):
      {
        set(data->headerGroup, MUIA_VertWeight, tag->ti_Data);

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;
      
      ATTR(TGVertWeight):
      {
        set(data->mailBodyGroup, MUIA_VertWeight, tag->ti_Data);

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;
    }
  }

  return DoSuperMethodA(cl, obj, msg);
}
///
/// OVERLOAD(MUIM_ContextMenuBuild)
OVERLOAD(MUIM_ContextMenuBuild)
{
  GETDATA;
  struct MUIP_ContextMenuBuild *mb = (struct MUIP_ContextMenuBuild *)msg;
  struct ReadMailData *rmData = data->readMailData;

  ENTER();

  // dispose the old contextMenu if it still exists
  if(data->contextMenu)
  {
    MUI_DisposeObject(data->contextMenu);
    data->contextMenu = NULL;
  }

  // generate a context menu title now
  if(_isinobject(data->headerGroup, mb->mx, mb->my))
  {
    strlcpy(data->menuTitle, GetStr(MSG_RE_HEADERDISPLAY), sizeof(data->menuTitle));

    data->contextMenu = MenustripObject,
      Child, MenuObjectT(data->menuTitle),
        Child, MenuitemCheck(GetStr(MSG_RE_NoHeaders),    NULL, C->ShowHeader==HM_NOHEADER,    FALSE, 0x06, RMEN_HNONE),
        Child, MenuitemCheck(GetStr(MSG_RE_ShortHeaders), NULL, C->ShowHeader==HM_SHORTHEADER, FALSE, 0x05, RMEN_HSHORT),
        Child, MenuitemCheck(GetStr(MSG_RE_FullHeaders),  NULL, C->ShowHeader==HM_FULLHEADER,  FALSE, 0x03, RMEN_HFULL),
        Child, MenuBarLabel,
        Child, MenuitemCheck(GetStr(MSG_RE_NoSInfo),      NULL, C->ShowSenderInfo==SIM_OFF,    FALSE, 0xE0, RMEN_SNONE),
        Child, MenuitemCheck(GetStr(MSG_RE_SInfo),        NULL, C->ShowSenderInfo==SIM_DATA,   FALSE, 0xD0, RMEN_SDATA),
        Child, MenuitemCheck(GetStr(MSG_RE_SInfoImage),   NULL, C->ShowSenderInfo==SIM_ALL,    FALSE, 0x90, RMEN_SFULL),
        Child, MenuitemCheck(GetStr(MSG_RE_SImageOnly),   NULL, C->ShowSenderInfo==SIM_IMAGE,  FALSE, 0x70, RMEN_SIMAGE),
        Child, MenuBarLabel,
        Child, MenuitemCheck(GetStr(MSG_RE_WrapHeader),   NULL, C->WrapHeader, TRUE, 0, RMEN_WRAPH),
      End,
    End;
  }
  else
  {
    if(rmData && rmData->mail)
    {
      snprintf(data->menuTitle, sizeof(data->menuTitle), "%s: ", GetStr(MSG_Subject));
      strlcat(data->menuTitle, rmData->mail->Subject, 30-strlen(data->menuTitle) > 0 ? 30-strlen(data->menuTitle) : 0);
      strlcat(data->menuTitle, "...", sizeof(data->menuTitle));
    }
    else
      strlcpy(data->menuTitle, GetStr(MSG_RE_MAILDETAILS), sizeof(data->menuTitle));

    data->contextMenu = MenustripObject,
      Child, MenuObjectT(data->menuTitle),
        Child, Menuitem(GetStr(MSG_MA_MReply),   NULL, TRUE, FALSE, RMEN_REPLY),
        Child, Menuitem(GetStr(MSG_MA_MForward), NULL, TRUE, FALSE, RMEN_FORWARD),
        Child, Menuitem(GetStr(MSG_MA_MMove),    NULL, TRUE, FALSE, RMEN_MOVE),
        Child, Menuitem(GetStr(MSG_MA_MCopy),    NULL, TRUE, FALSE, RMEN_COPY),
        Child, MenuBarLabel,
        Child, Menuitem(GetStr(MSG_MA_Save),     NULL, TRUE, FALSE, RMEN_SAVE),
        Child, Menuitem(GetStr(MSG_Print),       NULL, TRUE, FALSE, RMEN_PRINT),
        Child, Menuitem(GetStr(MSG_MA_MDelete),  NULL, TRUE, TRUE,  RMEN_DELETE),
        Child, MenuBarLabel,
        Child, MenuitemObject, MUIA_Menuitem_Title, GetStr(MSG_Attachments),
          Child, Menuitem(GetStr(MSG_RE_MDisplay),NULL, TRUE, FALSE, RMEN_DISPLAY),
          Child, Menuitem(GetStr(MSG_RE_SaveAll), NULL, TRUE, FALSE, RMEN_DETACH),
          Child, Menuitem(GetStr(MSG_MA_Crop),    NULL, TRUE, FALSE, RMEN_CROP),
        End,
        Child, MenuBarLabel,
        Child, MenuObject, MUIA_Menu_Title, "PGP",
          Child, Menuitem(GetStr(MSG_RE_ExtractKey),    NULL, TRUE, FALSE, RMEN_EXTKEY),
          Child, Menuitem(GetStr(MSG_RE_SigCheck),      NULL, TRUE, FALSE, RMEN_CHKSIG),
          Child, Menuitem(GetStr(MSG_RE_SaveDecrypted), NULL, TRUE, FALSE, RMEN_SAVEDEC),
        End,
        Child, MenuBarLabel,
        Child, MenuitemCheck(GetStr(MSG_RE_Textstyles), NULL, C->UseTextstyles, TRUE, 0, RMEN_TSTYLE),
        Child, MenuitemCheck(GetStr(MSG_RE_FixedFont),  NULL, C->FixedFontEdit, TRUE, 0, RMEN_FFONT),
      End,
    End;

  }

  RETURN((ULONG)data->contextMenu);
  return (ULONG)data->contextMenu;
}
///
/// OVERLOAD(MUIM_ContextMenuChoice)
OVERLOAD(MUIM_ContextMenuChoice)
{
  GETDATA;
  struct MUIP_ContextMenuChoice *m = (struct MUIP_ContextMenuChoice *)msg;
  struct ReadMailData *rmData = data->readMailData;
  ULONG result = 0;

  switch(xget(m->item, MUIA_UserData))
  {
    case RMEN_REPLY:    DoMethod(G->App, MUIM_CallHook, &MA_NewMessageHook, NEW_REPLY, 0); break;
    case RMEN_FORWARD:  DoMethod(G->App, MUIM_CallHook, &MA_NewMessageHook, NEW_FORWARD, 0); break;
    case RMEN_MOVE:     DoMethod(G->App, MUIM_CallHook, &MA_MoveMessageHook); break;
    case RMEN_COPY:     DoMethod(G->App, MUIM_CallHook, &MA_CopyMessageHook); break;

    #warning "finish ASAP!"

    default:
      result = DoSuperMethodA(cl, obj, msg);
  }

  RETURN(result);
  return result;
}
///

/* Public Methods */
/// DECLARE(Clear)
DECLARE(Clear) // BOOL noTextEditClear
{
  GETDATA;

  ENTER();

  if(data->hasContent)
  {
    // clear all relevant GUI elements
    DoMethod(data->headerList, MUIM_NList_Clear);

    if(msg->noTextEditClear == FALSE)
      set(data->mailTextObject, MUIA_TextEditor_Contents, "");

    // cleanup the senderInfoHeaders list
    FreeHeaderList(&data->senderInfoHeaders);

    // cleanup the SenderImage field as well
    if(DoMethod(data->senderImageGroup, MUIM_Group_InitChange))
    {
      if(data->senderImage)
      {
        DoMethod(data->senderImageGroup, OM_REMMEMBER, data->senderImage);
        MUI_DisposeObject(data->senderImage);
      }

      data->senderImage = NULL;

      DoMethod(data->senderImageGroup, MUIM_Group_ExitChange);
    }

    set(data->senderImageGroup, MUIA_ShowMe, FALSE);

    // hide the attachmentGroup also
    set(data->attachmentGroup, MUIA_ShowMe, FALSE);
  }

  CleanupReadMailData(data->readMailData, FALSE);

  RETURN(0);
  return 0;
}

///
/// DECLARE(ReadMail)
DECLARE(ReadMail) // struct Mail *mail, ULONG flags
{
  GETDATA;
  struct Mail *mail = msg->mail;
  struct Folder *folder = mail->Folder;
  struct ReadMailData *rmData = data->readMailData;
  char *cmsg;
  BOOL result = FALSE; // error per default

  ENTER();

  // before we actually start loading data into our readmailGroup
  // we have to make sure we didn't actually have something displayed
  // which should get freed first
  DoMethod(obj, MUIM_ReadMailGroup_Clear, hasUpdateOnlyFlag(msg->flags));

  // set the passed mail as the current mail read by our ReadMailData
  // structure
  rmData->mail = mail;

  // load the message now
  if(RE_LoadMessage(rmData, PM_ALL))
  {
    // now read in the Mail in a temporary buffer
    BusyText(GetStr(MSG_BusyDisplaying), "");
    if((cmsg = RE_ReadInMessage(rmData, RIM_READ)))
    {
      struct Person *from = &rmData->mail->From;
      struct ABEntry *ab = NULL;
      struct ABEntry abtmpl;
      char *body;
      BOOL dispheader;
      int hits;

      // the first operation should be: check if the mail is a multipart mail and if so we tell
      // our attachment group about it and read the partlist or otherwise a previously opened
      // attachmentgroup may still hold some references to our already deleted parts
      if(isMultiPartMail(mail))
      {
        if(DoMethod(data->attachmentGroup, MUIM_AttachmentGroup_Refresh, rmData->firstPart) > 0)
          set(data->attachmentGroup, MUIA_ShowMe, TRUE);
        else
        {
          set(data->attachmentGroup, MUIA_ShowMe, FALSE);

          // if this mail was/is a multipart mail but no part was
          // actually added to our attachment group we can remove the
          // multipart flag at all
          if(isMP_MixedMail(mail))
          {
            // clear the multipart/mixed flag
            CLEAR_FLAG(mail->mflags, MFLAG_MP_MIXED);

            // update the statusIconGroup of an eventually existing read window.
            if(rmData->readWindow)
              DoMethod(rmData->readWindow, MUIM_ReadWindow_StatusIconRefresh);

            // if the mail is no virtual mail we can also
            // refresh the maillist depending information
            if(!isVirtualMail(mail))
            {
              SET_FLAG(mail->Folder->Flags, FOFL_MODIFY);  // flag folder as modified
              DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_RedrawMail, mail);
            }
          }
        }
      }
      else
        set(data->attachmentGroup, MUIA_ShowMe, FALSE);

      // then we check whether we should disable the headerList display
      // or not.
      dispheader = (rmData->headerMode != HM_NOHEADER);
      set(data->headerGroup, MUIA_ShowMe, dispheader);
      set(data->balanceObject, MUIA_ShowMe, dispheader);
    
      set(data->headerList, MUIA_NList_Quiet, TRUE);
      body = cmsg;

      // we first go through the headerList of our first Part, which should in fact
      // be the headerPart
      if(dispheader && rmData->firstPart && rmData->firstPart->headerList)
      {
        struct MinNode *curNode;

        // Now we process the read header to set all flags accordingly
        for(curNode = rmData->firstPart->headerList->mlh_Head; curNode->mln_Succ; curNode = curNode->mln_Succ)
        {
          struct HeaderNode *hdrNode = (struct HeaderNode *)curNode;

          // now we use MatchNoCase() to find out if we should include that headerNode
          // in out headerList or not
          if(rmData->headerMode == HM_SHORTHEADER)
            dispheader = MatchNoCase(hdrNode->name, C->ShortHeaders);
          else
            dispheader = (rmData->headerMode == HM_FULLHEADER);

          if(dispheader)
          {
            // we simply insert the whole headerNode and split the display later in our HeaderDisplayHook
            DoMethod(data->headerList, MUIM_NList_InsertSingleWrap, hdrNode,
                                       MUIV_NList_Insert_Bottom,
                                       rmData->wrapHeaders ? WRAPCOL1 : NOWRAP, ALIGN_LEFT);
          }
        }
      }

      if((hits = AB_SearchEntry(from->Address, ASM_ADDRESS|ASM_USER, &ab)) == 0 &&
         *from->RealName)
      {
        hits = AB_SearchEntry(from->RealName, ASM_REALNAME|ASM_USER, &ab);
      }

      RE_GetSenderInfo(rmData->mail, &abtmpl);

      if(!stricmp(from->Address, C->EmailAddress) || !stricmp(from->RealName, C->RealName))
      {
        if(!ab)
        {
          ab = &abtmpl;
          *ab->Photo = 0;
        }
      }
      else
      {
        if(ab)
        {
          RE_UpdateSenderInfo(ab, &abtmpl);
          if(!hasUpdateOnlyFlag(msg->flags) &&
             C->AddToAddrbook > 0 && !*ab->Photo && *abtmpl.Photo && *C->GalleryDir)
          {
            RE_DownloadPhoto(_win(obj), abtmpl.Photo, ab);
          }
        }
        else
        {
          if(!hasUpdateOnlyFlag(msg->flags) &&
             C->AddToAddrbook > 0 && (ab = RE_AddToAddrbook(_win(obj), &abtmpl)))
          {
            if(*abtmpl.Photo && *C->GalleryDir)
              RE_DownloadPhoto(_win(obj), abtmpl.Photo, ab);
          }
          else
          {
            ab = &abtmpl;
            *ab->Photo = 0;
          }
        }
      }

      if(rmData->senderInfoMode != SIM_OFF)
      {
        if(rmData->senderInfoMode != SIM_IMAGE)
        {
          if(hits == 1 || ab->Type == AET_LIST)
          {
            struct HeaderNode *newNode;

            // make sure we cleaned up the senderInfoHeader List beforehand
            FreeHeaderList(&data->senderInfoHeaders);

            if(*ab->RealName && (newNode = malloc(sizeof(struct HeaderNode))))
            {
              newNode->name = StrBufCpy(NULL, MUIX_I);
              newNode->name = StrBufCat(newNode->name, StripUnderscore(GetStr(MSG_EA_RealName)));
              newNode->content = StrBufCpy(NULL, ab->RealName);
              AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
              DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Bottom);
            }

            if(*ab->Street && (newNode = malloc(sizeof(struct HeaderNode))))
            {
              newNode->name = StrBufCpy(NULL, MUIX_I);
              newNode->name = StrBufCat(newNode->name, StripUnderscore(GetStr(MSG_EA_Street)));
              newNode->content = StrBufCpy(NULL, ab->Street);
              AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
              DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Bottom);
            }

            if(*ab->City && (newNode = malloc(sizeof(struct HeaderNode))))
            {
              newNode->name = StrBufCpy(NULL, MUIX_I);
              newNode->name = StrBufCat(newNode->name, StripUnderscore(GetStr(MSG_EA_City)));
              newNode->content = StrBufCpy(NULL, ab->City);
              AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
              DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Bottom);
            }

            if(*ab->Country && (newNode = malloc(sizeof(struct HeaderNode))))
            {
              newNode->name = StrBufCpy(NULL, MUIX_I);
              newNode->name = StrBufCat(newNode->name, StripUnderscore(GetStr(MSG_EA_Country)));
              newNode->content = StrBufCpy(NULL, ab->Country);
              AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
              DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Bottom);
            }

            if(*ab->Phone && (newNode = malloc(sizeof(struct HeaderNode))))
            {
              newNode->name = StrBufCpy(NULL, MUIX_I);
              newNode->name = StrBufCat(newNode->name, StripUnderscore(GetStr(MSG_EA_Phone)));
              newNode->content = StrBufCpy(NULL, ab->Phone);
              AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
              DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Bottom);
            }

            if(*AB_ExpandBD(ab->BirthDay) && (newNode = malloc(sizeof(struct HeaderNode))))
            {
              newNode->name = StrBufCpy(NULL, MUIX_I);
              newNode->name = StrBufCat(newNode->name, StripUnderscore(GetStr(MSG_EA_DOB)));
              newNode->content = StrBufCpy(NULL, AB_ExpandBD(ab->BirthDay));
              AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
              DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Bottom);
            }

            if(*ab->Comment && (newNode = malloc(sizeof(struct HeaderNode))))
            {
              newNode->name = StrBufCpy(NULL, MUIX_I);
              newNode->name = StrBufCat(newNode->name, StripUnderscore(GetStr(MSG_EA_Description)));
              newNode->content = StrBufCpy(NULL, ab->Comment);
              AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
              DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Bottom);
            }

            if(*ab->Homepage && (newNode = malloc(sizeof(struct HeaderNode))))
            {
              newNode->name = StrBufCpy(NULL, MUIX_I);
              newNode->name = StrBufCat(newNode->name, StripUnderscore(GetStr(MSG_EA_Homepage)));
              newNode->content = StrBufCpy(NULL, ab->Homepage);
              AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
              DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Bottom);
            }
          }
        }

        if((rmData->senderInfoMode == SIM_ALL || rmData->senderInfoMode == SIM_IMAGE) &&
           DoMethod(data->senderImageGroup, MUIM_Group_InitChange))
        {
          char photopath[SIZE_PATHFILE];

          if(data->senderImage)
          {
            DoMethod(data->senderImageGroup, OM_REMMEMBER, data->senderImage);
            MUI_DisposeObject(data->senderImage);
          }

          data->senderImage = NULL;

          if(RE_FindPhotoOnDisk(ab, photopath) &&
             (data->senderImage = UserImageObject,
                                    MUIA_Weight,                 100,
                                    MUIA_UserImage_File,         photopath,
                                    MUIA_UserImage_MaxHeight,    64,
                                    MUIA_UserImage_MaxWidth,    64,
                                    MUIA_UserImage_NoMinHeight, TRUE,
                                  End))
          {
            D(DBF_GUI, "SenderPicture found: %s %ld %ld", photopath, xget(data->headerList, MUIA_Width), xget(data->headerList, MUIA_Height));

            DoMethod(data->senderImageGroup, OM_ADDMEMBER, data->senderImage);

            // resort the group so that the space object is at the bottom of it.
            DoMethod(data->senderImageGroup, MUIM_Group_Sort, data->senderImage,
                                                              data->senderImageSpace,
                                                              NULL);
          }
          DoMethod(data->senderImageGroup, MUIM_Group_ExitChange);
        }
      }
      set(data->senderImageGroup, MUIA_ShowMe, (rmData->senderInfoMode == SIM_ALL ||
                                                rmData->senderInfoMode == SIM_IMAGE) &&
                                               (data->senderImage != NULL));
      
      // enable the headerList again
      set(data->headerList, MUIA_NList_Quiet, FALSE);

      // before we can put the message body into the TextEditor, we have to preparse the text and
      // try to set some styles, as we don`t use the buggy ImportHooks of TextEditor anymore and are anyway
      // more powerful this way.
      if(!rmData->noTextstyles)
        body = ParseEmailText(body);

      SetAttrs(data->mailTextObject, MUIA_TextEditor_FixedFont, rmData->useFixedFont,
                                     MUIA_TextEditor_Contents,  body,
                                     TAG_DONE);

      // free the parsed text afterwards as the texteditor has copied it anyway.
      if(!rmData->noTextstyles)
        free(body);

      free(cmsg);

      // start the macro
      if(rmData->readWindow)
        MA_StartMacro(MACRO_READ, itoa(xget(rmData->readWindow, MUIA_ReadWindow_Num)));
      else
        MA_StartMacro(MACRO_READ, NULL);

      // if the displayed mail isn't a virtual one we flag it as read now
      if(!isVirtualMail(mail) &&
         (hasStatusNew(mail) || !hasStatusRead(mail)))
      {
        // depending on the local delayedStatusChange and the global
        // configuration settings for the mail status change interval we either
        // start the timer that will change the mail status to read after a
        // specified time has passed or we change it immediatley here
        if(hasStatusChangeDelayFlag(msg->flags) &&
           C->StatusChangeDelayOn && C->StatusChangeDelay > 0)
        {
          // start the timer event. Please note that the timer event might be
          // canceled by the MA_ChangeSelected() function when the next mail
          // will be selected.
          TC_Restart(TIO_READSTATUSUPDATE, 0, (C->StatusChangeDelay-500)*1000);
        }
        else
        {
          setStatusToRead(mail); // set to OLD
          DisplayStatistics(folder, TRUE);
        }

        // and depending on the PGP status we either check an existing
        // PGP signature or not.
        if((hasPGPSOldFlag(rmData) || hasPGPSMimeFlag(rmData))
           && !hasPGPSCheckedFlag(rmData))
        {
          DoMethod(obj, MUIM_ReadMailGroup_CheckPGPSignature, FALSE);
        }
         
        // check for any MDN and allow to reply to it.
        RE_DoMDN(MDN_READ, mail, FALSE);
      }

      // everything worked out fine so lets return it
      result = TRUE;
    }
    else
      ER_NewError(GetStr(MSG_ER_ErrorReadMailfile), GetMailFile(NULL, folder, mail));

    BusyEnd();
  }
  else
  {
    // check first if the mail file exists and if not we have to exit with an error
    if(!FileExists(mail->MailFile))
    {
      ER_NewError(GetStr(MSG_ER_CantOpenFile), GetMailFile(NULL, folder, mail));
    }
  }

  // make sure we know that there is some content
  data->hasContent = TRUE;

  RETURN(result);
  return result;
}

///
/// DECLARE(CheckPGPSignature)
DECLARE(CheckPGPSignature) // BOOL forceRequester
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;

  // Don't try to use PGP if it's not installed
  if(G->PGPVersion == 0)
    return FALSE;

  if((hasPGPSOldFlag(rmData) || hasPGPSMimeFlag(rmData)) &&
     !hasPGPSCheckedFlag(rmData))
  {
    int error;
    char fullfile[SIZE_PATHFILE];
    char options[SIZE_LARGE];
    
    if(!StartUnpack(GetMailFile(NULL, NULL, rmData->mail), fullfile, rmData->mail->Folder))
      return FALSE;
    
    snprintf(options, sizeof(options), (G->PGPVersion == 5) ? "%s -o %s +batchmode=1 +force +language=us" : "%s -o %s +bat +f +lang=en", fullfile, "T:PGP.tmp");
    error = PGPCommand((G->PGPVersion == 5) ? "pgpv": "pgp", options, KEEPLOG);
    FinishUnpack(fullfile);
    DeleteFile("T:PGP.tmp");
    if(error > 0)
      SET_FLAG(rmData->signedFlags, PGPS_BADSIG);
    
    if(error >= 0)
      RE_GetSigFromLog(rmData, NULL);
    else
      return FALSE;
  }

  if(hasPGPSBadSigFlag(rmData) || msg->forceRequester)
  {
    char buffer[SIZE_LARGE];
    
    strlcpy(buffer, hasPGPSBadSigFlag(rmData) ? GetStr(MSG_RE_BadSig) : GetStr(MSG_RE_GoodSig), sizeof(buffer));
    if(hasPGPSAddressFlag(rmData))
    {
      strlcat(buffer, GetStr(MSG_RE_SigFrom), sizeof(buffer));
      strlcat(buffer, rmData->sigAuthor, sizeof(buffer));
    }
    
    MUI_Request(G->App, _win(obj), MUIF_NONE, GetStr(MSG_RE_SigCheck), GetStr(MSG_Okay), buffer);
  }

  return TRUE;
}

///
/// DECLARE(ExtractPGPKey)
//  Extracts public PGP key from the current message
DECLARE(ExtractPGPKey)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  struct Mail *mail = rmData->mail;
  char fullfile[SIZE_PATHFILE];
  char options[SIZE_PATHFILE];

  if(StartUnpack(GetMailFile(NULL, NULL, mail), fullfile, mail->Folder))
  {
    snprintf(options, sizeof(options), (G->PGPVersion == 5) ? "-a %s +batchmode=1 +force" : "-ka %s +bat +f", fullfile);
    PGPCommand((G->PGPVersion == 5) ? "pgpk" : "pgp", options, 0);
    FinishUnpack(fullfile);
  }

  return 0;
}

///
/// DECLARE(SaveDisplay)
//  Saves current message as displayed
DECLARE(SaveDisplay) // FILE *fileHandle
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  FILE *fh = msg->fileHandle;
  char *ptr;

  if(rmData->headerMode != HM_NOHEADER)
  {
    int i;
    struct MUI_NList_GetEntryInfo res;

    fputs("\033[3m", fh);
    for(i=0;;i++)
    {
      struct HeaderNode *curNode;
      res.pos = MUIV_NList_GetEntryInfo_Line;
      res.line = i;
      
      DoMethod(data->headerList, MUIM_NList_GetEntryInfo, &res);

      if((curNode = (struct HeaderNode *)res.entry))
      {
        char *name = curNode->name;
        char *content = curNode->content;

        // skip the italic style if present
        if(strncmp(name, MUIX_I, strlen(MUIX_I)) == 0)
          name += strlen(MUIX_I);

        fprintf(fh, "%s: %s\n", name, content);
      }
      else
        break;
    }
    fputs("\033[23m\n", fh);
  }
  
  ptr = (char *)DoMethod(data->mailTextObject, MUIM_TextEditor_ExportText);
  for(; *ptr; ptr++)
  {
    if(*ptr == '\033')
    {
      switch(*++ptr)
      {
        case 'u':
          fputs("\033[4m", fh);
        break;
        
        case 'b':
          fputs("\033[1m", fh);
        break;
        
        case 'i':
          fputs("\033[3m", fh);
        break;
        
        case 'n':
          fputs("\033[0m", fh);
        break;
        
        case 'h':
          break;
        
        case '[':
        {
          if(!strncmp(ptr, "[s:18]", 6))
          {
            fputs("===========================================================", fh);
            ptr += 5;
          }
          else if(!strncmp(ptr, "[s:2]", 5))
          {
            fputs("-----------------------------------------------------------", fh);
            ptr += 4;
          }
        }
        break;

        case 'p':
          while(*ptr != ']' && *ptr && *ptr != '\n')
            ptr++;
        break;
      }
    }
    else
      fputc(*ptr, fh);
  }

  return 0;
}

///
/// DECLARE(SaveDecryptedMail)
//  Saves decrypted version of a PGP message
DECLARE(SaveDecryptedMail)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  struct Mail *mail = rmData->mail;
  struct Folder *folder = mail->Folder;
  struct WritePart *p1;
  int choice;
  char mfile[SIZE_MFILE];

  if(!folder)
    return 0;

  if((choice = MUI_Request(G->App, rmData->readWindow, 0, GetStr(MSG_RE_SaveDecrypted),
                                                          GetStr(MSG_RE_SaveDecGads),
                                                          GetStr(MSG_RE_SaveDecReq))))
  {
    struct Compose comp;
    memset(&comp, 0, sizeof(struct Compose));

    if((comp.FH = fopen(MA_NewMailFile(folder, mfile), "w")))
    {
      struct ExtendedMail *email;

      comp.Mode = NEW_SAVEDEC;
      comp.OrigMail = mail;
      comp.FirstPart = p1 = NewPart(2);
      p1->Filename = rmData->firstPart->Next->Filename;
      WriteOutMessage(&comp);
      FreePartsList(p1);
      fclose(comp.FH);

      if((email = MA_ExamineMail(folder, mfile, TRUE)))
      {
        struct Mail *newmail;

        // lets set some values depending on the original message
        email->Mail.sflags = mail->sflags;
        memcpy(&email->Mail.transDate, &mail->transDate, sizeof(struct timeval));

        // add the mail to the folder now
        newmail = AddMailToList(&email->Mail, folder);

        // if this was a compressed/encrypted folder we need to pack the mail now
        if(folder->Mode > FM_SIMPLE)
          RepackMailFile(newmail, -1, NULL);

        if(FO_GetCurrentFolder() == folder)
          DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_NList_InsertSingle, newmail, MUIV_NList_Insert_Sorted);
        
        MA_FreeEMailStruct(email);
        if(choice == 2)
        {
          MA_DeleteSingle(mail, FALSE, FALSE);

          DoMethod(rmData->readWindow, MUIM_ReadWindow_ReadMail, newmail);
        }
      }
      else
        ER_NewError(GetStr(MSG_ER_CreateMailError));
    }
  }

  return 0;
}

///
/// DECLARE(SaveAllAttachments)
DECLARE(SaveAllAttachments)
{
  GETDATA;
  return DoMethod(data->attachmentGroup, MUIM_AttachmentGroup_SaveAll);
}
///
/// DECLARE(ActivateMailText)
//  sets the mailTextObject as the active object of the window
DECLARE(ActivateMailText)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;

  if(rmData->readWindow)
    set(rmData->readWindow, MUIA_Window_DefaultObject, data->mailTextObject);

  return 0;
}

///
