/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 Marcel Beck
 Copyright (C) 2000-2013 YAM Open Source Team

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

 Superclass:  MUIC_Window
 Description: Folder edit window

***************************************************************************/

#include "FolderEditWindow_cl.h"

#include <stdlib.h>

#include <libraries/asl.h>
#include <libraries/iffparse.h>
#include <mui/NList_mcc.h>
#include <proto/dos.h>
#include <proto/muimaster.h>
#include <proto/xpkmaster.h>

#include "SDI_hook.h"

#include "YAM.h"
#include "YAM_addressbook.h"
#include "YAM_config.h"
#include "YAM_error.h"
#include "YAM_find.h"
#include "YAM_folderconfig.h"
#include "YAM_global.h"
#include "YAM_mainFolder.h"
#include "YAM_stringsizes.h"

#include "Busy.h"
#include "FileInfo.h"
#include "FolderList.h"
#include "Locale.h"
#include "MailList.h"
#include "MUIObjects.h"
#include "Requesters.h"
#include "Signature.h"
#include "UserIdentity.h"

#include "mui/FolderRequestListtree.h"
#include "mui/IdentityChooser.h"
#include "mui/MainFolderListtree.h"
#include "mui/Recipientstring.h"
#include "mui/SignatureChooser.h"
#include "mui/YAMApplication.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  Object *ST_FNAME;
  Object *ST_FPATH;
  Object *NM_MAXAGE;
  Object *CY_FMODE;
  Object *CY_FTYPE;
  Object *CY_SORT[2];
  Object *CH_REVERSE[2];
  Object *CH_EXPIREUNREAD;
  Object *ST_MLPATTERN;
  Object *CY_MLIDENTITY;
  Object *ST_MLREPLYTOADDRESS;
  Object *ST_MLADDRESS;
  Object *CY_MLSIGNATURE;
  Object *CH_STATS;
  Object *CH_MLSUPPORT;
  Object *BT_AUTODETECT;
  Object *BT_OKAY;
  Object *BT_CANCEL;
  Object *ST_HELLOTEXT;
  Object *ST_BYETEXT;
  Object *GR_MLPRORPERTIES;
  struct Folder *folder;
};
*/

#define INVALID_PATH_CHARACTERS ":/\";#?*|()[]<>"

/* Private Functions */
/// CompareFolders
// compare two folder structures for differences
static BOOL CompareFolders(const struct Folder *fo1, const struct Folder *fo2)
{
  BOOL equal = TRUE;

  ENTER();

  if(strcmp(fo1->Name,             fo2->Name) != 0 ||
     strcasecmp(fo1->Path,         fo2->Path) != 0 ||
     strcasecmp(fo1->Fullpath,     fo2->Fullpath) != 0 ||
     strcmp(fo1->Password,         fo2->Password) != 0 ||
     strcmp(fo1->WriteIntro,       fo2->WriteIntro) != 0 ||
     strcmp(fo1->WriteIntro,       fo2->WriteIntro) != 0 ||
     strcmp(fo1->WriteGreetings,   fo2->WriteGreetings) != 0 ||
     strcmp(fo1->MLReplyToAddress, fo2->MLReplyToAddress) != 0 ||
     strcmp(fo1->MLAddress,        fo2->MLAddress) != 0 ||
     strcmp(fo1->MLPattern,        fo2->MLPattern) != 0 ||
     (fo1->MLIdentity != NULL ? fo1->MLIdentity->id : -1) != (fo2->MLIdentity != NULL ? fo2->MLIdentity->id : -1) ||
     fo1->Mode                  != fo2->Mode ||
     fo1->Type                  != fo2->Type ||
     (fo1->MLSignature != NULL ? fo1->MLSignature->id : -1) != (fo2->MLSignature != NULL ? fo2->MLSignature->id : -1) ||
     fo1->Sort[0]               != fo2->Sort[0] ||
     fo1->Sort[1]               != fo2->Sort[1] ||
     fo1->MaxAge                != fo2->MaxAge ||
     fo1->ExpireUnread          != fo2->ExpireUnread ||
     fo1->Stats                 != fo2->Stats ||
     fo1->MLSupport             != fo2->MLSupport)
  {
    equal = FALSE;
  }

  RETURN(equal);
  return equal;
}

///
/// EnterPassword
//  Sets password for a protected folder
static BOOL EnterPassword(Object *obj, struct Folder *fo)
{
  BOOL result = FALSE;

  ENTER();

  do
  {
    char passwd[SIZE_PASSWORD];
    char passwd2[SIZE_PASSWORD];

    passwd[0] = '\0';
    passwd2[0] = '\0';

    if(StringRequest(passwd, SIZE_PASSWORD, tr(MSG_Folder), tr(MSG_CO_ChangeFolderPass), tr(MSG_Okay), NULL, tr(MSG_Cancel), TRUE, obj) == 0)
      break;

    if(passwd[0] != '\0' && StringRequest(passwd2, SIZE_PASSWORD, tr(MSG_Folder), tr(MSG_CO_RetypePass), tr(MSG_Okay), NULL, tr(MSG_Cancel), TRUE, obj) == 0)
      break;

    if(strcasecmp(passwd, passwd2) == 0)
    {
      strlcpy(fo->Password, passwd, sizeof(fo->Password));
      result = TRUE;
      break;
    }
    else
      DisplayBeep(NULL);
  }
  while(TRUE);

  RETURN(result);
  return result;
}

///
/// SaveOldFolder
static BOOL SaveOldFolder(struct IClass *cl, Object *obj)
{
  GETDATA;
  BOOL success = FALSE;
  struct Folder folder;

  ENTER();

  memcpy(&folder, data->folder, sizeof(folder));
  DoMethod(obj, METHOD(GUIToFolder), &folder);
  SHOWSTRING(DBF_FOLDER, folder.Name);

  // check if something has changed and if not we exit here immediately
  if(CompareFolders(&folder, data->folder) == FALSE)
  {
    BOOL nameChanged;
    nameChanged = (strcasecmp(data->folder->Name, folder.Name) != 0);

    // first check for a valid folder name
    // it is invalid if:
    // - the folder name is empty, or
    // - it was changed and the new name already exists
    if(folder.Name[0] == '\0' || (nameChanged == TRUE && FO_GetFolderByName(folder.Name, NULL) != NULL))
    {
      MUI_Request(_app(obj), obj, MUIF_NONE, NULL, tr(MSG_OkayReq), tr(MSG_FO_FOLDERNAMEINVALID));
      goto out;
    }

    // check if the filter name has changed and if it is part of
    // an active filter and if so rename it in the filter definition
    // as well.
    if(nameChanged == TRUE && FolderIsUsedByFilters(data->folder->Name) == TRUE)
      RenameFolderInFilters(data->folder->Name, folder.Name);

    // refresh a possibly existing folder tree in the search window
    if(nameChanged == TRUE && G->FI != NULL)
      DoMethod(G->FI->GUI.LV_FOLDERS, MUIM_FolderRequestListtree_RefreshTree);

    // copy the new folder name
    strlcpy(data->folder->Name, folder.Name, sizeof(data->folder->Name));

    SHOWSTRING(DBF_FOLDER, data->folder->Path);
    SHOWSTRING(DBF_FOLDER, data->folder->Fullpath);
    SHOWSTRING(DBF_FOLDER, folder.Path);
    SHOWSTRING(DBF_FOLDER, folder.Fullpath);

    // if the folderpath string has changed
    if(strcasecmp(data->folder->Path, folder.Path) != 0)
    {
      // check if the full pathes are different
      if(strcasecmp(data->folder->Fullpath, folder.Fullpath) != 0)
      {
        LONG result;

        // ask the user whether to perform the move or not
        result = MUI_Request(_app(obj), obj, MUIF_NONE, NULL, tr(MSG_YesNoReq), tr(MSG_FO_MOVEFOLDERTO), data->folder->Fullpath, folder.Fullpath);
        if(result == 1)
        {
          // first unload the old folder image to make it moveable/deletable
          FO_UnloadFolderImage(data->folder);

          if(Rename(data->folder->Fullpath, folder.Fullpath) == FALSE)
          {
            if(!(CreateDirectory(folder.Fullpath) && FO_MoveFolderDir(&folder, data->folder)))
            {
              ER_NewError(tr(MSG_ER_MOVEFOLDERDIR), folder.Name, folder.Fullpath);
              goto out;
            }
          }

          // now reload the image from the new path
          if(FO_LoadFolderImage(&folder) == TRUE)
          {
            // remember the newly obtained image pointer
            data->folder->imageObject = folder.imageObject;
          }
        }
        else
        {
          goto out;
        }
      }

      strlcpy(data->folder->Path, folder.Path, sizeof(data->folder->Path));
      strlcpy(data->folder->Fullpath, folder.Fullpath, sizeof(data->folder->Fullpath));
    }

    strlcpy(data->folder->WriteIntro,       folder.WriteIntro, sizeof(data->folder->WriteIntro));
    strlcpy(data->folder->WriteGreetings,   folder.WriteGreetings, sizeof(data->folder->WriteGreetings));
    strlcpy(data->folder->MLReplyToAddress, folder.MLReplyToAddress, sizeof(data->folder->MLReplyToAddress));
    strlcpy(data->folder->MLAddress,        folder.MLAddress, sizeof(data->folder->MLAddress));
    strlcpy(data->folder->MLPattern,        folder.MLPattern, sizeof(data->folder->MLPattern));
    data->folder->MLIdentity   = folder.MLIdentity;
    data->folder->MLSignature  = folder.MLSignature;
    data->folder->Sort[0]      = folder.Sort[0];
    data->folder->Sort[1]      = folder.Sort[1];
    data->folder->MaxAge       = folder.MaxAge;
    data->folder->ExpireUnread = folder.ExpireUnread;
    data->folder->Stats        = folder.Stats;
    data->folder->MLSupport    = folder.MLSupport;

    if(xget(data->CY_FTYPE, MUIA_Disabled) == FALSE)
    {
      enum FolderMode oldmode = data->folder->Mode;
      enum FolderMode newmode = folder.Mode;
      BOOL changed = TRUE;

      if(oldmode == newmode || (newmode > FM_SIMPLE && XpkBase == NULL))
      {
        changed = FALSE;
      }
      else if(!isProtectedFolder(&folder) && isProtectedFolder(data->folder) &&
              data->folder->LoadedMode != LM_VALID)
      {
        if((changed = MA_PromptFolderPassword(&folder, obj)) == FALSE)
          goto out;
      }
      else if(isProtectedFolder(&folder) && !isProtectedFolder(data->folder))
      {
        if((changed = EnterPassword(obj, &folder)) == FALSE)
          goto out;
      }

      if(isProtectedFolder(&folder) && isProtectedFolder(data->folder))
         strlcpy(folder.Password, data->folder->Password, sizeof(folder.Password));

      if(changed == TRUE)
      {
        if(!isProtectedFolder(&folder))
          folder.Password[0] = '\0';

        if(folder.Mode != oldmode)
        {
          struct BusyNode *busy;
          struct MailNode *mnode;
          ULONG i;

          busy = BusyBegin(BUSY_PROGRESS);
          BusyText(busy, tr(MSG_BusyUncompressingFO), "");

          LockMailListShared(folder.messages);

          i = 0;
          ForEachMailNode(folder.messages, mnode)
          {
            BusyProgress(busy, ++i, folder.Total);
            RepackMailFile(mnode->mail, folder.Mode, folder.Password);
          }

          UnlockMailList(folder.messages);

          BusyEnd(busy);

          data->folder->Mode = newmode;
        }

        strlcpy(data->folder->Password, folder.Password, sizeof(data->folder->Password));
      }
      data->folder->Type = folder.Type;
    }

    if(FO_SaveConfig(data->folder) == TRUE)
      success = TRUE;
  }
  else
  {
    // nothing changed
    success = TRUE;
  }

out:
  RETURN(success);
  return success;
}

///
/// SaveNewFolder
static BOOL SaveNewFolder(struct IClass *cl, Object *obj)
{
  GETDATA;
  BOOL success = FALSE;
  struct Folder folder;

  ENTER();

  memset(&folder, 0, sizeof(struct Folder));
  folder.ImageIndex = -1;

  if((folder.messages = CreateMailList()) != NULL)
  {
    LONG result;

    DoMethod(obj, MUIM_FolderEditWindow_GUIToFolder, &folder);
    SHOWSTRING(DBF_FOLDER, folder.Name);

    // first check for a valid folder name
    // it is invalid if:
    // - the folder name is empty, or
    // - the new name already exists
    if(folder.Name[0] == '\0' || FO_GetFolderByName(folder.Name, NULL) != NULL)
    {
      MUI_Request(_app(obj), obj, MUIF_NONE, NULL, tr(MSG_OkayReq), tr(MSG_FO_FOLDERNAMEINVALID));
      result = 0;
    }
    else
      result = 1;

    if(result == 1)
    {
      // lets check if entered folder path is valid or not
      if(folder.Path[0] == '\0')
      {
        MUI_Request(_app(obj), obj, MUIF_NONE, NULL, tr(MSG_OkayReq), tr(MSG_FO_FOLDERPATHINVALID));
        result = 0;
      }
      else if(FileExists(folder.Fullpath) == TRUE) // check if the combined full path already exists
      {
        result = MUI_Request(_app(obj), obj, MUIF_NONE, NULL, tr(MSG_YesNoReq), tr(MSG_FO_FOLDER_ALREADY_EXISTS), folder.Fullpath);
      }
      else
      {
        result = 1;
      }
    }

    // only if the user want to proceed we go on.
    if(result == 1)
    {
      if(isProtectedFolder(&folder) == FALSE || EnterPassword(obj, &folder) == TRUE)
      {
        if(CreateDirectory(folder.Fullpath) == TRUE)
        {
          if(FO_SaveConfig(&folder) == TRUE)
          {
            // allocate memory for the new folder
            if((data->folder = memdup(&folder, sizeof(folder))) != NULL)
            {
              struct FolderNode *fnode;

              // finally add the new folder to the global list
              LockFolderList(G->folders);
              fnode = AddNewFolderNode(G->folders, data->folder);
              UnlockFolderList(G->folders);

              if(fnode != NULL)
              {
                struct Folder *prevFolder;

                // allow the listtree to reorder our folder list
                set(G->MA->GUI.NL_FOLDERS, MUIA_MainFolderListtree_ReorderFolderList, TRUE);

                prevFolder = GetCurrentFolder();
                if(isGroupFolder(prevFolder))
                {
                  // add the folder to the end of the current folder group
                  DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_Insert, data->folder->Name, fnode, prevFolder->Treenode, MUIV_NListtree_Insert_PrevNode_Tail, MUIV_NListtree_Insert_Flag_Active);
                }
                else
                {
                  // add the folder after the current folder
                  DoMethod(G->MA->GUI.NL_FOLDERS, MUIM_NListtree_Insert, data->folder->Name, fnode, MUIV_NListtree_Insert_ListNode_Active, MUIV_NListtree_Insert_PrevNode_Active, MUIV_NListtree_Insert_Flag_Active);
                }

                // the MainFolderListtree class has catched the insert operation and
                // moved the new folder node within the folder list to the correct position.
                set(G->MA->GUI.NL_FOLDERS, MUIA_MainFolderListtree_ReorderFolderList, FALSE);

                success = TRUE;

                // No need to refresh a possibly existing folder tree in the search window here.
                // This has been done by the MUIM_NListtree_Insert method above already.
              }
            }
          }
        }
        else
        {
          LONG error = IoErr();
          char faultStr[256];

          Fault(error, NULL, faultStr, sizeof(faultStr));
          ER_NewError(tr(MSG_ER_CANNOT_CREATE_FOLDER), folder.Fullpath, faultStr);
        }
      }
    }
  }

  RETURN(success);
  return success;
}

///

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  static const char *ftypes[4];
  static const char *fmodes[5];
  static const char *sortopt[8];
  Object *ST_FNAME;
  Object *ST_FPATH;
  Object *NM_MAXAGE;
  Object *CY_FMODE;
  Object *CY_FTYPE;
  Object *CY_SORT[2];
  Object *CH_REVERSE[2];
  Object *CH_EXPIREUNREAD;
  Object *ST_MLPATTERN;
  Object *CY_MLIDENTITY;
  Object *ST_MLREPLYTOADDRESS;
  Object *ST_MLADDRESS;
  Object *CY_MLSIGNATURE;
  Object *CH_STATS;
  Object *CH_MLSUPPORT;
  Object *BT_AUTODETECT;
  Object *BT_OKAY;
  Object *BT_CANCEL;
  Object *ST_HELLOTEXT;
  Object *ST_BYETEXT;
  Object *GR_MLPRORPERTIES;

  ENTER();

  ftypes[0]  = tr(MSG_FO_FTRcvdMail);
  ftypes[1]  = tr(MSG_FO_FTSentMail);
  ftypes[2]  = tr(MSG_FO_FTBothMail);
  ftypes[3]  = NULL;

  fmodes[0]  = tr(MSG_FO_FMNormal);
  fmodes[1]  = tr(MSG_FO_FMSimple);
  // compression and encryption are only available if XPK is available
  fmodes[2]  = (XpkBase != NULL) ? tr(MSG_FO_FMPack) : NULL;
  fmodes[3]  = (XpkBase != NULL) ? tr(MSG_FO_FMEncPack) : NULL;
  fmodes[4]  = NULL;

  sortopt[0] = tr(MSG_FO_MessageDate);
  sortopt[1] = tr(MSG_FO_DateRecvd);
  sortopt[2] = tr(MSG_Sender);
  sortopt[3] = tr(MSG_Recipient);
  sortopt[4] = tr(MSG_Subject);
  sortopt[5] = tr(MSG_Size);
  sortopt[6] = tr(MSG_Status);
  sortopt[7] = NULL;

  if((obj = DoSuperNew(cl, obj,
    MUIA_Window_Title, tr(MSG_FO_EditFolder),
    MUIA_HelpNode,  "Windows#Foldersettings",
    MUIA_Window_ID, MAKE_ID('F','O','L','D'),
    MUIA_Window_LeftEdge, MUIV_Window_LeftEdge_Centered,
    MUIA_Window_TopEdge,  MUIV_Window_TopEdge_Centered,
    WindowContents, VGroup,
      Child, ColGroup(2), GroupFrameT(tr(MSG_FO_Properties)),
        Child, Label2(tr(MSG_FO_DISPLAYNAME)),
        Child, ST_FNAME = MakeString(SIZE_NAME, tr(MSG_FO_DISPLAYNAME)),
        Child, Label2(tr(MSG_FO_DIRECTORYNAME)),
        Child, ST_FPATH = MakeString(SIZE_PATH, tr(MSG_FO_DIRECTORYNAME)),
        Child, Label2(tr(MSG_FO_MaxAge)),
        Child, HGroup,
          Child, NM_MAXAGE = NumericbuttonObject,
            MUIA_CycleChain,      1,
            MUIA_Numeric_Min,     0,
            MUIA_Numeric_Max,     730,
            MUIA_Numeric_Format,  tr(MSG_FO_MAXAGEFMT),
          End,
          Child, CH_EXPIREUNREAD = MakeCheck(tr(MSG_FO_EXPIREUNREAD)),
          Child, LLabel1(tr(MSG_FO_EXPIREUNREAD)),
          Child, HSpace(0),
        End,
        Child, Label1(tr(MSG_FO_FolderType)),
        Child, CY_FTYPE = MakeCycle(ftypes,tr(MSG_FO_FolderType)),
        Child, Label1(tr(MSG_FO_FolderMode)),
        Child, CY_FMODE = MakeCycle(fmodes,tr(MSG_FO_FolderMode)),
        Child, Label1(tr(MSG_FO_SortBy)),
        Child, HGroup,
          Child, CY_SORT[0] = MakeCycle(sortopt,tr(MSG_FO_SortBy)),
          Child, CH_REVERSE[0] = MakeCheck(tr(MSG_FO_Reverse)),
          Child, LLabel1(tr(MSG_FO_Reverse)),
        End,
        Child, Label1(tr(MSG_FO_ThenBy)),
        Child, HGroup,
          Child, CY_SORT[1] = MakeCycle(sortopt,tr(MSG_FO_ThenBy)),
          Child, CH_REVERSE[1] = MakeCheck(tr(MSG_FO_Reverse)),
          Child, LLabel1(tr(MSG_FO_Reverse)),
        End,
        Child, Label2(tr(MSG_FO_Welcome)),
        Child, ST_HELLOTEXT = MakeString(SIZE_INTRO,tr(MSG_FO_Welcome)),
        Child, Label2(tr(MSG_FO_Greetings)),
        Child, ST_BYETEXT = MakeString(SIZE_INTRO,tr(MSG_FO_Greetings)),
        Child, Label1(tr(MSG_FO_DSTATS)),
        Child, HGroup,
          Child, CH_STATS = MakeCheck(tr(MSG_FO_DSTATS)),
          Child, HSpace(0),
        End,
      End,
      Child, GR_MLPRORPERTIES = ColGroup(2), GroupFrameT(tr(MSG_FO_MLSupport)),
        MUIA_ShowMe, FALSE,
        Child, Label2(tr(MSG_FO_MLSUPPORT)),
        Child, HGroup,
          Child, CH_MLSUPPORT = MakeCheck(tr(MSG_FO_MLSUPPORT)),
          Child, HVSpace,
          Child, BT_AUTODETECT = MakeButton(tr(MSG_FO_AUTODETECT)),
        End,
        Child, Label2(tr(MSG_FO_TO_PATTERN)),
        Child, ST_MLPATTERN = MakeString(SIZE_PATTERN,tr(MSG_FO_TO_PATTERN)),
        Child, Label2(tr(MSG_FO_TO_ADDRESS)),
        Child, MakeAddressField(&ST_MLADDRESS, tr(MSG_FO_TO_ADDRESS), MSG_HELP_FO_ST_MLADDRESS, ABM_CONFIG, -1, AFF_ALLOW_MULTI),
        Child, Label2(tr(MSG_FO_FROM_ADDRESS)),
        Child, CY_MLIDENTITY = IdentityChooserObject,
          MUIA_ControlChar, ShortCut(tr(MSG_FO_FROM_ADDRESS)),
          End,
        Child, Label2(tr(MSG_FO_REPLYTO_ADDRESS)),
        Child, MakeAddressField(&ST_MLREPLYTOADDRESS, tr(MSG_FO_REPLYTO_ADDRESS), MSG_HELP_FO_ST_MLREPLYTOADDRESS, ABM_CONFIG, -1, AFF_ALLOW_MULTI),
        Child, Label1(tr(MSG_WR_Signature)),
        Child, CY_MLSIGNATURE = SignatureChooserObject,
          MUIA_ControlChar, ShortCut(tr(MSG_WR_Signature)),
        End,
      End,
      Child, ColGroup(3),
        Child, BT_OKAY = MakeButton(tr(MSG_Okay)),
        Child, HSpace(0),
        Child, BT_CANCEL = MakeButton(tr(MSG_Cancel)),
      End,
    End,

    TAG_MORE, inittags(msg))) != NULL)
  {
    GETDATA;

    data->ST_FNAME            = ST_FNAME;
    data->ST_FPATH            = ST_FPATH;
    data->NM_MAXAGE           = NM_MAXAGE;
    data->CY_FMODE            = CY_FMODE;
    data->CY_FTYPE            = CY_FTYPE;
    data->CY_SORT[0]          = CY_SORT[0];
    data->CY_SORT[1]          = CY_SORT[1];
    data->CH_REVERSE[0]       = CH_REVERSE[0];
    data->CH_REVERSE[1]       = CH_REVERSE[1];
    data->CH_EXPIREUNREAD     = CH_EXPIREUNREAD;
    data->ST_MLPATTERN        = ST_MLPATTERN;
    data->CY_MLIDENTITY       = CY_MLIDENTITY;
    data->ST_MLREPLYTOADDRESS = ST_MLREPLYTOADDRESS;
    data->ST_MLADDRESS        = ST_MLADDRESS;
    data->CY_MLSIGNATURE      = CY_MLSIGNATURE;
    data->CH_STATS            = CH_STATS;
    data->CH_MLSUPPORT        = CH_MLSUPPORT;
    data->BT_AUTODETECT       = BT_AUTODETECT;
    data->BT_OKAY             = BT_OKAY;
    data->BT_CANCEL           = BT_CANCEL;
    data->ST_HELLOTEXT        = ST_HELLOTEXT;
    data->ST_BYETEXT          = ST_BYETEXT;
    data->GR_MLPRORPERTIES    = GR_MLPRORPERTIES;

    data->folder = (struct Folder *)GetTagData(ATTR(Folder), (ULONG)NULL, inittags(msg));

    DoMethod(G->App, OM_ADDMEMBER, obj);

    set(ST_FPATH, MUIA_String_Reject, INVALID_PATH_CHARACTERS);
    set(CH_STATS, MUIA_Disabled, C->WBAppIcon == FALSE && C->DockyIcon == FALSE);

    SetHelp(ST_FNAME,        MSG_HELP_FO_ST_FNAME       );
    SetHelp(ST_FPATH,        MSG_HELP_FO_TX_FPATH       );
    SetHelp(NM_MAXAGE,       MSG_HELP_FO_ST_MAXAGE      );
    SetHelp(CY_FMODE,        MSG_HELP_FO_CY_FMODE       );
    SetHelp(CY_FTYPE,        MSG_HELP_FO_CY_FTYPE       );
    SetHelp(CY_SORT[0],      MSG_HELP_FO_CY_SORT0       );
    SetHelp(CY_SORT[1],      MSG_HELP_FO_CY_SORT1       );
    SetHelp(CH_REVERSE[0],   MSG_HELP_FO_CH_REVERSE     );
    SetHelp(CH_REVERSE[1],   MSG_HELP_FO_CH_REVERSE     );
    SetHelp(ST_MLPATTERN,    MSG_HELP_FO_ST_MLPATTERN   );
    SetHelp(CY_MLSIGNATURE,  MSG_HELP_FO_CY_MLSIGNATURE );
    SetHelp(CH_STATS,        MSG_HELP_FO_CH_STATS       );
    SetHelp(CH_EXPIREUNREAD, MSG_HELP_FO_CH_EXPIREUNREAD);
    SetHelp(CH_MLSUPPORT,    MSG_HELP_FO_CH_MLSUPPORT   );
    SetHelp(BT_AUTODETECT,   MSG_HELP_FO_BT_AUTODETECT  );
    SetHelp(ST_HELLOTEXT,    MSG_HELP_FO_ST_HELLOTEXT   );
    SetHelp(ST_BYETEXT,      MSG_HELP_FO_ST_BYETEXT     );

    DoMethod(BT_CANCEL, MUIM_Notify, MUIA_Pressed, FALSE, obj, 3, MUIM_Set, ATTR(DisposeMe), TRUE);
    DoMethod(obj, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, obj, 3, MUIM_Set, ATTR(DisposeMe), TRUE);

    DoMethod(BT_AUTODETECT, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, METHOD(MLAutoDetect));
    DoMethod(BT_OKAY, MUIM_Notify, MUIA_Pressed, FALSE, obj, 1, METHOD(SaveFolder));
    DoMethod(ST_FNAME, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, obj, 2, METHOD(AdaptPath), MUIV_TriggerValue);
    DoMethod(NM_MAXAGE, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime, data->CH_EXPIREUNREAD, 3, MUIM_Set, MUIA_Disabled, MUIV_NotTriggerValue);
    DoMethod(CH_MLSUPPORT, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, obj, 2, METHOD(MLSupportUpdate), MUIV_NotTriggerValue);
  }

  RETURN((IPTR)obj);
  return (IPTR)obj;
}

///
/// OVERLOAD(OM_SET)
OVERLOAD(OM_SET)
{
  GETDATA;
  struct TagItem *tags = inittags(msg), *tag;

  while((tag = NextTagItem((APTR)&tags)) != NULL)
  {
    switch(tag->ti_Tag)
    {
      case ATTR(Folder):
      {
        data->folder = (struct Folder *)tag->ti_Data;
        DoMethod(obj, METHOD(FolderToGUI));
        tag->ti_Tag = TAG_IGNORE;
      }
      break;
    }
  }

  return DoSuperMethodA(cl, obj, msg);
}

///

/* Public Methods */
/// DECLARE(FolderToGUI)
DECLARE(FolderToGUI)
{
  GETDATA;
  struct Folder *folder = data->folder;
  struct Folder dummy;
  static const int type2cycle[10] = { FT_CUSTOM, FT_CUSTOM, FT_INCOMING, FT_INCOMING, FT_OUTGOING, -1, FT_INCOMING, FT_OUTGOING, FT_CUSTOM, FT_CUSTOM };
  int i;
  BOOL isDefault;
  BOOL isArchive;

  ENTER();

  if(folder == NULL)
  {
    InitFolder(&dummy, FT_CUSTOM);
    folder = &dummy;
  }

  isDefault = isDefaultFolder(folder);
  isArchive = isArchiveFolder(folder);

  nnset(data->ST_FNAME, MUIA_String_Contents, folder->Name);
  nnset(data->ST_FPATH, MUIA_String_Contents, folder->Path);
  set(data->ST_FNAME, MUIA_Disabled, isDefault || isArchive);
  set(data->ST_FPATH, MUIA_Disabled, isDefault || isArchive);

  xset(data->NM_MAXAGE, MUIA_Numeric_Value, folder->MaxAge,
                        MUIA_Disabled,      isArchive);

  xset(data->CH_EXPIREUNREAD, MUIA_Selected, folder->ExpireUnread,
                              MUIA_Disabled, isTrashFolder(folder) || isSpamFolder(folder) || folder->MaxAge == 0 || isArchive);

  xset(data->CY_FTYPE, MUIA_Cycle_Active, type2cycle[folder->Type],
                       MUIA_Disabled,     isDefault || isArchive);

  xset(data->CY_FMODE, MUIA_Cycle_Active, folder->Mode,
                       MUIA_Disabled,     isDefault);

  for(i = 0; i < 2; i++)
  {
    set(data->CY_SORT[i], MUIA_Cycle_Active, (folder->Sort[i] < 0 ? -folder->Sort[i] : folder->Sort[i])-1);
    set(data->CH_REVERSE[i], MUIA_Selected, folder->Sort[i] < 0);
  }

  set(data->CH_STATS,      MUIA_Selected, folder->Stats);
  xset(data->ST_HELLOTEXT, MUIA_String_Contents, folder->WriteIntro,
                           MUIA_Disabled,        isArchive);
  xset(data->ST_BYETEXT,   MUIA_String_Contents, folder->WriteGreetings,
                           MUIA_Disabled,        isArchive);

  // for ML-Support
  // The complete group is hidden for default and archive folders which do
  // not support mailing lists. This avoids the need to disable the indivial
  // objects for non-mailinglist folders.
  set(data->GR_MLPRORPERTIES, MUIA_ShowMe, isDefault == FALSE && isArchive == FALSE);
  set(data->BT_AUTODETECT, MUIA_Disabled, !folder->MLSupport || isDefault || isArchive);
  set(data->CH_MLSUPPORT, MUIA_Selected, isDefault || isArchive ? FALSE : folder->MLSupport);
  set(data->ST_MLADDRESS, MUIA_String_Contents, folder->MLAddress);
  set(data->ST_MLPATTERN, MUIA_String_Contents, folder->MLPattern);
  set(data->ST_MLREPLYTOADDRESS, MUIA_String_Contents, folder->MLReplyToAddress);
  set(data->CY_MLSIGNATURE, MUIA_SignatureChooser_Signature, folder->MLSignature);
  set(data->CY_MLIDENTITY, MUIA_IdentityChooser_Identity, folder->MLIdentity);

  // make the folder name object the active one for new folders
  if(data->folder == NULL)
    set(obj, MUIA_Window_ActiveObject, data->ST_FNAME);

  // we make sure the window is at the front if it is already open
  if(xget(obj, MUIA_Window_Open) == TRUE)
    DoMethod(obj, MUIM_Window_ToFront);

  RETURN(0);
  return 0;
}

///
/// DECLARE(GUIToFolder)
DECLARE(GUIToFolder) // struct Folder *folder
{
  GETDATA;
  struct Folder *folder = msg->folder;
  static const int cycle2type[3] = { FT_CUSTOM, FT_CUSTOMSENT, FT_CUSTOMMIXED };
  int i;

  ENTER();

  GetMUIString(folder->Name, data->ST_FNAME, sizeof(folder->Name));
  GetMUIString(folder->Path, data->ST_FPATH, sizeof(folder->Path));

  // set up the full path to the folder
  BuildFolderPath(folder->Fullpath, folder->Path, sizeof(folder->Fullpath));

  folder->MaxAge = GetMUINumer(data->NM_MAXAGE);
  if(!isDefaultFolder(folder) && !isArchiveFolder(folder))
  {
    folder->Type = cycle2type[GetMUICycle(data->CY_FTYPE)];
    folder->Mode = GetMUICycle(data->CY_FMODE);
  }

  for(i = 0; i < 2; i++)
  {
    folder->Sort[i] = GetMUICycle(data->CY_SORT[i]) + 1;
    if (GetMUICheck(data->CH_REVERSE[i]))
      folder->Sort[i] = -folder->Sort[i];
  }

  folder->ExpireUnread = GetMUICheck(data->CH_EXPIREUNREAD);
  folder->Stats = GetMUICheck(data->CH_STATS);
  folder->MLSupport = GetMUICheck(data->CH_MLSUPPORT);

  GetMUIString(folder->WriteIntro, data->ST_HELLOTEXT, sizeof(folder->WriteIntro));
  GetMUIString(folder->WriteGreetings, data->ST_BYETEXT, sizeof(folder->WriteGreetings));

  GetMUIString(folder->MLPattern, data->ST_MLPATTERN, sizeof(folder->MLPattern));

  // resolve the addresses first, in case someone entered an alias
  DoMethod(data->ST_MLADDRESS, MUIM_Recipientstring_Resolve, MUIF_NONE);
  DoMethod(data->ST_MLREPLYTOADDRESS, MUIM_Recipientstring_Resolve, MUIF_NONE);

  GetMUIString(folder->MLAddress, data->ST_MLADDRESS, sizeof(folder->MLAddress));
  GetMUIString(folder->MLReplyToAddress, data->ST_MLREPLYTOADDRESS, sizeof(folder->MLReplyToAddress));

  folder->MLSignature = (struct SignatureNode *)xget(data->CY_MLSIGNATURE, MUIA_SignatureChooser_Signature);
  folder->MLIdentity = (struct UserIdentityNode *)xget(data->CY_MLIDENTITY, MUIA_IdentityChooser_Identity);

  RETURN(0);
  return 0;
}

///
/// DECLARE(AdaptPath)
DECLARE(AdaptPath) // const char *name
{
  GETDATA;
  const char *path;

  ENTER();

  path = (const char *)xget(data->ST_FPATH, MUIA_String_Contents);
  D(DBF_FOLDER, "name '%s'", msg->name);
  D(DBF_FOLDER, "path '%s'", path);

  if(strncmp(path, msg->name, strlen(path)) == 0)
  {
    // name and path strings have an identical start
    // now strip possible invalid characters from the path
    char *newPath;

    if((newPath = strdup(msg->name)) != NULL)
    {
      char *p = newPath;

      while(p[0] != '\0')
      {
        if(strchr(INVALID_PATH_CHARACTERS, p[0]) != NULL)
          memmove(p, p+1, strlen(p+1)+1);
        else
          p++;
      }

      D(DBF_FOLDER, "adapted path '%s'", newPath);
      nnset(data->ST_FPATH, MUIA_String_Contents, newPath);
      free(newPath);
    }
  }

  RETURN(0);
  return 0;
}

/// DECLARE(MLSupportUpdate)
DECLARE(MLSupportUpdate) // ULONG disabled
{
  GETDATA;

  ENTER();

  DoMethod(obj, MUIM_MultiSet, MUIA_Disabled, msg->disabled,
    data->BT_AUTODETECT,
    data->ST_MLPATTERN,
    data->ST_MLADDRESS,
    data->CY_MLIDENTITY,
    data->ST_MLREPLYTOADDRESS,
    data->CY_MLSIGNATURE,
    NULL);

  RETURN(0);
  return 0;
}

///
/// DECLARE(MLAutoDetect)
DECLARE(MLAutoDetect)
{
  GETDATA;

  ENTER();

  if(data->folder != NULL)
  {
    LockMailListShared(data->folder->messages);

    if(IsMailListEmpty(data->folder->messages) == FALSE)
    {
      #define SCANMSGS  5
      struct MailNode *mnode;
      char *toPattern;
      char *toAddress;
      char *res = NULL;
      BOOL takePattern = TRUE;
      BOOL takeAddress = TRUE;
      int i;
      BOOL success;

      mnode = FirstMailNode(data->folder->messages);
      toPattern = mnode->mail->To.Address;
      toAddress = mnode->mail->To.Address;

      i = 0;
      success = TRUE;
      ForEachMailNode(data->folder->messages, mnode)
      {
        // skip the first mail as this has already been processed before
        if(i > 0)
        {
          struct Mail *mail = mnode->mail;
          char *result;

          D(DBF_FOLDER, "SWS: [%s] [%s]", toPattern, mail->To.Address);

          // Analyze the toAdress through the Smith&Waterman algorithm
          if(takePattern == TRUE && (result = SWSSearch(toPattern, mail->To.Address)) != NULL)
          {
            free(res);

            if((res = strdup(result)) == NULL)
            {
              success = FALSE;
              break;
            }

            toPattern = res;

            // If we reached a #? pattern then we break here
            if(strcmp(toPattern, "#?") == 0)
              takePattern = FALSE;
          }

          // Lets check if the toAddress kept the same and then we can use
          // it for the TOADDRESS string gadget
          if(takeAddress == TRUE && strcasecmp(toAddress, mail->To.Address) != 0)
            takeAddress = FALSE;
        }

        if(++i > SCANMSGS)
          break;
      }

      UnlockMailList(data->folder->messages);

      if(success == TRUE)
      {
        // lets make a pattern out of the found SWS string
        if(takePattern == TRUE)
        {
          if(strlen(toPattern) >= 2 && !(toPattern[0] == '#' && toPattern[1] == '?'))
          {
            if(res != NULL)
              res = realloc(res, strlen(res)+3);
            else if((res = malloc(strlen(toPattern)+3)) != NULL)
              strlcpy(res, toPattern, strlen(toPattern));

            if(res != NULL)
            {
              // move the actual string to the back and copy the wildcard in front of it.
              memmove(&res[2], res, strlen(res)+1);
              res[0] = '#';
              res[1] = '?';

              toPattern = res;
            }
            else
              success = FALSE;
          }

          if(success == TRUE && strlen(toPattern) >= 2 && !(toPattern[strlen(toPattern)-2] == '#' && toPattern[strlen(toPattern)-1] == '?'))
          {
            if(res != NULL)
              res = realloc(res, strlen(res)+3);
            else if((res = malloc(strlen(toPattern)+3)) != NULL)
              strlcpy(res, toPattern, strlen(toPattern));

            if(res != NULL)
            {
              // and now copy also the wildcard at the back of the string
              strcat(res, "#?");

              toPattern = res;
            }
            else
              success = FALSE;
          }
        }

        if(success == TRUE)
        {
          D(DBF_FOLDER, "ML-Pattern: [%s]", toPattern);

          // Now we set the new pattern & address values to the string gadgets
          setstring(data->ST_MLPATTERN, takePattern == TRUE && toPattern[0] != '\0' ? toPattern : tr(MSG_FO_NOTRECOGNIZED));
          setstring(data->ST_MLADDRESS, takeAddress == TRUE ? toAddress : tr(MSG_FO_NOTRECOGNIZED));
        }
      }

      // free all resources
      free(res);

      SWSSearch(NULL, NULL);
    }
  }

  RETURN(0);
  return 0;
}

///
/// DECLARE(SaveFolder)
DECLARE(SaveFolder)
{
  GETDATA;
  BOOL success;
  BOOL newFolder;

  ENTER();

  if(data->folder != NULL)
  {
    D(DBF_FOLDER, "old folder=%08lx '%s'", data->folder, SafeStr(data->folder->Name));
    newFolder = FALSE;
    success = SaveOldFolder(cl, obj);
  }
  else
  {
    D(DBF_FOLDER, "new folder");
    newFolder = TRUE;
    success = SaveNewFolder(cl, obj);
  }

  if(success == TRUE)
  {
    MA_SetSortFlag();
    DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_NList_Redraw, MUIV_NList_Redraw_Title);
    DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_NList_Sort);
    MA_ChangeFolder(FO_GetFolderByName(data->folder->Name, NULL), FALSE);

    // Save the folder tree only if we just created a new folder, otherwise
    // a temporarily modified open/close state of folder groups will be saved
    // as well, even if the user didn't want this.
    if(newFolder == TRUE)
      FO_SaveTree();

    DisplayStatistics(data->folder, TRUE);

    // ask for disposing
    set(obj, ATTR(DisposeMe), TRUE);
  }

  RETURN(0);
  return 0;
}

///