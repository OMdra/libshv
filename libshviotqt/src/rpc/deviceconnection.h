#pragma once

#include "clientappclioptions.h"
#include "clientconnection.h"

namespace shv {
namespace iotqt {
namespace rpc {

class SHVIOTQT_DECL_EXPORT DeviceAppCliOptions : public ClientAppCliOptions
{
	Q_OBJECT
private:
	using Super = ClientAppCliOptions;
public:
	DeviceAppCliOptions(QObject *parent = NULL);

	CLIOPTION_GETTER_SETTER2(QString, "device.mountPoint", m, setM, ountPoint)
	CLIOPTION_GETTER_SETTER2(QString, "device.id", d, setD, eviceId)
};

class SHVIOTQT_DECL_EXPORT DeviceConnection : public ClientConnection
{
	Q_OBJECT
	using Super = ClientConnection;
public:
	DeviceConnection(QObject *parent = 0);

	void setCliOptions(const DeviceAppCliOptions *cli_opts);
protected:
	//shv::chainpack::RpcValue createLoginParams(const shv::chainpack::RpcValue &server_hello) override;
};

} // namespace rpc
} // namespace iotqt
} // namespace shv
