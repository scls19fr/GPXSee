#include <QFileInfo>
#include <QEventLoop>
#include <QXmlStreamReader>
#include "downloader.h"
#include "crs.h"
#include "wms.h"


Downloader *WMS::_downloader = 0;

WMS::CTX::CTX(const Setup &setup) : setup(setup), formatSupported(false)
{
	QStringList ll = setup.layer().split(',');
	QStringList sl = setup.style().split(',');

	if (ll.size() != sl.size())
		return;

	for (int i = 0; i < ll.size(); i++) {
		layers.append(Layer(ll.at(i), sl.at(i)));
		if (sl.at(i) == "default")
			layers[i].hasStyle = true;
	}
}

void WMS::getMap(QXmlStreamReader &reader, CTX &ctx)
{
	while (reader.readNextStartElement()) {
		if (reader.name() == "Format") {
			if (reader.readElementText() == ctx.setup.format())
				ctx.formatSupported = true;
		} else
			reader.skipCurrentElement();
	}
}

void WMS::request(QXmlStreamReader &reader, CTX &ctx)
{
	while (reader.readNextStartElement()) {
		if (reader.name() == "GetMap")
			getMap(reader, ctx);
		else
			reader.skipCurrentElement();
	}
}

QString WMS::style(QXmlStreamReader &reader)
{
	QString name;

	while (reader.readNextStartElement()) {
		if (reader.name() == "Name")
			name = reader.readElementText();
		else
			reader.skipCurrentElement();
	}

	return name;
}

RectC WMS::geographicBoundingBox(QXmlStreamReader &reader)
{
	qreal left, top, right, bottom;

	while (reader.readNextStartElement()) {
		if (reader.name() == "westBoundLongitude")
			left = reader.readElementText().toDouble();
		else if (reader.name() == "eastBoundLongitude")
			right = reader.readElementText().toDouble();
		else if (reader.name() == "northBoundLatitude")
			top = reader.readElementText().toDouble();
		else if (reader.name() == "southBoundLatitude")
			bottom = reader.readElementText().toDouble();
		else
			reader.skipCurrentElement();
	}

	return RectC(Coordinates(left, top), Coordinates(right, bottom));
}

void WMS::layer(QXmlStreamReader &reader, CTX &ctx,
  const QList<QString> &pCRSs, const QList<QString> &pStyles,
  RangeF &pScaleDenominator, RectC &pBoundingBox)
{
	QString name;
	QList<QString> CRSs(pCRSs);
	QList<QString> styles(pStyles);
	RangeF scaleDenominator(pScaleDenominator);
	RectC boundingBox(pBoundingBox);
	int index;

	while (reader.readNextStartElement()) {
		if (reader.name() == "Name")
			name = reader.readElementText();
		else if (reader.name() == "CRS" || reader.name() == "SRS")
			CRSs.append(reader.readElementText());
		else if (reader.name() == "Style")
			styles.append(style(reader));
		else if (reader.name() == "MinScaleDenominator")
			scaleDenominator.setMin(reader.readElementText().toDouble());
		else if (reader.name() == "MaxScaleDenominator")
			scaleDenominator.setMax(reader.readElementText().toDouble());
		else if (reader.name() == "LatLonBoundingBox") {
			QXmlStreamAttributes attr = reader.attributes();
			boundingBox = RectC(Coordinates(
			  attr.value("minx").toString().toDouble(),
			  attr.value("maxy").toString().toDouble()),
			  Coordinates(attr.value("maxx").toString().toDouble(),
			  attr.value("miny").toString().toDouble()));
			reader.skipCurrentElement();
		} else if (reader.name() == "EX_GeographicBoundingBox")
			boundingBox = geographicBoundingBox(reader);
		else if (reader.name() == "Layer")
			layer(reader, ctx, CRSs, styles, scaleDenominator, boundingBox);
		else
			reader.skipCurrentElement();
	}

	if ((index = ctx.layers.indexOf(name)) >= 0) {
		Layer &layer = ctx.layers[index];
		layer.scaleDenominator = scaleDenominator;
		layer.boundingBox = boundingBox;
		layer.isDefined = true;
		layer.hasStyle = styles.contains(layer.style);
		layer.hasCRS = CRSs.contains(ctx.setup.crs());
	}
}

void WMS::capability(QXmlStreamReader &reader, CTX &ctx)
{
	QList<QString> CRSs;
	QList<QString> styles;
	RangeF scaleDenominator(2132.729583849784, 559082264.0287178);
	RectC boundingBox;

	while (reader.readNextStartElement()) {
		if (reader.name() == "Layer")
			layer(reader, ctx, CRSs, styles, scaleDenominator, boundingBox);
		else if (reader.name() == "Request")
			request(reader, ctx);
		else
			reader.skipCurrentElement();
	}
}

void WMS::capabilities(QXmlStreamReader &reader, CTX &ctx)
{
	_version = reader.attributes().value("version").toString();

	while (reader.readNextStartElement()) {
		if (reader.name() == "Capability")
			capability(reader, ctx);
		else
			reader.skipCurrentElement();
	}
}

bool WMS::parseCapabilities(const QString &path, const Setup &setup)
{
	QFile file(path);
	CTX ctx(setup);
	QXmlStreamReader reader;


	if (ctx.layers.isEmpty()) {
		_errorString = "Invalid layers definition";
		return false;
	}

	if (!file.open(QFile::ReadOnly | QFile::Text)) {
		_errorString = file.errorString();
		return false;
	}

	reader.setDevice(&file);
	if (reader.readNextStartElement()) {
		if (reader.name() == "WMS_Capabilities"
		  || reader.name() == "WMT_MS_Capabilities")
			capabilities(reader, ctx);
		else
			reader.raiseError("Not a WMS Capabilities XML file");
	}
	if (reader.error()) {
		_errorString = QString("%1:%2: %3").arg(path).arg(reader.lineNumber())
		  .arg(reader.errorString());
		return false;
	}

	if (!ctx.formatSupported) {
		_errorString = ctx.setup.format() + ": format not provided";
		return false;
	}

	for (int i = 0; i < ctx.layers.size(); i++) {
		const Layer &layer = ctx.layers.at(i);

		if (!layer.isDefined) {
			_errorString = ctx.setup.layer() + ": layer not provided";
			return false;
		}
		if (!layer.hasStyle) {
			_errorString = ctx.setup.style() + ": style not provided for layer "
			  + layer.name;
			return false;
		}
		if (!layer.hasCRS) {
			_errorString = ctx.setup.crs() + ": CRS not provided for layer "
			  + layer.name;
			return false;
		}
		if (!layer.scaleDenominator.isValid()) {
			_errorString = "Invalid scale denominator range for layer"
			  + layer.name;
			return false;
		}
		if (!layer.boundingBox.isValid()) {
			_errorString = "Invalid/missing bounding box for layer"
			  + layer.name;
			return false;
		}
	}

	_projection = CRS::projection(ctx.setup.crs());
	if (_projection.isNull()) {
		_errorString = ctx.setup.crs() + ": unknown CRS";
		return false;
	}

	for (int i = 0; i < ctx.layers.size(); i++)
		_boundingBox |= ctx.layers.at(i).boundingBox;
	for (int i = 0; i < ctx.layers.size(); i++)
		_scaleDenominator |= ctx.layers.first().scaleDenominator;

	return true;
}

bool WMS::getCapabilities(const QString &url, const QString &file,
  const Authorization &authorization)
{
	QList<Download> dl;

	dl.append(Download(url, file));

	QEventLoop wait;
	QObject::connect(_downloader, SIGNAL(finished()), &wait, SLOT(quit()));
	if (_downloader->get(dl, authorization))
		wait.exec();

	if (QFileInfo(file).exists())
		return true;
	else {
		_errorString = "Error downloading capabilities XML file";
		return false;
	}
}

WMS::WMS(const QString &file, const WMS::Setup &setup) : _valid(false)
{
	QString capaUrl = QString("%1?service=WMS&request=GetCapabilities")
	  .arg(setup.url());

	if (!QFileInfo(file).exists())
		if (!getCapabilities(capaUrl, file, setup.authorization()))
			return;
	if (!parseCapabilities(file, setup))
		return;

	_valid = true;
}