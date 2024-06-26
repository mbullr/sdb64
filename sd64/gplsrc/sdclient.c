/* SDCLIENT.C
 * SD VB Client DLL
 * Copyright (c) 2007 Ladybridge Systems, All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * 
 * START-HISTORY:
 * 31 Dec 23 SD launch - prior history suppressed
 * END-HISTORY
 *
 * START-DESCRIPTION:
 *
 *
 *
 * Session Management
 * ==================
 * SDConnect()
 * SDConnected()
 * SDConnectLocal()
 * SDDisconnect()
 * SDDisconnectAll()
 * SDGetSession()
 * SDSetSession()
 * SDLogto()
 * 
 * 
 * File Handling
 * =============
 * SDOpen()
 * SDClose()
 * SDRead()
 * SDReadl()
 * SDReadu()
 * SDWrite()
 * SDWriteu()
 * SDDelete()
 * SDDeleteu()
 * SDRelease()
 * SDSelect()
 * SDSelectIndex()
 * SDSelectLeft()
 * SDSelectRight()
 * SDSetLeft()
 * SDSetRight()
 * SDReadNext()
 * SDClearSelect()
 * SDRecordlock()
 * SDMarkMapping()
 *
 *
 * Dynamic Array Handling
 * ======================
 * SDExtract()
 * SDReplace()
 * SDIns()
 * SDDel()
 * SDLocate()
 *
 *
 * String Handling
 * ===============
 * SDChange()
 * SDDcount()
 * SDField()
 * SDMatch()
 * SDMatchfield()
 *
 * Command Execution
 * =================
 * SDExecute()
 * SDRespond()
 * SDEndCommand()
 *
 * Subroutine Execution
 * ====================
 * SDCall()
 *
 *
 * Error Handling
 * ==============
 * SDError()
 * SDStatus()
 *
 *
 * Internal
 * ========
 * SDDebug()
 * SDEnterPackage()
 * SDExitPackage()
 *
 *
 * VB data type compatibility:
 *
 *  VB               C
 *  Bool ByVal       int16_t     (0 or -1)
 *  Bool ByRef       int16_t *   (0 or -1)
 *  Integer ByVal    int16_t
 *  Long ByVal       int32_t
 *  Integer          int16_t *
 *  Long             int32_t *
 *  String ByVal     BSTR
 *  String           BSTR *
 *sd
 * END-DESCRIPTION
sd
 * START-CODE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys\types.h>
#include <sys\stat.h>
SDnclude <io.h>
#include <winsock2.h>

#define Public
#include <sddefs.h>
#include <revstamp.h>
#include <sdclient.h>
#include <err.h>
void set_default_character_maps(void);

#include <windows.h>
#include <stdarg.h>

/* Network data */
Private bool OpenSocket(struct SESSION* session, char* host, int16_t port);
Private bool CloseSocket(struct SESSION* session);
Private bool read_packet(struct SESSION* session);
Private bool write_packet(struct SESSION* session,
                          int type,
                          char* data,
                          int32_t bytes);
Private void NetError(char* prefix);
Private void debug(unsigned char* p, int n);
Private struct SESSION* FindFreeSession(void);
Private void disconnect(struct SESSION* session);

typedef int16_t VBBool;
#define VBFalse 0
#define VBTrue -1

typedef struct ARGDATA ARGDATA;
struct ARGDATA {
  int16_t argno;
  int32_t arglen;
  char text[1];
};

/* Packet buffer */
#define BUFF_INCR 4096
typedef struct INBUFF INBUFF;
struct INBUFF {
  union {
    struct {
      char message[1];
    } abort;
    struct { /* SDCall returned arguments */
      struct ARGDATA argdata;
    } call;
    struct { /* Error text retrieval */
      char text[1];
    } error;
    struct { /* SDExecute */
      char reply[1];
    } execute;
    struct { /* SDOpen */
      int16_t fno;
    } open;
    struct { /* SDRead, SDReadl, SDReadu */
      char rec[1];
    } read;
    struct { /* SDReadList */
      char list[1];
    } readlist;
    struct { /* SDReadNext */
      char id[1];
    } readnext;
    struct { /* SDSelectLeft, SDSelectRight */
      char key[1];
    } selectleft;
  } data;
};

FILE* srvr_debug = NULL;

#define MAX_SESSIONS 4
struct SESSION {
  int16_t idx; /* Session table index */
  bool is_local;
  int16_t context;
#define CX_DISCONNECTED 0 /* No session active */
#define CX_CONNECTED 1    /* Session active but not... */
#define CX_EXECUTING 2    /* ...executing command (implies connected) */
  INBUFF* buff;
  int32_t buff_size;  /* Allocated size of buffer */
  int32_t buff_bytes; /* Size of received packet */
  char sderror[512 + 1];
  int16_t server_error;
  int32_t sd_status;
  SOCKET sock;
  HANDLE hPipe;
};

struct SESSION sessions[MAX_SESSIONS];

DWORD tls = -1;

#define CDebug(name) ClientDebug(name)

void _stdcall SDDisconnect(void);
void _stdcall _export SDEndCommand(void);

/* Internal functions */
BSTR SelectLeftRight(int16_t fno,
                     BSTR index_name,
                     int16_t listno,
                     int16_t mode);
void SetLeftRight(int16_t fno, BSTR index_name, int16_t mode);
bool context_error(struct SESSION* session, int16_t expected);
void delete_record(int16_t mode, int16_t fno, BSTR id);
BSTR read_record(int16_t fno, BSTR id, int16_t* err, int mode);
void write_record(int16_t mode, int16_t fno, BSTR id, BSTR data);
bool GetResponse(struct SESSION* session);
void Abort(char* msg, bool use_response);
void catcall(struct SESSION* session, BSTR subrname, int16_t argc, ...);
char* memstr(char* str, char* substr, int str_len, int substr_len);
bool match_template(char* string,
                    char* template,
                    int16_t component,
                    int16_t return_component,
                    char** component_start,
                    char** component_end);
bool message_pair(struct SESSION* session,
                  int type,
                  unsigned char* data,
                  int32_t bytes);
char* sysdir(void);
void ClientDebug(char* name);

/* ======================================================================
   DllEntryPoint() -  Initialisation function                             */

BOOL WINAPI _export DllEntryPoint(instance, reason, reserved) HANDLE instance;
DWORD reason;
LPVOID reserved;
{
  int16_t i;

  switch (reason) {
    case DLL_PROCESS_ATTACH:
      if (tls == 0xFFFFFFFF) {
        tls = TlsAlloc(); /* Allocate thread storage for session index */

        for (i = 0; i < MAX_SESSIONS; i++) {
          sessions[i].idx = i;
          sessions[i].context = CX_DISCONNECTED;
          sessions[i].is_local = FALSE;
          sessions[i].buff = NULL;
          sessions[i].sderror[0] = '\0';
          sessions[i].hPipe = INVALID_HANDLE_VALUE;
          sessions[i].sock = INVALID_SOCKET;
        }

        set_default_character_maps();
      }
      break;
  }

  return 1;
}

/* ======================================================================
   DllRegisterServer()                                                    */

int WINAPI _export DllRegisterServer() {
  return S_OK;
}

/* ======================================================================
   SDCall()  - Call catalogued subroutine                                 */

void _stdcall _export SDCall(subrname,
                             argc,
                             a1,
                             a2,
                             a3,
                             a4,
                             a5,
                             a6,
                             a7,
                             a8,
                             a9,
                             a10,
                             a11,
                             a12,
                             a13,
                             a14,
                             a15,
                             a16,
                             a17,
                             a18,
                             a19,
                             a20) BSTR subrname;
int16_t argc;
BSTR* a1;
BSTR* a2;
BSTR* a3;
BSTR* a4;
BSTR* a5;
BSTR* a6;
BSTR* a7;
BSTR* a8;
BSTR* a9;
BSTR* a10;
BSTR* a11;
BSTR* a12;
BSTR* a13;
BSTR* a14;
BSTR* a15;
BSTR* a16;
BSTR* a17;
BSTR* a18;
BSTR* a19;
BSTR* a20;
{
  struct SESSION* session;

  CDebug("SDCall");
  session = TlsGetValue(tls);
  if (context_error(session, CX_CONNECTED))
    return;

  catcall(session, subrname, argc, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,
          a12, a13, a14, a15, a16, a17, a18, a19, a20);
}

/* ======================================================================
   SDChange()  -  Change substrings                                       */

BSTR _stdcall _export SDChange(src, old, new, occ, first) BSTR src;
BSTR old;
BSTR new;
int32_t* occ;
int32_t* first;
{
  int src_len;
  int old_len;
  int new_len;
  int32_t occurrences = -1;
  int32_t start = 1;
  int32_t bytes; /* Remaining bytes counter */
  char* start_pos;
  BSTR new_str;
  int32_t changes;
  char* pos;
  char* p;
  char* q;
  int32_t n;

  src_len = SysStringByteLen(src);
  old_len = SysStringByteLen(old);
  new_len = SysStringByteLen(new);

  if (src_len == 0) {
    new_str = SysAllocStringByteLen(NULL, 0);
    return new_str;
  }

  if (old_len == 0)
    goto return_unchanged;

  if (occ != NULL) {
    occurrences = *occ;
    if (occurrences < 1)
      occurrences = -1;
  }

  if (first != NULL) {
    start = *first;
    if (start < 1)
      start = 1;
  }

  /* Count occurences of old string in source string, remembering start of
    region to change.                                                     */

  changes = 0;
  bytes = src_len;
  pos = (char*)src;
  while (bytes > 0) {
    p = memstr(pos, (char*)old, bytes, old_len);
    if (p == NULL)
      break;

    if (--start <= 0) {
      if (start == 0)
        start_pos = p; /* This is the first one to replace */

      changes++;

      if (--occurrences == 0)
        break; /* This was the last one to replace */
    }

    bytes -= (p - pos) + old_len;
    pos = p + old_len;
  }

  if (changes == 0)
    goto return_unchanged;

  /* Now make the changes */

  new_str =
      SysAllocStringByteLen(NULL, src_len + changes * (new_len - old_len));

  q = (char*)new_str;
  pos = (char*)src;
  bytes = src_len;
  p = start_pos;
  do {
    /* Copy intermediate text */

    n = p - pos;
    if (n) {
      memcpy(q, pos, n);
      q += n;
      pos += n;
      bytes -= n;
    }

    /* Insert replacement */

    if (new_len) {
      memcpy(q, new, new_len);
      q += new_len;
    }

    /* Skip old substring */

    pos += old_len;
    bytes -= old_len;

    /* Find next replacement */

    p = memstr(pos, (char*)old, bytes, old_len);

  } while (--changes);

  /* Copy trailing substring */

  if (bytes)
    memcpy(q, pos, bytes);
  return new_str;

return_unchanged:
  new_str = SysAllocStringByteLen((char*)src, src_len);
  return new_str;
}

/* ======================================================================
   SDClearSelect()  - Clear select list                                   */

void _stdcall _export SDClearSelect(listno) int16_t listno;
{
  struct {
    int16_t listno;
  } packet;
  struct SESSION* session;

  CDebug(("SDClearSelect"));
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_sdclearselect;

  packet.listno = listno;

  if (!message_pair(session, SrvrClearSelect, (char*)&packet, sizeof(packet))) {
    goto exit_sdclearselect;
  }

  switch (session->server_error) {
    case SV_ON_ERROR:
      Abort("CLEARSELECT generated an abort event", TRUE);
      break;
  }

exit_sdclearselect:
  return;
}

/* ======================================================================
   SDClose()  -  Close a file                                             */

void _stdcall _export SDClose(fno) int16_t fno;
{
  struct {
    int16_t fno;
  } packet;
  struct SESSION* session;

  CDebug("SDClose");
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_sdclose;

  packet.fno = fno;

  if (!message_pair(session, SrvrClose, (char*)&packet, sizeof(packet))) {
    goto exit_sdclose;
  }

  switch (session->server_error) {
    case SV_ON_ERROR:
      Abort("CLOSE generated an abort event", TRUE);
      break;
  }

exit_sdclose:
  return;
}

/* ======================================================================
   SDConnect()  -  Open connection to server.                             */

VBBool _stdcall _export SDConnect(host,
                                  port,
                                  username,
                                  password,
                                  account) BSTR host;
int16_t port;
BSTR username;
BSTR password;
BSTR account;
{
  VBBool status = VBFalse;
  char login_data[2 + MAX_USERNAME_LEN + 2 + MAX_USERNAME_LEN];
  int n;
  char* p;
  struct SESSION* session;

  if ((session = FindFreeSession()) == NULL)
    goto exit_sdconnect;
  session->sderror[0] = '\0';
  session->is_local = FALSE;

  /* Set up login data */

  n = SysStringByteLen(host);
  if (n == 0) {
    strcpy(session->sderror, "Invalid host name");
    goto exit_sdconnect;
  }

  n = SysStringByteLen(username);
  if (n > MAX_USERNAME_LEN) {
    strcpy(session->sderror, "Invalid user name");
    goto exit_sdconnect;
  }

  p = login_data;
  *((int16_t*)p) = n; /* User name len */
  p += 2;

  memcpy(p, (char*)username, n); /* User name */
  p += n;
  if (n & 1)
    *(p++) = '\0';

  n = SysStringByteLen(password);
  if (n > MAX_USERNAME_LEN) {
    strcpy(session->sderror, "Invalid password");
    goto exit_sdconnect;
  }

  *((int16_t*)p) = n; /* Password len */
  p += 2;

  memcpy(p, (char*)password, n); /* Password */
  p += n;
  if (n & 1)
    *(p++) = '\0';

  /* Open connection to server */

  if (!OpenSocket(session, (char*)host, port))
    goto exit_sdconnect;

  /* Check username and password */

  n = p - login_data;
  if (!message_pair(session, SrvrLogin, login_data, n)) {
    sprintf(session->sderror, "Startup message returned an error");
    goto exit_sdconnect;
  }

  if (session->server_error != SV_OK) {
    if (session->server_error == SV_ON_ERROR) {
      n = session->buff_bytes - offsetof(INBUFF, data.abort.message);
      if (n > 0) {
        memcpy(session->sderror, session->buff->data.abort.message, n);
        session->buff->data.abort.message[n] = '\0';
      }
    }
    goto exit_sdconnect;
  }

  /* Now attempt to attach to required account */

  if (!message_pair(session, SrvrAccount, (char*)account,
                    SysStringByteLen(account))) {
    goto exit_sdconnect;
  }

  session->context = CX_CONNECTED;

  if (srvr_debug != NULL) {
    fprintf(srvr_debug, "Connected session %d\n", session->idx);
    fflush(srvr_debug);
  }

  status = VBTrue;

exit_sdconnect:
  if (!status)
    CloseSocket(session);

  return status;
}

/* ======================================================================
   SDConnected()  -  Are we connected?                                    */

VBBool _stdcall _export SDConnected() {
  struct SESSION* session;

  session = TlsGetValue(tls);
  if (session == NULL)
    return VBFalse;

  session->sderror[0] = '\0';

  return (session->context == CX_DISCONNECTED) ? VBFalse : VBTrue;
}

/* ======================================================================
   SDConnectLocal()  -  Open connection to local system as current user   */

VBBool _stdcall _export SDConnectLocal(account) BSTR account;
{
  VBBool status = VBFalse;
  char command[MAX_PATHNAME_LEN + 50];
  char pipe_name[40];
  struct _PROCESS_INFORMATION process_information;
  struct _STARTUPINFOA startupinfo;
  struct SESSION* session;

  if ((session = FindFreeSession()) == NULL)
    goto exit_sdconnect_local;
  session->sderror[0] = '\0';
  session->is_local = TRUE;

  /* Create pipe */

  sprintf(pipe_name, "\\\\.\\pipe\\~SDPipe%d-%d", GetCurrentProcessId(),
          session->idx);
  session->hPipe = CreateNamedPipe(
      pipe_name, PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, /* Max instances */
      1024,  /* Output buffer size */
      1024,  /* Input buffer size */
      0,     /* Timeout */
      NULL); /* Security attributes */

  if (session->hPipe == INVALID_HANDLE_VALUE) {
    sprintf(session->sderror, "Error %d creating pipe", GetLastError());
    goto exit_sdconnect_local;
  }

  /* Launch SD process */

  sprintf(command, "%s\\BIN\\SD.EXE -Q -C %s", sysdir(), pipe_name);

  GetStartupInfo(&startupinfo);

  if (CreateProcess(NULL, command, NULL, NULL, FALSE, DETACHED_PROCESS, NULL,
                    NULL, &startupinfo, &process_information)) {
    CloseHandle(process_information.hThread);
  } else {
    sprintf(session->sderror, "Failed to create child process. Error %d",
            (int)GetLastError());
    CloseHandle(session->hPipe);
    session->hPipe = INVALID_HANDLE_VALUE;
    goto exit_sdconnect_local;
  }

  /* Wait for child to connect to pipe */

  if (!ConnectNamedPipe(session->hPipe, NULL)) {
    sprintf(session->sderror, "Error %d connecting pipe", (int)GetLastError());
    CloseHandle(session->hPipe);
    session->hPipe = INVALID_HANDLE_VALUE;
    goto exit_sdconnect_local;
  }

  /* Send local login packet */

  if (!message_pair(session, SrvrLocalLogin, NULL, 0)) {
    goto exit_sdconnect_local;
  }

  /* Now attempt to attach to required account */

  if (!message_pair(session, SrvrAccount, (char*)account,
                    SysStringByteLen(account))) {
    goto exit_sdconnect_local;
  }

  session->context = CX_CONNECTED;

  if (srvr_debug != NULL) {
    fprintf(srvr_debug, "Connected session %d\n", session->idx);
    fflush(srvr_debug);
  }

  status = VBTrue;

exit_sdconnect_local:
  if (!status) {
    if (session->hPipe != INVALID_HANDLE_VALUE) {
      CloseHandle(session->hPipe);
      session->hPipe = INVALID_HANDLE_VALUE;
    }
  }

  return status;
}

/* ======================================================================
   SDDcount()  -  Count fields, values or subvalues                       */

int32_t _stdcall _export SDDcount(src, delim_str) BSTR src;
BSTR delim_str;
{
  int32_t src_len;
  char* p;
  int32_t ct = 0;
  char delim;

  if (SysStringByteLen(delim_str) != 0) {
    delim = *delim_str;

    src_len = SysStringByteLen(src);
    if (src_len != 0) {
      ct = 1;
      while ((p = memchr(src, delim, src_len)) != NULL) {
        src_len -= (1 + p - (char*)src);
        (char*)src = p + 1;
        ct++;
      }
    }
  }

  return ct;
}

/* ======================================================================
   SDDebug()  -  Turn on/off packet debugging                             */

void _stdcall _export SDDebug(mode) VBBool mode;
{
  if (mode && (srvr_debug == NULL)) /* Turn on */
  {
    srvr_debug = fopen("C:\\CLIDBG", "wt");
  }

  if (!mode && (srvr_debug != NULL)) /* Turn off */
  {
    fclose(srvr_debug);
    srvr_debug = NULL;
  }
}

/* ======================================================================
   SDDel()  -  Delete field, value or subvalue                            */

BSTR _stdcall _export SDDel(src, fno, vno, svno) BSTR src;
int16_t fno;
int16_t vno;
int16_t svno;
{
  int32_t src_len;
  char* pos;     /* Rolling source pointer */
  int32_t bytes; /* Remaining bytes counter */
  int32_t new_len;
  BSTR new_str;
  char* p;
  int16_t i;
  int32_t n;

  src_len = SysStringByteLen(src);
  if (src_len == 0)
    goto null_result; /* Deleting from null string */

  /* Setp 1  -  Initialise varaibles */

  if (fno < 1)
    fno = 1;

  /* Step 2  -  Position to start of item */

  pos = (char*)src;
  bytes = src_len;

  /* Skip to start field */

  i = fno;
  while (--i) {
    p = memchr(pos, FIELD_MARK, bytes);
    if (p == NULL)
      goto unchanged_result; /* No such field */
    bytes -= (1 + p - pos);
    pos = p + 1;
  }

  p = memchr(pos, FIELD_MARK, bytes);
  if (p != NULL)
    bytes = p - pos; /* Length of field, including end mark */

  if (vno < 1) /* Deleting whole field */
  {
    if (p == NULL) /* Last field */
    {
      if (fno > 1) /* Not only field. Back up to include leading mark */
      {
        pos--;
        bytes++;
      }
    } else /* Not last field */
    {
      bytes++; /* Include trailing mark in deleted region */
    }
    goto done;
  }

  /* Skip to start value */

  i = vno;
  while (--i) {
    p = memchr(pos, VALUE_MARK, bytes);
    if (p == NULL)
      goto unchanged_result; /* No such value */
    bytes -= (1 + p - pos);
    pos = p + 1;
  }

  p = memchr(pos, VALUE_MARK, bytes);
  if (p != NULL)
    bytes = p - pos; /* Length of value, including end mark */

  if (svno < 1) /* Deleting whole value */
  {
    if (p == NULL) /* Last value */
    {
      if (vno > 1) /* Not only value. Back up to include leading mark */
      {
        pos--;
        bytes++;
      }
    } else /* Not last value */
    {
      bytes++; /* Include trailing mark in deleted region */
    }
    goto done;
  }

  /* Skip to start subvalue */

  i = svno;
  while (--i) {
    p = memchr(pos, SUBVALUE_MARK, bytes);
    if (p == NULL)
      goto unchanged_result; /* No such subvalue */
    bytes -= (1 + p - pos);
    pos = p + 1;
  }
  p = memchr(pos, SUBVALUE_MARK, bytes);
  if (p != NULL)
    bytes = p - pos; /* Length of subvalue, including end mark */

  if (p == NULL) /* Last subvalue */
  {
    if (svno > 1) /* Not only subvalue. Back up to include leading mark */
    {
      pos--;
      bytes++;
    }
  } else /* Not last subvalue */
  {
    bytes++; /* Include trailing mark in deleted region */
  }

done:
  /* Now construct new string with 'bytes' bytes omitted starting at 'pos' */

  new_len = src_len - bytes;
  new_str = SysAllocStringByteLen(NULL, new_len);
  p = (char*)new_str;

  n = pos - (char*)src; /* Length of leading substring */
  if (n) {
    memcpy(p, src, n);
    p += n;
  }

  n = src_len - (bytes + n); /* Length of trailing substring */
  if (n) {
    memcpy(p, pos + bytes, n);
  }

  return new_str;

null_result:
  return SysAllocStringByteLen(NULL, 0);

unchanged_result:
  return SysAllocStringByteLen((char*)src, src_len);
}

/* ======================================================================
   SDDelete()  -  Delete record                                           */

void _stdcall _export SDDelete(fno, id) int16_t fno;
BSTR id;
{
  CDebug(("SDDelete"));
  delete_record(SrvrDelete, fno, id);
}

/* ======================================================================
   SDDeleteu()  -  Delete record, retaining lock                          */

void _stdcall _export SDDeleteu(fno, id) int16_t fno;
BSTR id;
{
  CDebug("SDDeleteu");
  delete_record(SrvrDeleteu, fno, id);
}

/* ======================================================================
   SDDisconnect()  -  Close connection to server.                         */

void _stdcall _export SDDisconnect() {
  struct SESSION* session;

  CDebug(("SDDisconnect"));
  session = TlsGetValue(tls);
  if (session != NULL) {
    if (session->context != CX_DISCONNECTED) {
      disconnect(session);
    }
  }
}

/* ======================================================================
   SDDisconnectAll()  -  Close connection to all servers.                 */

void _stdcall _export SDDisconnectAll() {
  int16_t i;

  CDebug("SDDisconnectAll");
  for (i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].context != CX_DISCONNECTED) {
      disconnect(&(sessions[i]));
    }
  }
}

/* ======================================================================
   SDEndCommand()  -  End a command that is requesting input              */

void _stdcall _export SDEndCommand() {
  struct SESSION* session;

  CDebug(("SDEndCommand"));
  session = TlsGetValue(tls);

  if (context_error(session, CX_EXECUTING))
    goto exit_sdendcommand;

  if (!message_pair(session, SrvrEndCommand, NULL, 0)) {
    goto exit_sdendcommand;
  }

  session->context = CX_CONNECTED;

exit_sdendcommand:
  return;
}

/* ======================================================================
   SDEnterPackage()  -  Enter a licensed package                          */

VBBool _stdcall _export SDEnterPackage(name) BSTR name;
{
  VBBool status = VBFalse;
  int name_len;
  struct SESSION* session;

  CDebug("SDEnterPackage");
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_EnterPackage;

  name_len = SysStringByteLen(name);
  if ((name_len < 1) || (name_len > MAX_PACKAGE_NAME_LEN)) {
    session->server_error = SV_ELSE;
    session->sd_status = ER_BAD_NAME;
  } else {
    if (!message_pair(session, SrvrEnterPackage, (char*)name, name_len)) {
      goto exit_EnterPackage;
    }
  }

  switch (session->server_error) {
    case SV_OK:
      status = VBTrue;
      break;

    case SV_ON_ERROR:
      Abort("EnterPackage generated an abort event", TRUE);
      break;

    case SV_LOCKED:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }

exit_EnterPackage:
  return status;
}

/* ======================================================================
   SDExitPackage()  -  Exit from a licensed package                       */

VBBool _stdcall _export SDExitPackage(name) BSTR name;
{
  VBBool status = VBFalse;
  int name_len;
  struct SESSION* session;

  CDebug(("SDExitPackage"));
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_ExitPackage;

  name_len = SysStringByteLen(name);
  if ((name_len < 1) || (name_len > MAX_PACKAGE_NAME_LEN)) {
    session->server_error = SV_ELSE;
    session->sd_status = ER_BAD_NAME;
  } else {
    if (!message_pair(session, SrvrExitPackage, (char*)name, name_len)) {
      goto exit_ExitPackage;
    }
  }

  switch (session->server_error) {
    case SV_OK:
      status = VBTrue;
      break;

    case SV_ON_ERROR:
      Abort("ExitPackage generated an abort event", TRUE);
      break;

    case SV_LOCKED:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }

exit_ExitPackage:
  return status;
}

/* ======================================================================
   SDError()  -  Return extended error string                             */

BSTR _stdcall _export SDError() {
  BSTR s;
  struct SESSION* session;

  session = TlsGetValue(tls);

  s = SysAllocStringByteLen(session->sderror, strlen(session->sderror));

  return s;
}

/* ======================================================================
   SDExecute()  -  Execute a command                                      */

BSTR _stdcall _export SDExecute(cmnd, err) BSTR cmnd;
int16_t* err;
{
  int32_t reply_len = 0;
  BSTR reply;
  struct SESSION* session;

  CDebug("SDExecute");
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_sdexecute;

  if (!message_pair(session, SrvrExecute, (char*)cmnd,
                    SysStringByteLen(cmnd))) {
    goto exit_sdexecute;
  }

  switch (session->server_error) {
    case SV_PROMPT:
      session->context = CX_EXECUTING;
      /* **** FALL THROUGH **** */

    case SV_OK:
      reply_len = session->buff_bytes - offsetof(INBUFF, data.execute.reply);
      break;

    case SV_ON_ERROR:
      Abort("EXECUTE generated an abort event", TRUE);
      break;
  }

exit_sdexecute:
  reply = SysAllocStringByteLen(
      (reply_len == 0) ? NULL : (session->buff->data.execute.reply), reply_len);
  *err = session->server_error;
  return reply;
}

/* ======================================================================
   SDExtract()  -  Extract field, value or subvalue                       */

BSTR _stdcall _export SDExtract(src, fno, vno, svno) BSTR src;
int16_t fno;
int16_t vno;
int16_t svno;
{
  int32_t src_len;
  char* p;

  src_len = SysStringByteLen(src);
  if (src_len == 0)
    goto null_result; /* Extracting from null string */

  /* Setp 1  -  Initialise variables */

  if (fno < 1)
    fno = 1;

  /* Step 2  -  Position to start of item */

  /* Skip to start field */

  while (--fno) {
    p = memchr(src, FIELD_MARK, src_len);
    if (p == NULL)
      goto null_result; /* No such field */
    src_len -= (1 + p - (char*)src);
    (char*)src = p + 1;
  }
  p = memchr(src, FIELD_MARK, src_len);
  if (p != NULL)
    src_len = p - (char*)src; /* Adjust to ignore later fields */

  if (vno < 1)
    goto done; /* Extracting whole field */

  /* Skip to start value */

  while (--vno) {
    p = memchr(src, VALUE_MARK, src_len);
    if (p == NULL)
      goto null_result; /* No such value */
    src_len -= (1 + p - (char*)src);
    (char*)src = p + 1;
  }

  p = memchr(src, VALUE_MARK, src_len);
  if (p != NULL)
    src_len = p - (char*)src; /* Adjust to ignore later values */

  if (svno < 1)
    goto done; /* Extracting whole value */

  /* Skip to start subvalue */

  while (--svno) {
    p = memchr(src, SUBVALUE_MARK, src_len);
    if (p == NULL)
      goto null_result; /* No such subvalue */
    src_len -= (1 + p - (char*)src);
    (char*)src = p + 1;
  }
  p = memchr(src, SUBVALUE_MARK, src_len);
  if (p != NULL)
    src_len = p - (char*)src; /* Adjust to ignore later fields */

done:
  return SysAllocStringByteLen((char*)src, src_len);

null_result:
  return SysAllocStringByteLen(NULL, 0);
}

/* ======================================================================
   SDField()  -  Extract delimited substring                              */

BSTR _stdcall _export SDField(src, delim, first, occ) BSTR src;
BSTR delim;
int32_t first;
int32_t* occ;
{
  int src_len;
  int delim_len;
  char delimiter;
  int32_t occurrences = -1;
  int32_t bytes; /* Remaining bytes counter */
  char* pos;
  char* p;
  char* q;

  src_len = SysStringByteLen(src);

  delim_len = SysStringByteLen(delim);
  if ((delim_len == 0) || (src_len == 0))
    goto return_null;

  if (first < 1)
    first = 1;

  if (occ != NULL) {
    occurrences = *occ;
    if (occurrences < 1)
      occurrences = 1;
  }

  delimiter = *delim;

  /* Find start of data to return */

  pos = (char*)src;
  bytes = src_len;
  while (--first) {
    p = memchr(pos, delimiter, bytes);
    if (p == NULL)
      goto return_null;
    bytes -= (p - pos + 1);
    pos = p + 1;
  }

  /* Find end of data to return */

  q = pos;
  do {
    p = memchr(q, delimiter, bytes);
    if (p == NULL) {
      p = q + bytes;
      break;
    }
    bytes -= (p - q + 1);
    q = p + 1;
  } while (--occurrences);

  return SysAllocStringByteLen(pos, p - pos);

return_null:
  return SysAllocStringByteLen(NULL, 0);
}

/* ======================================================================
   SDGetSession()  -  Return session index                                */

int16_t _stdcall _export SDGetSession() {
  struct SESSION* session;

  CDebug(("SDGetSession"));
  session = TlsGetValue(tls);
  return session->idx;
}

/* ======================================================================
   SDIns()  -  Insert field, value or subvalue                            */

BSTR _stdcall _export SDIns(xsrc, fno, vno, svno, new) BSTR xsrc;
int16_t fno;
int16_t vno;
int16_t svno;
BSTR new;
{
  char* src;
  int32_t src_len;
  char* pos;       /* Rolling source pointer */
  int32_t bytes;   /* Remaining bytes counter */
  int32_t ins_len; /* Length of inserted data */
  int32_t new_len;
  BSTR new_str;
  char* p;
  int16_t i;
  int32_t n;
  int16_t fm = 0;
  int16_t vm = 0;
  int16_t sm = 0;
  char postmark = '\0';

  src_len = SysStringByteLen(xsrc);
  src = (char*)xsrc;
  ins_len = SysStringByteLen(new);

  pos = src;
  bytes = src_len;

  if (fno < 1)
    fno = 1;
  if (vno < 0)
    vno = 0;
  if (svno < 0)
    svno = 0;

  if (src_len == 0) /* Inserting in null string */
  {
    if (fno > 1)
      fm = fno - 1;
    if (vno > 1)
      vm = vno - 1;
    if (svno > 1)
      sm = svno - 1;
    goto done;
  }

  /* Skip to start field */

  for (i = 1; i < fno; i++) {
    p = memchr(pos, FIELD_MARK, bytes);
    if (p == NULL) /* No such field */
    {
      fm = fno - i;
      if (vno > 1)
        vm = vno - 1;
      if (svno > 1)
        sm = svno - 1;
      pos = src + src_len;
      goto done;
    }
    bytes -= (1 + p - pos);
    pos = p + 1;
  }

  p = memchr(pos, FIELD_MARK, bytes);
  if (p != NULL)
    bytes = p - pos; /* Length of field */

  if (vno == 0) /* Inserting field */
  {
    postmark = FIELD_MARK;
    goto done;
  }

  /* Skip to start value */

  for (i = 1; i < vno; i++) {
    p = memchr(pos, VALUE_MARK, bytes);
    if (p == NULL) /* No such value */
    {
      vm = vno - i;
      if (svno > 1)
        sm = svno - 1;
      pos += bytes;
      goto done;
    }
    bytes -= (1 + p - pos);
    pos = p + 1;
  }

  p = memchr(pos, VALUE_MARK, bytes);
  if (p != NULL)
    bytes = p - pos; /* Length of value, including end mark */

  if (svno == 0) /* Inserting value */
  {
    postmark = VALUE_MARK;
    goto done;
  }

  /* Skip to start subvalue */

  for (i = 1; i < svno; i++) {
    p = memchr(pos, SUBVALUE_MARK, bytes);
    if (p == NULL) /* No such subvalue */
    {
      sm = svno - i;
      pos += bytes;
      goto done;
    }
    bytes -= (1 + p - pos);
    pos = p + 1;
  }

  postmark = SUBVALUE_MARK;

done:
  /* Now construct new string inserting fm, vm and sm marks and new data
    at 'pos'.                                                           */

  n = pos - src; /* Length of leading substring */
  if ((n == src_len) || (IsDelim(src[n]) && src[n] > postmark)) /* 0380 */
  {
    postmark = '\0';
  }

  new_len = src_len + fm + vm + sm + ins_len + (postmark != '\0');
  new_str = SysAllocStringByteLen(NULL, new_len);
  p = (char*)new_str;

  if (n) {
    memcpy(p, src, n);
    p += n;
  }

  while (fm--)
    *(p++) = FIELD_MARK;
  while (vm--)
    *(p++) = VALUE_MARK;
  while (sm--)
    *(p++) = SUBVALUE_MARK;

  if (ins_len) {
    memcpy(p, new, ins_len);
    p += ins_len;
  }

  if (postmark != '\0')
    *(p++) = postmark;

  n = src_len - n; /* Length of trailing substring */
  if (n) {
    memcpy(p, pos, n);
  }

  return new_str;
}

/* ======================================================================
   SDLocate()  -  Search dynamic array                                    */

VBBool _stdcall _export SDLocate(item, src, fno, vno, svno, pos, order)
    BSTR item;
BSTR src;
int16_t fno;
int16_t vno;
int16_t svno;
int16_t* pos;
BSTR order;
{
  int item_len;
  int src_len;
  char* p;
  char* q;
  bool ascending = TRUE;
  bool left = TRUE;
  bool sorted = FALSE;
  int16_t idx;
  int d;
  VBBool found = VBFalse;
  int i;
  int bytes;
  char mark;
  int n;
  int x;
  char* s1;
  char* s2;

  /* Establish sort mode */

  i = SysStringByteLen(order);
  if (i >= 1) {
    sorted = TRUE;
    ascending = ((order)[0] != 'D'); /* 0513 */
  }

  if (i >= 2)
    left = (order[1] != 'R'); /* 0513 */

  item_len = SysStringByteLen(item); /* Length of item to find */

  src_len = SysStringByteLen(src);

  p = (char*)src;
  bytes = src_len;

  if (fno < 1)
    fno = 1;

  /* Scan to start field */

  mark = FIELD_MARK;
  idx = fno;
  for (i = 1; i < fno; i++) {
    if (bytes == 0)
      goto done;
    q = memchr(p, FIELD_MARK, bytes);
    if (q == NULL)
      goto done; /* No such field */
    bytes -= (1 + q - p);
    p = q + 1;
  }

  if (vno > 0) /* Searching for value or subvalue */
  {
    q = memchr(p, FIELD_MARK, bytes);
    if (q != NULL)
      bytes = q - p; /* Limit view to length of field */

    mark = VALUE_MARK;
    idx = vno;
    for (i = 1; i < vno; i++) {
      if (bytes == 0)
        goto done;
      q = memchr(p, VALUE_MARK, bytes);
      if (q == NULL)
        goto done; /* No such value */
      bytes -= (1 + q - p);
      p = q + 1;
    }

    if (svno > 0) /* Searching for subvalue */
    {
      q = memchr(p, VALUE_MARK, bytes);
      if (q != NULL)
        bytes = q - p; /* Limit view to length of value */

      mark = SUBVALUE_MARK;
      idx = svno;
      for (i = 1; i < svno; i++) /* 0512 */
      {
        if (bytes == 0)
          goto done;
        q = memchr(p, SUBVALUE_MARK, bytes);
        if (q == NULL)
          goto done; /* No such subvalue */
        bytes -= (1 + q - p);
        p = q + 1;
      }
    }
  }

  /* We are now at the start position for the search and 'mark' contains the
    delimiting mark character.  Because we have adjusted 'bytes' to limit
    our view to the end of the item, we do not need to worry about higher
    level marks.  Examine successive items from this point.                 */

  if (bytes == 0) {
    if (item_len == 0)
      found = VBTrue;
    goto done;
  }

  do {
    q = memchr(p, mark, bytes);
    n = (q == NULL) ? bytes : (q - p); /* Length of entry */
    if ((n == item_len) && (memcmp(p, item, n) == 0)) {
      found = VBTrue;
      goto done;
    }

    if (sorted) /* Check if we have gone past correct position */
    {
      if (left || (n == item_len)) {
        d = memcmp(p, item, min(n, item_len));
        if (d == 0) {
          if ((n > item_len) == ascending)
            goto done;
        } else if ((d > 0) == ascending)
          goto done;
      } else /* Right sorted and lengths differ */
      {
        x = n - item_len;
        s1 = p;
        s2 = (char*)item;
        if (x > 0) /* Array entry longer than item to find */
        {
          do {
            d = *(s1++) - ' ';
          } while ((d == 0) && --x);
        } else /* Array entry shorter than item to find */
        {
          do {
            d = ' ' - *(s2++);
          } while ((d == 0) && ++x);
        }
        if (d == 0)
          d = memcmp(s1, s2, min(n, item_len));
        if ((d > 0) == ascending)
          goto done;
      }
    }

    bytes -= (1 + q - p);
    p = q + 1;
    idx++;
  } while (q);

done:
  *pos = idx;
  return found;
}

/* ======================================================================
   SDLogto()  -  LOGTO                                                    */

VBBool _stdcall _export SDLogto(account_name) BSTR account_name;
{
  VBBool status = VBFalse;
  int name_len;
  struct SESSION* session;

  CDebug("SDLogto");
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_logto;

  name_len = SysStringByteLen(account_name);
  if ((name_len < 1) || (name_len > MAX_ACCOUNT_NAME_LEN)) {
    session->server_error = SV_ELSE;
    session->sd_status = ER_BAD_NAME;
  } else {
    if (!message_pair(session, SrvrAccount, (char*)account_name, name_len)) {
      goto exit_logto;
    }
  }

  switch (session->server_error) {
    case SV_OK:
      status = VBTrue;
      break;

    case SV_ON_ERROR:
      Abort("LOGTO generated an abort event", TRUE);
      break;

    case SV_LOCKED:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }

exit_logto:
  return status;
}

/* ======================================================================
   SDMarkMapping()  -  Enable/disable mark mapping on a directory file    */

void _stdcall _export SDMarkMapping(fno, state) int16_t fno;
int16_t state;
{
  struct {
    int16_t fno;
    int16_t state;
  } packet;
  struct SESSION* session;

  CDebug(("SDMarkMapping"));
  session = TlsGetValue(tls);

  if (!context_error(session, CX_CONNECTED)) {
    packet.fno = fno;
    packet.state = state;

    message_pair(session, SrvrMarkMapping, (char*)&packet, sizeof(packet));
  }
}

/* ======================================================================
   SDMatch()  -  String matching                                          */

VBBool _stdcall _export SDMatch(str, pattern) BSTR str;
BSTR pattern;
{
  char template[MAX_MATCH_TEMPLATE_LEN + 1];
  char src_string[MAX_MATCHED_STRING_LEN + 1];
  int n;
  char* p;
  char* q;
  char* component_start = NULL;
  char* component_end = NULL;

  /* Copy template and string to our own buffers so we can alter them */

  n = SysStringByteLen(pattern);
  if ((n == 0) || (n > MAX_MATCH_TEMPLATE_LEN))
    goto no_match;
  memcpy(template, (char*)pattern, n);
  template[n] = '\0';

  n = SysStringByteLen(str);
  if (n > MAX_MATCHED_STRING_LEN)
    goto no_match;
  if (n)
    memcpy(src_string, (char*)str, n);
  src_string[n] = '\0';

  /* Try matching against each value mark delimited template */

  p = template;
  do { /* Outer loop - extract templates */
    q = strchr(p, (char)VALUE_MARK);
    if (q != NULL)
      *q = '\0';

    if (match_template(src_string, p, 0, 0, &component_start, &component_end)) {
      return VBTrue;
    }

    p = q + 1;
  } while (q != NULL);

no_match:
  return VBFalse;
}

/* ======================================================================
   SDMatchfield()  -  String matching                                     */

BSTR _stdcall _export SDMatchfield(str, pattern, component) BSTR str;
BSTR pattern;
int16_t component;
{
  char template[MAX_MATCH_TEMPLATE_LEN + 1];
  char src_string[MAX_MATCHED_STRING_LEN + 1];
  int n;
  char* p;
  char* q;
  char* component_start = NULL;
  char* component_end = NULL;

  if (component < 1)
    component = 1;

  /* Copy template and string to our own buffers so we can alter them */

  n = SysStringByteLen(pattern);
  if ((n == 0) || (n > MAX_MATCH_TEMPLATE_LEN))
    goto no_match;
  memcpy(template, (char*)pattern, n);
  template[n] = '\0';

  n = SysStringByteLen(str);
  if (n > MAX_MATCHED_STRING_LEN)
    goto no_match;
  if (n)
    memcpy(src_string, (char*)str, n);
  src_string[n] = '\0';

  /* Try matching against each value mark delimited template */

  p = template;
  do { /* Outer loop - extract templates */
    q = strchr(p, (char)VALUE_MARK);
    if (q != NULL)
      *q = '\0';

    if (match_template(src_string, p, 0, component, &component_start,
                       &component_end)) {
      if (component_end != NULL)
        *(component_end) = '\0';
      n = strlen(component_start);
      return SysAllocStringByteLen(component_start, n);
    }

    p = q + 1;
  } while (q != NULL);

no_match:
  return SysAllocStringByteLen(NULL, 0);
}

/* ======================================================================
   SDOpen()  -  Open file                                                 */

int16_t _stdcall _export SDOpen(filename) BSTR filename;
{
  int16_t fno = 0;
  struct SESSION* session;

  CDebug("SDOpen");
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_sdopen;

  if (!message_pair(session, SrvrOpen, (char*)filename,
                    SysStringByteLen(filename))) {
    goto exit_sdopen;
  }

  switch (session->server_error) {
    case SV_OK:
      fno = session->buff->data.open.fno;
      break;

    case SV_ON_ERROR:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }

exit_sdopen:
  return fno;
}

/* ======================================================================
   SDRead()  -  Read record                                               */

BSTR _stdcall _export SDRead(fno, id, err) int16_t fno;
BSTR id;
int16_t* err;
{
  CDebug(("SDRead"));
  return read_record(fno, id, err, SrvrRead);
}

/* ======================================================================
   SDReadl()  -  Read record with shared lock                             */

BSTR _stdcall _export SDReadl(fno, id, wait, err) int16_t fno;
BSTR id;
int16_t wait;
int16_t* err;
{ return read_record(fno, id, err, (wait) ? SrvrReadlw : SrvrReadl); }

/* ======================================================================
   SDReadList()  - Read select list                                       */

BSTR _stdcall _export SDReadList(listno, err) int16_t listno;
int16_t* err;
{
  BSTR list;
  int16_t status = 0;
  int32_t data_len = 0;
  struct {
    int16_t listno;
  } packet;
  struct SESSION* session;

  CDebug("SDReadList");
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_sdreadlist;

  packet.listno = listno;

  if (!message_pair(session, SrvrReadList, (char*)&packet, sizeof(packet))) {
    goto exit_sdreadlist;
  }

  switch (session->server_error) {
    case SV_OK:
      data_len = session->buff_bytes - offsetof(INBUFF, data.readlist.list);
      break;

    case SV_ON_ERROR:
      Abort("READLIST generated an abort event", TRUE);
      break;

    case SV_LOCKED:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }

  status = session->server_error;

exit_sdreadlist:

  list = SysAllocStringByteLen(
      (data_len == 0) ? NULL : (session->buff->data.readlist.list), data_len);
  *err = status;
  return list;
}

/* ======================================================================
   SDReadNext()  - Read select list entry                                 */

BSTR _stdcall _export SDReadNext(listno, err) int16_t listno;
int16_t* err;
{
  BSTR id;
  int16_t status = 0;
  int32_t id_len = 0;
  struct {
    int16_t listno;
  } packet;
  struct SESSION* session;

  CDebug(("SDReadNext"));
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_sdreadnext;

  packet.listno = listno;

  if (!message_pair(session, SrvrReadNext, (char*)&packet, sizeof(packet))) {
    goto exit_sdreadnext;
  }

  switch (session->server_error) {
    case SV_OK:
      id_len = session->buff_bytes - offsetof(INBUFF, data.readnext.id);
      break;

    case SV_ON_ERROR:
      Abort("READNEXT generated an abort event", TRUE);
      break;

    case SV_LOCKED:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }

  status = session->server_error;

exit_sdreadnext:
  id = SysAllocStringByteLen(
      (id_len == 0) ? NULL : (session->buff->data.readnext.id), id_len);
  *err = status;
  return id;
}

/* ======================================================================
   SDReadu()  -  Read record with exclusive lock                          */

BSTR _stdcall _export SDReadu(fno, id, wait, err) int16_t fno;
BSTR id;
int16_t wait;
int16_t* err;
{
  CDebug("SDReadu");
  return read_record(fno, id, err, (wait) ? SrvrReaduw : SrvrReadu);
}

/* ======================================================================
   SDRecordlock()  -  Get lock on a record                                */

void _stdcall _export SDRecordlock(fno, id, update_lock, wait) int16_t fno;
BSTR id;
int16_t update_lock;
int16_t wait;
{
  int id_len;
  struct {
    int16_t fno;
    int16_t flags; /* 0x0001 : Update lock */
                   /* 0x0002 : No wait */
    char id[MAX_ID_LEN];
  } packet;
  struct SESSION* session;

  CDebug(("SDRecordlock"));
  session = TlsGetValue(tls);

  if (!context_error(session, CX_CONNECTED)) {
    packet.fno = fno;

    id_len = SysStringByteLen(id);
    if (id_len > MAX_ID_LEN) {
      session->server_error = SV_ON_ERROR;
      session->sd_status = ER_IID;
    } else {
      memcpy(packet.id, id, id_len);
      packet.flags = (update_lock) ? 1 : 0;
      if (!wait)
        packet.flags |= 2;

      if (!message_pair(session, SrvrLockRecord, (char*)&packet, id_len + 4)) {
        session->server_error = SV_ON_ERROR;
      }
    }

    switch (session->server_error) {
      case SV_OK:
        break;

      case SV_ON_ERROR:
        Abort("RECORDLOCK generated an abort event", TRUE);
        break;

      case SV_LOCKED:
      case SV_ELSE:
      case SV_ERROR:
        break;
    }
  }
}

/* ======================================================================
   SDRelease()  -  Release lock                                           */

void _stdcall _export SDRelease(fno, id) int16_t fno;
BSTR id;
{
  int id_len;
  struct {
    int16_t fno;
    char id[MAX_ID_LEN];
  } packet;
  struct SESSION* session;

  CDebug("SDRelease");
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_release;

  packet.fno = fno;

  if (fno == 0) /* Release all locks */
  {
    id_len = 0;
  } else {
    id_len = SysStringByteLen(id);
    if (id_len > MAX_ID_LEN) {
      session->server_error = SV_ON_ERROR;
      session->sd_status = ER_IID;
      goto release_error;
    }
    memcpy(packet.id, id, id_len);
  }

  if (!message_pair(session, SrvrRelease, (char*)&packet, id_len + 2)) {
    goto exit_release;
  }

release_error:
  switch (session->server_error) {
    case SV_OK:
      break;

    case SV_ON_ERROR:
      Abort("RELEASE generated an abort event", TRUE);
      break;

    case SV_LOCKED:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }

exit_release:
  return;
}

/* ======================================================================
   SDReplace()  -  Replace field, value or subvalue                       */

BSTR _stdcall _export SDReplace(src, fno, vno, svno, new) BSTR src;
int16_t fno;
int16_t vno;
int16_t svno;
BSTR new;
{
  int32_t src_len;
  char* pos;       /* Rolling source pointer */
  int32_t bytes;   /* Remaining bytes counter */
  int32_t ins_len; /* Length of inserted data */
  int32_t new_len;
  BSTR new_str;
  char* p;
  int16_t i;
  int32_t n;
  int16_t fm = 0;
  int16_t vm = 0;
  int16_t sm = 0;

  src_len = SysStringByteLen(src);
  ins_len = SysStringByteLen(new);

  pos = (char*)src;
  bytes = src_len;

  if (src_len == 0) /* Replacing in null string */
  {
    if (fno > 1)
      fm = fno - 1;
    if (vno > 1)
      vm = vno - 1;
    if (svno > 1)
      sm = svno - 1;
    bytes = 0;
    goto done;
  }

  if (fno < 1) /* Appending a new field */
  {
    pos = (char*)src + src_len;
    fm = 1;
    if (vno > 1)
      vm = vno - 1;
    if (svno > 1)
      sm = svno - 1;
    bytes = 0;
    goto done;
  }

  /* Skip to start field */

  for (i = 1; i < fno; i++) {
    p = memchr(pos, FIELD_MARK, bytes);
    if (p == NULL) /* No such field */
    {
      fm = fno - i;
      if (vno > 1)
        vm = vno - 1;
      if (svno > 1)
        sm = svno - 1;
      pos = (char*)src + src_len;
      bytes = 0;
      goto done;
    }
    bytes -= (1 + p - pos);
    pos = p + 1;
  }

  p = memchr(pos, FIELD_MARK, bytes);
  if (p != NULL)
    bytes = p - pos; /* Length of field */

  if (vno == 0)
    goto done; /* Replacing whole field */

  if (vno < 0) /* Appending new value */
  {
    if (p != NULL)
      pos = p; /* 0553 */
    else
      pos += bytes; /* 0553 */
    if (bytes)
      vm = 1; /* 0553 */

    if (svno > 1)
      sm = svno - 1;
    bytes = 0;
    goto done;
  }

  /* Skip to start value */

  for (i = 1; i < vno; i++) {
    p = memchr(pos, VALUE_MARK, bytes);
    if (p == NULL) /* No such value */
    {
      vm = vno - i;
      if (svno > 1)
        sm = svno - 1;
      pos += bytes;
      bytes = 0;
      goto done;
    }
    bytes -= (1 + p - pos);
    pos = p + 1;
  }

  p = memchr(pos, VALUE_MARK, bytes);
  if (p != NULL)
    bytes = p - pos; /* Length of value, including end mark */

  if (svno == 0)
    goto done; /* Replacing whole value */

  if (svno < 1) /* Appending new subvalue */
  {
    if (p != NULL)
      pos = p; /* 0553 */
    else
      pos += bytes; /* 0553 */
    if (bytes)
      sm = 1; /* 0553 */
    bytes = 0;
    goto done;
  }

  /* Skip to start subvalue */

  for (i = 1; i < svno; i++) {
    p = memchr(pos, SUBVALUE_MARK, bytes);
    if (p == NULL) /* No such subvalue */
    {
      sm = svno - i;
      pos += bytes;
      bytes = 0;
      goto done;
    }
    bytes -= (1 + p - pos);
    pos = p + 1;
  }

  p = memchr(pos, SUBVALUE_MARK, bytes);
  if (p != NULL)
    bytes = p - pos; /* Length of subvalue, including end mark */

done:
  /* Now construct new string with 'bytes' bytes omitted starting at 'pos',
    inserting fm, vm and sm marks and new data                             */

  new_len = src_len - bytes + fm + vm + sm + ins_len;
  new_str = SysAllocStringByteLen(NULL, new_len);
  p = (char*)new_str;

  n = pos - (char*)src; /* Length of leading substring */
  if (n) {
    memcpy(p, src, n);
    p += n;
  }

  while (fm--)
    *(p++) = FIELD_MARK;
  while (vm--)
    *(p++) = VALUE_MARK;
  while (sm--)
    *(p++) = SUBVALUE_MARK;

  if (ins_len) {
    memcpy(p, new, ins_len);
    p += ins_len;
  }

  n = src_len - (bytes + n); /* Length of trailing substring */
  if (n) {
    memcpy(p, pos + bytes, n);
  }

  return new_str;
}

/* ======================================================================
   SDRespond()  -  Respond to request for input                           */

BSTR _stdcall _export SDRespond(response, err) BSTR response;
int16_t* err;
{
  int32_t reply_len = 0;
  BSTR reply;
  struct SESSION* session;

  CDebug(("SDRespond"));
  session = TlsGetValue(tls);

  if (context_error(session, CX_EXECUTING))
    goto exit_sdrespond;

  if (!message_pair(session, SrvrRespond, (char*)response,
                    SysStringByteLen(response))) {
    goto exit_sdrespond;
  }

  switch (session->server_error) {
    case SV_OK:
      session->context = CX_CONNECTED;
      /* **** FALL THROUGH **** */

    case SV_PROMPT:
      reply_len = session->buff_bytes - offsetof(INBUFF, data.execute.reply);
      break;

    case SV_ERROR: /* Probably SDRespond() used when not expected */
      break;

    case SV_ON_ERROR:
      session->context = CX_CONNECTED;
      Abort("EXECUTE generated an abort event", TRUE);
      break;
  }

exit_sdrespond:
  reply = SysAllocStringByteLen(
      (reply_len == 0) ? NULL : (session->buff->data.execute.reply), reply_len);
  *err = session->server_error;
  return reply;
}

/* ======================================================================
   SDSelect()  - Generate select list                                     */

void _stdcall _export SDSelect(fno, listno) int16_t fno;
int16_t listno;
{
  struct {
    int16_t fno;
    int16_t listno;
  } packet;
  struct SESSION* session;

  CDebug("SDSelect");
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_sdselect;

  packet.fno = fno;
  packet.listno = listno;

  if (!message_pair(session, SrvrSelect, (char*)&packet, sizeof(packet))) {
    goto exit_sdselect;
  }

  switch (session->server_error) {
    case SV_OK:
      break;

    case SV_ON_ERROR:
      Abort("Select generated an abort event", TRUE);
      break;
  }

exit_sdselect:
  return;
}

/* ======================================================================
   SDSelectIndex()  - Generate select list from index entry               */

void _stdcall _export SDSelectIndex(fno,
                                    index_name,
                                    index_value,
                                    listno) int16_t fno;
BSTR index_name;
BSTR index_value;
int16_t listno;
{
  struct {
    int16_t fno;
    int16_t listno;
    int16_t name_len;
    char index_name[255 + 1];
    int16_t data_len_place_holder;         /* Index name is actually... */
    char index_data_place_holder[255 + 1]; /* ...var length */
  } packet;
  int16_t n;
  char* p;
  struct SESSION* session;

  CDebug(("SDSelectIndex"));
  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_sdselectindex;

  packet.fno = fno;
  packet.listno = listno;

  /* Insert index name */

  n = SysStringByteLen(index_name);
  packet.name_len = n;
  p = packet.index_name;
  memcpy(p, index_name, n);
  p += n;
  if (n & 1)
    *(p++) = '\0';

  /* Insert index value */

  n = SysStringByteLen(index_value);
  *((int16_t*)p) = n;
  p += sizeof(int16_t);
  memcpy(p, index_value, n);
  p += n;
  if (n & 1)
    *(p++) = '\0';

  if (!message_pair(session, SrvrSelectIndex, (char*)&packet,
                    p - (char*)&packet)) {
    goto exit_sdselectindex;
  }

  switch (session->server_error) {
    case SV_OK:
      break;

    case SV_ON_ERROR:
      Abort("SelectIndex generated an abort event", TRUE);
      break;
  }

exit_sdselectindex:
  return;
}

/* ======================================================================
   SDSelectLeft()  - Scan index position to left
   SDSelectRight()  - Scan index position to right                        */

BSTR _stdcall _export SDSelectLeft(fno, index_name, listno) int16_t fno;
BSTR index_name;
int16_t listno;
{
  CDebug("SDSelectLeft");
  return SelectLeftRight(fno, index_name, listno, SrvrSelectLeft);
}

BSTR _stdcall _export SDSelectRight(fno, index_name, listno) int16_t fno;
BSTR index_name;
int16_t listno;
{
  CDebug(("SDSelectRight"));
  return SelectLeftRight(fno, index_name, listno, SrvrSelectRight);
}

BSTR SelectLeftRight(fno, index_name, listno, mode) int16_t fno;
BSTR index_name;
int16_t listno;
int16_t mode;
{
  int32_t key_len = 0;
  BSTR key;
  struct {
    int16_t fno;
    int16_t listno;
    char index_name[255 + 1];
  } packet;
  int16_t n;
  char* p;
  struct SESSION* session;

  session = TlsGetValue(tls);

  if (!context_error(session, CX_CONNECTED)) {
    packet.fno = fno;
    packet.listno = listno;

    /* Insert index name */

    n = SysStringByteLen(index_name);
    p = packet.index_name;
    memcpy(p, index_name, n);
    p += n;

    if (message_pair(session, mode, (char*)&packet, p - (char*)&packet)) {
      switch (session->server_error) {
        case SV_OK:
          key_len = session->buff_bytes - offsetof(INBUFF, data.selectleft.key);
          break;

        case SV_ON_ERROR:
          Abort("SelectLeft / SelectRight generated an abort event", TRUE);
          break;
      }
    }
  }

  key = SysAllocStringByteLen(
      (key_len == 0) ? NULL : (session->buff->data.selectleft.key), key_len);
  return key;
}

/* ======================================================================
   SDSetLeft()  - Set index position at extreme left
   SDSetRight()  - Set index position at extreme right                    */

void _stdcall _export SDSetLeft(fno, index_name) int16_t fno;
BSTR index_name;
{
  CDebug("SDSetLeft");
  SetLeftRight(fno, index_name, SrvrSetLeft);
}

void _stdcall _export SDSetRight(fno, index_name) int16_t fno;
BSTR index_name;
{
  CDebug(("SDSetRight"));
  SetLeftRight(fno, index_name, SrvrSetRight);
}

void SetLeftRight(fno, index_name, mode) int16_t fno;
BSTR index_name;
int16_t mode;
{
  struct {
    int16_t fno;
    char index_name[255 + 1];
  } packet;
  int16_t n;
  char* p;
  struct SESSION* session;

  session = TlsGetValue(tls);

  if (!context_error(session, CX_CONNECTED)) {
    packet.fno = fno;

    /* Insert index name */

    n = SysStringByteLen(index_name);
    p = packet.index_name;
    memcpy(p, index_name, n);
    p += n;

    if (message_pair(session, mode, (char*)&packet, p - (char*)&packet)) {
      switch (session->server_error) {
        case SV_OK:
          break;

        case SV_ON_ERROR:
          Abort("SetLeft / SetRight generated an abort event", TRUE);
          break;
      }
    }
  }
}

/* ======================================================================
   SDSetSession()  -  Set session index                                   */

VBBool _stdcall _export SDSetSession(idx) int16_t idx;
{
  if ((idx < 0) || (idx >= MAX_SESSIONS) ||
      (sessions[idx].context == CX_DISCONNECTED)) {
    return VBFalse;
  }

  CDebug("SDSetSession");
  TlsSetValue(tls, &(sessions[idx]));

  return VBTrue;
}

/* ======================================================================
   SDStatus()  -  Return STATUS() value                                   */

int32_t _stdcall _export SDStatus() {
  struct SESSION* session;

  session = TlsGetValue(tls);

  return session->sd_status;
}

/* ======================================================================
   SDWrite()  -  Write record                                             */

void _stdcall _export SDWrite(fno, id, data) int16_t fno;
BSTR id;
BSTR data;
{
  CDebug(("SDWrite"));
  write_record(SrvrWrite, fno, id, data);
}

/* ======================================================================
   SDWriteu()  -  Write record, retaining lock                            */

void _stdcall _export SDWriteu(fno, id, data) int16_t fno;
BSTR id;
BSTR data;
{
  CDebug("SDWriteu");
  write_record(SrvrWriteu, fno, id, data);
}

/* ======================================================================
   context_error()  - Check for appropriate context                       */

bool context_error(session, expected) struct SESSION* session;
int16_t expected;
{
  char* p;

  if (session->context != expected) {
    switch (session->context) {
      case CX_DISCONNECTED:
        p = "A server function has been attempted when no connection has been "
            "established";
        break;

      case CX_CONNECTED:
        switch (expected) {
          case CX_DISCONNECTED:
            p = "A function has been attempted which is not allowed when a "
                "connection has been established";
            break;

          case CX_EXECUTING:
            p = "Cannot send a response or end a command when not executing a "
                "server command";
            break;
        }
        break;

      case CX_EXECUTING:
        p = "A new server function has been attempted while executing a server "
            "command";
        break;

      default:
        p = "A function has been attempted in the wrong context";
        break;
    }

    MessageBox(NULL, p, "SDClient: Context error",
               MB_OK | MB_ICONEXCLAMATION | MB_APPLMODAL);
    return TRUE;
  }

  return FALSE;
}

/* ====================================================================== */

void delete_record(mode, fno, id) int16_t mode;
int16_t fno;
BSTR id;
{
  int id_len;
  struct {
    int16_t fno;
    char id[MAX_ID_LEN];
  } packet;
  struct SESSION* session;

  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_delete;

  packet.fno = fno;

  id_len = SysStringByteLen(id);
  if ((id_len < 1) || (id_len > MAX_ID_LEN)) {
    session->server_error = SV_ON_ERROR;
    session->sd_status = ER_IID;
  } else {
    memcpy(packet.id, id, id_len);

    if (!message_pair(session, mode, (char*)&packet, id_len + 2)) {
      goto exit_delete;
    }
  }

  switch (session->server_error) {
    case SV_OK:
      break;

    case SV_ON_ERROR:
      Abort("DELETE generated an abort event", TRUE);
      break;

    case SV_LOCKED:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }

exit_delete:
  return;
}

/* ======================================================================
   read_record()  -  Common path for READ, READL and READU                */

BSTR read_record(fno, id, err, mode) int16_t fno;
BSTR id;
int16_t* err;
int mode;
{
  int32_t status;
  int32_t rec_len = 0;
  int id_len;
  BSTR rec;
  struct {
    int16_t fno;
    char id[MAX_ID_LEN];
  } packet;
  struct SESSION* session;

  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_read;

  id_len = SysStringByteLen(id);
  if ((id_len < 1) || (id_len > MAX_ID_LEN)) {
    session->sd_status = ER_IID;
    status = SV_ON_ERROR;
    goto exit_read;
  }

  packet.fno = fno;
  memcpy(packet.id, id, id_len);

  if (!message_pair(session, mode, (char*)&packet, id_len + 2)) {
    goto exit_read;
  }

  switch (session->server_error) {
    case SV_OK:
      rec_len = session->buff_bytes - offsetof(INBUFF, data.read.rec);
      break;

    case SV_ON_ERROR:
      Abort("Read generated an abort event", TRUE);
      break;

    case SV_LOCKED:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }

  status = session->server_error;

exit_read:
  rec = SysAllocStringByteLen(
      (rec_len == 0) ? NULL : (session->buff->data.read.rec), rec_len);
  *err = status;
  return rec;
}

/* ====================================================================== */

void write_record(mode, fno, id, data) int16_t mode;
int16_t fno;
BSTR id;
BSTR data;
{
  int id_len;
  int32_t data_len;
  int bytes;
  INBUFF* q;
  struct PACKET {
    int16_t fno;
    int16_t id_len;
    char id[1];
  };
  struct SESSION* session;

  session = TlsGetValue(tls);

  if (context_error(session, CX_CONNECTED))
    goto exit_write;

  id_len = SysStringByteLen(id);
  if ((id_len < 1) || (id_len > MAX_ID_LEN)) {
    Abort("Illegal record id", FALSE);
    session->sd_status = ER_IID;
    goto exit_write;
  }

  data_len = SysStringByteLen(data);

  /* Ensure buffer is big enough for this record */

  bytes = sizeof(struct PACKET) + id_len + data_len;
  if (bytes >= session->buff_size) /* Must reallocate larger buffer */
  {
    bytes = (bytes + BUFF_INCR - 1) & ~BUFF_INCR;
    q = (INBUFF*)malloc(bytes);
    if (q == NULL) {
      Abort("Insufficient memory", FALSE);
      session->sd_status = ER_SRVRMEM;
      goto exit_write;
    }
    free(session->buff);
    session->buff = q;
    session->buff_size = bytes;
  }

  /* Set up outgoing packet */

  ((struct PACKET*)(session->buff))->fno = fno;
  ((struct PACKET*)(session->buff))->id_len = id_len;
  memcpy(((struct PACKET*)(session->buff))->id, id, id_len);
  memcpy(((struct PACKET*)(session->buff))->id + id_len, data, data_len);

  if (!message_pair(session, mode, (char*)(session->buff),
                    offsetof(struct PACKET, id) + id_len + data_len)) {
    goto exit_write;
  }

  switch (session->server_error) {
    case SV_OK:
      break;

    case SV_ON_ERROR:
      Abort("Write generated an abort event", TRUE);
      break;

    case SV_LOCKED:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }

exit_write:
  return;
}

/* ====================================================================== */

bool GetResponse(struct SESSION* session) {
  if (!read_packet(session))
    return FALSE;

  if (session->server_error == SV_ERROR) {
    strcpy(session->sderror, "Unable to retrieve error text");
    write_packet(session, SrvrGetError, NULL, 0);
    if (read_packet(session))
      strcpy(session->sderror, session->buff->data.error.text);
    return FALSE;
  }

  return TRUE;
}

/* ====================================================================== */

void Abort(msg, use_response) char* msg;
bool use_response;
{
  char abort_msg[1024 + 1];
  int n;
  char* p;
  struct SESSION* session;

  strcpy(abort_msg, msg);

  if (use_response) {
    session = TlsGetValue(tls);
    n = session->buff_bytes - offsetof(INBUFF, data.abort.message);
    if (n > 0) {
      p = abort_msg + strlen(msg);
      *(p++) = '\r';
      memcpy(p, session->buff->data.abort.message, n);
      *(p + n) = '\0';
    }
  }

  MessageBox(NULL, abort_msg, NULL, MB_OK | MB_ICONEXCLAMATION | MB_APPLMODAL);
}

/* ======================================================================
   Call a catalogued subroutine                                           */

void catcall(struct SESSION* session, BSTR subrname, int16_t argc, ...) {
  va_list ap;
  int16_t i;
  BSTR arg;
  int subrname_len;
  int32_t arg_len;
  int32_t bytes; /* Total length of outgoing packet */
  int32_t n;
  char* p;
  INBUFF* q;
  struct ARGDATA* argptr;
  BSTR* argx;
  int offset;

#define MAX_ARGS 20

  subrname_len = SysStringByteLen(subrname);
  if ((subrname_len < 1) || (subrname_len > MAX_CALL_NAME_LEN)) {
    Abort("Illegal subroutine name in call", FALSE);
  }

  if ((argc < 0) || (argc > MAX_ARGS)) {
    Abort("Illegal argument count in call", FALSE);
  }

  /* Accumulate outgoing packet size */

  bytes = 2;                        /* Subrname length */
  bytes += (subrname_len + 1) & ~1; /* Subrname string (padded) */
  bytes += 2;                       /* Arg count */

  va_start(ap, argc);
  for (i = 0; i < argc; i++) {
    arg = *(va_arg(ap, BSTR*));
    arg_len = (arg == NULL) ? 0 : SysStringByteLen(arg);
    bytes +=
        4 + ((arg_len + 1) & ~1); /* Four byte length + arg text (padded) */
  }
  va_end(ap);

  /* Ensure buffer is big enough */

  if (bytes >= session->buff_size) /* Must reallocate larger buffer */
  {
    n = (bytes + BUFF_INCR - 1) & ~BUFF_INCR;
    q = (INBUFF*)malloc(n);
    if (q == NULL) {
      Abort("Unable to allocate network buffer", FALSE);
    }
    free(session->buff);
    session->buff = q;
    session->buff_size = bytes;
  }

  /* Set up outgoing packet */

  p = (char*)(session->buff);

  *((int16_t*)p) = subrname_len; /* Subrname length */
  p += 2;

  memcpy(p, subrname, subrname_len); /* Subrname */
  p += (subrname_len + 1) & ~1;

  *((int16_t*)p) = argc; /* Arg count */
  p += 2;

  va_start(ap, argc);
  for (i = 1; i <= argc; i++) {
    arg = *(va_arg(ap, BSTR*));
    arg_len = (arg == NULL) ? 0 : SysStringByteLen(arg);
    *((int32_t*)p) = arg_len; /* Arg length */
    p += 4;
    if (arg_len)
      memcpy(p, arg, arg_len); /* Arg text */
    p += (arg_len + 1) & ~1;
  }
  va_end(ap);

  if (!message_pair(session, SrvrCall, (char*)(session->buff), bytes)) {
    goto err;
  }

  /* Now update any returned arguments */

  offset = offsetof(INBUFF, data.call.argdata);
  if (offset < session->buff_bytes) {
    va_start(ap, argc);
    for (i = 1; i <= argc; i++) {
      argptr = (ARGDATA*)(((char*)(session->buff)) + offset);

      argx = va_arg(ap, BSTR*);
      if (i == argptr->argno) {
        if (*argx != NULL)
          SysFreeString(*argx);

        *argx = SysAllocStringByteLen(argptr->text, argptr->arglen);

        offset += offsetof(ARGDATA, text) + ((argptr->arglen + 1) & ~1);
        if (offset >= session->buff_bytes)
          break;
      }
    }
    va_end(ap);
  }

err:
  switch (session->server_error) {
    case SV_OK:
      break;

    case SV_ON_ERROR:
      Abort("CALL generated an abort event", TRUE);
      break;

    case SV_LOCKED:
    case SV_ELSE:
    case SV_ERROR:
      break;
  }
}

/* ====================================================================== */

char* memstr(str, substr, str_len, substr_len) char* str;
char* substr;
int str_len;
int substr_len;
{
  char* p;

  while (str_len != 0) {
    p = memchr(str, *substr, str_len);
    if (p == NULL)
      break;

    str_len -= p - str;
    if (str_len < substr_len)
      break;

    if (memcmp(p, substr, substr_len) == 0)
      return p;

    str = p + 1;
    str_len--;
  }

  return NULL;
}

/* ======================================================================
   match_template()  -  Match string against template                     */

bool match_template(
    char* string,
    char* template,
    int16_t component,        /* Current component number - 1 (incremented) */
    int16_t return_component, /* Required component number */
    char** component_start,
    char** component_end) {
  bool not;
  int16_t n;
  int16_t m;
  int16_t z;
  char* p;
  char delimiter;
  char* start;

  while (*template != '\0') {
    component++;
    if (component == return_component)
      *component_start = string;
    else if (component == return_component + 1)
      *component_end = string;

    start = template;

    if (*template == '~') {
      not = TRUE;
      template ++;
    } else
      not = FALSE;

    if (isdigit(*template)) {
      n = 0;
      do {
        n = (n * 10) + (*(template ++) - '0');
      } while (isdigit(*template));

      switch (UpperCase(*(template ++))) {
        case '\0': /* String longer than template */
          /* 0115 rewritten */
          n = --template - start;
          if (n == 0)
            return FALSE;
          if (!memcmp(start, string, n) == not)
            return FALSE;
          string += n;
          break;

        case 'A': /* nA  Alphabetic match */
          if (n) {
            while (n--) {
              if (*string == '\0')
                return FALSE;
              if ((isalpha(*string) != 0) == not)
                return FALSE;
              string++;
            }
          } else /* 0A */
          {
            if (*template != '\0') /* Further template items exist */
            {
              /* Match as many as possible further chaarcters */

              for (z = 0, p = string;; z++, p++) {
                if ((*p == '\0') || ((isalpha(*p) != 0) == not))
                  break;
              }

              /* z now holds number of contiguous alphabetic characters ahead
                 of current position. Starting one byte after the final
                 alphabetic character, backtrack until the remainder of the
                 string matches the remainder of the template.               */

              for (p = string + z; z-- >= 0; p--) {
                if (match_template(p, template, component, return_component,
                                   component_start, component_end)) {
                  goto match_found;
                }
              }
              return FALSE;
            } else {
              while ((*string != '\0') && ((isalpha(*string) != 0) != not)) {
                string++;
              }
            }
          }
          break;

        case 'N': /* nN  Numeric match */
          if (n) {
            while (n--) {
              if (*string == '\0')
                return FALSE;
              if ((isdigit(*string) != 0) == not)
                return FALSE;
              string++;
            }
          } else /* 0N */
          {
            if (*template != '\0') /* Further template items exist */
            {
              /* Match as many as possible further chaarcters */

              for (z = 0, p = string;; z++, p++) {
                if ((*p == '\0') || ((isdigit(*p) != 0) == not))
                  break;
              }

              /* z now holds number of contiguous numeric characters ahead
                 of current position. Starting one byte after the final
                 numeric character, backtrack until the remainder of the
                 string matches the remainder of the template.               */

              for (p = string + z; z-- >= 0; p--) {
                if (match_template(p, template, component, return_component,
                                   component_start, component_end)) {
                  goto match_found;
                }
              }
              return FALSE;
            } else {
              while ((*string != '\0') && ((isdigit(*string) != 0) != not)) {
                string++;
              }
            }
          }
          break;

        case 'X': /* nX  Unrestricted match */
          if (n) {
            while (n--) {
              if (*(string++) == '\0')
                return FALSE;
            }
          } else /* 0X */
          {
            if (*template != '\0') /* Further template items exist */
            {
              /* Match as few as possible further characters */

              do {
                if (match_template(string, template, component,
                                   return_component, component_start,
                                   component_end)) {
                  goto match_found;
                }
              } while (*(string++) != '\0');
              return FALSE;
            }
            goto match_found;
          }
          break;

        case '-': /* Count range */
          if (!isdigit(*template))
            return FALSE;
          m = 0;
          do {
            m = (m * 10) + (*(template ++) - '0');
          } while (isdigit(*template));
          m -= n;
          if (m < 0)
            return FALSE;

          switch (UpperCase(*(template ++))) {
            case '\0': /* String longer than template */
              n = --template - start;
              if (n) /* We have found a trailing unquoted literal */
              {
                if ((memcmp(start, string, n) == 0) != not)
                  return TRUE;
              }
              return FALSE;

            case 'A': /* n-mA  Alphabetic match */
                      /* Match n alphabetic items */

              while (n--) {
                if (*string == '\0')
                  return FALSE;
                if ((isalpha(*string) != 0) == not)
                  return FALSE;
                string++;
              }

              /* We may match up to m further alphabetic characters but must
                  also match as many as possible.  Check how many alphabetic
                  characters there are (up to m) and then backtrack trying
                  matches against the remaining template (if any).           */

              for (z = 0, p = string; z < m; z++, p++) {
                if ((*p == '\0') || ((isalpha(*p) != 0) == not))
                  break;
              }

              /* z now holds max number of matchable characters.
                  Try match at each of these positions and also at the next
                  position (Even if it is the end of the string)            */

              if (*template != '\0') /* Further template items exist */
              {
                for (p = string + z; z-- >= 0; p--) {
                  if (match_template(p, template, component, return_component,
                                     component_start, component_end)) {
                    goto match_found;
                  }
                }
                return FALSE;
              } else
                string += z;
              break;

            case 'N': /* n-mN  Numeric match */
                      /* Match n numeric items */

              while (n--) {
                if (*string == '\0')
                  return FALSE;
                if ((isdigit(*string) != 0) == not)
                  return FALSE;
                string++;
              }

              /* We may match up to m further numeric characters but must
                  also match as many as possible.  Check how many numeric
                  characters there are (up to m) and then backtrack trying
                  matches against the remaining template (if any).           */

              for (z = 0, p = string; z < m; z++, p++) {
                if ((*p == '\0') || ((isdigit(*p) != 0) == not))
                  break;
              }

              /* z now holds max number of matchable characters.
                  Try match at each of these positions and also at the next
                  position (Even if it is the end of the string)            */

              if (*template != '\0') /* Further template items exist */
              {
                for (p = string + z; z-- >= 0; p--) {
                  if (match_template(p, template, component, return_component,
                                     component_start, component_end)) {
                    goto match_found;
                  }
                }
                return FALSE;
              } else
                string += z;
              break;

            case 'X': /* n-mX  Unrestricted match */
              /* Match n items of any type */

              while (n--) {
                if (*(string++) == '\0')
                  return FALSE;
              }

              /* Match as few as possible up to m further characters */

              if (*template != '\0') {
                while (m--) {
                  if (match_template(string, template, component,
                                     return_component, component_start,
                                     component_end)) {
                    goto match_found;
                  }
                  string++;
                }
                return FALSE;
              } else {
                if ((signed int)strlen(string) > m)
                  return FALSE;
                goto match_found;
              }

            default:
              /* We have found an unquoted literal */
              n = --template - start;
              if ((memcmp(start, string, n) == 0) == not)
                return FALSE;
              string += n;
              break;
          }
          break;

        default:
          /* We have found an unquoted literal */
          n = --template - start;
          if ((memcmp(start, string, n) == 0) == not)
            return FALSE;
          string += n;
          break;
      }
    } else if (memcmp(template, "...", 3) == 0) /* ... same as 0X */
    {
      template += 3;
      if (not)
        return FALSE;
      if (*template != '\0') /* Further template items exist */
      {
        /* Match as few as possible further characters */

        while (*string != '\0') {
          if (match_template(string, template, component, return_component,
                             component_start, component_end)) {
            goto match_found;
          }
          string++;
        }
        return FALSE;
      }
      goto match_found;
    } else /* Must be literal text item */
    {
      delimiter = *template;
      if ((delimiter == '\'') || (delimiter == '"')) /* Quoted literal */
      {
        template ++;
        p = strchr(template, (char)delimiter);
        if (p == NULL)
          return FALSE;
        n = p - template;
        if (n) {
          if ((memcmp(template, string, n) == 0) == not)
            return FALSE;
          string += n;
        }
        template += n + 1;
      } else /* Unquoted literal. Treat as single character */
      {
        if ((*(template ++) == *(string++)) == not)
          return FALSE;
      }
    }
  }

  if (*string != '\0')
    return FALSE; /* String longer than template */

match_found:
  return TRUE;
}

/* ======================================================================
   message_pair()  -  Send message and receive response                   */

bool message_pair(session, type, data, bytes) struct SESSION* session;
int type;
unsigned char* data;
int32_t bytes;
{
  if (write_packet(session, type, data, bytes)) {
    return GetResponse(session);
  }

  return FALSE;
}

/* ======================================================================
   OpenSocket()  -  Open connection to server                            */

Private bool OpenSocket(struct SESSION* session, char* host, int16_t port) {
  bool status = FALSE;
  WSADATA wsadata;
  u_int32_t nInterfaceAddr;
  struct sockaddr_in sock_addr;
  int nPort;
  struct hostent* hostdata;
  char ack_buff;
  int n;
  unsigned int n1, n2, n3, n4;

  if (port < 0)
    port = 4245;

  /* Start Winsock up */

  if (WSAStartup(MAKEWORD(1, 1), &wsadata) != 0) {
    sprintf(session->sderror, "WSAStartup error");
    goto exit_opensocket;
  }

  if ((sscanf(host, "%u.%u.%u.%u", &n1, &n2, &n3, &n4) == 4) && (n1 <= 255) &&
      (n2 <= 255) && (n3 <= 255) && (n4 <= 255)) {
    /* Looks like an IP address */
    nInterfaceAddr = inet_addr(host);
  } else {
    hostdata = gethostbyname(host);
    if (hostdata == NULL) {
      NetError("gethostbyname()");
      goto exit_opensocket;
    }

    nInterfaceAddr = *((int32_t*)(hostdata->h_addr));
  }

  nPort = htons(port);

  session->sock = socket(AF_INET, SOCK_STREAM, 0);
  if (session->sock == INVALID_SOCKET) {
    NetError("socket()");
    goto exit_opensocket;
  }

  sock_addr.sin_family = AF_INET;
  sock_addr.sin_addr.s_addr = nInterfaceAddr;
  sock_addr.sin_port = nPort;

  if (connect(session->sock, (struct sockaddr*)&sock_addr, sizeof(sock_addr))) {
    NetError("connect()");
    goto exit_opensocket;
  }

  n = TRUE;
  setsockopt(session->sock, IPPROTO_TCP, TCP_NODELAY, (char*)&n, sizeof(int));

  /* Wait for an Ack character to arrive before we assume the connection
    to be open and working. This is necessary because Linux loses anything
    we send before the SD process is up and running.                       */

  do {
    if (recv(session->sock, &ack_buff, 1, 0) < 1)
      goto exit_opensocket;
  } while (ack_buff != '\x06');

  status = 1;

exit_opensocket:
  return status;
}

/* ======================================================================
   CloseSocket()  -  Close connection to server                          */

Private bool CloseSocket(struct SESSION* session) {
  bool status = FALSE;

  if (session->sock == INVALID_SOCKET)
    goto exit_closesocket;

  closesocket(session->sock);
  session->sock = INVALID_SOCKET;

  status = TRUE;

exit_closesocket:
  return status;
}

/* ======================================================================
   NetError                                                               */

static void NetError(char* prefix) {
  char msg[80];

  sprintf(msg, "Error %d from %s", WSAGetLastError(), prefix);
  MessageBox(NULL, msg, "SDClient: Network error",
             MB_OK | MB_ICONEXCLAMATION | MB_APPLMODAL);
}

/* ======================================================================
   read_packet()  -  Read a SD data packet                                */

Private bool read_packet(struct SESSION* session) {
  int rcvd_bytes;       /* Length of received packet fragment */
  int32_t packet_bytes; /* Total length of incoming packet */
  int rcv_len;
  int32_t n;
  unsigned char* p;

  /* 0272 restructured */
  struct {
    int32_t packet_length;
    int16_t server_error;
    int32_t status;
  } in_packet_header;
#define IN_PKT_HDR_BYTES 10

  if ((!session->is_local) && (session->sock == INVALID_SOCKET))
    return FALSE;

  /* Read packet header */

  p = (char*)&in_packet_header;
  session->buff_bytes = 0;
  do {
    rcv_len = IN_PKT_HDR_BYTES - session->buff_bytes;
    if (session->is_local) {
      if (!ReadFile(session->hPipe, p, rcv_len, (DWORD*)&rcvd_bytes, NULL))
        return FALSE;
    } else {
      if ((rcvd_bytes = recv(session->sock, p, rcv_len, 0)) <= 0)
        return FALSE;
    }

    session->buff_bytes += rcvd_bytes;
    p += rcvd_bytes;
  } while (session->buff_bytes < IN_PKT_HDR_BYTES);

  if (srvr_debug != NULL)
    debug((char*)&in_packet_header, IN_PKT_HDR_BYTES);

  /* Calculate remaining bytes to read */

  packet_bytes = in_packet_header.packet_length - IN_PKT_HDR_BYTES;

  if (srvr_debug != NULL) {
    fprintf(srvr_debug, "IN (%ld bytes)\n", packet_bytes);
    fflush(srvr_debug);
  }

  if (packet_bytes >= session->buff_size) /* Must reallocate larger buffer */
  {
    free(session->buff);
    n = (packet_bytes + BUFF_INCR) & ~(BUFF_INCR - 1);
    session->buff = (INBUFF*)malloc(n);
    if (session->buff == NULL)
      return FALSE;
    session->buff_size = n;

    if (srvr_debug != NULL) {
      fprintf(srvr_debug, "Resized buffer to %ld bytes\n", session->buff_size);
      fflush(srvr_debug);
    }
  }

  /* Read data part of packet */

  p = (char*)(session->buff);
  session->buff_bytes = 0;
  while (session->buff_bytes < packet_bytes) {
    rcv_len = min(session->buff_size - session->buff_bytes, 16384);
    if (session->is_local) {
      if (!ReadFile(session->hPipe, p, rcv_len, (DWORD*)&rcvd_bytes, NULL))
        return FALSE;
    } else {
      if ((rcvd_bytes = recv(session->sock, p, rcv_len, 0)) <= 0)
        return FALSE;
    }

    session->buff_bytes += rcvd_bytes;
    p += rcvd_bytes;
  }

  ((char*)(session->buff))[session->buff_bytes] = '\0';

  if (srvr_debug != NULL)
    debug((char*)(session->buff), session->buff_bytes);

  session->server_error = in_packet_header.server_error;
  session->sd_status = in_packet_header.status;

  return TRUE;
}

/* ======================================================================
   write_packet()  -  Send SD data packet                                 */

Private bool write_packet(struct SESSION* session,
                          int type,
                          char* data,
                          int32_t bytes) {
  DWORD n;

  struct {
    int32_t length;
    int16_t type;
  } packet_header;
#define PKT_HDR_BYTES 6

  if ((!session->is_local) && (session->sock == INVALID_SOCKET))
    return FALSE;

  packet_header.length = bytes + PKT_HDR_BYTES; /* 0272 */
  packet_header.type = type;
  if (session->is_local) {
    if (!WriteFile(session->hPipe, (char*)&packet_header, PKT_HDR_BYTES, &n,
                   NULL)) {
      return FALSE;
    }
  } else {
    if (send(session->sock, (unsigned char*)&packet_header, PKT_HDR_BYTES, 0) !=
        PKT_HDR_BYTES)
      return FALSE;
  }

  if (srvr_debug != NULL) {
    fprintf(srvr_debug, "OUT (%ld bytes). Type %d\n", packet_header.length,
            (int)packet_header.type);
    fflush(srvr_debug);
  }

  if ((data != NULL) && (bytes > 0)) {
    if (session->is_local) {
      if (!WriteFile(session->hPipe, data, (DWORD)bytes, &n, NULL)) {
        return FALSE;
      }
    } else {
      if (send(session->sock, data, bytes, 0) != bytes)
        return FALSE;
    }

    if (srvr_debug != NULL)
      debug(data, bytes);
  }

  return TRUE;
}

/* ======================================================================
   debug()  -  Debug function                                             */

Private void debug(unsigned char* p, int n) {
  int16_t i;
  int16_t j;
  unsigned char c;
  char s[72 + 1];
  static char hex[] = "0123456789ABCDEF";

  for (i = 0; i < n; i += 16) {
    memset(s, ' ', 72);
    s[72] = '\0';

    s[0] = hex[i >> 12];
    s[1] = hex[(i >> 8) & 0x0F];
    s[2] = hex[(i >> 4) & 0x0F];
    s[3] = hex[i & 0x0F];
    s[4] = ':';
    s[54] = '|';

    for (j = 0; (j < 16) && ((j + i) < n); j++) {
      c = *(p++);
      s[6 + 3 * j] = hex[c >> 4];
      s[7 + 3 * j] = hex[c & 0x0F];
      s[56 + j] = ((c >= 32) && (c < 128)) ? c : '.';
    }

    fprintf(srvr_debug, "%s\n", s);
  }
  fprintf(srvr_debug, "\n");
  fflush(srvr_debug);
}

/* ======================================================================
   sysdir()  -  Return static SDSYS directory pointer                     */

char* sysdir() {
  static char sysdirpath[MAX_PATHNAME_LEN + 1];
  FILE* fu;
  char* p;
  char path[MAX_PATHNAME_LEN + 1];
  char section[50];
  char rec[200 + 1];
  struct SESSION* session;

  session = TlsGetValue(tls);

  GetWindowsDirectory(path, MAX_PATHNAME_LEN);
  strcat(path, "\\sd.ini");

  fu = fopen(path, "rt");
  if (fu == NULL) {
    sprintf(session->sderror, "%s not found", path);
    return NULL;
  }

  section[0] = '\0';
  while (fgets(rec, 200, fu) != NULL) {
    if ((p = strchr(rec, '\n')) != NULL)
      *p = '\0';

    if ((rec[0] == '#') || (rec[0] == '\0'))
      continue;

    if (rec[0] == '[') {
      if ((p = strchr(rec, ']')) != NULL)
        *p = '\0';
      strcpy(section, rec + 1);
      strupr(section);
      continue;
    }

    if (strcmp(section, "SD") == 0) /* [sd] items */
    {
      if (strncmp(rec, "SDSYS=", 6) == 0) {
        strcpy(sysdirpath, rec + 6);
        break;
      }
    }
  }

  fclose(fu);

  if (sysdirpath[0] == '\0') {
    sprintf(session->sderror, "No SDSYS parameter in configuration file");
    return NULL;
  }

  return sysdirpath;
}

/* ====================================================================== */

Private struct SESSION* FindFreeSession() {
  int16_t i;
  struct SESSION* session;

  /* Find a free session table entry */

  for (i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].context == CX_DISCONNECTED)
      break;
  }

  if (i == MAX_SESSIONS) {
    /* Must return error via a currently connected session */
    session = TlsGetValue(tls);
    strcpy(session->sderror, "Too many sessions");
    return NULL;
  }

  session = &(sessions[i]);
  TlsSetValue(tls, session);
  if (srvr_debug != NULL) {
    fprintf(srvr_debug, "Set session %d as %08lX, tls %08lX\n", session->idx,
            (int32_t)session, (int32_t)tls);
    fflush(srvr_debug);
  }

  if (session->buff == NULL) {
    session->buff_size = 2048;
    session->buff = (INBUFF*)malloc(session->buff_size);
  }

  return session;
}

/* ====================================================================== */

Private void disconnect(struct SESSION* session) {
  if (srvr_debug != NULL) {
    fprintf(srvr_debug, "Disconnect session %d (%08lX)\n", session->idx,
            (int32_t)session);
    fflush(srvr_debug);
  }

  (void)write_packet(session, SrvrQuit, NULL, 0);
  if (session->is_local) {
    if (session->hPipe != INVALID_HANDLE_VALUE) {
      CloseHandle(session->hPipe);
      session->hPipe = INVALID_HANDLE_VALUE;
    }
  } else
    (void)CloseSocket(session);
  session->context = CX_DISCONNECTED;

  if (session->buff != NULL) {
    free(session->buff);
    session->buff = NULL;
  }
}

/* ====================================================================== */

void ClientDebug(char* name) {
  struct SESSION* session;

  if (srvr_debug != NULL) {
    session = TlsGetValue(tls);
    fprintf(srvr_debug, "%d (%08lX): %s\n", session->idx, GetCurrentThreadId(),
            name);
    fflush(srvr_debug);
  }
}

/* END-CODE */
