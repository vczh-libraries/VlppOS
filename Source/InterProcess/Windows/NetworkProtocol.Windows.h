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
#define _WINSOCKAPI_
#include <windows.h>
#include <http.h>
#define RPC_USE_NATIVE_WCHAR
#include <rpc.h>
#include <winhttp.h>

#include "../NetworkProtocol.h"
#include "../../Threading.h"
#include "../../Stream/MemoryStream.h"

namespace vl::inter_process
{
	/*
	* GET: /Request
	* To connect and initialize the server.
	* Returns available URLs.
	*
	* It can only be called once, all subsequence calls will be rejected.
	*/
	constexpr const wchar_t* HttpServerUrl_Connect = L"/VlppInterProcess/Connect";

	/*
	* POST: /Request/GUID
	* Client should always maintain a living request on the server.
	*
	* Returns only when a request is issued.
	* It will be pending or timeout if no request is issued.
	* If a request is issued but no living request available, it waits.
	*/
	constexpr const wchar_t* HttpServerUrl_Request = L"/VlppInterProcess/Request";

	/*
	* POST: /Response/GUID
	* To send responses or events to the server.
	* Returns nothing.
	*/
	constexpr const wchar_t* HttpServerUrl_Response = L"/VlppInterProcess/Response";
}

#endif
