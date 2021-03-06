#include "clioptions.h"
#include "../core/log.h"

#include <shv/core/log.h>
#include <shv/core/utils.h>
#include <shv/core/assert.h>
#include <shv/core/exception.h>

#include <QStringBuilder>
#include <QStringList>
#include <QDir>
#include <QJsonParseError>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <qt_windows.h> // needed by CLIOptions::applicationDirAndName()
#endif

#include <limits>

namespace shv {
namespace coreqt {
namespace utils {

const CLIOptions::Option & CLIOptions::Option::sharedNull()
{
	static Option n = Option(NullConstructor());
	return n;
}

CLIOptions::Option::Option(CLIOptions::Option::NullConstructor)
{
	d = new Data();
}

CLIOptions::Option::Option()
{
	*this = sharedNull();
}

CLIOptions::Option::Option(QVariant::Type type)
{
	d = new Data(type);
}

CLIOptions::Option& CLIOptions::Option::setValueString(const QString& val_str)
{
	QVariant::Type t = type();
	switch(t) {
	case(QVariant::Invalid):
		shvWarning() << "Setting value:" << val_str << "to an invalid type option.";
		break;
	case(QVariant::Bool):
	{
		if(val_str.isEmpty()) {
			setValue(true);
		}
		else {
			bool ok;
			int n = val_str.toInt(&ok);
			if(ok) {
				setValue(n != 0);
			}
			else {
				bool is_true = true;
				for(const char * const s : {"n", "no", "false"}) {
					if(val_str.compare(QLatin1String(s), Qt::CaseInsensitive)) {
						is_true = false;
						break;
					}
				}
				setValue(is_true);
			}
		}
		break;
	}
	case(QVariant::Int):
	{
		bool ok;
		setValue(val_str.toInt(&ok));
		if(!ok)
			shvWarning() << "Value:" << val_str << "cannot be converted to Int.";
		break;
	}
	case(QVariant::Double):
	{
		bool ok;
		setValue(val_str.toDouble(&ok));
		if(!ok)
			shvWarning() << "Value:" << val_str << "cannot be converted to Double.";
		break;
	}
	default:
		setValue(val_str);
		//shvWarning() << val_str << "->" << names() << "->" << value();
	}
	return *this;
}

CLIOptions::CLIOptions(QObject *parent)
	: QObject(parent), m_parsedArgIndex(), m_isAppBreak()
{
	addOption("abortOnException").setType(QVariant::Bool).setNames("--abort-on-exception").setComment(tr("Abort application on exception"));
	addOption("help").setType(QVariant::Bool).setNames("-h", "--help").setComment(tr("Print help"));
	addOption("config").setType(QVariant::String).setNames("--config").setComment(tr("Config name, it is loaded from {app-name}[.conf] if file exists in {config-path}"));
	addOption("configDir").setType(QVariant::String).setNames("--config-dir").setComment("Directory where server config fiels are searched, default value: {app-dir-path}.");
}

CLIOptions::~CLIOptions()
{
}

CLIOptions::Option& CLIOptions::addOption(const QString key, const CLIOptions::Option& opt)
{
	m_options[key] = opt;
	return m_options[key];
}

CLIOptions::Option CLIOptions::option(const QString& name, bool throw_exc) const
{
	Option ret = m_options.value(name);
	if(ret.isNull() && throw_exc) {
		std::string msg = "Key '" + name.toStdString() + "' not found.";
		shvWarning() << msg;
		SHV_EXCEPTION(msg);
	}
	return ret;
}

CLIOptions::Option& CLIOptions::optionRef(const QString& name)
{
	if(!m_options.contains(name)) {
		std::string msg = "Key '" + name.toStdString() + "' not found.";
		shvWarning() << msg;
		SHV_EXCEPTION(msg);
	}
	return m_options[name];
}

QVariantMap CLIOptions::values() const
{
	QVariantMap ret;
	QMapIterator<QString, Option> it(m_options);
	while(it.hasNext()) {
		it.next();
		ret[it.key()] = value(it.key());
	}
	return ret;
}

QVariant CLIOptions::value(const QString &name) const
{
	QVariant ret = value_helper(name, shv::core::Exception::Throw);
	return ret;
}

QVariant CLIOptions::value(const QString& name, const QVariant default_value) const
{
	QVariant ret = value_helper(name, !shv::core::Exception::Throw);
	if(!ret.isValid())
		ret = default_value;
	return ret;
}

bool CLIOptions::isValueSet(const QString &name) const
{
	return option(name, !shv::core::Exception::Throw).isSet();
}

QVariant CLIOptions::value_helper(const QString &name, bool throw_exception) const
{
	Option opt = option(name, throw_exception);
	if(opt.isNull())
		return QVariant();
	QVariant ret = opt.value();
	if(!ret.isValid())
		ret = opt.defaultValue();
	if(!ret.isValid())
		ret = QVariant(opt.type());
	return ret;
}

bool CLIOptions::optionExists(const QString &name) const
{
	return !option(name, !shv::core::Exception::Throw).isNull();
}

bool CLIOptions::setValue(const QString& name, const QVariant val, bool throw_exc)
{
	Option o = option(name, false);
	if(optionExists(name)) {
		Option &orf = optionRef(name);
		orf.setValue(val);
		return true;
	}
	else {
		QString msg = "setValue():"%val.toString()%" Key '"%name%"' not found.";
		shvWarning() << msg.toStdString();
		if(throw_exc) {
			SHV_EXCEPTION(msg.toStdString());
		}
		return false;
	}
}

QString CLIOptions::takeArg()
{
	QString ret = m_arguments.value(m_parsedArgIndex++);
	return ret;
}

QString CLIOptions::peekArg() const
{
	QString ret = m_arguments.value(m_parsedArgIndex);
	return ret;
}

void CLIOptions::parse(int argc, char* argv[])
{
	QStringList args;
	for(int i=0; i<argc; i++)
		args << QString::fromUtf8(argv[i]);
	parse(args);
}

void CLIOptions::parse(const QStringList& cmd_line_args)
{
	//shvLogFuncFrame() << cmd_line_args;
	m_isAppBreak = false;
	m_parsedArgIndex = 0;
	m_arguments = cmd_line_args.mid(1);
	m_unusedArguments = QStringList();
	m_parseErrors = QStringList();
	m_allArgs = cmd_line_args;

	while(true) {
		QString arg = takeArg();
		if(arg.isEmpty())
			break;
		if(arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
			setHelp(true);
			//printHelp();
			m_isAppBreak = true;
			return;
		}
		else {
			bool found = false;
			QMutableMapIterator<QString, Option> it(m_options);
			while(it.hasNext()) {
				it.next();
				Option &opt = it.value();
				QStringList names = opt.names();
				if(names.contains(arg)) {
					found = true;
					arg = peekArg();
					if(arg.startsWith('-') || arg.isEmpty()) {
						// switch has no value entered
						arg = QString();
					}
					else {
						arg = takeArg();
					}
					opt.setValueString(arg);
					break;
				}
			}
			if(!found) {
				if(arg.startsWith("-"))
					m_unusedArguments << arg;
			}
		}
	}
	{
		QMapIterator<QString, Option> it(m_options);
		while(it.hasNext()) {
			it.next();
			Option opt = it.value();
			//LOGDEB() << "option:" << it.key() << "is mandatory:" << opt.isMandatory() << "is valid:" << opt.value().isValid();
			if(opt.isMandatory() && !opt.value().isValid()) {
				addParseError(QString("Mandatory option '%1' not set.").arg(opt.names().value(0)));
			}
		}
	}
	shv::core::Exception::setAbortOnException(isAbortOnException());
}

QPair<QString, QString> CLIOptions::applicationDirAndName() const
{
	static QString app_dir;
	static QString app_name;
	if(app_name.isEmpty()) {
		if(m_allArgs.size()) {
	#ifdef Q_OS_WIN
			QString app_file_path;
			wchar_t buffer[MAX_PATH + 2];
			DWORD v = GetModuleFileName(0, buffer, MAX_PATH + 1);
			buffer[MAX_PATH + 1] = 0;
			if (v <= MAX_PATH)
				app_file_path = QString::fromWCharArray(buffer);
			QChar sep = '\\';
	#else
			QString app_file_path = m_allArgs[0];
			QChar sep = '/';
	#endif
			app_dir = app_file_path.section(sep, 0, -2);
			app_name = app_file_path.section(sep, -1);
			//shvInfo() << "app dir:" << app_dir << "name:" << app_name;
	#ifdef Q_OS_WIN
			if(app_name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
				app_name = app_name.mid(0, app_name.length() - 4);
	#else
			if(app_name.endsWith(QLatin1String(".so"), Qt::CaseInsensitive)) {
				// for example zygotized Android application
				app_name = app_name.mid(0, app_name.length() - 3);
			}
	#endif
		}
	}
	return QPair<QString, QString>(app_dir, app_name);
}

QString CLIOptions::applicationDir() const
{
	return QDir::fromNativeSeparators(applicationDirAndName().first);
}

QString CLIOptions::applicationName() const
{
	return applicationDirAndName().second;
}

void CLIOptions::printHelp(QTextStream& os) const
{
	using namespace std;
	os << applicationName() << " [OPTIONS]" << endl << endl;
	os << "OPTIONS:" << endl << endl;
	QMapIterator<QString, Option> it(m_options);
	while(it.hasNext()) {
		it.next();
		Option opt = it.value();
		os << opt.names().join(", ");
		if(opt.type() != QVariant::Bool) {
			if(opt.type() == QVariant::Int || opt.type() == QVariant::Double) os << " " << "number";
			else os << " " << "'string'";
		}
		//os << ':';
		QVariant def_val = opt.defaultValue();
		if(def_val.isValid()) os << " [default(" << def_val.toString() << ")]";
		if(opt.isMandatory()) os << " [MANDATORY]";
		os << endl;
		QString oc = opt.comment();
		if(!oc.isEmpty()) os << "\t" << opt.comment() << endl;
	}
	//os << shv::core::ShvLog::logCLIHelp() << endl;
	os << NecroLog::cliHelp() << endl;
}

void CLIOptions::printHelp() const
{
	QTextStream ts(stdout);
	printHelp(ts);
}

void CLIOptions::dump(QTextStream &os) const
{
	QMapIterator<QString, Option> it(m_options);
	while(it.hasNext()) {
		it.next();
		Option opt = it.value();
		os << it.key() << '(' << opt.names().join(", ") << ')' << ": " << opt.value().toString() << endl;
	}
}

void CLIOptions::dump() const
{
	QTextStream ts(stdout);
	ts << "=============== options values dump ==============" << endl;
	dump(ts);
	ts << "-------------------------------------------------" << endl;
}

void CLIOptions::addParseError(const QString& err)
{
	m_parseErrors << err;
}

ConfigCLIOptions::ConfigCLIOptions(QObject *parent)
	: Super(parent)
{
	addOption("config").setType(QVariant::String).setNames("--config").setComment("Application config name, it is loaded from {config}[.conf] if file exists in {config-path}, deault value is {app-name}.conf");
	addOption("configDir").setType(QVariant::String).setNames("--config-dir").setComment("Directory where application config fiels are searched, default value: {app-dir-path}.");
}

void ConfigCLIOptions::parse(const QStringList &cmd_line_args)
{
	Super::parse(cmd_line_args);
}

bool ConfigCLIOptions::loadConfigFile()
{
	QString config_file = configFile();
	QFile f(config_file);
	shvInfo() << "Checking presence of config file:" << f.fileName().toStdString();
	if(f.open(QFile::ReadOnly)) {
		shvInfo() << "Reading config file:" << f.fileName();
		QByteArray ba = f.readAll();
		std::string str = shv::core::Utils::removeJsonComments(std::string(ba.constData()));
		//shvDebug() << str;
		QJsonParseError err;
		auto jsd = QJsonDocument::fromJson(str.c_str(), &err);
		if(err.error == QJsonParseError::NoError) {
			mergeConfig(jsd.toVariant().toMap());
		}
		else {
			shvError() << "Error parsing config file:" << f.fileName() << "on offset:" << err.offset << err.errorString();
			return false;
		}
	}
	else {
		shvInfo() << "Config file:" << f.fileName() << "not found.";
	}
	return true;
}

QString ConfigCLIOptions::configFile()
{
	auto config = QStringLiteral("config");
	auto conf_ext = QStringLiteral(".conf");
	QString config_file;
	if(isValueSet(config)) {
		config_file = value(config).toString();
		if(config_file.isEmpty()) {
			/// explicitly set empty config means DO NOT load config from any file
			return QString();
		}
	}
	else {
		config_file = applicationName() + conf_ext;
	}
	if(!QDir::isAbsolutePath(config_file)) {
		QString config_dir = configDir();
		if(config_dir.isEmpty())
			config_dir = applicationDir();
		config_file = config_dir + '/' + config_file;
	}
	if(!config_file.endsWith(conf_ext)) {
		if(QFile::exists(config_file + conf_ext))
			config_file += conf_ext;
	}
	return config_file;
}

void ConfigCLIOptions::mergeConfig_helper(const QString &key_prefix, const QVariantMap &config_map)
{
	//shvLogFuncFrame() << key_prefix;
	QMapIterator<QString, QVariant> it(config_map);
	while(it.hasNext()) {
		it.next();
		QString key = it.key().trimmed();
		SHV_ASSERT(!key.isEmpty(), "Empty key!", continue);
		if(!key_prefix.isEmpty()) {
			key = key_prefix + '.' + key;
		}
		QVariant v = it.value();
		if(v.type() == QVariant::Map) {
			QVariantMap m = v.toMap();
			mergeConfig_helper(key, m);
		}
		else {
			try {
				bool opt_exists = !option(key, !shv::core::Exception::Throw).isNull();
				if(opt_exists) {
					Option &opt = optionRef(key);
					if(!opt.isSet()) {
						//shvInfo() << key << "-->" << v;
						opt.setValue(v);
					}
				}
				else {
					shvWarning() << "Cannot merge nonexisting option key:" << key;
				}
			}
			catch(const shv::core::Exception &e) {
				shvWarning() << "Merge option" << key << "error:" << e.message();
			}
		}
	}
}

}}}
