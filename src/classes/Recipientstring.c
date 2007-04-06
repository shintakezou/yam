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
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site : http://www.yam.ch
 YAM OpenSource project     : http://sourceforge.net/projects/yamos/

 $Id$

 Superclass:  MUIC_BetterString
 Description: Auto-completes email addresses etc.

***************************************************************************/

#include "Recipientstring_cl.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  struct MUI_EventHandlerNode ehnode;
  Object *Matchwindow;                //, *Matchlist;
  Object *From, *ReplyTo;             // used when resolving a list address
  STRPTR CurrentRecipient;
  BOOL MultipleRecipients;
  BOOL ResolveOnCR;
  BOOL AdvanceOnCR;                    // we have to save this attribute ourself because Betterstring.mcc is buggy.
};
*/

/* EXPORT
#define MUIF_Recipientstring_Resolve_NoFullName  (1 << 0) // do not resolve with fullname "Mister X <misterx@mister.com>"
#define MUIF_Recipientstring_Resolve_NoValid     (1 << 1) // do not resolve already valid string like "misterx@mister.com"
#define MUIF_Recipientstring_Resolve_NoCache     (1 << 2) // do not resolve addresses out of the eMailCache

#define hasNoFullNameFlag(v)   (isFlagSet((v), MUIF_Recipientstring_Resolve_NoFullName))
#define hasNoValidFlag(v)      (isFlagSet((v), MUIF_Recipientstring_Resolve_NoValid))
#define hasNoCacheFlag(v)      (isFlagSet((v), MUIF_Recipientstring_Resolve_NoCache))
*/

/* Hooks */
/// FindAddressHook
HOOKPROTONHNO(FindAddressFunc, LONG, struct MUIP_NListtree_FindUserDataMessage *msg)
{
  struct ABEntry *entry;
  ULONG result = ~0;

  ENTER();

  entry = (struct ABEntry *)msg->UserData;

  if(entry->Type == AET_USER || entry->Type == AET_LIST) {
    if(Stricmp(msg->User, entry->Alias) == 0)
    {
      D(DBF_GUI, "\"%s\" matches alias \"%s\"", msg->User, entry->Alias);
      result = 0;
    }
    else if(Stricmp(msg->User, entry->RealName) == 0)
    {
      D(DBF_GUI, "\"%s\" matches realname \"%s\"", msg->User, entry->RealName);
      result = 0;
    }
    else if(Stricmp(msg->User, entry->Address) == 0)
    {
      D(DBF_GUI, "\"%s\" matches address \"%s\"", msg->User, entry->Address);
      result = 0;
    }
  }

  RETURN(result);
  return result;
}
MakeStaticHook(FindAddressHook, FindAddressFunc);
///

/* Private Functions */
/// rcptok()
// Non-threadsafe strtok() alike recipient tokenizer.
// "," is the hardcoded token. Ignored if surrounded by quotes ("").
static char *rcptok(char *s, BOOL *quote)
{
  static char *p;

  ENTER();

  if (s)
    p = s;
  else
    s = p;

  if (!p || !*p)
  {
    RETURN(NULL);
    return NULL;
  }

  while (*p)
  {
    if (*p == '"')
      *quote ^= TRUE;
    else if (*p == ',' && !*quote)
    {
      *p++ = '\0';
      return s;
    }
    p++;
  }

  RETURN(s);
  return s;
}
///

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  if((obj = (Object *)DoSuperMethodA(cl, obj, msg)))
  {
    GETDATA;

    struct TagItem *tags = inittags(msg), *tag;
    while((tag = NextTagItem(&tags)))
    {
      switch(tag->ti_Tag)
      {
        ATTR(ResolveOnCR)        : data->ResolveOnCR = tag->ti_Data        ; break;
        ATTR(MultipleRecipients) : data->MultipleRecipients = tag->ti_Data ; break;
        ATTR(FromString)         : data->From = (Object *)tag->ti_Data     ; break;
        ATTR(ReplyToString)      : data->ReplyTo = (Object *)tag->ti_Data  ; break;

        // we also catch foreign attributes
        case MUIA_String_AdvanceOnCR: data->AdvanceOnCR = tag->ti_Data     ; break;
      }
    }

    SetAttrs(obj,
      MUIA_String_Popup, obj,
      MUIA_String_Reject, data->MultipleRecipients ? NULL : ",",
      TAG_DONE);
  }
  return (ULONG)obj;
}
///
/// OVERLOAD(OM_DISPOSE)
OVERLOAD(OM_DISPOSE)
{
  GETDATA;

  if(data->Matchwindow)
  {
    D(DBF_GUI, "Dispose addrlistpopup: %08lx", data->Matchwindow);

    // we know that _app(obj) is documented as only valid in
    // MUIM_Cleanup/Setup, but these two methods are also called
    // if a window gets iconfied only. And by looking at the MUI source,
    // MUI first calls each OM_DISPOSE of all children, so _app(obj)
    // should also be valid during a OM_DISPOSE call, and obviously it
    // doesn`t cause any problem.
    DoMethod(_app(obj), OM_REMMEMBER, data->Matchwindow);
    MUI_DisposeObject(data->Matchwindow);

    data->Matchwindow = NULL;
  }

  if(data->CurrentRecipient)
    free(data->CurrentRecipient);

  return DoSuperMethodA(cl, obj, msg);
}
///
/// OVERLOAD(OM_GET)
/* this is just so that we can notify the popup tag */
OVERLOAD(OM_GET)
{
  GETDATA;
  ULONG *store = ((struct opGet *)msg)->opg_Storage;

  switch(((struct opGet *)msg)->opg_AttrID)
  {
    ATTR(Popup) : *store = FALSE ; return TRUE;

    // we also return foreign attributes
    case MUIA_String_AdvanceOnCR: *store = data->AdvanceOnCR; return TRUE;
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
      ATTR(ResolveOnCR):
      {
        data->ResolveOnCR = tag->ti_Data;

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;

      ATTR(MultipleRecipients):
      {
        data->MultipleRecipients = tag->ti_Data;

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;

      ATTR(FromString):
      {
        data->From = (Object *)tag->ti_Data;

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;

      ATTR(ReplyToString):
      {
        data->ReplyTo = (Object *)tag->ti_Data;

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;

      // we also catch foreign attributes
      case MUIA_String_AdvanceOnCR:
      {
        data->AdvanceOnCR = tag->ti_Data;
      }
      break;
    }
  }

  return DoSuperMethodA(cl, obj, msg);
}
///
/// OVERLOAD(MUIM_Setup)
OVERLOAD(MUIM_Setup)
{
  GETDATA;
  ULONG result;

  ENTER();

  // create the address match list object, if it does not exist yet
  if(data->Matchwindow == NULL)
  {
    if((data->Matchwindow = AddrmatchlistObject, MUIA_Addrmatchlist_String, obj, End) != NULL)
    {
      D(DBF_GUI, "Create addrlistpopup: %lx", data->Matchwindow);
      DoMethod(_app(obj), OM_ADDMEMBER, data->Matchwindow);
    }
  }

  if(data->Matchwindow != NULL)
  {
    if((result = DoSuperMethodA(cl, obj, msg)))
    {
      data->ehnode.ehn_Priority = 1;
      data->ehnode.ehn_Flags    = 0;
      data->ehnode.ehn_Object   = obj;
      data->ehnode.ehn_Class    = cl;
      data->ehnode.ehn_Events   = IDCMP_RAWKEY | IDCMP_CHANGEWINDOW;
    }
  }
  else
    result = FALSE;

  RETURN(result);
  return result;
}
///
/// OVERLOAD(MUIM_Show)
OVERLOAD(MUIM_Show)
{
  GETDATA;
  ULONG result;

  ENTER();

  DoMethod(data->Matchwindow, MUIM_Addrmatchlist_ChangeWindow);
  result = DoSuperMethodA(cl, obj, msg);

  RETURN(result);
  return result;
}
///
/// OVERLOAD(MUIM_GoActive)
OVERLOAD(MUIM_GoActive)
{
  GETDATA;
  ULONG result;

  ENTER();

  DoMethod(_win(obj), MUIM_Window_AddEventHandler, &data->ehnode);
  result = DoSuperMethodA(cl, obj, msg);

  RETURN(result);
  return result;
}
///
/// OVERLOAD(MUIM_GoInactive)
OVERLOAD(MUIM_GoInactive)
{
  GETDATA;
  ULONG result;

  ENTER();

  DoMethod(_win(obj), MUIM_Window_RemEventHandler, &data->ehnode);

  // only if the matchwindow is not active we can close it on a inactive state of
  // this object
  if(!xget(data->Matchwindow, MUIA_Window_Activate))
    set(data->Matchwindow, MUIA_Window_Open, FALSE);

  result = DoSuperMethodA(cl, obj, msg);

  RETURN(result);
  return result;
}
///
/// OVERLOAD(MUIM_DragQuery)
OVERLOAD(MUIM_DragQuery)
{
  struct MUIP_DragQuery *d = (struct MUIP_DragQuery *)msg;
  ULONG result = MUIV_DragQuery_Refuse;

  ENTER();

  if(d->obj == G->AB->GUI.LV_ADDRESSES)
  {
    struct MUI_NListtree_TreeNode *active;
    if((active = (struct MUI_NListtree_TreeNode *)xget(d->obj, MUIA_NListtree_Active)))
    {
      if(isFlagClear(active->tn_Flags, TNF_LIST))
        result = MUIV_DragQuery_Accept;
    }
  }
  else if(DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_IsMailList, d->obj) == TRUE)
  {
    result = MUIV_DragQuery_Accept;
  }

  RETURN(result);
  return result;
}
///
/// OVERLOAD(MUIM_DragDrop)
OVERLOAD(MUIM_DragDrop)
{
  struct MUIP_DragQuery *d = (struct MUIP_DragQuery *)msg;

  ENTER();

  if(d->obj == G->AB->GUI.LV_ADDRESSES)
  {
    struct MUI_NListtree_TreeNode *active = (struct MUI_NListtree_TreeNode *)xget(d->obj, MUIA_NListtree_Active);
    struct ABEntry *addr = (struct ABEntry *)(active->tn_User);
    AB_InsertAddress(obj, addr->Alias, addr->RealName, "");
  }
  else if(DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_IsMailList, d->obj) == TRUE)
  {
    struct Mail *mail;

    DoMethod(d->obj, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &mail);

    if(isSentMailFolder(mail->Folder))
      AB_InsertAddress(obj, "", mail->To.RealName,   mail->To.Address);
    else
      AB_InsertAddress(obj, "", mail->From.RealName, mail->From.Address);
  }

  RETURN(0);
  return 0;
}
///
/// OVERLOAD(MUIM_Popstring_Open)
/* this method is invoked when the MUI popup key is pressed, we let it trigger a notify, so that the address book will open -- in the future this should be removed and we should just use a Popupstring object */
OVERLOAD(MUIM_Popstring_Open)
{
  ENTER();

  set(obj, MUIA_Recipientstring_Popup, TRUE);

  RETURN(0);
  return 0;
}
///
/// OVERLOAD(MUIM_HandleEvent)
OVERLOAD(MUIM_HandleEvent)
{
  GETDATA;
  ULONG result = 0;
  struct IntuiMessage *imsg;

  ENTER();

  if(!(imsg = ((struct MUIP_HandleEvent *)msg)->imsg))
  {
    RETURN(0);
    return 0;
  }

  if(imsg->Class == IDCMP_RAWKEY)
  {
    switch(imsg->Code)
    {
      case IECODE_RETURN:
      {
        if(data->ResolveOnCR)
        {
          // only if we successfully resolved the string we move on to the next object.
          if(DoMethod(obj, MUIM_Recipientstring_Resolve, hasFlag(imsg->Qualifier, (IEQUALIFIER_RSHIFT | IEQUALIFIER_LSHIFT)) ? MUIF_Recipientstring_Resolve_NoFullName : MUIF_NONE))
          {
            set(data->Matchwindow, MUIA_Window_Open, FALSE);
            set(_win(obj), MUIA_Window_ActiveObject, obj);

            // If the MUIA_String_AdvanceOnCR is TRUE we have to set the next object active in the window
            // we have to check this within our instance data because Betterstring.mcc is buggy and don`t
            // return MUIA_String_AdvanceOnCR within a get().
            if(data->AdvanceOnCR)
            {
              set(_win(obj), MUIA_Window_ActiveObject, MUIV_Window_ActiveObject_Next);
            }
          }
          else
            DisplayBeep(_screen(obj));

          result = MUI_EventHandlerRC_Eat;
        }
      }
      break;

      /* keys are sent to the popup-list */
      case IECODE_UP:
      case IECODE_DOWN:
      case NM_WHEEL_UP:
      case NM_WHEEL_DOWN:
      case NM_WHEEL_LEFT:
      case NM_WHEEL_RIGHT:
      {
        // forward this event to the addrmatchlist
        if(DoMethod(data->Matchwindow, MUIM_Addrmatchlist_Event, imsg))
          result = MUI_EventHandlerRC_Eat;
      }
      break;

      case IECODE_DEL:
      case IECODE_ESCAPE: /* FIXME: Escape should clear the marked text. Currently the marked text goes when leaving the gadget or e.g. pressing ','. Seems to be a refresh problem */
        set(data->Matchwindow, MUIA_Window_Open, FALSE);
      break;

      // a IECODE_TAB will only be triggered if the tab key
      // is used within the matchwindow
      case IECODE_TAB:
      {
        if(xget(data->Matchwindow, MUIA_Window_Open))
        {
          set(data->Matchwindow, MUIA_Window_Open, FALSE);
          set(_win(obj), MUIA_Window_ActiveObject, obj);
          set(_win(obj), MUIA_Window_ActiveObject, MUIV_Window_ActiveObject_Next);
        }
      }
      break;

      default:
      {
        BOOL changed = FALSE;
        long select_size;

        // check if some text is actually selected
        if((select_size = xget(obj, MUIA_BetterString_SelectSize)) != 0)
        {
          if(imsg->Code == IECODE_BACKSPACE)
          {
            // now we do check whether everything until the end was selected
            long pos = xget(obj, MUIA_String_BufferPos)+select_size;
            char *content = (char *)xget(obj, MUIA_String_Contents);

            if(content && (content[pos] == '\0' || content[pos] == ','))
              DoMethod(obj, MUIM_BetterString_ClearSelected);

            changed = TRUE;
          }
          else if(ConvertKey(imsg) == ',')
            set(obj, MUIA_String_BufferPos, MUIV_BetterString_BufferPos_End);
        }

        // if we do not have an early change result we
        // do have to evaluate the superMethod call
        if(changed == FALSE)
        {
          char *old;
          char *new;

          // now we get a temporary copy of our string contents, call the supermethod
          // and compare if something has changed or not
          old = strdup((char *)xget(obj, MUIA_String_Contents));
          result = DoSuperMethodA(cl, obj, msg);
          new = (char *)xget(obj, MUIA_String_Contents);

          // check if the content changed
          if(strcmp(old, new) != 0)
            changed = TRUE;

          // free our temporary buffer
          free(old);
        }
        else
          result = DoSuperMethodA(cl, obj, msg);

        // if the content changed we do get the current recipient
        if(changed)
        {
          char *cur_rcpt = (char *)DoMethod(obj, MUIM_Recipientstring_CurrentRecipient);
          char *new_address;

          if(cur_rcpt &&
             (new_address = (char *)DoMethod(data->Matchwindow, MUIM_Addrmatchlist_Open, cur_rcpt)))
          {
            long start = DoMethod(obj, MUIM_Recipientstring_RecipientStart);
            long pos = xget(obj, MUIA_String_BufferPos);

            DoMethod(obj, MUIM_BetterString_Insert, &new_address[pos - start], pos);

            SetAttrs(obj, MUIA_String_BufferPos, pos,
                          MUIA_BetterString_SelectSize, strlen(new_address) - (pos - start),
                          TAG_DONE);
          }
        }
      }
      break;
    }
  }
  else if(imsg->Class == IDCMP_CHANGEWINDOW)
  {
    // only if the matchwindow is open we advice the matchwindow to refresh it`s position.
    if(xget(data->Matchwindow, MUIA_Window_Open))
      DoMethod(data->Matchwindow, MUIM_Addrmatchlist_ChangeWindow);
  }

  RETURN(result);
  return result;
}
///

/* Public Methods */
/// DECLARE(Resolve)
/* resolve all addresses */
DECLARE(Resolve) // ULONG flags
{
  GETDATA;
  BOOL list_expansion;
  LONG max_list_nesting = 5;
  STRPTR s, contents, tmp;
  BOOL res = TRUE;
  BOOL withrealname = TRUE, checkvalids = TRUE, withcache = TRUE;
  BOOL quiet;
  ULONG result;

  ENTER();

  quiet = muiRenderInfo(obj) == NULL ? TRUE : FALSE; // if this object doesn`t have a renderinfo we are quiet

  // Lets check the flags first
  if(hasNoFullNameFlag(msg->flags)) withrealname= FALSE;
  if(hasNoValidFlag(msg->flags))    checkvalids = FALSE;
  if(hasNoCacheFlag(msg->flags))    withcache   = FALSE;

  set(G->AB->GUI.LV_ADDRESSES, MUIA_NListtree_FindUserDataHook, &FindAddressHook);

  do
  {
    struct MUI_NListtree_TreeNode *tn;
    struct ABEntry *entry;
    BOOL quote = FALSE;

    list_expansion = FALSE;
    s = (STRPTR)xget(obj, MUIA_String_Contents);
    if(!(contents = tmp = strdup(s)))
      break;

    // clear the string gadget without notifing others
    nnset(obj, MUIA_String_Contents, NULL);

    D(DBF_GUI, "Resolve this string: '%s'", tmp);
    while((s = Trim(rcptok(tmp, &quote)))) /* tokenize string and resolve each recipient */
    {
      D(DBF_GUI, "token: '%s'", s);

      // if the resolve string is empty we skip it and go on
      if(!s[0])
      {
        tmp=NULL;
        continue;
      }

      if(checkvalids == FALSE && (tmp = strchr(s, '@')))
      {
        D(DBF_GUI, "Valid address found.. will not resolve it: %s", s);
        DoMethod(obj, MUIM_Recipientstring_AddRecipient, s);

        /* email address lacks domain... */
        if(tmp[1] == '\0')
          DoMethod(obj, MUIM_BetterString_Insert, strchr(C->EmailAddress, '@')+1, MUIV_BetterString_Insert_EndOfString);
      }
      else if((tn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_FindUserData, MUIV_NListtree_FindUserData_ListNode_Root, s, MUIF_NONE))) /* entry found in address book */
      {
        struct MUI_NListtree_TreeNode *nexttn = (struct MUI_NListtree_TreeNode *)DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_GetEntry, tn, MUIV_NListtree_GetEntry_Position_Next, MUIF_NONE);
        struct ABEntry *entry = (struct ABEntry *)tn->tn_User;

        D(DBF_GUI, "Found match: %s", s);

        // Now we have to check if there exists another entry in the AB with this string
        if(nexttn == NULL || DoMethod(G->AB->GUI.LV_ADDRESSES, MUIM_NListtree_FindUserData, nexttn, s, MUIV_NListtree_FindUserData_Flag_StartNode) == (ULONG)NULL)
        {
          if(entry->Type == AET_USER) /* it's a normal person */
          {
            D(DBF_GUI, "\tPlain user: %s (%s, %s)", AB_PrettyPrintAddress(entry), entry->RealName, entry->Address);
            DoMethod(obj, MUIM_Recipientstring_AddRecipient, withrealname && entry->RealName[0] ? AB_PrettyPrintAddress(entry) : (STRPTR)entry->Address);
          }
          else if(entry->Type == AET_LIST) /* it's a list of persons */
          {
            if(data->MultipleRecipients)
            {
              STRPTR members, lf;
              if((members = strdup(entry->Members)))
              {
                while((lf = strchr(members, '\n')))
                  lf[0] = ',';

                D(DBF_GUI, "Found list: �%s�", members);
                DoMethod(obj, MUIM_Recipientstring_AddRecipient, members);
                free(members);

                if(data->From && entry->RealName[0])
                  set(data->From, MUIA_String_Contents, AB_PrettyPrintAddress2(entry->RealName, C->EmailAddress));

                if(data->ReplyTo && entry->Address[0])
                  set(data->ReplyTo, MUIA_String_Contents, entry->Address);

                list_expansion = TRUE;
              }
            }
            else
            {
              D(DBF_GUI, "String doesn't allow multiple recipients");
              DoMethod(obj, MUIM_Recipientstring_AddRecipient, s);
              res = FALSE;
            }
          }
          else /* it's unknown... */
          {
            D(DBF_GUI, "Found matching entry in address book with unknown type: %ld", entry->Type);
            DoMethod(obj, MUIM_Recipientstring_AddRecipient, s);
            if(!quiet)
              set(_win(obj), MUIA_Window_ActiveObject, obj);
            res = FALSE;
          }
        }
        else
        {
          D(DBF_GUI, "Found more than one matching entry in address book!");
          DoMethod(obj, MUIM_Recipientstring_AddRecipient, s);
          if(!quiet)
            set(_win(obj), MUIA_Window_ActiveObject, obj);
          res = FALSE;
        }
      }
      else if(withcache && (entry = (struct ABEntry *)DoMethod(G->App, MUIM_YAM_FindEmailCacheMatch, s)))
      {
        D(DBF_GUI, "\tEmailCache Hit: %s (%s, %s)", AB_PrettyPrintAddress(entry), entry->RealName, entry->Address);
        DoMethod(obj, MUIM_Recipientstring_AddRecipient, withrealname && entry->RealName[0] ? AB_PrettyPrintAddress(entry) : (STRPTR)entry->Address);
      }
      else
      {
        D(DBF_GUI, "Entry not found: '%s'", s);

        if((tmp = strchr(s, '@'))) /* entry seems to be an email address */
        {
          D(DBF_GUI, "Email address: '%s'", s);
          DoMethod(obj, MUIM_Recipientstring_AddRecipient, s);

          /* email address lacks domain... */
          if(tmp[1] == '\0')
            DoMethod(obj, MUIM_BetterString_Insert, strchr(C->EmailAddress, '@')+1, MUIV_BetterString_Insert_EndOfString);
        }
        else
        {
          D(DBF_GUI, "No entry found in addressbook for alias: '%s'", s);
          DoMethod(obj, MUIM_Recipientstring_AddRecipient, s);
          if(!quiet) set(_win(obj), MUIA_Window_ActiveObject, obj);
          res = FALSE;
        }
      }

      tmp = NULL;
    }
    free(contents);

  } while(list_expansion && max_list_nesting-- > 0);

  result = (res ? xget(obj, MUIA_String_Contents) : 0);

  RETURN(result);
  return result;
}
///
/// DECLARE(AddRecipient)
/* add a recipient to this string taking care of comma (if in multi-mode). */
DECLARE(AddRecipient) // STRPTR address
{
  GETDATA;
  STRPTR contents;

  ENTER();

  D(DBF_GUI, "add recipient \"%s\"", msg->address);

  if(!data->MultipleRecipients)
    nnset(obj, MUIA_String_Contents, NULL);

  if((contents = (STRPTR)xget(obj, MUIA_String_Contents)), contents[0] != '\0')
    DoMethod(obj, MUIM_BetterString_Insert, ", ", MUIV_BetterString_Insert_EndOfString);

  DoMethod(obj, MUIM_BetterString_Insert, msg->address, MUIV_BetterString_Insert_EndOfString);
  set(obj, MUIA_String_BufferPos, -1);

  RETURN(0);
  return 0;
}
///
/// DECALRE(RecipientStart)
/* return the index where current recipient start (from cursor pos), this is only useful for objects with more than one recipient */
DECLARE(RecipientStart)
{
  STRPTR buf;
  ULONG pos, i;
  BOOL quote = FALSE;

  ENTER();

  buf = (STRPTR)xget(obj, MUIA_String_Contents);
  pos = xget(obj, MUIA_String_BufferPos);

  for(i = 0; i < pos; i++)
  {
    if(buf[i] == '\"')
      quote ^= TRUE;
  }

  while(i > 0 && (buf[i-1] != ',' || quote))
  {
    i--;
    if(buf[i] == '"')
      quote ^= TRUE;
  }
  while(isspace(buf[i]))
    i++;

  RETURN(i);
  return i;
}
///
/// DECLARE(CurrentRecipient)
/* return current recipient if cursor is at the end of it (i.e at comma or '\0'-byte */
DECLARE(CurrentRecipient)
{
  GETDATA;
  STRPTR buf, end;
  LONG pos;

  ENTER();

  if(data->CurrentRecipient)
  {
    free(data->CurrentRecipient);
    data->CurrentRecipient = NULL;
  }

  buf = (STRPTR)xget(obj, MUIA_String_Contents);
  pos = xget(obj, MUIA_String_BufferPos);

  if((buf[pos] == '\0' || buf[pos] == ',') &&
     (data->CurrentRecipient = strdup(&buf[DoMethod(obj, MUIM_Recipientstring_RecipientStart)])) &&
     (end = strchr(data->CurrentRecipient, ',')))
  {
    end[0] = '\0';
  }

  RETURN(data->CurrentRecipient);
  return (ULONG)data->CurrentRecipient;
}
///
/// DECLARE(ReplaceSelected)
DECLARE(ReplaceSelected) // char *address
{
  char *new_address = msg->address;
  char *old;
  char *ptr;
  long start;
  long pos;
  long len;

  ENTER();

  // we first have to clear the selected area
  DoMethod(obj, MUIM_BetterString_ClearSelected);

  start = DoMethod(obj, MUIM_Recipientstring_RecipientStart);
  old = (char *)xget(obj, MUIA_String_Contents);

  // try to find out the length of our current recipient
  if((ptr = strchr(&old[start], ',')))
    len = ptr-(&old[start]);
  else
    len = strlen(&old[start]);

  if(Strnicmp(new_address, &old[start], len) != 0)
  {
    SetAttrs(obj, MUIA_String_BufferPos, start,
                  MUIA_BetterString_SelectSize, len,
                  TAG_DONE);

    DoMethod(obj, MUIM_BetterString_ClearSelected);

    start = DoMethod(obj, MUIM_Recipientstring_RecipientStart);
  }

  pos = xget(obj, MUIA_String_BufferPos);

  DoMethod(obj, MUIM_BetterString_Insert, &new_address[pos - start], pos);

  SetAttrs(obj, MUIA_String_BufferPos, pos,
                MUIA_BetterString_SelectSize, strlen(new_address) - (pos - start),
                TAG_DONE);

  RETURN(0);
  return 0;
}
///
