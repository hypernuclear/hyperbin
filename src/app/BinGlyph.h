// The bin glyph, as one SVG path.
//
// Single source of truth for both consumers: the QML component draws it
// with Shape/PathSvg so its colour can follow the theme, and the tray
// builds a QIcon from the same string. It is 7.6KB of coordinates —
// two hand-maintained copies would diverge the first time the artwork
// is touched.
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace hyperbin {

class BinGlyphData : public QObject
{
    Q_OBJECT
    // Named so it does not collide with BinGlyph.qml, which registers
    // the same type name from the same module.
    QML_NAMED_ELEMENT(BinGlyphData)
    QML_SINGLETON
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(qreal viewWidth READ viewWidth CONSTANT)
    Q_PROPERTY(qreal viewHeight READ viewHeight CONSTANT)

public:
    using QObject::QObject;

    static QString pathData();
    QString path() const { return pathData(); }
    qreal viewWidth() const { return 1200.0; }
    qreal viewHeight() const { return 1200.0; }
};

} // namespace hyperbin
