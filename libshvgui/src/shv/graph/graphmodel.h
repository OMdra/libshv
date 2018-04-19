#pragma once

#include "../../shvguiglobal.h"

#include <shv/core/exception.h>

#include <QObject>
#include <QPair>
#include <memory>
#include <cmath>
#include <QVector>
#include <QMap>

namespace shv {
namespace gui {

enum class ValueType { TimeStamp, Int, Double, Bool };

struct SHVGUI_DECL_EXPORT ValueChange
{
	using TimeStamp = qint64;
	union ValueX {
		ValueX(TimeStamp value) : timeStamp(value) {}
		ValueX(int value) : intValue(value) {}
		ValueX(double value) : doubleValue(value) {}
		ValueX() : intValue(0) {}

		double toDouble(ValueType stored_type) const{
			switch (stored_type) {
				case ValueType::Int: return intValue;
				case ValueType::Double: return doubleValue;
				case ValueType::TimeStamp: return timeStamp;
				default: Q_ASSERT_X(false,"valueX", "Unsupported conversion"); return 0;
			}
		}

		int toInt(ValueType stored_type) const{
			switch (stored_type) {
				case ValueType::Int: return intValue;
				case ValueType::Double: return std::nearbyint(doubleValue);
				case ValueType::TimeStamp: return timeStamp;
				default: Q_ASSERT_X(false,"valueX", "Unsupported conversion"); return 0;
			}
		}

		TimeStamp timeStamp;
		int intValue;
		double doubleValue;
	} valueX;

	union ValueY {
		ValueY(bool value) : boolValue(value) {}
		ValueY(int value) : intValue(value) {}
		ValueY(double value) : doubleValue(value) {}
		ValueY() : intValue(0) {}

		double toDouble(ValueType stored_type) const{
			switch (stored_type) {
				case ValueType::Int: return intValue;
				case ValueType::Double: return doubleValue;
				case ValueType::Bool: return boolValue;
				default: Q_ASSERT_X(false,"valueY", "Unsupported conversion"); return 0;
			}
		}

		int toInt(ValueType stored_type) const{
			switch (stored_type) {
				case ValueType::Int: return intValue;
				case ValueType::Double: return std::nearbyint(doubleValue);
				case ValueType::Bool: return boolValue;
				default: Q_ASSERT_X(false,"valueY", "Unsupported conversion"); return 0;
			}
		}

		bool toBool(ValueType stored_type) const{
			switch (stored_type) {
				case ValueType::Int: return (intValue > 0);
				case ValueType::Double: return (doubleValue > 0.0);
				case ValueType::Bool: return boolValue;
				default: Q_ASSERT_X(false,"valueY", "Unsupported conversion"); return false;
			}
		}

		bool boolValue;
		int intValue;
		double doubleValue;
	} valueY;

	ValueChange(ValueX value_x, ValueY value_y) : valueX(value_x), valueY(value_y) {}
	ValueChange(TimeStamp value_x, ValueY value_y) : ValueChange(ValueX(value_x), value_y) {}
	ValueChange(TimeStamp value_x, bool value_y) : ValueChange(value_x, ValueY(value_y)) {}
	ValueChange(TimeStamp value_x, int value_y) : ValueChange(value_x, ValueY(value_y)) {}
	ValueChange(TimeStamp value_x, double value_y) : ValueChange(value_x, ValueY(value_y)) {}
	ValueChange() {}
};

struct SHVGUI_DECL_EXPORT ValueXInterval
{
	inline ValueXInterval(ValueChange min, ValueChange max, ValueType type) : min(min.valueX), max(max.valueX), type(type) {}
	inline ValueXInterval(ValueChange::ValueX min, ValueChange::ValueX max, ValueType type) : min(min), max(max), type(type) {}
	inline ValueXInterval(int min, int max) : min(min), max(max), type(ValueType::Int) {}
	inline ValueXInterval(ValueChange::TimeStamp min, ValueChange::TimeStamp max) : min(min), max(max), type(ValueType::TimeStamp) {}
	inline ValueXInterval(double min, double max) : min(min), max(max), type(ValueType::Double) {}

	ValueChange::ValueX length() const;
	ValueChange::ValueX min;
	ValueChange::ValueX max;
	ValueType type;
};

SHVGUI_DECL_EXPORT bool compareValueX(const ValueChange &value1, const ValueChange &value2, ValueType type);
SHVGUI_DECL_EXPORT bool compareValueX(const ValueChange::ValueX &value1, const ValueChange::ValueX &value2, ValueType type);

SHVGUI_DECL_EXPORT bool compareValueY(const ValueChange &value1, const ValueChange &value2, ValueType type);
SHVGUI_DECL_EXPORT bool compareValueY(const ValueChange::ValueY &value1, const ValueChange::ValueY &value2, ValueType type);

class SHVGUI_DECL_EXPORT SerieData : public std::vector<ValueChange>
{
	using Super = std::vector<ValueChange>;

public:
	SerieData() : m_xType(ValueType::Int), m_yType(ValueType::Int)	{}
	SerieData(ValueType x_type, ValueType y_type) : m_xType(x_type), m_yType(y_type) {}

	const_iterator upper_bound(shv::gui::ValueChange::ValueX value_x) const;
	const_iterator lower_bound(shv::gui::ValueChange::ValueX value_x) const;

	const_iterator lessOrEqualIterator(ValueChange::ValueX value_x) const;
	QPair<const_iterator, const_iterator> intersection(const ValueChange::ValueX &start, const ValueChange::ValueX &end, bool &valid) const;

	ValueType xType() const	{ return m_xType; }
	ValueType yType() const	{ return m_yType; }

	ValueXInterval range() const;
	bool addValueChange(const ValueChange &value);
	iterator insertValueChange(const_iterator position, const ValueChange &value);

	void extendRange(int &min, int &max) const;
	void extendRange(double &min, double &max) const;
	void extendRange(ValueChange::TimeStamp &min, ValueChange::TimeStamp &max) const;

private:
	ValueType m_xType;
	ValueType m_yType;
};

struct SHVGUI_DECL_EXPORT GraphModelData : public QObject
{
	Q_OBJECT

public:
	GraphModelData(QObject *parent);

	Q_SIGNAL void dataChanged(QVector<int> series);

	virtual const SerieData &serieData(int serie_index) const;

	void addValueChange(int serie_index, const shv::gui::ValueChange &value);
	void addValueChanges(int serie_index, const std::vector<shv::gui::ValueChange> &values); // adds array of valyes to one serie
	void addValueChanges(const std::vector<shv::gui::ValueChange> &values); //adds array of values where every value belongs to one serie
	void addValueChanges(const QMap<int, shv::gui::ValueChange> &values);
	SerieData::iterator insertValueChange(int serie_index, std::vector<ValueChange>::const_iterator position, const shv::gui::ValueChange &value);

	int addSerie(SerieData values);
	void clearSerie(int serie_index);
	void clearSeries();

	void dataChangeBegin();
	void dataChangeEnd();

	int serieCount() const;

	SerieData::iterator removeValueChanges(int serie_index, SerieData::const_iterator from, SerieData::const_iterator to);

	ValueXInterval range() const;

protected:
	void checkIndex(int serie_index) const;
	template<typename T> ValueXInterval computeRange() const;

	std::vector<SerieData> m_valueChanges;
	QVector<int> m_changedSeries;
	bool m_dataChangeEnabled;
};

struct SHVGUI_DECL_EXPORT GraphModel : public QObject
{
	Q_OBJECT

public:
	GraphModel(QObject *parent);

	Q_SIGNAL void dataChanged(QVector<int> series);

	virtual const SerieData &serieData(int serie_index) const;

	void setData(GraphModelData *model_data);
	GraphModelData *data(bool throw_exc = shv::core::Exception::Throw) const;

protected:
	GraphModelData *m_data = nullptr;
};

}
}
