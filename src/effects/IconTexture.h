// The bin's artwork, handed to Qt Quick 3D as texture data.
//
// QQuick3DTextureData rather than a file or a QSGTexture: the artwork
// comes from the shell at runtime, changes when the bin fills or empties,
// and never exists on disk.
//
// Shared, because more than one effect needs the same thing and for
// different reasons — the ooze refracts the icon, the tentacles redraw
// its front wall over themselves to be occluded by it.
#pragma once

#include <QImage>
#include <QQuick3DTextureData>

namespace hyperbin {

class IconTexture : public QQuick3DTextureData
{
    Q_OBJECT
public:
    void setImage(const QImage &img);
};

} // namespace hyperbin
