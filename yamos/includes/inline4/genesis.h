#ifndef INLINE4_GENESIS_H
#define INLINE4_GENESIS_H

/*
** This file was auto generated by idltool 50.10.
**
** It provides compatibility to OS3 style library
** calls by substituting functions.
**
** Do not edit manually.
*/ 

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef EXEC_EXEC_H
#include <exec/exec.h>
#endif
#ifndef EXEC_INTERFACES_H
#include <exec/interfaces.h>
#endif

#ifndef LIBRARIES_GENESIS_H
#include <libraries/genesis.h>
#endif

/* Inline macros for Interface "main" */
#define GetFileSize(last) IGenesis->GetFileSize(last) 
#define ParseConfig(par1, last) IGenesis->ParseConfig(par1, last) 
#define ParseNext(last) IGenesis->ParseNext(last) 
#define ParseNextLine(last) IGenesis->ParseNextLine(last) 
#define ParseEnd(last) IGenesis->ParseEnd(last) 
#define ReallocCopy(par1, last) IGenesis->ReallocCopy(par1, last) 
#define FreeUser(last) IGenesis->FreeUser(last) 
#define GetUserName(par1, par2, last) IGenesis->GetUserName(par1, par2, last) 
#define GetUser(par1, par2, par3, last) IGenesis->GetUser(par1, par2, par3, last) 
#define GetGlobalUser() IGenesis->GetGlobalUser() 
#define SetGlobalUser(last) IGenesis->SetGlobalUser(last) 
#define ClearUserList() IGenesis->ClearUserList() 
#define ReloadUserList() IGenesis->ReloadUserList() 
#define ReadFile(par1, par2, last) IGenesis->ReadFile(par1, par2, last) 
#define WriteFile(par1, par2, last) IGenesis->WriteFile(par1, par2, last) 
#define IsOnline(last) IGenesis->IsOnline(last) 

#endif /* INLINE4_GENESIS_H */
