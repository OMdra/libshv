#include "exception.h"

namespace shv {
namespace coreqt {

Exception::Exception(const QString &msg, const QString &where)
	: shv::core::Exception(msg.toStdString(), where.toStdString())
{
}

}
}
