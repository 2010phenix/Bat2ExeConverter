#include <stdio.h>
#include <windows.h>
#include "tstring.h"
#include "prepare.h"

#define BUFSIZE 4096

HANDLE hChildStdinRd, hChildStdinWr, hChildStdinWrDup,
       hChildStdoutRd, hChildStdoutWr, hChildStdoutRdDup,
       hInputFile, hStdout;

int createIncludeFile(LPCSTR batFileName) {
	DWORD dwRead, dwWritten;
	CHAR chBuf[BUFSIZE], chFileBuf[BUFSIZE * 2], chReplBuf[BUFSIZE * 2], *pDes;
	CHAR cmdVar[] = "CHAR cmdStr[] = \"\"\r\n\"";
	
	ZeroMemory(chBuf, BUFSIZE);
	ZeroMemory(chFileBuf, BUFSIZE * 2);
	ZeroMemory(chReplBuf, BUFSIZE * 2);
	
	HANDLE hBat = CreateFile(batFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	ReadFile(hBat, chBuf, BUFSIZE, &dwRead, NULL);
	printf("%s", chBuf);
	
	HANDLE hInc = CreateFile("compile\\batcmd.h", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	pDes = strrepl(chBuf, chReplBuf, sizeof(chReplBuf), "\r\n", "\\n\"\r\n\"");
	dwRead = sprintf(chFileBuf, "%s%s\";", cmdVar, pDes);
	WriteFile(hInc, chFileBuf, dwRead, &dwWritten, NULL);
	
	CloseHandle(hBat);
	CloseHandle(hInc);
	
	return 0;
}

VOID ErrorExit (const char *lpszMessage) {
    fprintf(stderr, "%s\n", lpszMessage);
    ExitProcess(0);
}

BOOL CreateChildProcess() {
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    BOOL bFuncRetn = FALSE;

    // Set up members of the PROCESS_INFORMATION structure.
    ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );

    // Set up members of the STARTUPINFO structure.
    // �趨DOS���̵ı�׼���롢����ʹ�����Ϣ�Ĺܵ�
    // ʹ��ǰ�洴����ֵ��DOS���ڵ�����������ᱻ���򵽱�Ӧ����
    ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = hChildStdoutWr;
    siStartInfo.hStdOutput = hChildStdoutWr;
    siStartInfo.hStdInput = hChildStdinRd;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    char cmdLine[] = "cmd";
    // Create the child process.
    bFuncRetn = CreateProcess(NULL,
                              cmdLine,       // command line
                              NULL,          // process security attributes
                              NULL,          // primary thread security attributes
                              TRUE,          // handles are inherited
                              0,             // creation flags
                              NULL,          // use parent's environment
                              NULL,          // use parent's current directory
                              &siStartInfo,  // STARTUPINFO pointer
                              &piProcInfo);  // receives PROCESS_INFORMATION

    if (bFuncRetn == 0)
        ErrorExit("CreateProcess failed");
    else {
        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);
        return bFuncRetn;
    }
}

VOID WriteToPipe(VOID) {
    DWORD dwRead, dwWritten;
    CHAR chBuf[BUFSIZE];
    CHAR cmds[] = "cd compile\n"
                    "mingw32-make.exe -f Makefile.win all\n"
                    ""; 

    WriteFile(hChildStdinWrDup, cmds, sizeof(cmds), &dwWritten, NULL);

    // Close the pipe handle so the child process stops reading.
    if (! CloseHandle(hChildStdinWrDup))
        ErrorExit("Close pipe failed");
}

VOID ReadFromPipe(VOID) {
    DWORD dwRead, dwWritten;
    CHAR chBuf[BUFSIZE];

    // Close the write end of the pipe before reading from the
    // read end of the pipe.
    if (!CloseHandle(hChildStdoutWr))
        ErrorExit("CloseHandle failed");

    // Read output from the child process, and write to parent's STDOUT.
    // ��ȡ���̣߳���DOS���ڵ��������ʾ����׼����豸��
    for (;;) {
        if( !ReadFile( hChildStdoutRdDup, chBuf, BUFSIZE, &dwRead,
                       NULL) || dwRead == 0) break;
        if (! WriteFile(hStdout, chBuf, dwRead, &dwWritten, NULL))
            break;
    }
}

int createBatExe() {
    // SECURITY_ATTRIBUTES�ṹ����һ������İ�ȫ����������ָ��������ָ������ṹ�ľ���Ƿ��ǿɼ̳еġ�
    // ����ṹΪ�ܶຯ����������ʱ�ṩ��ȫ������
    SECURITY_ATTRIBUTES saAttr;
    BOOL fSuccess;

    // Set the bInheritHandle flag so pipe handles are inherited.
    // ���þ��Ϊ�ɼ̳еģ�ʹ�����߳̿���ʹ�ø��߳�
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Get the handle to the current STDOUT.
    // ȡ�õ�ǰӦ�õı�׼������������Windows����̨Ӧ����˵��һ�����������Ļ
    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    // Create a pipe for the child process's STDOUT.
    // ����һ��������������������ܵ���
    if (! CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0))
        ErrorExit("Stdout pipe creation failed\n");

    // Create noninheritable read handle and close the inheritable read handle.
    // ������ܵ��ľ���󶨵���ǰ����
    fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdoutRd,
                               GetCurrentProcess(), &hChildStdoutRdDup , 0,
                               FALSE,
                               DUPLICATE_SAME_ACCESS);
    if( !fSuccess )
        ErrorExit("DuplicateHandle failed");
    CloseHandle(hChildStdoutRd);

    // Create a pipe for the child process's STDIN.
    // ����һ��������������������ܵ���
    if (! CreatePipe(&hChildStdinRd, &hChildStdinWr, &saAttr, 0))
        ErrorExit("Stdin pipe creation failed\n");

    // Duplicate the write handle to the pipe so it is not inherited.
    // ������ܵ��ľ���󶨵���ǰ����
    fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdinWr,
                               GetCurrentProcess(), &hChildStdinWrDup, 0,
                               FALSE,                  // not inherited
                               DUPLICATE_SAME_ACCESS);
    if (! fSuccess)
        ErrorExit("DuplicateHandle failed");

    CloseHandle(hChildStdinWr);

    // Now create the child process.
    // ����DOS�ӽ���
    fSuccess = CreateChildProcess();
    if (! fSuccess)
        ErrorExit("Create process failed");

    // Write to pipe that is the standard input for a child process.
    WriteToPipe();

    // Read from pipe that is the standard output for child process.
    ReadFromPipe();

    return 0;
}

int main(int argc, char *argv[]) {
	createIncludeFile("example.bat");
	CopyFile("template/Bat2Exe_private.rc", "compile/Bat2Exe_private.rc", false);
	CopyFile("template/main.cpp", "compile/main.cpp", false);
	CopyFile("template/Bat2Exe.ico", "compile/Bat2Exe.ico", false);
	DeleteFile("compile/obj/main.o");
	createResourceFile("./compile.ini");
	createBatExe();
	
	return 0;
}

