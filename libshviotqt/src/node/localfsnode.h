#pragma once

#include "shvtreenode.h"

#include <QDir>
#include <QMap>

namespace shv {
namespace iotqt {
namespace node {

class SHVIOTQT_DECL_EXPORT LocalFSNode : public shv::iotqt::node::ShvTreeNode
{
	Q_OBJECT

	using Super = shv::iotqt::node::ShvTreeNode;
public:
	LocalFSNode(const QString &root_path, Super *parent = nullptr);

	chainpack::RpcValue call2(const std::string &method, const chainpack::RpcValue &params, const std::string &shv_path) override;

	StringList childNames2(const std::string &shv_path = std::string()) override;
	chainpack::RpcValue hasChildren2(const std::string &shv_path) override;
	//chainpack::RpcValue lsAttributes(const std::string &node_id, unsigned attributes, const std::string &shv_path) override;

	size_t methodCount2(const std::string &shv_path = std::string()) override;
	const shv::chainpack::MetaMethod* metaMethod2(size_t ix, const std::string &shv_path = std::string()) override;
private:
	QFileInfo ndFileInfo(const std::string &path);
	chainpack::RpcValue ndSize(const std::string &path);
	chainpack::RpcValue ndRead(const std::string &path);
	/*
	chainpack::RpcValue ndLs(const std::string &path, const chainpack::RpcValue &methods_params);
	shv::chainpack::RpcValue ndCall(const std::string &path, const std::string &method, const shv::chainpack::RpcValue &params);
	//chainpack::RpcValue dir(const chainpack::RpcValue &methods_params) override;
	chainpack::RpcValue processRpcRequest(const chainpack::RpcRequest &rq) override;
	*/
private:
	QDir m_rootDir;
};

} // namespace node
} // namespace iotqt
} // namespace shv

