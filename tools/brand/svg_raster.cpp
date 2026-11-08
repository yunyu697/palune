// SVG → PNG 栅格化小工具（Qt Svg）。仅供品牌图标导出使用，不参与主工程。
// 用法：svg_raster <in.svg> <out.png> <像素边长>
// 说明：按目标像素边长等比渲染，保留透明通道；不写任何元数据。

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 4) {
        std::fprintf(stderr, "用法：svg_raster <in.svg> <out.png> <像素边长>\n");
        return 2;
    }

    const QString input = QString::fromLocal8Bit(argv[1]);
    const QString output = QString::fromLocal8Bit(argv[2]);
    const int size = std::atoi(argv[3]);

    QSvgRenderer renderer(input);
    if (!renderer.isValid()) {
        std::fprintf(stderr, "SVG 无法解析：%s\n", argv[1]);
        return 1;
    }
    if (size <= 0) {
        std::fprintf(stderr, "像素边长非法：%s\n", argv[3]);
        return 2;
    }

    // ARGB32_Premultiplied 便于 QPainter 混合，保存 PNG 时 Qt 会转回直通 alpha。
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.end();

    if (!image.save(output, "PNG")) {
        std::fprintf(stderr, "写入失败：%s\n", argv[2]);
        return 1;
    }
    std::printf("%s %dx%d\n", argv[2], size, size);
    return 0;
}
