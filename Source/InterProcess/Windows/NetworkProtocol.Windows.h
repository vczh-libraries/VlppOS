/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_NETWORKPROTOCOL
#define VCZH_INTERPROCESS_WINDOWS_NETWORKPROTOCOL

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <http.h>
#define RPC_USE_NATIVE_WCHAR
#include <rpc.h>
#include <winhttp.h>

#include "../TextNetworkProtocol.h"

#endif