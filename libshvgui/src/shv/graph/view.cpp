#include "view.h"

#include "backgroundstripe.h"
#include "outsideseriegroup.h"
#include "serie.h"
#include "pointofinterest.h"

#include <shv/core/exception.h>

#include <QDateTime>
#include <QFrame>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <limits>

namespace shv {
namespace gui {
namespace graphview {

static constexpr const int POI_SYMBOL_WIDTH = 12;
static constexpr const int POI_SYMBOL_HEIGHT = 18;

View::View(QWidget *parent) : QWidget(parent)
  , m_model(0)
  , m_displayedRangeMin(0LL)
  , m_displayedRangeMax(0LL)
  , m_loadedRangeMin(0LL)
  , m_loadedRangeMax(0LL)
  , m_dataRangeMin(0LL)
  , m_dataRangeMax(0LL)
  , m_zoomSelection({ 0, 0 })
  , m_currentSelectionModifiers(Qt::NoModifier)
  , m_moveStart(-1)
  , m_currentPosition(-1)
  , m_verticalGridDistance(0.0)
  , m_horizontalGridDistance(0.0)
  , m_yAxisShownDecimalPoints(0)
  , m_y2AxisShownDecimalPoints(0)
  , m_xValueScale(0.0)
  , m_leftRangeSelectorHandle(0)
  , m_rightRangeSelectorHandle(0)
  , m_leftRangeSelectorPosition(0)
  , m_rightRangeSelectorPosition(0)
  , m_mode(Mode::Static)
  , m_dynamicModePrepend(60000LL)
  , m_preserveZoom(false)
  , m_enableVerticalZoom(true)
  , m_verticalZoom(1.0)
  , m_minimumVerticalZoom(0.1)
  , m_maximumVerticalZoom(10.0)
  , m_verticalMove(0)
{
	m_toolTipTimer.setSingleShot(true);
	connect(&m_toolTipTimer, &QTimer::timeout, this, &View::showToolTip);

	QColor text_color = palette().text().color();
	settings.xAxis.color = text_color;
	settings.yAxis.color = text_color;
	settings.y2Axis.color = text_color;
	QFont fd = font();
	fd.setBold(true);
//	fd.setPointSize(fd.pointSize() + 2);
	settings.xAxis.description.font = fd;
	settings.yAxis.description.font = fd;
	settings.y2Axis.description.font = fd;
	QFont fl = font();
//	fd.setPointSize(fd.pointSize() + 1);
	settings.xAxis.labelFont = fl;
	settings.yAxis.labelFont = fl;
	settings.y2Axis.labelFont = fl;
	settings.xAxis.description.text = QString();
	settings.y2Axis.show = false;
	QFont fsl = font();
	fsl.setBold(true);
	settings.serieList.font = fsl;
	settings.backgroundColor = palette().base().color();
	settings.frameColor = palette().mid().color();
	settings.legendStyle =
			"<style>"
			"  table {"
			"    width: 100%;"
			"  }"
			"  td.label {"
			"    padding-right: 3px; "
			"    padding-left: 5px;"
			"    text-align: right;"
			"  }"
			"  td.value {"
			"    font-weight: bold;"
			"    padding-right: 5px;"
			"    padding-left: 3px;"
			"  }"
			"  table.head td {"
			"    padding-top: 5px;"
			"  }"
			"  td.headLabel {"
			"    padding-left: 5px;"
			"    padding-right: 5px;"
			"  }"
			"  td.headValue {"
			"    font-weight: bold;"
			"    padding-right: 5px;"
			"    padding-left: 3px;"
			"  }"
			"</style>";
//	setFocus();
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_leftRangeSelectorHandle = new RangeSelectorHandle(this);
	m_leftRangeSelectorHandle->installEventFilter(this);
	m_leftRangeSelectorHandle->hide();
	m_rightRangeSelectorHandle = new RangeSelectorHandle(this);
	m_rightRangeSelectorHandle->installEventFilter(this);
	m_rightRangeSelectorHandle->hide();
}

View::~View()
{
	for (const QMetaObject::Connection &connection : m_connections) {
		disconnect(connection);
	}
}

QPainterPath View::createPoiPath(int x, int y) const
{
	QPainterPath painter_path(QPointF(x, y));
	painter_path.moveTo(x + POI_SYMBOL_WIDTH / 2, y + POI_SYMBOL_HEIGHT);
	painter_path.arcTo(QRect(x, y, POI_SYMBOL_WIDTH, POI_SYMBOL_WIDTH), -30, 240);
	painter_path.closeSubpath();
	return painter_path;
}

void View::releaseModel()
{
	m_displayedRangeMin = 0LL;
	m_displayedRangeMax = 0LL;
	m_loadedRangeMin = 0LL;
	m_loadedRangeMax = 0LL;
	m_dataRangeMin = 0LL;
	m_dataRangeMax = 0LL;
	m_zoomSelection = { 0, 0 };
	m_currentSelectionModifiers = Qt::NoModifier;
	m_moveStart = -1;
	m_currentPosition = -1;
	m_xValueScale = 0.0;
	m_leftRangeSelectorPosition = 0;
	m_rightRangeSelectorPosition = 0;
	m_leftRangeSelectorHandle->hide();
	m_rightRangeSelectorHandle->hide();
	m_selections.clear();

	m_model = nullptr;
	removePointsOfInterest();

	if (m_toolTipTimer.isActive()) {
		m_toolTipTimer.stop();
	}

	computeGeometry();
	update();
}

void View::showRange(ValueChange::ValueX from, ValueChange::ValueX to)
{
	showRangeInternal(xValue(from), xValue(to));
}

void View::showRange(ValueXInterval range)
{
	showRange(range.min, range.max);
}

void View::setModel(GraphModel *model)
{
	if (m_model) {
		disconnect(m_model, &GraphModel::dataChanged, this, &View::onModelDataChanged);
		disconnect(m_model, &GraphModel::destroyed, this, &View::releaseModel);
		releaseModel();
	}

	m_model = model;

	if (m_model) {
		connect(m_model, &GraphModel::dataChanged, this, &View::onModelDataChanged);
		connect(m_model, &GraphModel::destroyed, this, &View::releaseModel);

		onModelDataChanged();
	}
}

void View::onModelDataChanged() //TODO improve change detection in model
{
	if (m_toolTipTimer.isActive()) {
		m_toolTipTimer.stop();
	}

	qint64 orig_loaded_range_min = m_loadedRangeMin;
	qint64 orig_loaded_range_max = m_loadedRangeMax;
	qint64 orig_loaded_range_length = orig_loaded_range_max - orig_loaded_range_min;

	qint64 orig_displayed_range_min = m_displayedRangeMin;
	qint64 orig_displayed_range_max = m_displayedRangeMax;
	qint64 orig_displayed_range_length = orig_displayed_range_max - orig_displayed_range_min;

	for (Serie *serie : m_series) {
		const SerieData &serie_model_data = serie->serieModelData(this);
		serie->displayedDataBegin = serie_model_data.begin();
		serie->displayedDataEnd = serie_model_data.end();
		for (Serie *dep_serie : serie->dependentSeries()) {
			const SerieData &dep_serie_model_data = dep_serie->serieModelData(this);
			dep_serie->displayedDataBegin = dep_serie_model_data.begin();
			dep_serie->displayedDataEnd = dep_serie_model_data.end();
		}
	}
	switch (settings.xAxisType) {
	case ValueType::Double:
	{
		double min, max;
		computeRange(min, max);
		m_xValueScale = INT64_MAX / max;
		m_loadedRangeMin = static_cast<qint64>(min * m_xValueScale);
		m_loadedRangeMax = static_cast<qint64>(max * m_xValueScale);
		break;
	}
	case ValueType::Int:
	{
		int min, max;
		computeRange(min, max);
		m_loadedRangeMin = min;
		m_loadedRangeMax = max;
		break;
	}
	case ValueType::TimeStamp:
	{
		computeRange(m_loadedRangeMin, m_loadedRangeMax);
		break;
	}
	default:
		break;
	}
	if (m_loadedRangeMin < 0LL) {
		SHV_EXCEPTION("GraphView cannot operate negative values on x axis");
	}
	m_dataRangeMin = m_loadedRangeMin;
	m_dataRangeMax = m_loadedRangeMax;

	if (!m_preserveZoom) {
		m_displayedRangeMin = m_loadedRangeMin;
		m_displayedRangeMax = m_loadedRangeMax;
	}
	else if (m_mode == Mode::Static) {
		if (orig_loaded_range_min == 0LL || orig_loaded_range_min != m_loadedRangeMin) {
			m_displayedRangeMin = m_loadedRangeMin;
		}
		if (orig_loaded_range_max == 0LL || orig_loaded_range_max != m_loadedRangeMax) {
			m_displayedRangeMax = m_loadedRangeMax;
		}
	}

	if (m_mode == Mode::Dynamic) {
		qint64 loaded_range_length = m_loadedRangeMax - m_loadedRangeMin;

		if (m_loadedRangeMin) {
			if (loaded_range_length < 60000LL) {
				m_displayedRangeMin = m_loadedRangeMin = m_loadedRangeMin - (m_dynamicModePrepend - loaded_range_length);
			}
			if (orig_loaded_range_length > 10LL) {
				if (orig_displayed_range_max == orig_loaded_range_max) {
					m_displayedRangeMin = m_displayedRangeMax - orig_displayed_range_length;
				}
				else {
					m_displayedRangeMin = orig_displayed_range_min;
					m_displayedRangeMax = orig_displayed_range_max;
				}
				if (m_displayedRangeMin < m_loadedRangeMin) {
					m_displayedRangeMin = m_loadedRangeMin;
				}
				if (m_displayedRangeMax > m_loadedRangeMax) {
					m_displayedRangeMax = m_loadedRangeMax;
				}
			}
		}
	}

	computeDataRange();

	if (settings.rangeSelector.show) {
		m_leftRangeSelectorHandle->show();
		m_rightRangeSelectorHandle->show();
	}
	computeGeometry();
	update();
}

void View::resizeEvent(QResizeEvent *resize_event)
{
	QWidget::resizeEvent(resize_event);
	if (!m_model || !m_series.count()) {
		return;
	}

	computeGeometry();
	update();
}

void View::computeDataRange()
{
	for (Serie *serie : m_series) {
		computeDataRange(serie);
	}
}

void View::computeDataRange(Serie *serie)
{
	if (!m_model) {
		return;
	}
	const SerieData &serie_model_data = serie->serieModelData(this);
	serie->displayedDataBegin = findMinYValue(serie_model_data.begin(), serie_model_data.end(), m_displayedRangeMin);
	serie->displayedDataEnd = findMaxYValue(serie_model_data.begin(), serie_model_data.end(), m_displayedRangeMax);
	for (Serie *dep_serie : serie->dependentSeries()) {
		computeDataRange(dep_serie);
	}
}

int View::computeYLabelWidth(const Settings::Axis &axis, int &shownDecimalPoints) const
{
	double range = (axis.rangeMax / m_verticalZoom) - (axis.rangeMin / m_verticalZoom);
	int place_value = 0;
	while (range >= 1.0) {
		++place_value;
		range /= 10.0;
	}
	if (axis.decimalPoints == -1) {
		shownDecimalPoints = 0;

		if (place_value < 2) {
			range = (axis.rangeMax / m_verticalZoom) - (axis.rangeMin / m_verticalZoom);
			while (range < 1.0 || (place_value + shownDecimalPoints) < 3) {
				++shownDecimalPoints;
				range *= 10.0;
			}
		}
	}
	else {
		shownDecimalPoints = axis.decimalPoints;
	}
	QString test_string;
	test_string.fill('8', 2 + place_value + shownDecimalPoints + (shownDecimalPoints ? 1 : 0) + (axis.rangeMin < 0.0 ? 1 : 0));
	return QFontMetrics(axis.labelFont).width(test_string);
}

void View::computeRangeSelectorPosition()
{
	if (m_loadedRangeMax > m_loadedRangeMin) {
		m_leftRangeSelectorPosition = m_rangeSelectorRect.x() +
									  m_rangeSelectorRect.width()  * (m_displayedRangeMin - m_loadedRangeMin) / (m_loadedRangeMax - m_loadedRangeMin);
		m_rightRangeSelectorPosition = m_rangeSelectorRect.x() +
									   m_rangeSelectorRect.width() * (m_displayedRangeMax - m_loadedRangeMin) / (m_loadedRangeMax - m_loadedRangeMin);
	}
	else {
		m_leftRangeSelectorPosition = m_rangeSelectorRect.x();
		m_rightRangeSelectorPosition = m_rangeSelectorRect.x() + m_rangeSelectorRect.width();
	}
	int dist = m_rightRangeSelectorPosition - m_leftRangeSelectorPosition;
	int left_handle_width = m_leftRangeSelectorHandle->width() / 2;
	int right_handle_width = m_rightRangeSelectorHandle->width() / 2;
	int handle_width = left_handle_width + right_handle_width;
	if (dist < handle_width && m_rangeSelectorRect.width() > handle_width) {
		int mid = (m_leftRangeSelectorPosition + m_rightRangeSelectorPosition) / 2;
		if (mid - left_handle_width < m_rangeSelectorRect.x()) {
			mid = m_rangeSelectorRect.x() + left_handle_width;
		}
		else if (mid + right_handle_width > m_rangeSelectorRect.right()) {
			mid = m_rangeSelectorRect.right() - right_handle_width;
		}
		m_leftRangeSelectorPosition = mid - left_handle_width;
		m_rightRangeSelectorPosition = mid + right_handle_width;
	}
	m_leftRangeSelectorHandle->move(
				m_leftRangeSelectorPosition - left_handle_width,
				m_rangeSelectorRect.y() + (m_rangeSelectorRect.height() - m_leftRangeSelectorHandle->height()) / 2
				);
	m_rightRangeSelectorHandle->move(
				m_rightRangeSelectorPosition - right_handle_width,
				m_rangeSelectorRect.y() + (m_rangeSelectorRect.height() - m_rightRangeSelectorHandle->height()) / 2
				);
}

QVector<View::SerieInGroup> View::shownSeriesInGroup(const OutsideSerieGroup &group, const QVector<const Serie*> &only_series) const
{
	QVector<SerieInGroup> shown_series_in_group;
	if (!group.isHidden()) {
		for (const Serie *serie : group.series()) {
			const Serie *master_serie = 0;
			for (const Serie *s : m_series) {
				if (s == serie) {
					master_serie = serie;
					break;
				}
				else {
					for (const Serie *ds : s->dependentSeries()) {
						if (ds == serie) {
							master_serie = s;
							break;
						}
					}
				}
			}
			if (master_serie && !master_serie->isHidden()) {
				shown_series_in_group << SerieInGroup{ serie, master_serie };
			}
		}
	}
	for (int j = 0; j < shown_series_in_group.count(); ++j) {
		bool is_serie_in_area = false;
		for (const Serie *s : only_series) {
			if (s == shown_series_in_group[j].serie) {
				is_serie_in_area = true;
				break;
			}
			for (const Serie *ds : s->dependentSeries()) {
				if (ds == shown_series_in_group[j].serie) {
					is_serie_in_area = true;
					break;
				}
			}
			if (is_serie_in_area) {
				break;
			}
		}
		if (!is_serie_in_area) {
			shown_series_in_group.removeAt(j--);
		}
	}

	return shown_series_in_group;
}

void View::computeGeometry()
{
	int poi_strip_height = m_pointsOfInterest.count() ? (POI_SYMBOL_HEIGHT + 2) : 0;
	QRect all_graphs_rect(settings.margin.left, settings.margin.top + poi_strip_height,
					 width() - settings.margin.left - settings.margin.right,
					 height() - settings.margin.top - poi_strip_height - settings.margin.bottom);
	if (settings.serieList.show) {
		m_serieListRect = QRect(all_graphs_rect.x(), all_graphs_rect.bottom() - settings.serieList.height, all_graphs_rect.width(), settings.serieList.height);
		all_graphs_rect.setBottom(all_graphs_rect.bottom() - settings.serieList.height);
	}
	if (settings.rangeSelector.show) {
		m_rangeSelectorRect = QRect(all_graphs_rect.x(), all_graphs_rect.bottom() - settings.rangeSelector.height, all_graphs_rect.width(), settings.rangeSelector.height);
		all_graphs_rect.setBottom(all_graphs_rect.bottom() - settings.rangeSelector.height - 15);
	}
	if (settings.xAxis.show) {
		if (settings.xAxis.description.show) {
			int x_axis_description_font_height = QFontMetrics(settings.xAxis.description.font).lineSpacing() * 1.5;
			m_xAxisDescriptionRect = QRect(all_graphs_rect.left(), all_graphs_rect.bottom() - x_axis_description_font_height, all_graphs_rect.width(), x_axis_description_font_height);
			all_graphs_rect.setBottom(all_graphs_rect.bottom() - x_axis_description_font_height);
		}
		int x_axis_label_font_height = QFontMetrics(settings.xAxis.labelFont).lineSpacing();
		m_xAxisLabelRect = QRect(all_graphs_rect.left(), all_graphs_rect.bottom() - x_axis_label_font_height, all_graphs_rect.width(), x_axis_label_font_height);
		all_graphs_rect.setBottom(all_graphs_rect.bottom() - x_axis_label_font_height * 1.4);
	}


	int vertical_space = 20;
	QVector<QVector<const Serie*>> visible_blocks;

	for (int i = 0; i < m_serieBlocks.count(); ++i) {
		for (const Serie *serie : m_serieBlocks[i]) {
			if (!serie->isHidden()) {
				visible_blocks << m_serieBlocks[i];
				break;
			}
		}
	}

	if (visible_blocks.count() == 0 && m_graphArea.count()) { //pokud nemame zobrazenou zadnou serii, pocitame geometrii
		visible_blocks << m_graphArea[0].series;              //podle naposledy zobrazene
	}
	int graph_count = visible_blocks.count();

	if (graph_count) {
		m_graphArea.clear();

		int group_spacing = 5;
		QVector<int> group_heights;
		QVector<QVector<int>> block_group_heights;
		int all_group_height = 0;
		for (int i = 0; i < visible_blocks.count(); ++i) {
			QVector<const OutsideSerieGroup*> block_groups = groupsForSeries(visible_blocks[i]);
			group_heights << 0;
			block_group_heights << QVector<int>();
			for (const OutsideSerieGroup *group : block_groups) {
				const QVector<SerieInGroup> shown_series_in_group = shownSeriesInGroup(*group, visible_blocks[i]);
				if (shown_series_in_group.count()) {
					int group_height = group->serieSpacing();
					for (const SerieInGroup &serie_in_group : shown_series_in_group) {
						group_height = group_height + serie_in_group.serie->lineWidth() + group->serieSpacing();
					}
					if (group_height < group->minimumHeight()) {
						group_height = group->minimumHeight();
					}
					all_group_height = all_group_height + group_height + group_spacing;
					block_group_heights.last() << group_height;
					group_heights.last() += group_height;
				}
			}
		}

		int single_graph_height = (all_graphs_rect.height() - (vertical_space * (graph_count - 1)) - all_group_height) / graph_count;

		int max_y_description_width = 0;
		int max_y_label_width = 0;
		int max_y2_description_width = 0;
		int max_y2_label_width = 0;
		int this_graph_offset = all_graphs_rect.y();

		for (int i = 0; i < visible_blocks.count(); ++i) {
			int y_description_width = 0;
			int y_label_width = 0;
			int y2_description_width = 0;
			int y2_label_width = 0;

			GraphArea area;
			area.series = visible_blocks[i];

			area.showYAxis = false;
			area.showY2Axis = false;
			for (const Serie *serie : area.series) {
				if (serie->relatedAxis() == Serie::YAxis::Y1) {
					area.showYAxis = true;
				}
				else if (serie->relatedAxis() == Serie::YAxis::Y2) {
					area.showY2Axis = true;
				}
			}
			area.switchAxes = (area.showY2Axis && !area.showYAxis);

			for (int group_height : block_group_heights[i]) {
				area.outsideSerieGroupsRects << QRect(all_graphs_rect.x(), this_graph_offset, all_graphs_rect.width(), group_height);
				this_graph_offset += group_height;
			}

			area.graphRect = QRect(all_graphs_rect.x(), this_graph_offset, all_graphs_rect.width(), single_graph_height);
			this_graph_offset = this_graph_offset + single_graph_height + vertical_space;

			if (settings.yAxis.show && (area.showYAxis || (area.showY2Axis && area.switchAxes))) {
				if (area.showYAxis) {
					if (settings.yAxis.description.show) {
						y_description_width = QFontMetrics(settings.yAxis.description.font).lineSpacing() * 1.5;
					}
					y_label_width = computeYLabelWidth(settings.yAxis, m_yAxisShownDecimalPoints);
				}
				else {
					if (!settings.y2Axis.description.show) {
						y_description_width = QFontMetrics(settings.y2Axis.description.font).lineSpacing() * 1.5;
					}
					y_label_width = computeYLabelWidth(settings.y2Axis, m_y2AxisShownDecimalPoints);
				}
			}

			if (settings.y2Axis.show && area.showY2Axis && !area.switchAxes) {
				if (!settings.y2Axis.description.show) {
					y2_description_width = QFontMetrics(settings.y2Axis.description.font).lineSpacing() * 1.5;
				}
				y2_label_width = computeYLabelWidth(settings.y2Axis, m_y2AxisShownDecimalPoints);
			}

			double x_scale = (double)((settings.yAxis.rangeMax / m_verticalZoom) - (settings.yAxis.rangeMin / m_verticalZoom)) / area.graphRect.height();
			area.xAxisPosition = area.graphRect.y() + m_verticalMove + (settings.yAxis.rangeMax / m_verticalZoom) / x_scale;
			if (settings.y2Axis.show && area.showY2Axis && !area.switchAxes) {
				double x2_scale = (double)((settings.y2Axis.rangeMax / m_verticalZoom) - (settings.y2Axis.rangeMin / m_verticalZoom)) / area.graphRect.height();
				area.x2AxisPosition = area.graphRect.y() + m_verticalMove + (settings.y2Axis.rangeMax / m_verticalZoom) / x2_scale;
			}
			m_graphArea << area;

			max_y_description_width = qMax(max_y_description_width, y_description_width);
			max_y_label_width = qMax(max_y_label_width, y_label_width);
			max_y2_description_width = qMax(max_y2_description_width, y2_description_width);
			max_y2_label_width = qMax(max_y2_label_width, y2_label_width);
		}
		int left_offset = max_y_description_width + max_y_label_width;
		int right_offset = max_y2_description_width + max_y2_label_width;
		if (left_offset) {
			left_offset += 5;
			if (settings.xAxis.show) {
				m_xAxisDescriptionRect.setX(m_xAxisDescriptionRect.x() + left_offset);
			}
			if (settings.serieList.show) {
				m_serieListRect.setX(m_serieListRect.x() + left_offset);
			}
			if (settings.rangeSelector.show) {
				m_rangeSelectorRect.setX(m_rangeSelectorRect.x() + left_offset);
			}
			for (GraphArea &area : m_graphArea) {
				area.yAxisDescriptionRect = QRect(area.graphRect.x(), area.graphRect.y(), max_y_description_width, area.graphRect.height());
				area.yAxisLabelRect = QRect(area.graphRect.x() + max_y_description_width, area.graphRect.y(), max_y_label_width, area.graphRect.height());
				area.graphRect.setX(area.graphRect.x() + left_offset);
				for (QRect &outsideRect : area.outsideSerieGroupsRects) {
					outsideRect.setX(outsideRect.x() + left_offset);
				}
			}
		}
		if (right_offset) {
			right_offset += 5;
			if (settings.xAxis.show) {
				m_xAxisDescriptionRect.setRight(m_xAxisDescriptionRect.right() - right_offset);
			}
			if (settings.serieList.show) {
				m_serieListRect.setRight(m_serieListRect.right() - right_offset);
			}
			if (settings.rangeSelector.show) {
				m_rangeSelectorRect.setRight(m_rangeSelectorRect.right() - right_offset);
			}
			for (GraphArea &area : m_graphArea) {
				area.y2AxisDescriptionRect = QRect(area.graphRect.right() - max_y2_description_width, area.graphRect.y(), max_y2_description_width, area.graphRect.height());
				area.y2AxisLabelRect = QRect(area.graphRect.right() - max_y2_description_width - max_y2_label_width, area.graphRect.y(), max_y2_label_width, area.graphRect.height());
				area.graphRect.setRight(area.graphRect.right() - right_offset);
				for (QRect &outsideRect : area.outsideSerieGroupsRects) {
					outsideRect.setRight(outsideRect.right() - right_offset);
				}
			}
		}
	}
	else {
		GraphArea area;

		area.showYAxis = false;
		area.showY2Axis = false;
		area.switchAxes = false;

		area.graphRect = all_graphs_rect;
		area.xAxisPosition = area.graphRect.bottom();
		area.x2AxisPosition = area.graphRect.bottom();

		m_graphArea << area;
	}

	if (settings.verticalGrid.type == Settings::Grid::Type::FixedCount) {
		m_verticalGridDistance = (double)m_graphArea[0].graphRect.width() / (settings.verticalGrid.fixedCount + 1.0);
	}
	else if (settings.verticalGrid.type == Settings::Grid::Type::FixedDistance) {
		m_verticalGridDistance = settings.verticalGrid.fixedWidth;
	}

	if (settings.horizontalGrid.type == Settings::Grid::Type::FixedCount) {
		m_horizontalGridDistance = (double)m_graphArea[0].graphRect.height() / (settings.horizontalGrid.fixedCount + 1.0);
	}
	else if (settings.horizontalGrid.type == Settings::Grid::Type::FixedDistance) {
		m_horizontalGridDistance = settings.horizontalGrid.fixedWidth;
	}
	if (settings.rangeSelector.show) {
		computeRangeSelectorPosition();
	}
}

bool View::hasVisibleSeries() const
{
	for (const Serie *serie : m_series) {
		if (!serie->isHidden()) {
			return true;
		}
	}
	return false;
}

void View::paintEvent(QPaintEvent *paint_event)
{
	QWidget::paintEvent(paint_event);

	QPainter painter(this);
	painter.fillRect(0, 0, width(), height(), settings.backgroundColor);
	if (settings.frameWidth) {
		painter.save();
		QPen frame_pen(settings.frameColor);
		frame_pen.setWidth(settings.frameWidth);
		painter.setPen(frame_pen);
		painter.drawRect(0, 0, width(), height());
		painter.restore();
	}

	if (!m_model) {
		return;
	}

	int tooltip_timeout = 0;
	if (m_toolTipTimer.isActive()) {
		tooltip_timeout = m_toolTipTimer.remainingTime();
		m_toolTipTimer.stop();
	}

	QRect paint_rect = paint_event->rect();

	if (settings.serieList.show && paint_rect.intersects(m_serieListRect)) {
		paintSerieList(&painter);
	}
	if (settings.rangeSelector.show && paint_rect.intersects(m_rangeSelectorRect)) {
		paintRangeSelector(&painter);
	}
	if (settings.xAxis.show) {
		if (settings.xAxis.description.show && paint_rect.intersects(m_xAxisDescriptionRect)) {
			paintXAxisDescription(&painter);
		}
		if (paint_rect.intersects(m_xAxisLabelRect)) {
			paintXAxisLabels(&painter);
		}
	}
//    if (settings.legend.show && settings.legend.type == Settings::Legend::Type::Fixed) {
//        paintLegend(&painter);
//    }
	for (const GraphArea &area : m_graphArea) {

		if (settings.yAxis.show && (area.showYAxis || (area.showY2Axis && area.switchAxes))) {
			if (((area.showYAxis && settings.yAxis.description.show) || (area.showY2Axis && settings.y2Axis.description.show)) && paint_rect.intersects(area.yAxisDescriptionRect)) {
				paintYAxisDescription(&painter, area);
			}
			if (paint_rect.intersects(area.yAxisLabelRect)) {
				paintYAxisLabels(&painter, area);
			}
		}

		if (settings.y2Axis.show && area.showY2Axis && !area.switchAxes) {
			if (settings.y2Axis.description.show && paint_rect.intersects(area.y2AxisDescriptionRect)) {
				paintY2AxisDescription(&painter, area);
			}
			if (paint_rect.intersects(area.y2AxisLabelRect)) {
				paintY2AxisLabels(&painter, area);
			}
		}

		if (paint_rect.intersects(area.graphRect)) {
			if (settings.xAxis.show) {
				paintXAxis(&painter, area);
			}
			if (settings.yAxis.show && (area.showYAxis || (area.showY2Axis && area.switchAxes))) {
				paintYAxis(&painter, area);
			}
			if (settings.y2Axis.show && area.showY2Axis && !area.switchAxes) {
				paintY2Axis(&painter, area);
			}
			if (settings.verticalGrid.show) {
				paintVerticalGrid(&painter, area);
			}
			if (settings.horizontalGrid.show) {
				paintHorizontalGrid(&painter, area);
			}
			if (settings.showSerieBackgroundStripes) {
				paintSerieBackgroundStripes(&painter, area);
			}
			if (m_backgroundStripes.count()) {
				paintViewBackgroundStripes(&painter, area);
			}
			if (m_displayedRangeMax - m_displayedRangeMin) {
				paintPointsOfInterest(&painter, area);
			}
			if (m_series.count()) {
				paintSeries(&painter, area);
			}
			if (m_outsideSeriesGroups.count()) {
				paintOutsideSeriesGroups(&painter, area);
			}
			if (m_selections.count() || m_zoomSelection.start || m_zoomSelection.end) {
				paintSelections(&painter, area);
			}
			if (m_currentPosition != -1) {
				if (settings.showCrossLine) {
					paintCrossLine(&painter, area);
				}
				paintCurrentPosition(&painter, area);
			}
		}
	}
	if (tooltip_timeout) {
		m_toolTipTimer.start(tooltip_timeout);
	}
}

bool View::posInGraph(const QPoint &pos) const
{
	QRect graph_rect;
	if (m_graphArea.count()) {
		graph_rect = m_graphArea[0].graphRect;
	}
	for (int i = 1; i < m_graphArea.count(); ++i) {
		graph_rect = graph_rect.united(m_graphArea[i].graphRect);
	}
	graph_rect.setLeft(graph_rect.left() - 1);
	return graph_rect.contains(pos, true);
}

bool View::posInRangeSelector(const QPoint &pos) const
{
	return m_rangeSelectorRect.contains(pos);
}

void View::wheelEvent(QWheelEvent *wheel_event)
{
	if (posInGraph(wheel_event->pos())) {

		if (wheel_event->modifiers() == Qt::NoModifier) {
			qint64 center = widgetPositionToXValue(wheel_event->pos().x());
			if (wheel_event->angleDelta().y() > 0) {
				zoom(center, 6.5 / 10.0);
			}
			else if (wheel_event->angleDelta().y() < 0) {
				zoom(center, 10.0 / 6.5);
			}
		}
		else if (wheel_event->modifiers() == Qt::ControlModifier) {
			if (m_enableVerticalZoom) {
				double vertical_zoom = m_verticalZoom;
				if (wheel_event->angleDelta().y() > 0) {
					vertical_zoom = m_verticalZoom / 0.92;
					if (vertical_zoom > m_maximumVerticalZoom) {
						vertical_zoom = m_maximumVerticalZoom;
					}
				}
				else if (wheel_event->angleDelta().y() < 0) {
					vertical_zoom = m_verticalZoom * 0.92;
					if (vertical_zoom < m_minimumVerticalZoom) {
						vertical_zoom = m_minimumVerticalZoom;
					}
				}
				setVerticalZoomInternal(vertical_zoom);
			}
		}
	}
}

void View::mouseDoubleClickEvent(QMouseEvent *mouse_event)
{
	QPoint mouse_pos = mouse_event->pos();
	bool pos_in_graph = posInGraph(mouse_pos);
	if (pos_in_graph || posInRangeSelector(mouse_pos)) {
		if (m_zoomSelection.start || m_zoomSelection.end) {
			m_zoomSelection = { 0, 0 };
			m_currentSelectionModifiers = Qt::NoModifier;
		}
		bool horizontal_zoom_changed = m_loadedRangeMin != m_displayedRangeMin || m_loadedRangeMax != m_displayedRangeMax;
		if (((m_verticalZoom != 1.0  || m_verticalMove != 0) && pos_in_graph) || horizontal_zoom_changed) {
			if (pos_in_graph) {
				if (m_verticalZoom != 1.0) {
					m_verticalZoom = 1.0;
					Q_EMIT verticalZoomChanged();
				}
				if (m_verticalMove != 0) {
					m_verticalMove = 0;
					computeGeometry();
				}
				if (!horizontal_zoom_changed) {
					update();
				}
			}
			showRangeInternal(m_loadedRangeMin, m_loadedRangeMax);
		}
	}
}

void View::mousePressEvent(QMouseEvent *mouse_event)
{
	QPoint pos = mouse_event->pos();
	if (mouse_event->buttons() & Qt::LeftButton) {
		if (posInGraph(pos)) {
			if (mouse_event->modifiers() & Qt::MetaModifier || mouse_event->modifiers() & Qt::ShiftModifier) {
				qint64 value = widgetPositionToXValue(pos.x());
				m_currentSelectionModifiers = mouse_event->modifiers();
				if (mouse_event->modifiers() & Qt::MetaModifier) {
					m_zoomSelection = { value, value };
				}
				else {
					m_selections << Selection { value, value };
				}
			}
			else if (mouse_event->modifiers() == Qt::ControlModifier) {
				m_moveStart = pos.y();
			}
			else if (mouse_event->modifiers() == Qt::NoModifier) {
				m_moveStart = pos.x() - m_graphArea[0].graphRect.x();
			}
		}
		else if (posInRangeSelector(pos)) {
			if (mouse_event->modifiers() == Qt::NoModifier) {
				if (pos.x() >= m_leftRangeSelectorPosition && pos.x() <= m_rightRangeSelectorPosition) {
					m_moveStart = pos.x() - m_rangeSelectorRect.x();
				}
			}
		}
		else {
			for (int i = 0; i < m_seriesListRect.count(); ++i) {
				const QRect &rect = m_seriesListRect[i];
				if (rect.contains(pos)) {
					m_series[i]->show(m_series[i]->isHidden());
					break;
				}
			}
		}
	}
	else if (mouse_event->buttons() & Qt::RightButton) {
		if (posInGraph(pos) && mouse_event->modifiers() == Qt::NoModifier) {
			popupContextMenu(pos);
		}
	}
}

const View::Selection *View::selectionOnValue(qint64 value) const
{
	for (const Selection &selection : m_selections) {
		if (selection.containsValue(value)) {
			return &selection;
		}
	}
	return 0;
}

void View::updateLastValueInLastSelection(qint64 value)
{
	m_selections.last().end = value;
	Q_EMIT selectionsChanged();
}

void View::mouseMoveEvent(QMouseEvent *mouse_event)
{
	if (!m_model) {
		return;
	}

	QPoint pos = mouse_event->pos();
	if (posInGraph(pos)) {
		int x_pos = pos.x() - m_graphArea[0].graphRect.x();
		if (mouse_event->buttons() & Qt::LeftButton) {
			m_currentPosition = -1;
			if (m_currentSelectionModifiers != Qt::NoModifier) {
				qint64 value = widgetPositionToXValue(pos.x());
				if (m_currentSelectionModifiers & Qt::MetaModifier) {
					m_zoomSelection.end = value;
				}
				else {
					updateLastValueInLastSelection(value);
				}
				update();
			}
			else if (mouse_event->modifiers() == Qt::ControlModifier) {
				if (m_moveStart != -1) {
					m_verticalMove += pos.y() - m_moveStart;
					m_moveStart = pos.y();
					computeGeometry();
					update();
				}
			}
			else if (mouse_event->modifiers() == Qt::NoModifier) {
				if (m_moveStart != -1) {
					qint64 difference = (m_displayedRangeMax - m_displayedRangeMin) * ((double)(x_pos - m_moveStart) / graphWidth());
					if (difference < 0LL) {
						if ((qint64)(m_loadedRangeMax - m_displayedRangeMax) < -difference) {
							difference = -(m_loadedRangeMax - m_displayedRangeMax);
						}
					}
					else {
						if ((qint64)(m_displayedRangeMin - m_loadedRangeMin) < difference) {
							difference = m_displayedRangeMin - m_loadedRangeMin;
						}
					}
					showRangeInternal(m_displayedRangeMin - difference, m_displayedRangeMax - difference);
					m_moveStart = x_pos;
				}
			}
		}
		else if (mouse_event->buttons() == Qt::NoButton) {
			if (m_currentPosition != x_pos) {
				m_currentPosition = x_pos;
				update();
			}
			if (settings.legend.show && settings.legend.type == Settings::Legend::Type::ToolTip && hasVisibleSeries()) {
				if (m_toolTipTimer.isActive()) {
					m_toolTipTimer.stop();
				}
				m_toolTipPosition = mouse_event->globalPos();
				m_toolTipTimer.start(100);
			}
		}
	}
	else if (posInRangeSelector(pos)) {
		int x_pos = pos.x() - m_rangeSelectorRect.x();
		if (mouse_event->buttons() & Qt::LeftButton) {
			if (mouse_event->modifiers() == Qt::NoModifier) {
				if (m_moveStart != -1) {
					qint64 difference = (qint64)(m_loadedRangeMax - m_loadedRangeMin) * (m_moveStart - x_pos) / m_rangeSelectorRect.width();
					if (difference < 0LL) {
						if ((qint64)(m_loadedRangeMax - m_displayedRangeMax) < -difference) {
							difference = -(m_loadedRangeMax - m_displayedRangeMax);
						}
					}
					else {
						if ((qint64)(m_displayedRangeMin - m_loadedRangeMin) < difference) {
							difference = m_displayedRangeMin - m_loadedRangeMin;
						}
					}
					showRangeInternal(m_displayedRangeMin - difference, m_displayedRangeMax - difference);
					m_moveStart = x_pos;
				}
			}
		}
	}
	else {
		bool in_poi = false;
		for (const PointOfInterest *poi : m_pointsOfInterest) {
			const QPainterPath &painter_path = m_poiPainterPaths[poi];
			if (painter_path.contains(pos)) {
				QToolTip::showText(mouse_event->globalPos(), poi->comment(), this, painter_path.boundingRect().toRect());
				in_poi = true;
				break;
			}
		}
		if (!in_poi) {
			bool on_serie_list = false;
			for (const QRect &rect : m_seriesListRect) {
				if (rect.contains(pos)) {
					on_serie_list = true;
					break;
				}
			}
			Qt::CursorShape current_cursor = cursor().shape();
			Qt::CursorShape requested_cursor;
			if (on_serie_list) {
				requested_cursor = Qt::PointingHandCursor;
			}
			else {
				requested_cursor = Qt::ArrowCursor;
			}
			if (current_cursor != requested_cursor) {
				setCursor(requested_cursor);
			}
		}
	}
}

void View::showToolTip()
{
	QPoint mouse_pos = mapTo(this,  m_toolTipPosition);
	QPoint top_left = mouse_pos;
	top_left.setY(top_left.y() - 5);
	QPoint bottom_right = mouse_pos;
	bottom_right.setY(bottom_right.y() + 5);
	QToolTip::showText(m_toolTipPosition, legend(rectPositionToXValue(m_currentPosition)), this, QRect(top_left, bottom_right));
}

void View::mouseReleaseEvent(QMouseEvent *mouse_event)
{
	QPoint pos = mouse_event->pos();
	if (posInGraph(pos)) {
		if (mouse_event->button() == Qt::LeftButton) {
			if (m_currentSelectionModifiers != Qt::NoModifier) {
				qint64 value = widgetPositionToXValue(pos.x());
				if (m_currentSelectionModifiers & Qt::MetaModifier) {
					qint64 start = m_zoomSelection.start;
					qint64 end = value;
					if (end < start) {
						start = end;
						end = m_zoomSelection.start;
					}
					if (start != end) {
						showRangeInternal(start, end);
					}
					m_zoomSelection = { 0, 0 };
				}
				else if (m_currentSelectionModifiers & Qt::ShiftModifier) {
					updateLastValueInLastSelection(value);
					m_selections.last().normalize();
					unionLastSelection();
				}
				m_currentSelectionModifiers = Qt::NoModifier;
			}
			else if (m_moveStart != -1) {
				m_moveStart = -1;
			}
		}
	}
	else if (posInRangeSelector(pos)) {
		if (m_moveStart != -1) {
			m_moveStart = -1;
		}
	}
}

bool View::eventFilter(QObject *watched, QEvent *event)
{
	auto rangeRectPositionToXValue = [this](int pos)->qint64
	{
		return m_loadedRangeMin + ((pos - m_rangeSelectorRect.x()) * (m_loadedRangeMax - m_loadedRangeMin) / m_rangeSelectorRect.width());
	};

	static int last_mouse_position = 0;
	if (watched == m_leftRangeSelectorHandle || watched == m_rightRangeSelectorHandle) {
		if (event->type() == QEvent::MouseButtonPress) {
			QMouseEvent *mouse_event = (QMouseEvent *)event;
			last_mouse_position = mouse_event->pos().x();
		}
		else if (event->type() == QEvent::MouseMove) {
			QMouseEvent *mouse_event = (QMouseEvent *)event;
			if (watched == m_leftRangeSelectorHandle) {
				int new_position = m_leftRangeSelectorPosition + mouse_event->pos().x() - last_mouse_position;
				if (new_position >= m_rangeSelectorRect.x() && new_position + (m_leftRangeSelectorHandle->width() / 2 + m_rightRangeSelectorHandle->width() / 2) < m_rightRangeSelectorPosition) {
					if (new_position == m_rangeSelectorRect.x()) {
						m_displayedRangeMin = m_loadedRangeMin;
					}
					else {
						m_displayedRangeMin = rangeRectPositionToXValue(new_position);
					}
					computeDataRange();
					computeGeometry();
					update();
					Q_EMIT shownRangeChanged();
				}
			}
			else { //(watched == m_rightRangeSelectorHandle)
				int new_position = m_rightRangeSelectorPosition + mouse_event->pos().x() - last_mouse_position;
				if (new_position <= m_rangeSelectorRect.right() && new_position - (m_leftRangeSelectorHandle->width() / 2 + m_rightRangeSelectorHandle->width() / 2) > m_leftRangeSelectorPosition) {
					if (new_position == m_rangeSelectorRect.right()) {
						m_displayedRangeMax = m_loadedRangeMax;
					}
					else {
						m_displayedRangeMax = rangeRectPositionToXValue(new_position);
					}
					computeDataRange();
					computeGeometry();
					update();
					Q_EMIT shownRangeChanged();
				}

			}
			return true;
		}
	}
	return false;
}

void View::popupContextMenu(const QPoint &pos)
{
	QMenu popup_menu(this);
	QAction *zoom_to_fit = popup_menu.addAction(tr("Zoom to &fit"), [this]() {
		showRangeInternal(m_loadedRangeMin, m_loadedRangeMax);
	});
	if (m_displayedRangeMin == m_loadedRangeMin && m_displayedRangeMax == m_loadedRangeMax) {
		zoom_to_fit->setEnabled(false);
	}
	qint64 matching_value = widgetPositionToXValue(pos.x());

	for (int i = 0; i < m_selections.count(); ++i) {
		const Selection &selection = m_selections[i];
		if (selection.containsValue(matching_value)) {
			QAction *zoom_to_selection = popup_menu.addAction(tr("&Zoom to selection"), [this, i]() {
				showRangeInternal(m_selections[i].start, m_selections[i].end);
			});
			if (m_selections[i].start == m_displayedRangeMin && m_selections[i].end == m_displayedRangeMax) {
				zoom_to_selection->setEnabled(false);
			}
			popup_menu.addAction(tr("&Clear selection"), [this, i]() {
				m_selections.removeAt(i);
				update();
				Q_EMIT selectionsChanged();
			});
			break;
		}
	}
	QAction *remove_all_selections = popup_menu.addAction(tr("Clear &all selections"), [this]() {
		m_selections.clear();
		update();
	});
	if (m_selections.count() == 0) {
		remove_all_selections->setEnabled(false);
	}
	popup_menu.addSeparator();
	if (settings.contextMenuExtend) {
		settings.contextMenuExtend(&popup_menu);
	}
	popup_menu.exec(mapToGlobal(pos));
}

void View::zoom(qint64 center, double scale)
{
	if (scale == 1.0) {
		return;
	}
	qint64 old_length = m_displayedRangeMax - m_displayedRangeMin;
	qint64 new_length = old_length * scale;
	qint64 from = 0LL;
	qint64 to = 0LL;
	if (new_length > old_length) {
		qint64 difference = new_length - old_length;
		qint64 difference_from = difference * (double)(center - m_displayedRangeMin) / old_length;
		qint64 difference_to = difference - difference_from;
		if (m_displayedRangeMin - m_loadedRangeMin > difference_from) {
			from = m_displayedRangeMin - difference_from;
		}
		else {
			from = m_loadedRangeMin;
		}
		if (m_loadedRangeMax - m_displayedRangeMax > difference_to) {
			to = m_displayedRangeMax + difference_to;
		}
		else {
			to = m_loadedRangeMax;
		}
	}
	else if (new_length < old_length) {
		qint64 difference = old_length - new_length;
		qint64 difference_from = difference * (double)(center - m_displayedRangeMin) / old_length;
		qint64 difference_to = difference - difference_from;
		from = m_displayedRangeMin;
		if (from + difference_from < center) {
			from += difference_from;
		}
		to = m_displayedRangeMax;
		if (to - difference_to > center) {
			to -= difference_to;
		}
	}

	showRangeInternal(from, to);
}

void View::setVerticalZoom(double scale)
{
	if (m_enableVerticalZoom) {
		if (scale < m_minimumVerticalZoom) {
			scale = m_minimumVerticalZoom;
		}
		if (scale > m_maximumVerticalZoom) {
			scale = m_maximumVerticalZoom;
		}
		setVerticalZoomInternal(scale);
	}
}

void View::setVerticalZoomInternal(double zoom)
{
	if (m_verticalZoom != zoom) {
		m_verticalZoom = zoom;
		update();
		Q_EMIT verticalZoomChanged();
	}

}
void View::setupVerticalZoom(bool enable, double minimumZoom, double maximumZoom)
{
	double vertical_zoom = m_verticalZoom;

	m_enableVerticalZoom = enable;
	if (!enable) {
		vertical_zoom = 1.0;
		if (m_verticalMove != 0) {
			m_verticalMove = 0;
			computeGeometry();
		}
	}

	m_minimumVerticalZoom = minimumZoom;
	m_maximumVerticalZoom = maximumZoom;

	if (enable && m_verticalZoom < minimumZoom) {
		vertical_zoom = minimumZoom;
	}

	if (enable && m_verticalZoom > maximumZoom) {
		vertical_zoom = maximumZoom;
	}

	setVerticalZoomInternal(vertical_zoom);
}

GraphModel *View::model() const
{
	if (!m_model)
		SHV_EXCEPTION("Model is NULL!");
	return m_model;
}

void View::addSerie(Serie *serie)
{
	if (!m_series.contains(serie)) {
//		if (serie->type() == ValueType::Bool && !serie->boolValue && !serie->serieGroup) {
//			SHV_EXCEPTION(("Bool serie (" + serie->name() + ") must have set boolValue or serie group").toStdString());
//		}
//		for (const Serie *dependent_serie : serie->dependentSeries) {
//			if (dependent_serie->type() == ValueType::Bool && !dependent_serie->boolValue && !dependent_serie->serieGroup) {
//				SHV_EXCEPTION(("Bool serie (" + dependent_serie->name() + ") must have set boolValue or serie group").toStdString());
//			}
//		}
		serie->setParent(this);
		m_series << serie;
		m_connections << connect(serie, &Serie::destroyed, [this, serie]() {
			m_series.removeOne(serie);
			for (int i = 0; i < m_serieBlocks.count(); ++i) {
				QVector<const Serie*> &serie_block = m_serieBlocks[i];
				if (serie_block.contains(serie)) {
					serie_block.removeOne(serie);
					if (serie_block.count() == 0) {
						m_serieBlocks.removeAt(i);
					}
					break;
				}
			}
			for (int i = 0; i < m_graphArea.count(); ++i) {
				GraphArea &area = m_graphArea[i];
				if (area.series.contains(serie)) {
					area.series.removeOne(serie);
				}
			}
			computeGeometry();
			update();
		});
		if (m_serieBlocks.count() == 0) {
			m_serieBlocks.append(QVector<const Serie*>());
		}
		m_serieBlocks.last() << serie;
	}
}

Serie *View::serie(int index)
{
	for (Serie *serie : m_series){
		if (serie->serieIndex() == index){
			return serie;
		}
	}

	SHV_EXCEPTION("GraphView: invalid serie index");
}

void View::splitSeries()
{
	m_serieBlocks.clear();
	for (const Serie *serie : m_series) {
		m_serieBlocks.append(QVector<const Serie*>());
		m_serieBlocks.last() << serie;
	}
	computeGeometry();
	update();
}

void View::unsplitSeries()
{
	m_serieBlocks.clear();
	m_serieBlocks.append(QVector<const Serie*>());
	for (Serie *serie : m_series) {
		m_serieBlocks.last() << serie;
	}
	computeGeometry();
	update();
}

void View::showDependentSeries(bool enable)
{
	settings.showDependent = enable;
	computeGeometry();
	update();
}

std::vector<ValueXInterval> View::selections() const
{
	ValueChange::ValueX start(0);
	ValueChange::ValueX end(0);

	std::vector<ValueXInterval> selections;
	for (const Selection &selection : m_selections) {
		qint64 s_start = selection.start;
		qint64 s_end = selection.end;
		if (s_start > s_end) {
			std::swap(s_start, s_end);
		}
		switch (settings.xAxisType) {
		case ValueType::TimeStamp:
			start.timeStamp = s_start;
			end.timeStamp = s_end;
			break;
		case ValueType::Int:
			start.intValue = s_start;
			end.intValue = s_end;
			break;
		case ValueType::Double:
			start.doubleValue = s_start / m_xValueScale;
			end.doubleValue = s_end / m_xValueScale;
			break;
		default:
			break;
		}
		selections.emplace_back(start, end, settings.xAxisType);
	}

	return selections;
}

ValueXInterval View::loadedRange() const
{
	return ValueXInterval(internalToValueX(m_loadedRangeMin), internalToValueX(m_loadedRangeMax), settings.xAxisType);
}

ValueXInterval View::shownRange() const
{
	return ValueXInterval(internalToValueX(m_displayedRangeMin), internalToValueX(m_displayedRangeMax), settings.xAxisType);
}

void View::addSelection(ValueXInterval selection)
{
	bool overlap = false;

	Selection new_selection;
	new_selection.start = xValue(ValueChange { selection.min, {} });
	new_selection.end = xValue(ValueChange { selection.max, {} });

	for (const Selection &s: m_selections){
		qint64 s_start = s.start;
		qint64 s_end = s.end;

		if (s_start > s_end) {
			std::swap(s_start, s_end);
		}

		if ((new_selection.start <= s_end) && (new_selection.end >= s_start)) {
			overlap = true;
		}
	}

	if (!overlap){
		m_selections << new_selection;
		Q_EMIT selectionsChanged();
		update();
	}
}

void View::clearSelections()
{
	m_selections.clear();
	Q_EMIT selectionsChanged();
	update();
}

void View::setMode(View::Mode mode)
{
	m_mode = mode;
	if (m_model) {
		onModelDataChanged();
	}
}

void View::setDynamicModePrepend(ValueChange::ValueX prepend)
{
	m_dynamicModePrepend = xValue(prepend);
	if (m_mode == Mode::Dynamic && m_model) {
		onModelDataChanged();
	}
}

void View::addPointOfInterest(ValueChange::ValueX position, const QString &comment, const QColor &color)
{
	PointOfInterest *poi = new PointOfInterest(position, comment, color, this);
	addPointOfInterest(poi);
}

void View::addPointOfInterest(PointOfInterest *poi)
{
	if (!m_pointsOfInterest.contains(poi)) {
		poi->setParent(this);
		m_pointsOfInterest << poi;
		m_connections << connect(poi, &PointOfInterest::destroyed, [this, poi](){
			m_pointsOfInterest.removeOne(poi);
			m_poiPainterPaths.remove(poi);
			computeGeometry();
			update();
		});
		computeGeometry();
		update();
	}
}

void View::removePointsOfInterest()
{
	while (m_pointsOfInterest.count()) {
		delete m_pointsOfInterest[0];
	}
}

void View::addBackgroundStripe(BackgroundStripe *stripe)
{
	if (!m_backgroundStripes.contains(stripe)) {
//		if (stripe->type() != BackgroundStripe::Type::Vertical) {
//			SHV_EXCEPTION("View can contain only vertical stripes");
//		}
		stripe->setParent(this);
		m_backgroundStripes << stripe;
		m_connections << connect(stripe, &BackgroundStripe::destroyed, [this, stripe]() {
			m_backgroundStripes.removeOne(stripe);
//			computeGeometry();
			update();
		});
//		computeGeometry();
		update();
	}
}

void View::showBackgroundSerieStripes(bool enable)
{
	if (enable != settings.showSerieBackgroundStripes) {
		settings.showSerieBackgroundStripes = enable;
		update();
	}
}

OutsideSerieGroup *View::addOutsideSerieGroup(const QString &name)
{
	OutsideSerieGroup *group = new OutsideSerieGroup(name, this);
	addOutsideSerieGroup(group);
	return group;
}

void View::addOutsideSerieGroup(OutsideSerieGroup *group)
{
	if (!m_outsideSeriesGroups.contains(group)) {
		group->setParent(this);
		m_outsideSeriesGroups << group;
		m_connections << connect(group, &OutsideSerieGroup::destroyed, [this, group](){
			m_outsideSeriesGroups.removeOne(group);
			if (!group->isHidden()) {
				computeGeometry();
				update();
			}
		});
		if (!group->isHidden()) {
			computeGeometry();
			update();
		}
	}
}

void View::setViewTimezone(const QTimeZone &tz)
{
	if (settings.viewTimeZone != tz) {
		settings.viewTimeZone = tz;
		update();
	}
}

void View::setLoadedRange(const ValueChange::ValueX &min, const ValueChange::ValueX &max)
{
	m_displayedRangeMin = m_loadedRangeMin = xValue(min);
	m_displayedRangeMax = m_loadedRangeMax = xValue(max);

	computeGeometry();
	update();
}

void View::showRangeInternal(qint64 from, qint64 to)
{
	if (from < m_loadedRangeMin || from > to) {
		from = m_loadedRangeMin;
	}
	if (to > m_loadedRangeMax) {
		to = m_loadedRangeMax;
	}
	if (from == m_displayedRangeMin && to == m_displayedRangeMax) {
		return;
	}
	m_displayedRangeMin = from;
	m_displayedRangeMax = to;
	computeDataRange();
	if (settings.rangeSelector.show) {
		computeRangeSelectorPosition();
	}
	update();
	Q_EMIT shownRangeChanged();
}

void View::paintYAxisDescription(QPainter *painter, const GraphArea &area)
{
	const Settings::Axis &axis = area.switchAxes ? settings.y2Axis : settings.yAxis;
	painter->save();
	QPen pen(axis.color);
	painter->setPen(pen);
	painter->setFont(axis.description.font);
	painter->translate(area.yAxisDescriptionRect.x(), area.yAxisDescriptionRect.bottom());
	painter->rotate(270.0);
	painter->drawText(QRect(0, 0, area.yAxisDescriptionRect.height(), area.yAxisDescriptionRect.width()), axis.description.text, QTextOption(Qt::AlignCenter));
	painter->restore();
}

void View::paintY2AxisDescription(QPainter *painter, const GraphArea &area)
{
	painter->save();
	QPen pen(settings.y2Axis.color);
	painter->setPen(pen);
	painter->setFont(settings.y2Axis.description.font);
	painter->translate(area.y2AxisDescriptionRect.right(), area.y2AxisDescriptionRect.y());
	painter->rotate(90.0);
	painter->drawText(QRect(0, 0, area.y2AxisDescriptionRect.height(), area.y2AxisDescriptionRect.width()), settings.y2Axis.description.text, QTextOption(Qt::AlignCenter));
	painter->restore();
}

void View::paintYAxis(QPainter *painter, const GraphArea &area)
{
	painter->save();
	QPen pen(settings.yAxis.color);
	pen.setWidth(settings.yAxis.lineWidth);
	painter->setPen(pen);
	painter->drawLine(area.graphRect.topLeft(), area.graphRect.bottomLeft());
	painter->restore();
}

void View::paintY2Axis(QPainter *painter, const GraphArea &area)
{
	painter->save();
	QPen pen(settings.y2Axis.color);
	pen.setWidth(settings.y2Axis.lineWidth);
	painter->setPen(pen);
	painter->drawLine(area.graphRect.topRight(), area.graphRect.bottomRight());
	painter->restore();
}

void View::paintXAxisLabels(QPainter *painter)
{
	painter->save();
	QPen pen(settings.xAxis.color);
	painter->setPen(pen);
	painter->setFont(settings.xAxis.labelFont);
	QString time_format = "HH:mm:ss";
	if (m_displayedRangeMax - m_displayedRangeMin < 30000) {
		time_format += ".zzz";
	}
	int label_width = painter->fontMetrics().width(time_format);

	qint64 ts;
	QString x_value_label;

	const GraphArea &area = m_graphArea[0];
	for (double x = area.graphRect.x(); x < area.graphRect.right(); x += m_verticalGridDistance) {
		ts = widgetPositionToXValue(x);
		x_value_label = xValueString(ts, time_format);
		painter->drawText(x - label_width / 2, m_xAxisLabelRect.y(), label_width, m_xAxisLabelRect.height(), Qt::AlignCenter, x_value_label);
	}
	if (width() - area.graphRect.right()  > label_width / 2) {
		x_value_label = xValueString(m_displayedRangeMax, time_format);
		painter->drawText(area.graphRect.right() + (settings.margin.right / 2) - label_width, m_xAxisLabelRect.y(), label_width, m_xAxisLabelRect.height(), Qt::AlignCenter, x_value_label);
	}
	painter->restore();
}

void View::paintYAxisLabels(QPainter *painter, const View::GraphArea &area)
{
	if (!area.switchAxes) {
		paintYAxisLabels(painter, settings.yAxis, m_yAxisShownDecimalPoints, area.yAxisLabelRect, Qt::AlignVCenter | Qt::AlignRight);
	}
	else {
		paintYAxisLabels(painter, settings.y2Axis, m_y2AxisShownDecimalPoints, area.yAxisLabelRect, Qt::AlignVCenter | Qt::AlignRight);
	}
}

void View::paintY2AxisLabels(QPainter *painter, const GraphArea &area)
{
	paintYAxisLabels(painter, settings.y2Axis, m_y2AxisShownDecimalPoints, area.y2AxisLabelRect, Qt::AlignVCenter | Qt::AlignLeft);
}

void View::paintYAxisLabels(QPainter *painter, const Settings::Axis &axis, int shownDecimalPoints, const QRect &rect, int align)
{
	if (m_horizontalGridDistance < 1.0) {
		return;
	}
	painter->save();
	QPen pen(axis.color);
	painter->setPen(pen);
	painter->setFont(axis.labelFont);
	int font_height = painter->fontMetrics().lineSpacing();

	double y_scale = (double)((axis.rangeMax / m_verticalZoom) - (axis.rangeMin / m_verticalZoom)) / rect.height();

	double x_axis_position = rect.y() + m_verticalMove + (axis.rangeMax / m_verticalZoom)/ y_scale;


	QByteArray format;
	format = "%." + QByteArray::number(shownDecimalPoints) + 'f';

	double y = rect.top() + (m_verticalMove % (int)m_horizontalGridDistance);
	if (y < rect.top()) {
		y += m_horizontalGridDistance;
	}

	while (y <= rect.bottom() + 1) {
		QString value;
		value.sprintf(format.constData(), (x_axis_position - y) * y_scale);
		painter->drawText(rect.x(), (int)y - font_height / 2, rect.width(), font_height, align, value);
		y += m_horizontalGridDistance;
	}
	painter->restore();

}

void View::paintXAxisDescription(QPainter *painter)
{
	painter->save();
	QPen pen(settings.xAxis.color);
	painter->setPen(pen);
	painter->setFont(settings.xAxis.description.font);
	painter->drawText(m_xAxisDescriptionRect, settings.xAxis.description.text, QTextOption(Qt::AlignCenter));

	painter->restore();
}

void View::paintXAxis(QPainter *painter, const GraphArea &area)
{
	if (area.xAxisPosition >= area.graphRect.top() && area.xAxisPosition <= area.graphRect.bottom()) {
		painter->save();
		QPen pen(settings.xAxis.color);
		pen.setWidth(settings.xAxis.lineWidth);
		painter->setPen(pen);

		painter->drawLine(area.graphRect.x(), area.xAxisPosition, area.graphRect.right(), area.xAxisPosition);

		painter->restore();
	}
}

void View::paintVerticalGrid(QPainter *painter, const GraphArea &area)
{
	painter->save();
	painter->setPen(settings.verticalGrid.color);
	for (double x = area.graphRect.x() + m_verticalGridDistance; x < area.graphRect.right(); x += m_verticalGridDistance) {
		painter->drawLine(x, area.graphRect.y(), x, area.graphRect.bottom());
	}
	if (!settings.y2Axis.show) {
		painter->drawLine(area.graphRect.topRight(), area.graphRect.bottomRight());
	}
	painter->restore();
}

void View::paintHorizontalGrid(QPainter *painter, const GraphArea &area)
{
	painter->save();
	painter->setPen(settings.horizontalGrid.color);
	if (m_horizontalGridDistance > 1.0) {
		double y = area.graphRect.bottom() + (m_verticalMove % (int)m_horizontalGridDistance);
		if (y > area.graphRect.bottom()) {
			y -= m_horizontalGridDistance;
		}
		while (y > area.graphRect.y()) {
			painter->drawLine(area.graphRect.x(), y, area.graphRect.right(), y);
			y -= m_horizontalGridDistance;
		}
	}
	painter->drawLine(area.graphRect.topLeft(), area.graphRect.topRight());
	painter->restore();
}

void View::paintRangeSelector(QPainter *painter)
{
	painter->save();

	painter->translate(m_rangeSelectorRect.topLeft());

	for (int i = 0; i < m_series.count(); ++i) {
		const Serie *serie = m_series[i];
		if (!serie->isHidden()) {
			int x_axis_position;
			if (serie->relatedAxis() == Serie::YAxis::Y1) {
				double x_scale = (double)(settings.yAxis.rangeMax - settings.yAxis.rangeMin) / m_rangeSelectorRect.height();
				x_axis_position = settings.yAxis.rangeMax / x_scale;
			}
			else {
				double x_scale = (double)(settings.y2Axis.rangeMax - settings.y2Axis.rangeMin) / m_rangeSelectorRect.height();
				x_axis_position = settings.y2Axis.rangeMax / x_scale;
			}
			QPen pen(settings.rangeSelector.color);
			pen.setWidth(settings.rangeSelector.lineWidth);
			paintSerie(painter, m_rangeSelectorRect, 1.0, x_axis_position, serie, m_loadedRangeMin, m_loadedRangeMax, pen,
					   Serie::Fill(Serie::Fill::Type::Gradient, settings.rangeSelector.color), m_rangeSelectorRect.height());
			break;
		}
	}

	painter->restore();
	painter->save();

	QColor shade_color = settings.backgroundColor;
	shade_color.setAlpha(160);
	if (m_leftRangeSelectorPosition > m_rangeSelectorRect.x()) {
		painter->fillRect(QRect(m_rangeSelectorRect.topLeft(), QPoint(m_leftRangeSelectorPosition, m_rangeSelectorRect.bottom())), shade_color);
	}
	if (m_rightRangeSelectorPosition < m_rangeSelectorRect.right()) {
		painter->fillRect(QRect(QPoint(m_rightRangeSelectorPosition, m_rangeSelectorRect.top()), m_rangeSelectorRect.bottomRight()), shade_color);
	}
	QPen pen(Qt::gray);
	pen.setWidth(1);
	painter->setPen(pen);
	QRect selection(QPoint(m_leftRangeSelectorPosition, m_rangeSelectorRect.top()), QPoint(m_rightRangeSelectorPosition, m_rangeSelectorRect.bottom()));
	if (m_leftRangeSelectorPosition > m_rangeSelectorRect.x()) {
		painter->drawLine(m_rangeSelectorRect.topLeft(), m_rangeSelectorRect.bottomLeft());
		painter->drawLine(m_rangeSelectorRect.bottomLeft(), selection.bottomLeft());
	}
	if (m_rightRangeSelectorPosition < m_rangeSelectorRect.right()) {
		painter->drawLine(m_rangeSelectorRect.topRight(), m_rangeSelectorRect.bottomRight());
		painter->drawLine(selection.bottomRight(), m_rangeSelectorRect.bottomRight());
	}
	pen.setColor(Qt::black);
	pen.setWidth(2);
	painter->setPen(pen);
	if (m_leftRangeSelectorPosition > m_rangeSelectorRect.x()) {
		painter->drawLine(m_rangeSelectorRect.topLeft(), selection.topLeft());
	}
	painter->drawLine(selection.topLeft(), selection.bottomLeft());
	painter->drawLine(selection.bottomLeft(), selection.bottomRight());
	painter->drawLine(selection.topRight(), selection.bottomRight());
	if (m_rightRangeSelectorPosition < m_rangeSelectorRect.right()) {
		painter->drawLine(selection.topRight(), m_rangeSelectorRect.topRight());
	}

	painter->restore();
}

void View::paintSeries(QPainter *painter, const GraphArea &area)
{
	painter->save();

	painter->translate(area.graphRect.topLeft());

	for (int i = 0; i < area.series.count(); ++i) {
		const Serie *serie = area.series[i];
		int x_axis_position;
		if (serie->relatedAxis() == Serie::YAxis::Y1 || area.switchAxes) {
			x_axis_position = area.xAxisPosition - area.graphRect.top();
		}
		else {
			x_axis_position = area.x2AxisPosition - area.graphRect.top();
		}
		QPen pen(serie->color());
		if (!serie->isHidden() && settings.showDependent) {
			for (const Serie *dependent_serie : serie->dependentSeries()) {
				pen.setWidth(dependent_serie->lineWidth());
				paintSerie(painter, area.graphRect, m_verticalZoom, x_axis_position, dependent_serie, m_displayedRangeMin, m_displayedRangeMax, pen, dependent_serie->fill());
			}
		}
		pen.setWidth(serie->lineWidth());
		paintSerie(painter, area.graphRect, m_verticalZoom, x_axis_position, serie, m_displayedRangeMin, m_displayedRangeMax, pen, serie->fill());
	}
	painter->restore();
}

void View::paintSerie(QPainter *painter, const QRect &rect, double vertical_zoom, int x_axis_position, const Serie *serie, qint64 min, qint64 max, const QPen &pen, const Serie::Fill &fill_rect, int fill_base)
{
	if (fill_base == -1) {
		fill_base = x_axis_position;
	}
	if (!serie->isHidden()) {
		if (serie->type() == ValueType::Bool) {
			if (!serie->serieGroup()) {
				paintBoolSerie(painter, rect, vertical_zoom, x_axis_position, serie, min, max, pen, fill_rect, fill_base);
			}
		}
		else {
			paintValueSerie(painter, rect, vertical_zoom, x_axis_position, serie, min, max, pen, fill_rect, fill_base);
		}
	}
}

void View::paintBoolSerie(QPainter *painter, const QRect &rect, double vertical_zoom, int x_axis_position, const Serie *serie, qint64 min, qint64 max, const QPen &pen, const Serie::Fill &fill_rect, int fill_base)
{
	if (serie->lineType() == Serie::LineType::TwoDimensional) {
		SHV_EXCEPTION("Cannot paint two dimensional bool serie");
	}

	double y_scale = 0.0;
	if (serie->relatedAxis() == Serie::YAxis::Y1) {
		y_scale = (double)((settings.yAxis.rangeMax / vertical_zoom) - (settings.yAxis.rangeMin / vertical_zoom)) / rect.height();
	}
	else if (serie->relatedAxis() == Serie::YAxis::Y2) {
		y_scale = (double)((settings.y2Axis.rangeMax / vertical_zoom) - (settings.y2Axis.rangeMin / vertical_zoom)) / rect.height();
	}
	painter->setPen(pen);

	int y_true_line_position = x_axis_position - serie->boolValue() / y_scale;
	paintBoolSerieAtPosition(painter, rect, y_true_line_position, serie, min, max, fill_rect, fill_base);
}

void View::paintBoolSerieAtPosition(QPainter *painter, const QRect &rect, int y_position, const Serie *serie, qint64 min, qint64 max, const Serie::Fill &fill_rect, int fill_base)
{
	const SerieData &data = serie->serieModelData(this);
	if (data.size() == 0) {
		return;
	}
	double x_scale = (double)(max - min) / rect.width();

	auto begin = data.cbegin();
	if (min > m_loadedRangeMin) {
		begin = findMinYValue(data.cbegin(), data.cend(), min);
	}
	auto end = data.cend();
	if (max < m_loadedRangeMax) {
		end = findMaxYValue(begin, data.cend(), max);
	}

	QPolygon polygon;
	polygon << QPoint(0, fill_base);

	for (auto it = begin; it != end; ++it) {
		ValueChange::ValueY value_y = formattedSerieValue(serie, it);
		if (value_y.boolValue) {
			int begin_line = (xValue(*it) - min) / x_scale;
			if (begin_line < 0) {
				begin_line = 0;
			}
			int end_line;
			if (it + 1 == end) {
				end_line = rect.width();
			}
			else {
				++it;
				end_line = (xValue(*it) - min) / x_scale;
			}
			painter->drawLine(begin_line, y_position, end_line, y_position);
			if (fill_rect.type() != Serie::Fill::Type::None) {
				polygon << QPoint(begin_line, rect.height()) << QPoint(begin_line, y_position)
						<< QPoint(end_line, y_position) << QPoint(end_line, rect.height());
			}
		}
	}
	if (fill_rect.type() != Serie::Fill::Type::None) {
		polygon << QPoint(rect.width(), fill_base);
		fillSerie(painter, polygon, serie, fill_rect);
	}
}

int View::fitPointInRect(int y, const QRect &rect)
{
	if (y <= 0) {
		return 1;
	}
	if (y >= rect.height()) {
		return rect.height() - 1;
	}
	return y;
}

QPainterPath View::paintTopTriangle(const QRectF &rect)
{
	QPainterPath path;
	path.moveTo(rect.left() + (rect.width() / 2), rect.top());
	path.lineTo(rect.bottomLeft());
	path.lineTo(rect.bottomRight());
	path.lineTo(rect.left() + (rect.width() / 2), rect.top());
	return path;
};

QPainterPath View::paintBottomTriangle(const QRectF &rect)
{
	QPainterPath path;
	path.moveTo(rect.left() + (rect.width() / 2), rect.bottom());
	path.lineTo(rect.topLeft());
	path.lineTo(rect.topRight());
	path.lineTo(rect.left() + (rect.width() / 2), rect.bottom());
	return path;
}

void View::paintValueSerie(QPainter *painter, const QRect &rect, double vertical_zoom, int x_axis_position, const Serie *serie, qint64 min, qint64 max, const QPen &pen, const Serie::Fill &fill_rect, int fill_base)
{
	const SerieData &data = serie->serieModelData(this);
	if (data.size() == 0) {
		return;
	}

	double x_scale = (double)(max - min) / graphWidth();

	double y_scale = 0.0;
	if (serie->relatedAxis() == Serie::YAxis::Y1) {
		y_scale = (double)((settings.yAxis.rangeMax / vertical_zoom) - (settings.yAxis.rangeMin / vertical_zoom)) / rect.height();
	}
	else if (serie->relatedAxis() == Serie::YAxis::Y2) {
		y_scale = (double)((settings.y2Axis.rangeMax / vertical_zoom) - (settings.y2Axis.rangeMin / vertical_zoom)) / rect.height();
	}
	painter->setPen(pen);

	SerieData::const_iterator begin;
	SerieData::const_iterator end;
	if (min == m_loadedRangeMin) {
		begin = data.cbegin();
	}
	else if (min == m_displayedRangeMin) {
		begin = serie->displayedDataBegin;
	}
	else {
		begin = findMinYValue(data.cbegin(), data.cend(), min);
	}
	if (max == m_loadedRangeMax) {
		end = data.cend();
	}
	else if (max == m_displayedRangeMax) {
		end = serie->displayedDataEnd;
	}
	else {
		end = findMaxYValue(begin, data.cend(), max);
	}

	if (begin == end) {
		return;
	}
	QPoint first_point;
	ValueChange::ValueY first_value_y = formattedSerieValue(serie, begin);
	int first_point_x = (m_dataRangeMin - min) / x_scale;
	if (first_point_x < 0) {
		first_point_x = 0;
	}
	first_point = QPoint(first_point_x, x_axis_position - (first_value_y.toDouble(serie->type()) / y_scale));

	int max_on_first = first_point.y();
	int min_on_first = first_point.y();
	int last_on_first = first_point.y();

	QPolygon polygon;
	polygon << QPoint(first_point.x(), fitPointInRect(fill_base, rect));
	polygon << QPoint(first_point.x(), fitPointInRect(first_point.y(), rect));

	auto drawTriangleLine = [this, &serie, &painter, &rect](int first_point, int last_point, TriangleLineType line_type) {
		for (int i = first_point; i < last_point; i += 5) {
			QPainterPath path;
			if (line_type == TriangleLineType::Top) {
				path = paintTopTriangle(QRectF(i, 0, 4, 4));
			}
			else {
				path = paintBottomTriangle(QRectF(i, rect.height() - 4, 4, 4));
			}
			painter->fillPath(path, serie->color());
		}
	};

	QPoint last_point(0, 0);
	for (auto it = begin + 1; it != end; ++it) {
		ValueChange::ValueY value_change = formattedSerieValue(serie, it);
		qint64 x_value = xValue(*it);
		last_point = QPoint((x_value - min) / x_scale, x_axis_position - (value_change.toDouble(serie->type()) / y_scale));

		if (last_point.x() == first_point.x()) {
			if (max_on_first < last_point.y()) {
				max_on_first = last_point.y();
			}
			if (min_on_first > last_point.y()) {
				min_on_first = last_point.y();
			}
			last_on_first = last_point.y();
		}
		else { //if (last_point.x() > first_point.x()) {
			if (max_on_first > first_point.y() || min_on_first < first_point.y()) {
				int y1 = fitPointInRect(max_on_first, rect);
				int y2 = fitPointInRect(min_on_first, rect);
				if (y1 != y2 || (max_on_first > 0 && max_on_first < rect.height() && min_on_first > 0 && min_on_first < rect.height())) {
					painter->drawLine(first_point.x(), y1, first_point.x(), y2);
				}
			}
			if (last_on_first > 0 && last_on_first < rect.height()) {
				painter->drawLine(first_point.x(), last_on_first, last_point.x(), last_on_first);
			}
			else if (settings.inicateOutOfRectSerie) {
				drawTriangleLine(first_point.x(), last_point.x(), (last_on_first <= 0) ? TriangleLineType::Top : TriangleLineType::Bottom);
			}
			int y1 = fitPointInRect(last_on_first, rect);
			int y2 = fitPointInRect(last_point.y(), rect);
			if (y1 != y2 || (last_on_first > 0 && last_on_first < rect.height() && last_point.y() > 0 && last_point.y() < rect.height())) {
				painter->drawLine(last_point.x(), y1, last_point.x(), y2);
			}

			if (fill_rect.type() != Serie::Fill::Type::None) {
				polygon << QPoint(first_point.x(), y1);
				polygon << QPoint(last_point.x(), y1);
				polygon << QPoint(last_point.x(), y2);
			}

			first_point = last_point;
			max_on_first = first_point.y();
			min_on_first = first_point.y();
			last_on_first = first_point.y();
		}
	}

	int rect_last_point = graphWidth();
	if (last_point.x() == 0) { //last point was not found
		if (first_point.y() > 0 && first_point.y() < rect.height()) {
			painter->drawLine(first_point.x(), first_point.y(), rect_last_point, first_point.y());
		}
		else if (settings.inicateOutOfRectSerie) {
			drawTriangleLine(first_point.x(), rect_last_point, (first_point.y() <= 0) ? TriangleLineType::Top : TriangleLineType::Bottom);
		}
		polygon << QPoint(rect_last_point, fitPointInRect(first_point.y(), rect));
	}
	else if (last_point.x() < rect_last_point) {
		if (m_dataRangeMax < m_loadedRangeMax) {
			rect_last_point = xValueToRectPosition(m_dataRangeMax);
		}
		if (last_point.y() > 0 && last_point.y() < rect.height()) {
			painter->drawLine(last_point.x(), last_point.y(), rect_last_point, last_point.y());
		}
		else if (settings.inicateOutOfRectSerie) {
			drawTriangleLine(last_point.x(), rect_last_point, (last_point.y() <= 0) ? TriangleLineType::Top : TriangleLineType::Bottom);
		}
		polygon << QPoint(last_point.x(), fitPointInRect(last_point.y(), rect));
		polygon << QPoint(rect_last_point, fitPointInRect(last_point.y(), rect));
	}

	if (fill_rect.type() != Serie::Fill::Type::None) {
		polygon << QPoint(rect.width(), fitPointInRect(fill_base, rect));
		fillSerie(painter, polygon, serie, fill_rect);
	}
}

void View::fillSerie(QPainter *painter, const QPolygon &polygon, const Serie *serie, const Serie::Fill &fill_rect)
{
	QPainterPath path;
	path.addPolygon(polygon);
	path.closeSubpath();
	Serie::Fill::Type type = fill_rect.type();
	if (type == Serie::Fill::Type::SerieColor || type == Serie::Fill::Type::Color) {
		QColor color;
		if (type == Serie::Fill::Type::SerieColor) {
			color = serie->color();
			color.setAlpha(50);
		}
		else {
			color = fill_rect.color();
		}
		painter->fillPath(path, color);
	}
	else if (type == Serie::Fill::Type::SerieGradient || type == Serie::Fill::Type::Gradient) {
		QRect bounding_rect = polygon.boundingRect();
		QLinearGradient gradient(bounding_rect.topLeft(), bounding_rect.bottomLeft());
		QColor color;
		if (type == Serie::Fill::Type::SerieGradient) {
			color = serie->color();
		}
		else {
			color = fill_rect.color();
		}
		gradient.setColorAt(0, color.lighter());
		gradient.setColorAt(1, color);
		painter->fillPath(path, gradient);
	}
}

void View::paintSelection(QPainter *painter, const GraphArea &area, const Selection &selection, const QColor &color)
{
	painter->save();

	QColor fill_color = color;
	fill_color.setAlpha(60);
	QColor select_color = color;
	select_color.setAlpha(170);
	QPen pen;
	pen.setColor(select_color);
	pen.setWidth(2);
	painter->setPen(pen);

	qint64 start = selection.start;
	qint64 end = selection.end;
	bool draw_left_line = true;
	bool draw_right_line = true;
	if (start > end) {
		std::swap(start, end);
	}
	if (start < m_displayedRangeMax && end > m_displayedRangeMin) {
		if (start < m_displayedRangeMin) {
			start = m_displayedRangeMin;
			draw_left_line = false;
		}
		if (end > m_displayedRangeMax) {
			end = m_displayedRangeMax;
			draw_right_line = false;
		}
		int r_start = xValueToWidgetPosition(start);
		int r_end = xValueToWidgetPosition(end);
		if (draw_left_line) {
			painter->drawLine(r_start, area.graphRect.y(), r_start, area.graphRect.bottom());
		}
		if (draw_right_line) {
			painter->drawLine(r_end, area.graphRect.y(), r_end, area.graphRect.bottom());
		}
		painter->fillRect(r_start, area.graphRect.y(), r_end - r_start, area.graphRect.height(), fill_color);
	}

	painter->restore();
}

void View::paintSelections(QPainter *painter, const GraphArea &area)
{
	for (const Selection &selection : m_selections) {
		paintSelection(painter, area, selection, palette().color(QPalette::Highlight));
	}
	paintSelection(painter, area, m_zoomSelection, Qt::green);
}

void View::paintSerieList(QPainter *painter)
{
	painter->save();

	painter->setFont(settings.serieList.font);

	int space = 20;
	QList<int> widths;
	int total_width = 0;
	for (const Serie *serie : m_series) {
		int width = painter->fontMetrics().width(serie->name());
		total_width += width;
		widths << width;
	}
	int square_width = painter->fontMetrics().width("X");
	total_width += (widths.count() * 3 * square_width) + ((widths.count() - 1) * space);

	int x = (m_serieListRect.width() - total_width) / 2;
	if (x < 0) {
		x = 0;  //idealne rozdelit na 2 radky
	}
	m_seriesListRect.clear();
	int label_height = painter->fontMetrics().lineSpacing();
	for (int i = 0; i < m_series.count(); ++i) {
		const Serie *serie = m_series[i];
		if (!serie->isHidden()) {
			painter->setPen(palette().color(QPalette::Active, QPalette::Text));
		}
		else {
			painter->setPen(palette().color(QPalette::Disabled, QPalette::Text));
		}
		m_seriesListRect << QRect(m_serieListRect.x() + x, m_serieListRect.top() + ((m_serieListRect.height() - label_height) / 2), 3 * square_width + widths[i], label_height);
		QRect square_rect(m_serieListRect.x() + x, m_serieListRect.top() + ((m_serieListRect.height() - square_width) / 2), square_width, square_width);
		painter->fillRect(square_rect, serie->isHidden() ? serie->color().lighter(180) : serie->color());
		painter->drawRect(square_rect);
		x += 2 * square_width;
		painter->drawText(m_serieListRect.x() + x, m_serieListRect.top(), widths[i], m_serieListRect.height(), Qt::AlignVCenter | Qt::AlignLeft, serie->name());
		x += widths[i] + space;
	}
	painter->restore();
}

void View::paintCrossLine(QPainter *painter, const GraphArea &area)
{
	if (!hasVisibleSeries()) {
		return;
	}
	painter->save();

	painter->setPen(Qt::DashLine);
	painter->drawLine(m_currentPosition + area.graphRect.x(), area.graphRect.top(), m_currentPosition + area.graphRect.x(), area.graphRect.bottom());

	painter->restore();
}

void View::paintLegend(QPainter *painter)
{
	Q_UNUSED(painter)
}

void View::paintCurrentPosition(QPainter *painter, const GraphArea &area, const Serie *serie, qint64 current)
{
	if (serie->isShowCurrent() && !serie->serieGroup()) {
		auto begin = findMinYValue(serie->displayedDataBegin, serie->displayedDataEnd, current);
		if (begin == shv::gui::SerieData::const_iterator()) {
			return;
		}
		int y_position = yPosition(formattedSerieValue(serie, begin), serie, area);
		if (area.xAxisPosition - y_position - 3 >= area.graphRect.top() && area.xAxisPosition - y_position + 3 <= area.graphRect.bottom()) {
			QPainterPath path;
			if (serie->relatedAxis() == Serie::YAxis::Y1 || area.switchAxes) {
				path.addEllipse(m_currentPosition + area.graphRect.x() - 3, area.xAxisPosition - y_position - 3, 6, 6);
			}
			else {
				path.addEllipse(m_currentPosition + area.graphRect.x() - 3, area.x2AxisPosition - y_position - 3, 6, 6);
			}
			painter->fillPath(path, serie->color());
		}
	}

}

int View::yPosition(ValueChange::ValueY value, const Serie *serie, const GraphArea &area)
{
	double range;
	if (serie->relatedAxis() == Serie::YAxis::Y1) {
		range = (settings.yAxis.rangeMax / m_verticalZoom) - (settings.yAxis.rangeMin / m_verticalZoom);
	}
	else {
		range = (settings.y2Axis.rangeMax / m_verticalZoom) - (settings.y2Axis.rangeMin / m_verticalZoom);
	}
	double scale = range / area.graphRect.height();
	int y_position = 0;
	if (serie->type() == ValueType::Double) {
		y_position = value.doubleValue / scale;
	}
	else if (serie->type() == ValueType::Int) {
		y_position = value.intValue / scale;
	}
	else if (serie->type() == ValueType::Bool) {
		y_position = value.boolValue ? (serie->boolValue() / scale) : 0;
	}
	return y_position;
}

void View::unionLastSelection()
{
	Selection &last_selection = m_selections.last();

	int i = 0;
	while (i < m_selections.count()-1){
		Selection &s = m_selections[i];

		if ((last_selection.start <= s.end) && (last_selection.end >= s.start)){
			last_selection = {qMin(last_selection.start, s.start), qMax(last_selection.end, s.end)};
			m_selections.removeAt(i);
		}
		else{
			i++;
		}
	}

	Q_EMIT selectionsChanged();
	update();
}

void View::paintPointsOfInterest(QPainter *painter, const GraphArea &area)
{
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);

	for (PointOfInterest *poi : m_pointsOfInterest) {
		paintPointOfInterest(painter, area, poi);
	}

	painter->restore();
}

void View::paintPointOfInterest(QPainter *painter, const GraphArea &area, PointOfInterest *poi)
{
	if (poi->type() == PointOfInterest::Type::Vertical) {
		paintPointOfInterestVertical(painter, area, poi);
	}
	else {
		paintPointOfInterestPoint(painter, area, poi);
	}
}

void View::paintPointOfInterestVertical(QPainter *painter, const GraphArea &area, PointOfInterest *poi)
{
	int pos = xValueToWidgetPosition(xValue(poi->position().valueX));
	if (pos >= area.graphRect.left() && pos <= area.graphRect.right()) {

		QPen pen(poi->color());
		pen.setStyle(Qt::PenStyle::DashLine);
		painter->setPen(pen);
		painter->drawLine(pos, area.graphRect.top(), pos, area.graphRect.bottom());

		if (&area == &m_graphArea[0]) {
			QPainterPath painter_path = createPoiPath(pos - (POI_SYMBOL_WIDTH / 2), area.graphRect.top() - POI_SYMBOL_HEIGHT - 2);
			painter->drawLine(pos, area.graphRect.top() - 2, pos, area.graphRect.top());
			painter->fillPath(painter_path, poi->color());
			painter->drawPath(painter_path);
			m_poiPainterPaths[poi] = painter_path;
			QPainterPath circle_path;
			circle_path.addEllipse(pos - (POI_SYMBOL_WIDTH / 2) + 2, area.graphRect.top() - POI_SYMBOL_HEIGHT, POI_SYMBOL_WIDTH - 4, POI_SYMBOL_WIDTH - 4);
			painter->fillPath(circle_path, Qt::white);
		}
	}
}

void View::paintPointOfInterestPoint(QPainter *painter, const GraphArea &area, PointOfInterest *poi)
{
	if (m_loadedRangeMax - m_loadedRangeMin == 0LL) {
		return;
	}

	const Serie *serie = poi->serie();
	if (serie && !area.series.contains(serie)) {
		return;
	}
	if (serie && serie->isHidden()) {
		return;
	}
	int x_pos = xValueToWidgetPosition(xValue(poi->position().valueX));
	if (x_pos >= area.graphRect.left() && x_pos <= area.graphRect.right()) {
		ValueChange::ValueY value = serie->valueFormatter() ? serie->valueFormatter()(poi->position()) : poi->position().valueY;

		int y_pos = yPosition(value, serie, area);
		if (serie->relatedAxis() == Serie::YAxis::Y1 || area.switchAxes) {
			y_pos = area.xAxisPosition - y_pos;
		}
		else {
			y_pos = area.x2AxisPosition - y_pos;
		}
		if (y_pos - (POI_SYMBOL_WIDTH / 2) >= area.graphRect.top() && y_pos + (POI_SYMBOL_WIDTH / 2) <= area.graphRect.bottom()) {
			QPainterPath circle_path;
			circle_path.addEllipse(x_pos - (POI_SYMBOL_WIDTH / 2), y_pos - (POI_SYMBOL_WIDTH / 2), POI_SYMBOL_WIDTH, POI_SYMBOL_WIDTH);
			if (serie) {
				painter->fillPath(circle_path, serie->color());
			}
			else {
				painter->fillPath(circle_path, poi->color());
			}
		}
	}
}

void View::paintSerieBackgroundStripes(QPainter *painter, const View::GraphArea &area)
{
	painter->save();
	for (int i = 0; i < area.series.count(); ++i) {
		const Serie *serie = area.series[i];
		if (!serie->isHidden()) {
			const QVector<BackgroundStripe*> &stripes = serie->backgroundStripes();
			for (const BackgroundStripe *stripe : stripes) {
				if (stripe->type() == BackgroundStripe::Type::Vertical) {
					paintSerieVerticalBackgroundStripe(painter, area, serie, stripe);
				}
				else {
					paintSerieHorizontalBackgroundStripe(painter, area, serie, stripe);
				}
			}
		}
	}
	painter->restore();
}

void View::paintSerieVerticalBackgroundStripe(QPainter *painter, const View::GraphArea &area, const Serie *serie, const BackgroundStripe *stripe)
{
	Q_UNUSED(painter);
	Q_UNUSED(area);
	Q_UNUSED(serie);
	Q_UNUSED(stripe);
	SHV_EXCEPTION("Serie vertical background stripes are not implemented yet");
}

void View::paintSerieHorizontalBackgroundStripe(QPainter *painter, const View::GraphArea &area, const Serie *serie, const BackgroundStripe *stripe)
{
	if (area.graphRect.height() <= 0) {
		return;
	}
	double range;
	if (serie->relatedAxis() == Serie::YAxis::Y1) {
		range = (settings.yAxis.rangeMax / m_verticalZoom) - (settings.yAxis.rangeMin / m_verticalZoom);
	}
	else {
		range = (settings.y2Axis.rangeMax / m_verticalZoom) - (settings.y2Axis.rangeMin / m_verticalZoom);
	}
	double scale = range / area.graphRect.height();

	QColor stripe_color = serie->color();
	stripe_color.setAlpha(30);

	int min = 0;
	int max = 0;
	if (serie->type() == ValueType::Double) {
		min = stripe->min().valueY.doubleValue / scale;
		max = stripe->max().valueY.doubleValue / scale;
	}
	else if (serie->type() == ValueType::Int) {
		min = stripe->min().valueY.intValue / scale;
		max = stripe->max().valueY.intValue / scale;
	}
	else if (serie->type() == ValueType::Bool) {
		SHV_EXCEPTION("GraphView: Cannot paint background stripe for bool serie");
	}
	int max_position = area.xAxisPosition - max;
	int min_position = area.xAxisPosition - min;
	bool max_outline_visible = true;
	bool min_outline_visible = true;
	if (max_position < area.graphRect.top()) {
		max_position = area.graphRect.top();
		max_outline_visible = false;
	}
	if (min_position > area.graphRect.bottom()) {
		min_position = area.graphRect.bottom();
		min_outline_visible = false;
	}
	if (max - min > 0 && min_position > area.graphRect.top() && max_position < area.graphRect.bottom()) {
		painter->fillRect(area.graphRect.x(), max_position, area.graphRect.width(), min_position - max_position, stripe_color);
		if (stripe->outLineType() != BackgroundStripe::OutlineType::No) {
			QColor outline_color = serie->color();
			outline_color.setAlpha(70);
			painter->setPen(QPen(outline_color, 2.0));
			if (max_outline_visible &&
				(stripe->outLineType() == BackgroundStripe::OutlineType::Max ||
				stripe->outLineType() == BackgroundStripe::OutlineType::Both)) {
				painter->drawLine(area.graphRect.x(), max_position, area.graphRect.right(), max_position);
			}
			if (min_outline_visible &&
				(stripe->outLineType() == BackgroundStripe::OutlineType::Min ||
				stripe->outLineType() == BackgroundStripe::OutlineType::Both)) {
				painter->drawLine(area.graphRect.x(), min_position, area.graphRect.right(), min_position);
			}
		}
	}
}

void View::paintViewBackgroundStripes(QPainter *painter, const View::GraphArea &area)
{
	painter->save();
	for (const BackgroundStripe *stripe : m_backgroundStripes) {

		if (stripe->type() == BackgroundStripe::Type::Vertical) {
			paintViewVerticalBackgroundStripe(painter, area, stripe);
		}
		else {
			paintViewHorizontalBackgroundStripe(painter, area, stripe);
		}
	}
	painter->restore();
}

void View::paintViewVerticalBackgroundStripe(QPainter *painter, const View::GraphArea &area, const BackgroundStripe *stripe)
{
	QColor stripe_color = stripe->stripeColor();
	if (stripe_color.alpha() == 255) {
		stripe_color.setAlpha(30);
	}

	qint64 min_value = xValue(stripe->min().valueX);
	if (min_value < m_displayedRangeMin) {
		min_value = m_displayedRangeMin;
	}
	qint64 max_value = xValue(stripe->max().valueX);
	if (max_value > m_displayedRangeMax) {
		max_value = m_displayedRangeMax;
	}
	if (max_value - min_value > 0LL) {
		int min = xValueToWidgetPosition(min_value);
		int max = xValueToWidgetPosition(max_value);

		painter->fillRect(min, area.graphRect.top(), max - min, area.graphRect.height(), stripe_color);
		if (stripe->outLineType() != BackgroundStripe::OutlineType::No) {
			QColor outline_color = stripe->outlineColor();
			if (!outline_color.isValid()) {
				outline_color = stripe->stripeColor();
			}
			if (outline_color.alpha() == 255) {
				outline_color.setAlpha(80);
			}
			painter->setPen(QPen(outline_color, 2.0));
			if (stripe->outLineType() == BackgroundStripe::OutlineType::Min ||
				stripe->outLineType() == BackgroundStripe::OutlineType::Both) {
				painter->drawLine(min, area.graphRect.top(), min, area.graphRect.bottom());
			}
			if (stripe->outLineType() == BackgroundStripe::OutlineType::Max ||
				stripe->outLineType() == BackgroundStripe::OutlineType::Both) {
				painter->drawLine(max, area.graphRect.top(), max, area.graphRect.bottom());
			}
		}
	}
}

void View::paintViewHorizontalBackgroundStripe(QPainter *painter, const View::GraphArea &area, const BackgroundStripe *stripe)
{
	QColor stripe_color = stripe->stripeColor();
	if (stripe_color.alpha() == 255) {
		stripe_color.setAlpha(30);
	}

	if (area.graphRect.height() <= 0) {
		return;
	}
	//horizontal stripe can be related only to y1 axis
	double range = (settings.yAxis.rangeMax / m_verticalZoom) - (settings.yAxis.rangeMin / m_verticalZoom);
	double scale = range / area.graphRect.height();

	int min = 0;
	int max = 0;
	if (stripe->valueYType() == ValueType::Double) {
		min = stripe->min().valueY.doubleValue / scale;
		max = stripe->max().valueY.doubleValue / scale;
	}
	else if (stripe->valueYType() == ValueType::Int) {
		min = stripe->min().valueY.intValue / scale;
		max = stripe->max().valueY.intValue / scale;
	}
	else if (stripe->valueYType() == ValueType::Bool) {
		SHV_EXCEPTION("GraphView: Cannot paint background stripe for bool serie");
	}
	int max_position = area.xAxisPosition - max;
	int min_position = area.xAxisPosition - min;
	bool max_outline_visible = true;
	bool min_outline_visible = true;
	if (max_position < area.graphRect.top()) {
		max_position = area.graphRect.top();
		max_outline_visible = false;
	}
	if (min_position > area.graphRect.bottom()) {
		min_position = area.graphRect.bottom();
		min_outline_visible = false;
	}
	if (max - min > 0 && min_position > area.graphRect.top() && max_position < area.graphRect.bottom()) {
		painter->fillRect(area.graphRect.x(), max_position, area.graphRect.width(), min_position - max_position, stripe_color);
		if (stripe->outLineType() != BackgroundStripe::OutlineType::No) {
			QColor outline_color = stripe->outlineColor();
			outline_color.setAlpha(70);
			painter->setPen(QPen(outline_color, 2.0));
			if (max_outline_visible &&
				(stripe->outLineType() == BackgroundStripe::OutlineType::Max ||
				stripe->outLineType() == BackgroundStripe::OutlineType::Both)) {
				painter->drawLine(area.graphRect.x(), max_position, area.graphRect.right(), max_position);
			}
			if (min_outline_visible &&
				(stripe->outLineType() == BackgroundStripe::OutlineType::Min ||
				stripe->outLineType() == BackgroundStripe::OutlineType::Both)) {
				painter->drawLine(area.graphRect.x(), min_position, area.graphRect.right(), min_position);
			}
		}
	}
}

QVector<const OutsideSerieGroup*> View::groupsForSeries(const QVector<const Serie*> &series) const
{
	QVector<const OutsideSerieGroup*> groups;
	for (const Serie *s : series) {
		if (s->serieGroup() && !groups.contains(s->serieGroup())) {
			groups << s->serieGroup();
		}
		for (const Serie *ds : s->dependentSeries()) {
			if (ds->serieGroup() && !groups.contains(ds->serieGroup())) {
				groups << ds->serieGroup();
			}
		}
	}
	QVector<const OutsideSerieGroup*> sorted_groups;
	for (const OutsideSerieGroup *group : m_outsideSeriesGroups) {
		if (groups.contains(group)) {
			sorted_groups << group;
		}
	}
	return sorted_groups;

}

void View::paintOutsideSeriesGroups(QPainter *painter, const View::GraphArea &area)
{
	QVector<const OutsideSerieGroup*> groups = groupsForSeries(area.series);

	int i = 0;
	for (const OutsideSerieGroup *group : groups) {
		QVector<SerieInGroup> shown_series_in_group = shownSeriesInGroup(*group, area.series);
		if (shown_series_in_group.count()) {
			if (i == area.outsideSerieGroupsRects.count()) {
				SHV_EXCEPTION("Something wrong in outside serie groups computation");
			}
			int position = group->serieSpacing();
			painter->save();
			painter->translate(area.outsideSerieGroupsRects[i].topLeft());
			for (const SerieInGroup &serie_in_group : shown_series_in_group) {
				if (serie_in_group.serie->type() != ValueType::Bool || serie_in_group.serie->lineType() != Serie::LineType::OneDimensional) {
					SHV_EXCEPTION("In outside groups can be only one dimensional bool series");
				}
				QPen pen(serie_in_group.masterSerie->color());
				pen.setWidth(serie_in_group.serie->lineWidth());
				painter->setPen(pen);

				paintBoolSerieAtPosition(painter, area.outsideSerieGroupsRects[i], position, serie_in_group.serie, m_displayedRangeMin, m_displayedRangeMax, Serie::Fill(), position);
				position = position + serie_in_group.serie->lineWidth() + group->serieSpacing();
			}
			++i;
			painter->restore();
		}
	}
}

void View::paintCurrentPosition(QPainter *painter, const GraphArea &area)
{
	painter->save();
	qint64 current = rectPositionToXValue(m_currentPosition);
	if (current >= m_dataRangeMin && current <= m_dataRangeMax) {
		for (int i = 0; i < area.series.count(); ++i) {
			const Serie *serie = area.series[i];
			if (!serie->isHidden()) {
				const SerieData &data = serie->serieModelData(this);
				if (data.size()) {
					paintCurrentPosition(painter, area, serie, current);
					if (settings.showDependent) {
						for (const Serie *dependent_serie : serie->dependentSeries()) {
							if (!dependent_serie->isHidden()) {
								paintCurrentPosition(painter, area, dependent_serie, current);
							}
						}
					}
				}
			}
		}
	}
	painter->restore();
}

QString View::legendRow(const Serie *serie, qint64 position) const
{
	QString s;
	if (!serie->isHidden()) {
		auto begin = findMinYValue(serie->displayedDataBegin, serie->displayedDataEnd, position);
		if (begin != shv::gui::SerieData::const_iterator()) {
			s = s + "<tr><td class=\"label\">" + serie->name() + ":</td><td class=\"value\">";
			if (serie->legendValueFormatter()) {
				s += serie->legendValueFormatter()(*begin);
			}
			else {
				ValueChange::ValueY value_change = formattedSerieValue(serie, begin);
				if (serie->type() == ValueType::Double) {
					s += QString::number(value_change.doubleValue);
				}
				else if (serie->type() == ValueType::Int) {
					s += QString::number(value_change.intValue);
				}
				else if (serie->type() == ValueType::Bool) {
					s += value_change.boolValue ? tr("true") : tr("false");
				}
			}
			s += "</td></tr>";
		}
	}
	return s;
}

QString View::legend(qint64 position) const
{
	QString s = "<html><head>" + settings.legendStyle + "</head><body>";
	s = s + "<table class=\"head\"><tr><td class=\"headLabel\">" + settings.xAxis.description.text + ":</td><td class=\"headValue\">" +
		xValueString(position, "dd.MM.yyyy HH:mm:ss.zzz").replace(" ", "&nbsp;") + "</td></tr></table><hr>";
	s += "<table>";
	if (position >= m_dataRangeMin && position <= m_dataRangeMax) {
		for (const Serie *serie : m_series) {
			const SerieData &data = serie->serieModelData(this);
			if (data.size()) {
				s += legendRow(serie, position);
				if (!serie->isHidden() && settings.showDependent) {
					for (const Serie *dependent_serie : serie->dependentSeries()) {
						s += legendRow(dependent_serie, position);
					}
				}
			}
		}
	}
	s += "</table></body></html>";
	return s;
}

int View::graphWidth() const
{
	int graph_width = m_graphArea[0].graphRect.width();
	if (settings.y2Axis.show) {
		graph_width -= settings.y2Axis.lineWidth;
	}
	return graph_width;
}

qint64 View::widgetPositionToXValue(int pos) const
{
	return rectPositionToXValue(pos - m_graphArea[0].graphRect.x());
}

qint64 View::rectPositionToXValue(int pos) const
{
	return m_displayedRangeMin + ((m_displayedRangeMax - m_displayedRangeMin) * ((double)pos / graphWidth()));
}

int View::xValueToRectPosition(qint64 value) const
{
	return (value - m_displayedRangeMin) * graphWidth() / (m_displayedRangeMax - m_displayedRangeMin);
}

int View::xValueToWidgetPosition(qint64 value) const
{
	return m_graphArea[0].graphRect.x() + xValueToRectPosition(value);
}

qint64 View::xValue(const ValueChange &value_change) const
{
	return xValue(value_change.valueX);
}

qint64 View::xValue(const ValueChange::ValueX &value_x) const
{
	qint64 val;
	switch (settings.xAxisType) {
	case ValueType::TimeStamp:
		val = value_x.timeStamp;
		break;
	case ValueType::Int:
		val = value_x.intValue;
		break;
	case ValueType::Double:
		val = value_x.doubleValue * m_xValueScale;
		break;
	default:
		val = 0;
		break;
	}
	return val;
}

ValueChange::ValueX View::internalToValueX(qint64 value) const
{
	ValueChange::ValueX val;
	switch (settings.xAxisType) {
	case ValueType::TimeStamp:
		val.timeStamp = value;
		break;
	case ValueType::Int:
		val.intValue = value;
		break;
	case ValueType::Double:
		val.doubleValue = value / m_xValueScale;
		break;
	default:
		val.intValue = 0;
		break;
	}
	return val;
}

QString View::xValueString(qint64 value, const QString &datetime_format) const
{
	QString s;
	switch (settings.xAxisType) {
	case ValueType::TimeStamp:
	{
		QDateTime date = QDateTime::fromMSecsSinceEpoch(value, settings.sourceDataTimeZone);
		s = date.toTimeZone(settings.viewTimeZone).toString(datetime_format);
		break;
	}
	case ValueType::Int:
		s = QString::number(value);
		break;
	case ValueType::Double:
		s = QString::number(value / m_xValueScale);
		break;
	default:
		break;
	}
	return s;
}

template<typename T>
void View::computeRange(T &min, T &max) const
{
	min = std::numeric_limits<T>::max();
	max = std::numeric_limits<T>::min();

	for (const Serie *serie : m_series) {
		serie->serieModelData(this).extendRange(min, max);
		for (const Serie *dependent_serie : serie->dependentSeries()) {
			dependent_serie->serieModelData(this).extendRange(min, max);
		}
	}
	if (min == std::numeric_limits<T>::max() && max == std::numeric_limits<T>::min()) {
		min = max = 0;
	}
}

shv::gui::SerieData::const_iterator View::findMinYValue(const SerieData::const_iterator &data_begin, const SerieData::const_iterator &data_end, qint64 x_value) const
{
	auto it = std::lower_bound(data_begin, data_end, x_value, [this](const ValueChange &data, qint64 value) {
	   return xValue(data) < value;
	});
	if (it != data_begin) {
		--it;
	}
	return it;
}

shv::gui::SerieData::const_iterator View::findMaxYValue(const SerieData::const_iterator &data_begin, const SerieData::const_iterator &data_end, qint64 x_value) const
{
	return std::upper_bound(data_begin, data_end, x_value, [this](qint64 value, const ValueChange &value_change) {
		return value < xValue(value_change);
	});
}

ValueChange::ValueY View::formattedSerieValue(const Serie *serie, SerieData::const_iterator it)
{
	return serie->valueFormatter() ? serie->valueFormatter()(*it) : it->valueY;
}

RangeSelectorHandle::RangeSelectorHandle(QWidget *parent) : QPushButton(parent)
{
	setCursor(Qt::SizeHorCursor);
	setFixedSize(9, 20);
}

void RangeSelectorHandle::paintEvent(QPaintEvent *event)
{
	QPushButton::paintEvent(event);

	QPainter painter(this);
	painter.drawLine(3, 3, 3, height() - 6);
	painter.drawLine(width() - 4, 3, width() - 4, height() - 6);
}

bool View::Selection::containsValue(qint64 value) const
{
	return ((start <= end && value >= start && value <= end) ||	(start > end && value >= end && value <= start));
}

} //namespace graphview
}
}
