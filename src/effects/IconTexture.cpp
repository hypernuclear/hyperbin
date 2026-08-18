#include "IconTexture.h"

namespace hyperbin {

void IconTexture::setImage(const QImage &img)
{
    if (img.isNull()) {
        setTextureData({});
        return;
    }
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    setSize(rgba.size());
    setFormat(QQuick3DTextureData::RGBA8);
    setHasTransparency(true);
    setTextureData(QByteArray(reinterpret_cast<const char *>(rgba.constBits()),
                              qsizetype(rgba.sizeInBytes())));
}

} // namespace hyperbin
