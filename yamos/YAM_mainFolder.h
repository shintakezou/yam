#ifndef YAM_MAINFOLDER_H
#define YAM_MAINFOLDER_H

/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2001 by YAM Open Source Team

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

#include "YAM_utilities.h"

struct Mail
{
   struct Mail *    Next;
   struct Mail *    Reference;
   struct Folder *  Folder;
   char *           UIDL;
   long             cMsgID;
   long             cIRTMsgID;
   long             Size;
   int              Flags;
   int              Position;
   int              Index;
   struct DateStamp Date;
   struct Person    From;
   struct Person    To;
   struct Person    ReplyTo;

   char             Status;
   char             Importance;
   char             Subject[SIZE_SUBJECT];
   char             MailFile[SIZE_MFILE];
};

struct ExtendedMail
{
   struct Mail      Mail;
   struct Person *  STo;
   struct Person *  CC;
   struct Person *  BCC;
   char *           Headers;
   char *           SenderInfo;
   int              NoSTo;
   int              NoCC;
   int              NoBCC;
   int              Signature;
   int              Security;
   int              ReceiptType;
   BOOL             DelSend;
   BOOL             RetRcpt;
   struct Person    ReceiptTo;
   struct Person    OriginalRcpt;

   char             MsgID[SIZE_MSGID];
   char             IRTMsgID[SIZE_MSGID];
};

struct ExtendedMail *MA_ExamineMail(struct Folder *folder, char *file, char *statstr, BOOL deep);
void   MA_FreeEMailStruct(struct ExtendedMail *email);

#endif /* YAM_MAINFOLDER_H */
