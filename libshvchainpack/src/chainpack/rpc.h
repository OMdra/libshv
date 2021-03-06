#pragma once

#include "../shvchainpackglobal.h"

#include <string>

namespace shv {
namespace chainpack {

class SHVCHAINPACK_DECL_EXPORT Rpc
{
public:
	enum class ProtocolType {Invalid = 0, ChainPack, Cpon, JsonRpc};
	static const char* ProtocolTypeToString(ProtocolType pv);

	static const char* OPT_IDLE_WD_TIMEOUT;

	static const char* KEY_CLIENT_ID;
	static const char* KEY_MOUT_POINT;
	static const char* KEY_DEVICE_ID;
	static const char* KEY_TUNNEL_HANDLE;

	static const char* TYPE_CLIENT;
	static const char* TYPE_DEVICE;
	static const char* TYPE_TUNNEL;

	static const char* METH_HELLO;
	static const char* METH_LOGIN;
	static const char* METH_GET;
	static const char* METH_SET;
	static const char* METH_DIR;
	static const char* METH_LS;
	static const char* METH_PING;
	static const char* METH_ECHO;
	static const char* METH_APP_NAME;
	static const char* METH_DEVICE_ID;
	static const char* METH_CONNECTION_TYPE;
	static const char* METH_SUBSCRIBE;

	static const char* PAR_PATH;
	static const char* PAR_METHOD;

	static const char* NTF_VAL_CHANGED;
	static const char* NTF_CONNECTED;
	static const char* NTF_DISCONNECTED;

	static const char* DIR_BROKER;
	static const char* DIR_BROKER_APP;

	static const char* JSONRPC_ID;
	static const char* JSONRPC_METHOD;
	static const char* JSONRPC_PARAMS;
	static const char* JSONRPC_RESULT;
	static const char* JSONRPC_ERROR;
	static const char* JSONRPC_ERROR_CODE;
	static const char* JSONRPC_ERROR_MESSAGE;
	static const char* JSONRPC_SHV_PATH;
	static const char* JSONRPC_CALLER_ID;

	static std::string joinShvPath(const std::string &p1, const std::string &p2);
};

} // namespace chainpack
} // namespace shv
