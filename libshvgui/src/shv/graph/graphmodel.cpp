#include "graphmodel.h"

#include <shv/core/exception.h>

//#include <shv/core/log.h>

namespace shv {
namespace gui {

SerieData::const_iterator SerieData::lessOrEqualIterator(ValueChange::ValueX value_x) const
{
	const_iterator it = lower_bound(value_x);

	if (it == cend()) {
		if (!empty()) {
			it--;
		}
	}
	else {
		if (!compareValueX(it->valueX, value_x, m_xType)) {
			if (it == cbegin()) {
				it = cend();
			}
			else {
				it--;
			}
		}
	}
	return it;
}

QPair<SerieData::const_iterator, SerieData::const_iterator> SerieData::intersection(const ValueChange::ValueX &start, const ValueChange::ValueX &end, bool &valid) const
{
	QPair<const_iterator, const_iterator> result;
	result.first = lessOrEqualIterator(start);
	result.second = lessOrEqualIterator(end);

	if (result.first == cend() && !empty()){
		result.first = cbegin();
	}

	valid = (result.second != cend());
	return result;
}

ValueXInterval SerieData::range() const
{
	if (size()) {
		return ValueXInterval(at(0).valueX, back().valueX, xType());
	}
	else {
		switch (xType()) {
		case ValueType::Double:
			return ValueXInterval(0.0, 0.0);
		case ValueType::Int:
			return ValueXInterval(0, 0);
		case ValueType::TimeStamp:
			return ValueXInterval(0LL, 0LL);
		default:
			SHV_EXCEPTION("Invalid type on X axis");
		}
	}
}

bool SerieData::addValueChange(const ValueChange &value)
{
	int sz = size();
	if (sz == 0) {
		push_back(value);
		return true;
	}
	else {
		const ValueChange &last = at(sz - 1);
		if (!compareValueX(last, value, xType()) && !compareValueY(last, value, yType())) {
			push_back(value);
			return true;
		}
	}
	return false;
}

SerieData::iterator SerieData::insertValueChange(const_iterator position, const ValueChange &value)
{
	return insert(position, value);
}

void SerieData::extendRange(int &min, int &max) const
{
	if (size()) {
		if (at(0).valueX.intValue < min) {
			min = at(0).valueX.intValue;
		}
		if (back().valueX.intValue > max) {
			max = back().valueX.intValue;
		}
	}
}

void SerieData::extendRange(double &min, double &max) const
{
	if (size()) {
		if (at(0).valueX.doubleValue < min) {
			min = at(0).valueX.doubleValue;
		}
		if (back().valueX.doubleValue > max) {
			max = back().valueX.doubleValue;
		}
	}
}

void SerieData::extendRange(ValueChange::TimeStamp &min, ValueChange::TimeStamp &max) const
{
	if (size()) {
		if (at(0).valueX.timeStamp < min) {
			min = at(0).valueX.timeStamp;
		}
		if (back().valueX.timeStamp > max) {
			max = back().valueX.timeStamp;
		}
	}
}

void GraphModelData::checkIndex(int serie_index) const
{
	if (serie_index >= (int)m_valueChanges.size()) {
		SHV_EXCEPTION("bad serie index");
	}
}

bool compareValueX(const ValueChange &value1, const ValueChange &value2, ValueType type)
{
	return compareValueX(value1.valueX, value2.valueX, type);
}

bool compareValueX(const ValueChange::ValueX &value1, const ValueChange::ValueX &value2, ValueType type)
{
	switch (type) {
	case ValueType::TimeStamp:
		return value1.timeStamp == value2.timeStamp;
	case ValueType::Double:
		return value1.doubleValue == value2.doubleValue;
	case ValueType::Int:
		return value1.intValue == value2.intValue;
	default:
		SHV_EXCEPTION("Invalid type on valueX");
	}
}

bool compareValueY(const ValueChange &value1, const ValueChange &value2, ValueType type)
{
	return compareValueY(value1.valueY, value2.valueY, type);
}

bool compareValueY(const ValueChange::ValueY &value1, const ValueChange::ValueY &value2, ValueType type)
{
	switch (type) {
	case ValueType::Double:
		return value1.doubleValue == value2.doubleValue;
	case ValueType::Int:
		return value1.intValue == value2.intValue;
	case ValueType::Bool:
		return value1.boolValue == value2.boolValue;
	default:
		SHV_EXCEPTION("Invalid type on valueY");
	}
}

GraphModelData::GraphModelData(QObject *parent)
	: QObject(parent)
	, m_dataChangeEnabled(true)
{
}

const SerieData &GraphModelData::serieData(int serie_index) const
{
	checkIndex(serie_index);
	return m_valueChanges[serie_index];
}

void GraphModelData::addValueChange(int serie_index, const shv::gui::ValueChange &value)
{
	checkIndex(serie_index);
	bool added = m_valueChanges[serie_index].addValueChange(value);
	if (added) {
		if (m_dataChangeEnabled) {
			Q_EMIT dataChanged(QVector<int>{ serie_index });
		}
		else if (!m_changedSeries.contains(serie_index)){
			m_changedSeries << serie_index;
		}
	}
}

void GraphModelData::addValueChanges(int serie_index, const std::vector<shv::gui::ValueChange> &values)
{
	checkIndex(serie_index);
	bool added = false;
	for (const shv::gui::ValueChange &value : values) {
		added = m_valueChanges[serie_index].addValueChange(value) || added;
	}
	if (added) {
		if (m_dataChangeEnabled) {
			Q_EMIT dataChanged(QVector<int>{ serie_index });
		}
		else if (!m_changedSeries.contains(serie_index)){
			m_changedSeries << serie_index;
		}
	}
}

void GraphModelData::addValueChanges(const std::vector<ValueChange> &values)
{
	if (values.size() >= m_valueChanges.size()) {
		SHV_EXCEPTION("addValueChanges: number of values in array exceeds the number of series");
	}
	QVector<int> added;
	for (uint i = 0; i < m_valueChanges.size(); ++i) {
		if (m_valueChanges[i].addValueChange(values[i])) {
			added << i;
		}
	}
	if (!added.isEmpty()) {
		if (m_dataChangeEnabled) {
			Q_EMIT dataChanged(added);
		}
		else {
			for (int serie_index : added) {
				if (!m_changedSeries.contains(serie_index)) {
					m_changedSeries << serie_index;
				}
			}
		}
	}
}

void GraphModelData::addValueChanges(const QMap<int, shv::gui::ValueChange> &values)
{
	QVector<int> added;

	for (QMap<int, shv::gui::ValueChange>::const_iterator value = values.constBegin(); value != values.constEnd(); ++value) {
		checkIndex(value.key());

		if (m_valueChanges[value.key()].addValueChange(value.value())) {
			added << value.key();
		}
	}

	if (!added.isEmpty()) {
		if (m_dataChangeEnabled) {
			Q_EMIT dataChanged(added);
		}
		else {
			for (int serie_index : added) {
				if (!m_changedSeries.contains(serie_index)) {
					m_changedSeries << serie_index;
				}
			}
		}
	}
}

SerieData::iterator GraphModelData::insertValueChange(int serie_index, std::vector<ValueChange>::const_iterator position, const shv::gui::ValueChange &value)
{
	return m_valueChanges[serie_index].insertValueChange(position, value);
}

int GraphModelData::addSerie(SerieData values)
{
	int index = m_valueChanges.size();
	m_valueChanges.push_back(values);
	return index;
}

void GraphModelData::clearSerie(int serie_index)
{
	checkIndex(serie_index);
	SerieData &serie = m_valueChanges[serie_index];
	if (serie.size()) {
		serie.clear();
		serie.shrink_to_fit();
		if (m_dataChangeEnabled) {
			Q_EMIT dataChanged(QVector<int>{ serie_index });
		}
		else if (!m_changedSeries.contains(serie_index)){
			m_changedSeries << serie_index;
		}
	}
}

void GraphModelData::clearSeries()
{
	QVector<int> changed;
	for (uint serie_index = 0; serie_index < m_valueChanges.size(); ++serie_index) {
		SerieData &serie = m_valueChanges[serie_index];
		if (serie.size()) {
			changed << serie_index;
			serie.clear();
		}
		serie.shrink_to_fit();
	}
	if (changed.size()) {
		if (m_dataChangeEnabled) {
			Q_EMIT dataChanged(changed);
		}
		else {
			for (int serie_index : changed) {
				if (!m_changedSeries.contains(serie_index)) {
					m_changedSeries << serie_index;
				}
			}
		}
	}
}

void GraphModelData::dataChangeBegin()
{
	m_dataChangeEnabled = false;
}

void GraphModelData::dataChangeEnd()
{
	if (!m_dataChangeEnabled && m_changedSeries.size()) {
		Q_EMIT dataChanged(m_changedSeries);
	}
	m_changedSeries.clear();
	m_dataChangeEnabled = true;
}

int GraphModelData::serieCount() const
{
	return  m_valueChanges.size();
}

SerieData::iterator GraphModelData::removeValueChanges(int serie_index, SerieData::const_iterator from, SerieData::const_iterator to)
{
	checkIndex(serie_index);
	SerieData &serie = m_valueChanges[serie_index];
	auto old_size = serie.size();
	auto it = serie.erase(from, to);
	if (serie.size() != old_size) {
		if (m_dataChangeEnabled) {
			Q_EMIT dataChanged(QVector<int>{ serie_index });
		}
		else if (!m_changedSeries.contains(serie_index)) {
			m_changedSeries << serie_index;
		}
	}
	return it;
}

ValueXInterval GraphModelData::range() const
{
	if (!m_valueChanges.size()) {
		SHV_EXCEPTION("Cannot state range where no series are present");
	}

	ValueType type = m_valueChanges[0].xType();
	switch (type) {
	case ValueType::Double:
		return computeRange<double>();
	case ValueType::Int:
		return computeRange<int>();
	case ValueType::TimeStamp:
		return computeRange<ValueChange::TimeStamp>();
	default:
		SHV_EXCEPTION("Invalid X axis type");
	}
}

template<typename T>
ValueXInterval GraphModelData::computeRange() const
{
	T min = std::numeric_limits<T>::max();
	T max = std::numeric_limits<T>::min();

	ValueType type = m_valueChanges[0].xType();

	for (const SerieData &serie : m_valueChanges) {
		if (serie.xType() != type) {
			SHV_EXCEPTION("Cannot determine range when different types on X axis");
		}
		serie.extendRange(min, max);
	}
	if (min == std::numeric_limits<T>::max() && max == std::numeric_limits<T>::min()) {
		min = max = 0;
	}
	return ValueXInterval(min, max);
}

GraphModel::GraphModel(QObject *parent)
	: QObject(parent)
{
}

void GraphModel::setData(GraphModelData *model_data)
{
	if (m_data) {
		disconnect(m_data, 0, this, 0);
	}
	m_data = model_data;
	if (m_data) {
		connect(m_data, &GraphModelData::dataChanged, this, &GraphModel::dataChanged);
		connect(m_data, &GraphModelData::destroyed, this, &GraphModel::deleteLater);
	}
}

GraphModelData *GraphModel::data(bool throw_exc) const
{
	if(!m_data && throw_exc)
		SHV_EXCEPTION("No data set!");
	return m_data;
}

const SerieData &GraphModel::serieData(int serie_index) const
{
	static SerieData empty_data;
	GraphModelData *d = data(!shv::core::Exception::Throw);
	if(!d)
		return empty_data;
	return d->serieData(serie_index);
}

SerieData::const_iterator SerieData::upper_bound(ValueChange::ValueX value_x) const
{
	const_iterator it = end();
	if (m_xType == ValueType::TimeStamp) {
		it = std::upper_bound(cbegin(), cend(), value_x.timeStamp, [](const ValueChange::TimeStamp &x, const ValueChange &val) {
			return val.valueX.timeStamp > x;
		});
	}
	else if (m_xType == ValueType::Int) {
		it = std::upper_bound(cbegin(), cend(), value_x.intValue, [](int x, const ValueChange &val) {
			return val.valueX.intValue > x;
		});
	}
	else if (m_xType == ValueType::Double) {
		it = std::upper_bound(cbegin(), cend(), value_x.doubleValue, [](double x, const ValueChange &val) {
			return val.valueX.doubleValue > x;
		});
	}
	return it;
}

SerieData::const_iterator SerieData::lower_bound(ValueChange::ValueX value_x) const
{
	const_iterator it = end();
	if (m_xType == ValueType::TimeStamp) {
		it = std::lower_bound(cbegin(), cend(), value_x.timeStamp, [](const ValueChange &val, const ValueChange::TimeStamp &x) {
			return val.valueX.timeStamp < x;
		});
	}
	else if (m_xType == ValueType::Int) {
		it = std::lower_bound(cbegin(), cend(), value_x.intValue, [](const ValueChange &val, int x) {
			return val.valueX.intValue < x;
		});
	}
	else if (m_xType == ValueType::Double) {
		it = std::lower_bound(cbegin(), cend(), value_x.doubleValue, [](const ValueChange &val, double x) {
			return val.valueX.doubleValue < x;
		});
	}
	return it;
}

ValueChange::ValueX ValueXInterval::length() const
{
	if (type == ValueType::TimeStamp) {
		return ValueChange::ValueX(max.timeStamp - min.timeStamp);
	}
	else if (type == ValueType::Int) {
		return ValueChange::ValueX(max.intValue - min.intValue);
	}
	else if (type == ValueType::Double) {
		return ValueChange::ValueX(max.doubleValue - min.doubleValue);
	}
	SHV_EXCEPTION("Invalid interval type");
}

}
}
