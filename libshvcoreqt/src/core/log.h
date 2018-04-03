#pragma once

#include "../shvcoreqtglobal.h"
#include <shv/core/log.h>

#include <QString>
#include <QByteArray>
#include <QDateTime>

inline NecroLog &operator<<(NecroLog log, const QString &s) { return log.operator <<(s.toUtf8().constData()); }
NecroLog &operator<<(NecroLog log, const QByteArray &s);
inline NecroLog &operator<<(NecroLog log, const QDate &d) { return log << d.toString(); }
inline NecroLog &operator<<(NecroLog log, const QDateTime &d) { return log << d.toString(); }
inline NecroLog &operator<<(NecroLog log, const QTime &d) {  return log << d.toString(); }
