#pragma once

#include "../shviotqtglobal.h"

#include <shv/core/utils.h>
#include <shv/chainpack/rpcvalue.h>

#include <QTcpServer>

namespace shv { namespace chainpack { class RpcMessage; }}

namespace shv {
namespace iotqt {
namespace rpc {

class ServerConnection;

class SHVIOTQT_DECL_EXPORT TcpServer : public QTcpServer
{
	Q_OBJECT
	//Q_PROPERTY(int numConnections READ numConnections)

	using Super = QTcpServer;

	//SHV_FIELD_IMPL(int, p, P, ort)
public:
	explicit TcpServer(QObject *parent = 0);
	~TcpServer() override;

	bool start(int port);
	std::vector<unsigned> connectionIds() const;
	ServerConnection* connectionById(unsigned connection_id);
protected:
	virtual ServerConnection* createServerConnection(QTcpSocket *socket, QObject *parent) = 0;
	void onNewConnection();
	void onConnectionDeleted(int connection_id);
protected:
	std::map<unsigned, ServerConnection*> m_connections;
};

}}}

