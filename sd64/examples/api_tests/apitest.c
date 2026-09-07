/*
apitest.c
program to test some of the api functions, by no means an exhaustive test suite


   compile apitest 
gcc -Wall -Wformat=2 -Wno-format-nonliteral -DLINUX -D_FILE_OFFSET_BITS=64 -I ./gplsrc/ -DGPL -g  ./apitest.c -o apitest  -lsdcli

 assumes api shared library found at /usr/lib64/libsdcli.so
 sudo cp ~/sdb64/sd64/bin/libsdcli.so  /usr/lib64/libsdcli.so
*/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "sdclilib.h"

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

extern int SDConnectUDS(char* account);

int main(void){

  int status;
  int fno;
  char* myrec;
  int myerr;  
  char * rsp;
  int err;

  time_t rawtime;
  struct tm * timeinfo;
#define uname_sz 30
  char uname[uname_sz];  
  
  printf("This is a sdclilib shared library test...\n");
  uid_t uid = getuid();
  struct passwd *pw = getpwuid(uid);
  if (pw) {
    printf("Username: %s\n", pw->pw_name);
    snprintf(uname,uname_sz,"%s", pw->pw_name);
  } else {
    printf("Failed to get username");
    return 0;
  }

  printf("Attempting LocalLogin(), then Account change to %s\n",uname);


  status = SDConnectLocal(uname);

  printf("SDConnectLocal status: %d \n", status);
  
  /* rem The SDStatus() function returns the value of the QMBasic STATUS() function for the last server function executed. */
    
  if (status == 1) {

//  run listu 
    printf( "Execute listu 1st time \n");
    rsp = SDExecute("LISTU", &err);
    printf("Response:  %s \n\n", rsp);
    SDFree(rsp);
   
    printf( "Attempt to open file: TESTDATA \n");
    fno = SDOpen("TESTDATA");

    printf( "File number set: %d  \n", fno);
    if (fno == 0){
        printf("Failed to Open - Status: %d Message:  %s\n",SDStatus(), SDError()); 
    
    } else {
        printf( "Attempting to Read file: TESTDATA, Rec: REC2\n");
      
        myrec = SDRead(fno, "REC2", &myerr);
        printf( "REC1:  %s \n", myrec);
        printf( "myerr:  %d \n", myerr);
        printf( "Error Msg:  %s \n", SDError());
        printf( "SDStatus():  %d \n\n", SDStatus());
      
        printf( "Attempting to Read with bad file number\n");
      
        myrec = SDRead(13, "REC1", &myerr);
        printf( "REC1:  %s \n", myrec);
        printf( "myerr:  %d \n", myerr);
        printf( "Error Msg:  %s \n", SDError());
        printf( "SDStatus():  %d \n\n", SDStatus());
      

        if (myrec!=NULL){
          free(myrec);
        }


        printf( "Attempting to Write file: TESTDATA, Rec: REC2\n");


        time ( &rawtime );
        timeinfo = localtime ( &rawtime );
        myrec = malloc(512 * sizeof(char));
        sprintf(myrec, "This is the new record written at time: %s", asctime (timeinfo) );
        SDWrite(fno, "REC2", myrec);
        printf( "SDStatus():  %d \n\n", SDStatus());
        

        printf( "Attempting to Write file with bad file number\n");


        SDWrite(13, "REC3", myrec);
        printf( "SDStatus():  %d \n", SDStatus());
        printf( "Error Msg:  %s \n\n", SDError());
        free(myrec);

        printf( "Attempt to close file: TESTDATA \n\n");


        SDClose(fno);
        
/*  BP TESTSUB 
* very simple test of SDCallx
*  open TESTDATA
*  create a record made up of passed values
*  add a field with a time date stamp
*  write the record as TESTSUB
*  return the field with the time date stamp to the caller
subroutine testsub(a1, a2, a3)
* we use set.status, must flag and rem to start sd with -interal flag when compiling
$internal
$include err.h                           
open 'TESTDATA' to TS then
  a2rtn = a2:  ' Plus we added TIMEDATE ':TIMEDATE()
  rec = ''
  rec = a1
  rec<2> = a2
  rec<3> = a3
  rec<4> = a2rtn
  write rec on TS, "TESTSUB"
  a2 = a2rtn
  set.status 1313
end else
  set.status ER$FNF
end
Return
End
*/        
        printf( "Attempting to Callx on TESTSUB, rem sets status to 1313 \n");
        printf("SDCallx(TESTSUB,3,this is arg 1,and arg 2,last arg 3)\n");
        SDCallx("TESTSUB",3,"this is arg 1","and arg 2","last arg 3");
        printf( "SDStatus():  %d \n", SDStatus());
        printf( "Error Msg:  %s \n\n", SDError());
    
        printf( "Attempting to retrieve arg2 Callx on TESTSUB\n");
        rsp = SDGetArg(2);
        if (rsp != NULL) {
             printf( "arg2: %s\n",rsp);
             SDFree(rsp);
        }else{
             printf( "Null returned from SDGetArg\n");
        }
        printf( "SDStatus():  %d \n", SDStatus());
        printf( "Error Msg:  %s \n\n", SDError());    
    }

    printf( "Disconnect \n"); 
    SDDisconnect();

  } else {
  
    printf( "connectlocal failed \n");   
  }



  printf("Test complete\n");
}