#ifndef WMSMAP_H
#define WMSMAP_H

#include "transform.h"
#include "projection.h"
#include "map.h"
#include "wms.h"
#include "rectd.h"

class TileLoader;

class WMSMap : public Map
{
	Q_OBJECT

public:
	WMSMap(const QString &name, const WMS::Setup &setup, int tileSize,
	  QObject *parent = 0);

	QString name() const {return _name;}

	QRectF bounds();

	int zoom() const {return _zoom;}
	void setZoom(int zoom);
	int zoomFit(const QSize &size, const RectC &rect);
	int zoomIn();
	int zoomOut();

	QPointF ll2xy(const Coordinates &c);
	Coordinates xy2ll(const QPointF &p);

	void draw(QPainter *painter, const QRectF &rect, Flags flags);

	void setDevicePixelRatio(qreal /*deviceRatio*/, qreal mapRatio)
	  {_mapRatio = mapRatio;}
	void clearCache();

	bool isValid() const {return _valid;}
	QString errorString() const {return _errorString;}

private:
	QString tileUrl(const QString &baseUrl, const QString &version) const;
	double sd2res(double scaleDenominator) const;
	QString tilesDir() const;
	void computeZooms(const RangeF &scaleDenominator);
	void updateTransform();
	bool loadWMS();
	qreal tileSize() const;

	QString _name;

	WMS::Setup _setup;
	TileLoader *_tileLoader;
	Projection _projection;
	Transform _transform;
	CoordinateSystem _cs;
	QVector<double> _zooms;
	RectC _bbox;
	RectD _bounds;
	int _zoom;
	int _tileSize;
	qreal _mapRatio;

	bool _valid;
	QString _errorString;
};

#endif // WMSMAP_H
